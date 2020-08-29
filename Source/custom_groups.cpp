#include "web_cache_exporter.h"
#include "custom_groups.h"

/*
	This file defines the necessary functions used to load groups files, and match their contents to cache entries. Each group file
	contains zero or more groups which label cache entries based on their file types and URLs. Only files which have a .group extension
	are loaded. Group files use UTF-8 as their character encoding but only comments may use any Unicode characters for now. Any remaining
	text is limited to ASCII characters. The following comparisons are done for each cache entry, for each group, in the following order.
	All string comparisons are case insensitive.

	For file groups:
	1. Check if the file's contents begin with any of the group's file signatures.
	2. Check if the MIME type in the cached file's "Content-Type" HTTP header begins with one of the group's MIME types.
	3. Check if the file's extension matches one of the group's file extensions.

	For example:
	1. If a group specifies the file signature "3C 68 74 6D 6C" it will match any file that exists and whose contents begin with "<html".
	A special identifier "__" can be used to match any byte. If the bytes "3C 68 __ 6D __" are specified, it will match "<html", "<hxmy",
	"<hamb", etc. If the file's size is smaller than the file signature, it will never match it.
	2. If a group specifies the MIME type "text/java", it will match "text/javascript", "TEXT/JAVASCRIPT",
	and "text/javascript; charset=UTF-8".
	3. If a group specifies the file extension "htm", it will match .htm and .HTM files, but not .html.

	For URL groups:
	1. Check if the cached file's original URL matches the group's URL. The entry's URL is separated into a host and path, and the check
	is performed by comparing the end of the host and the beginning of the path to the group's URL.

	For example:
	1a. If a group specifies "example.com", it will match "http://www.example.com/index.html", "mms://cdn.example.com:80/path/video.mp4",
	and "https://download.example.com/path/updates/file.php?version=123&platform=ABC#top". This check allows you to match subdomains.
	1b. Using the same example, if a group specifies "example.com/path", it will match the last two URLs. If a group instead specifies
	"example.com/path/updates", it will only match the last one.

	Here's an example of a group file which defines one file group (called Flash) and one URL group (called Cartoon Network). If a line
	starts with a ';' character, then it's considered a comment and is not processed.

	; This is a comment. 
	BEGIN_FILE_GROUP Flash

		BEGIN_FILE_SIGNATURES
			; "FWS" (uncompressed)
			46 57 53
			; "CWS" (ZLIB compressed)
			43 57 53
			; "ZWS" (LZMA compressed)
			5A 57 53
		END

		BEGIN_MIME_TYPES
			application/x-shockwave-flash
			application/vnd.adobe.flash-movie
			application/futuresplash
		END

		BEGIN_FILE_EXTENSIONS
			swf spl
		END

	END

	BEGIN_URL_GROUP Cartoon Network

		BEGIN_DOMAINS
			cartoonnetwork.com
			turner.com/toon
		END

	END

*/

static const TCHAR* GROUP_FILES_DIRECTORY_NAME = TEXT("Groups");
static const TCHAR* GROUP_FILES_SEARCH_PATH = TEXT("Groups\\*.group");
static const TCHAR* GROUP_FILES_SEARCH_QUERY = TEXT("*.group");

// Various keywords and delimiters for the group file syntax.
static const char COMMENT = ';';
static const char* LINE_DELIMITERS = "\r\n";
static const char* TOKEN_DELIMITERS = " \t";
static const char* BEGIN_FILE_GROUP = "BEGIN_FILE_GROUP";
static const char* BEGIN_URL_GROUP = "BEGIN_URL_GROUP";
static const char* END_GROUP = "END";

static const char* BEGIN_FILE_SIGNATURES = "BEGIN_FILE_SIGNATURES";
static const TCHAR* BYTE_DELIMITERS = TEXT(" ");
static const char* BEGIN_MIME_TYPES = "BEGIN_MIME_TYPES";
static const char* BEGIN_FILE_EXTENSIONS = "BEGIN_FILE_EXTENSIONS";
static const char* BEGIN_DOMAINS = "BEGIN_DOMAINS";
static const TCHAR* URL_PATH_DELIMITERS = TEXT("/");

// The maximum size of the file signature buffer in bytes.
static const u32 MAX_FILE_SIGNATURE_BUFFER_SIZE = (u32) kilobytes_to_bytes(1);

// Determines if a given group file should be loaded by searching an array of filenames. This function is only called when
// the '-load-group-files' is passed to the application.
//
// @Parameters:
// 1. filenames_to_load - The array of filenames to check. These are passed as command line options to the application.
// 2. num_filenames_to_load - The number of elements in this array.
// 3. filename - The current name of the group file on disk that should be checked. This filename may contain a .group extension,
// though this will be ignored when comparing it with the other array elements.
//
// @Returns: True if this filename is allowed to be loaded. Otherwise, false.
static bool should_load_group_with_filename(TCHAR** filenames_to_load, size_t num_filenames_to_load, TCHAR* filename)
{
	// Temporarily remove the .group file extension.
	TCHAR* file_extension = skip_to_file_extension(filename, true);
	TCHAR previous_char = *file_extension;
	*file_extension = TEXT('\0');
		
	bool success = false;

	for(size_t i = 0; i < num_filenames_to_load; ++i)
	{
		if(strings_are_equal(filename, filenames_to_load[i]))
		{
			success = true;
			break;
		}
	}

	*file_extension = previous_char;

	return success;
}

// Helper structure to pass some values to and from find_total_group_size_callback().
struct Total_Group_File_Result
{
	Exporter* exporter;
	size_t total_group_size;
	u32 total_num_groups;
};

// Called every time a group file is found. Used to find the number of groups and the amount of memory required to store them.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_total_group_size_callback)
{
	Total_Group_File_Result* result = (Total_Group_File_Result*) user_data;
	Exporter* exporter = result->exporter;
	TCHAR* filename = find_data->cFileName;

	if(exporter->should_load_specific_groups_files
		&& !should_load_group_with_filename(exporter->group_filenames_to_load, exporter->num_group_filenames_to_load, filename))
	{
		return;
	}

	TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(group_file_path, directory_path, filename);

	HANDLE group_file_handle = INVALID_HANDLE_VALUE;
	u64 group_file_size = 0;
	char* group_file = (char*) memory_map_entire_file(group_file_path, &group_file_handle, &group_file_size, false);
	
	if(group_file != NULL)
	{
		char* remaining_lines = NULL;
		char* line = strtok_s(group_file, LINE_DELIMITERS, &remaining_lines);

		while(line != NULL)
		{
			line = skip_leading_whitespace(line);

			if(*line == COMMENT || string_is_empty(line))
			{
				// Skip comments and empty lines.
			}
			else
			{
				// Keep track of the total group file string data:
				// Any characters outside of comments are limited to ASCII characters (for now).
				// We're essentially getting a single byte character string's length plus the null terminator here.
				// At the end, we'll multiply this by sizeof(TCHAR). For the Windows 2000 to 10 builds, this includes
				// an extra null terminator (since this size is two).
				result->total_group_size += string_size(line);

				char* group_name = NULL;
				char* group_type = strtok_s(line, TOKEN_DELIMITERS, &group_name);
				if(group_type != NULL && group_name != NULL)
				{
					// Keep track of the total amount of groups across multiple files.
					if(strings_are_equal(group_type, BEGIN_FILE_GROUP)
					|| strings_are_equal(group_type, BEGIN_URL_GROUP))
					{
						++(result->total_num_groups);
					}
				}
			}

			line = strtok_s(NULL, LINE_DELIMITERS, &remaining_lines);
		}
	}

	safe_unmap_view_of_file((void**) &group_file);
	safe_close_handle(&group_file_handle);
}

// Finds each group file on disk and retrieves the number of groups and many bytes are (roughly) required to store them.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the executable, which is used to resolve the group files directory.
// 2. result_num_groups - The number of groups found.
//
// @Returns: The total size in bytes to store the groups found.
size_t get_total_group_files_size(Exporter* exporter, u32* result_num_groups)
{
	TCHAR group_files_directory_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(group_files_directory_path, exporter->executable_path, GROUP_FILES_DIRECTORY_NAME);

	Total_Group_File_Result result = {};
	result.exporter = exporter;
	traverse_directory_objects(	group_files_directory_path, GROUP_FILES_SEARCH_QUERY, TRAVERSE_FILES, false,
								find_total_group_size_callback, &result);

	*result_num_groups = result.total_num_groups;
	// Total Size = Size for the Group array + Size for the string data + Size for the file signature buffer.
	return 	sizeof(Custom_Groups) + MAX(result.total_num_groups - 1, 0) * sizeof(Group)
			+ result.total_group_size * sizeof(TCHAR)
			+ MAX_FILE_SIGNATURE_BUFFER_SIZE;
}

// Converts an UTF-8 string to a TCHAR one and copies the final result to the permanent arena. On the Windows 98 and ME builds, the
// intermediary UTF-16 string is stored in the temporary arena.
//
// On the Windows 98 and ME builds, this function converts the ANSI string to UTF-16, and then to UTF-8.
// On the Windows 2000 through 10 builds, this function converts the UTF-16 string to a UTF-8 one.
//
// @Parameters:
// 1. permanent_arena - The Arena structure that will receive the final converted TCHAR string.
// 2. temporary_arena - The Arena structure that will receive the intermediary converted UTF-16 string. This only applies to Windows 98
// and ME. On the Windows 2000 to 10 builds, this parameter is unused.
// 3. utf_8_string - The UTF-8 string to convert and copy to the arena.
//
// @Returns: The pointer to the TCHAR string on success. Otherwise, it returns NULL.
static TCHAR* copy_utf_8_string_to_tchar(Arena* permanent_arena, Arena* temporary_arena, const char* utf_8_string)
{
	int num_chars_required_wide = MultiByteToWideChar(CP_UTF8, 0, utf_8_string, -1, NULL, 0);
	if(num_chars_required_wide == 0)
	{
		log_print(LOG_ERROR, "Copy Utf-8 String To Tchar: Failed to find the number of characters necessary to represent the string as a wide string with the error code %lu.", GetLastError());
		_ASSERT(false);
		return NULL;
	}

	#ifdef BUILD_9X
		Arena* wide_string_arena = temporary_arena;
	#else
		Arena* wide_string_arena = permanent_arena;
	#endif

	int size_required_wide = num_chars_required_wide * sizeof(wchar_t);
	wchar_t* wide_string = push_arena(wide_string_arena, size_required_wide, wchar_t);
	if(MultiByteToWideChar(CP_UTF8, 0, utf_8_string, -1, wide_string, num_chars_required_wide) == 0)
	{
		log_print(LOG_ERROR, "Copy Utf-8 String To Tchar: Failed to convert the string to a wide string with the error code %lu.", GetLastError());
		_ASSERT(false);
		return NULL;
	}

	#ifdef BUILD_9X
		int size_required_ansi = WideCharToMultiByte(CP_ACP, 0, wide_string, -1, NULL, 0, NULL, NULL);
		if(size_required_ansi == 0)
		{
			log_print(LOG_ERROR, "Copy Utf-8 String To Tchar: Failed to find the number of characters necessary to represent the intermediate Wide '%ls' as an ANSI string with the error code %lu.", wide_string, GetLastError());
			_ASSERT(false);
			return NULL;
		}

		char* ansi_string = push_arena(permanent_arena, size_required_ansi, char);
		if(WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, ansi_string, size_required_ansi, NULL, NULL) == 0)
		{
			log_print(LOG_ERROR, "Copy Utf-8 String To Tchar: Failed to convert the intermediate Wide string '%ls' to an ANSI string with the error code %lu.", wide_string, GetLastError());
			_ASSERT(false);
			return NULL;
		}

		return ansi_string;
	#else
		return wide_string;
	#endif
}

// Counts the number of tokens in a string that are delimited by spaces. This is used to find out how many tokens will be created
// after iterating over this string with strtok_s(). Note that multiple spaces in a row are skipped (e.g. "aa bb     c" counts three
// tokens). 
//
// @Parameters:
// 1. str - The string.
//
// @Returns: The number of tokens delimited by spaces.
static u32 count_tokens_delimited_by_spaces(TCHAR* str)
{
	u32 count = 0;
	bool was_previously_whitespace = true;

	while(*str != TEXT('\0'))
	{
		if(*str == TEXT(' '))
		{
			was_previously_whitespace = true;
		}
		else
		{
			if(was_previously_whitespace)
			{
				++count;
			}
			was_previously_whitespace = false;
		}

		++str;
	}

	return count;
}

// Copies the current string tokens from a line of delimited values, and converts it from UTF-8 to a TCHAR (ANSI or UTF-16 string
// depending on the build target). This function is meant to be called consecutively until the current group list type ends. It will
// keep track of currently processed elements and the number of tokens found.
//
// @Parameters:
// 1. permanent_arena - The Arena structure that will receive the final converted TCHAR string.
// 2. temporary_arena - The Arena structure that will be used to store an intermediary UTF-16 string on Windows 98 and ME.
// See: copy_utf_8_string_to_tchar().
// 3. list_line - The current line of delimited values.
// 4. token_delimiters - The character delimiters.
// 5. token_strings - The currently processed strings tokens stored contiguously in memory. You must pass NULL when calling this
// function for the first time.
// 6. token_counter - The current number of processed tokens. You must pass zero when calling this function for the first time.
//
// @Returns: Nothing.
static void copy_string_from_list_to_group(	Arena* permanent_arena, Arena* temporary_arena,
											char* list_line, const char* token_delimiters,
											TCHAR** token_strings, u32* token_counter)
{
	if(*token_strings == NULL) *token_strings = push_arena(permanent_arena, 0, TCHAR);

	char* remaining_tokens = NULL;
	char* token = strtok_s(list_line, token_delimiters, &remaining_tokens);
	while(token != NULL)
	{
		++(*token_counter);
		copy_utf_8_string_to_tchar(permanent_arena, temporary_arena, token);
		token = strtok_s(NULL, token_delimiters, &remaining_tokens);
	}
}

// Loads a specific group file on disk, while also keeping track of the total number of processed groups and the maximum file signature
// size in bytes.
//
// @Parameters:
// 1. permanent_arena - The Arena structure that will receive the loaded group data.
// 2. temporary_arena - The Arena structure where any intermediary strings are stored.
// 3. file_path - The path of the group file to load.
// 4. group_array - The preallocated group array. Each group loads its data to a specific index, which is tracked by 'num_processed_groups'.
// 5. num_processed_groups - The current total number of processed groups.
// 6. max_num_file_signature_bytes - The current maximum file signature size. This is later used to allocate an array that is just large
// enough to load each processed file signature.
//
// @Returns: Nothing.
void load_group_file(Arena* permanent_arena, Arena* temporary_arena,
					 const TCHAR* file_path, Group* group_array, u32* num_processed_groups, u32* max_num_file_signature_bytes)
{
	TCHAR* group_filename = PathFindFileName(file_path);
	log_print(LOG_INFO, "Load Group File: Loading the group file '%s'...", group_filename);

	HANDLE group_file_handle = INVALID_HANDLE_VALUE;
	u64 group_file_size = 0;
	char* group_file = (char*) memory_map_entire_file(file_path, &group_file_handle, &group_file_size, false);
	
	if(group_file == NULL)
	{
		log_print(LOG_ERROR, "Load Group File: Failed to load the group file '%s'.", group_filename);
		safe_close_handle(&group_file_handle);
		return;
	}

	char* remaining_lines = NULL;
	char* line = strtok_s(group_file, LINE_DELIMITERS, &remaining_lines);

	// Keep track of which group we're loading data to.
	Group_Type current_group_type = GROUP_NONE;
	Group* group = NULL;

	// Keep track of the strings that are loaded from each list type. These are kept contiguously in memory, so this address points
	// to the first string.
	// These counters and addresses are set back to zero and NULL after processing their respective list type.
	List_Type current_list_type = LIST_NONE;
	
	u32 num_file_signatures = 0;
	TCHAR* file_signature_strings = NULL;

	u32 num_mime_types = 0;
	TCHAR* mime_type_strings = NULL;
	
	u32 num_file_extensions = 0;
	TCHAR* file_extension_strings = NULL;
	
	u32 num_domains = 0;
	TCHAR* domain_strings = NULL;

	while(line != NULL)
	{
		line = skip_leading_whitespace(line);

		if(*line == COMMENT || string_is_empty(line))
		{
			// Skip comments and empty lines.
		}
		// Reached the end of a list. Aggregate and perform any last operations on the data before moving on to the next list or group.
		else if(current_list_type != LIST_NONE && strings_are_equal(line, END_GROUP))
		{
			switch(current_list_type)
			{
				// Aggregate the group's file signatures.
				case(LIST_FILE_SIGNATURES):
				{
					// Create a file signature array.
					File_Signature** file_signatures = push_arena(permanent_arena, num_file_signatures * sizeof(File_Signature*), File_Signature*);
					
					for(u32 i = 0; i < num_file_signatures; ++i)
					{
						// Create each file signature by converting each space delimited string to a byte or wildcard.
						++(group->file_info.num_file_signatures);
						File_Signature* signature = push_arena(permanent_arena, sizeof(File_Signature), File_Signature);

						u32 num_bytes = count_tokens_delimited_by_spaces(file_signature_strings);
						if(num_bytes > MAX_FILE_SIGNATURE_BUFFER_SIZE)
						{
							log_print(LOG_WARNING, "Load Group File: The file signature number %I32u in the group '%s' has %I32u bytes when the maximum is %I32u. These extra bytes will be ignored.", i+1, group->name, num_bytes, MAX_FILE_SIGNATURE_BUFFER_SIZE);
							num_bytes = MAX_FILE_SIGNATURE_BUFFER_SIZE;
						}

						// Keep track of the global maximum signature size. This will allow us to allocate a file buffer
						// capable of holding any file signature we need.
						if(num_bytes > *max_num_file_signature_bytes)
						{
							*max_num_file_signature_bytes = num_bytes;
						}

						u8* bytes = push_arena(permanent_arena, num_bytes * sizeof(u8), u8);
						bool* is_wildcard = push_arena(permanent_arena, num_bytes * sizeof(bool), bool);

						TCHAR* next_file_signature_string = skip_to_end_of_string(file_signature_strings);
						++next_file_signature_string;

						TCHAR* remaining_bytes = NULL;
						TCHAR* byte_string = _tcstok_s(file_signature_strings, BYTE_DELIMITERS, &remaining_bytes);
						u32 byte_idx = 0;
						while(byte_string != NULL)
						{
							if(byte_idx >= num_bytes)
							{
								_ASSERT(false);
								break;
							}

							if(strings_are_equal(byte_string, TEXT("__")))
							{
								is_wildcard[byte_idx] = true;
							}
							else if(convert_hexadecimal_string_to_byte(byte_string, &bytes[byte_idx]))
							{
								is_wildcard[byte_idx] = false;
							}
							else
							{
								log_print(LOG_ERROR, "Load Group File: The string '%s' cannot be converted into a byte. The file signature number %I32u in the group '%s' will be skipped.", byte_string, i+1, group->name);
								num_bytes = 0;
								bytes = NULL;
								is_wildcard = NULL;
								break;
							}

							byte_string = _tcstok_s(NULL, BYTE_DELIMITERS, &remaining_bytes);
							++byte_idx;
						}

						signature->num_bytes = num_bytes;
						signature->bytes = bytes;
						signature->is_wildcard = is_wildcard;
						file_signatures[i] = signature;

						file_signature_strings = next_file_signature_string;
					}

					group->file_info.file_signatures = file_signatures;

					num_file_signatures = 0;
					file_signature_strings = NULL;
				} break;

				// Aggregate the group's MIME types.
				case(LIST_MIME_TYPES):
				{
					// Create a MIME type array.
					group->file_info.mime_types = build_array_from_contiguous_strings(permanent_arena, mime_type_strings, num_mime_types);
					group->file_info.num_mime_types = num_mime_types;

					num_mime_types = 0;
					mime_type_strings = NULL;
				} break;

				// Aggregate the group's file extensions.
				case(LIST_FILE_EXTENSIONS):
				{
					// Create a file extension array.
					group->file_info.file_extensions = build_array_from_contiguous_strings(permanent_arena, file_extension_strings, num_file_extensions);
					group->file_info.num_file_extensions = num_file_extensions;

					num_file_extensions = 0;
					file_extension_strings = NULL;
				} break;

				// Aggregate the group's domains.
				case(LIST_DOMAINS):
				{
					// Create a domain array.
					Domain** domains = push_arena(permanent_arena, num_domains * sizeof(Domain*), Domain*);
					
					for(u32 i = 0; i < num_domains; ++i)
					{
						// Create each domain by splitting the host and path URL components.
						++(group->url_info.num_domains);
						Domain* domain = push_arena(permanent_arena, sizeof(Domain), Domain);

						TCHAR* next_domain_string = skip_to_end_of_string(domain_strings);
						++next_domain_string;

						TCHAR* path = NULL;
						TCHAR* host = _tcstok_s(domain_strings, URL_PATH_DELIMITERS, &path);

						domain->host = push_string_to_arena(permanent_arena, host);
						if(path != NULL && !string_is_empty(path))
						{
							domain->path = push_string_to_arena(permanent_arena, path);
						}
						else
						{
							domain->path = NULL;
						}

						domains[i] = domain;

						domain_strings = next_domain_string;
					}

					group->url_info.domains = domains;

					num_domains = 0;
					domain_strings = NULL;
				} break;
			}

			current_list_type = LIST_NONE;
		}
		// Reached the end of a group.
		else if(current_group_type != GROUP_NONE && strings_are_equal(line, END_GROUP))
		{
			current_group_type = GROUP_NONE;
			group = NULL;
		}
		else
		{
			switch(current_group_type)
			{
				// Found the start of a new group.
				case(GROUP_NONE):
				{
					char* group_name = NULL;
					char* group_type = strtok_s(line, TOKEN_DELIMITERS, &group_name);
					if(group_type != NULL && group_name != NULL)
					{
						// Find the group's type.
						if(strings_are_equal(group_type, BEGIN_FILE_GROUP))
						{
							current_group_type = GROUP_FILE;
							
						}
						else if(strings_are_equal(group_type, BEGIN_URL_GROUP))
						{
							current_group_type = GROUP_URL;
						}
						else
						{
							log_print(LOG_ERROR, "Load Group File: Unknown group type '%hs'.", group_type);
						}

						if(current_group_type != GROUP_NONE)
						{
							// Get this group's index in the global custom groups array.
							// This will allow us to use this group's preallocated Group structure.
							u32 group_idx = *num_processed_groups;
							++(*num_processed_groups);
							group = &group_array[group_idx];

							group->type = current_group_type;
							group->name = copy_utf_8_string_to_tchar(permanent_arena, temporary_arena, group_name);

							// Clear the data for each group type.
							if(current_group_type == GROUP_FILE)
							{
								ZeroMemory(&(group->file_info), sizeof(group->file_info));
							}
							else if(current_group_type == GROUP_URL)
							{
								ZeroMemory(&(group->url_info), sizeof(group->url_info));
							}
							else
							{
								_ASSERT(false);
							}
						}
					}
				} break;

				// Add the lists from a file group.
				case(GROUP_FILE):
				{
					switch(current_list_type)
					{
						// Found the start of a list.
						case(LIST_NONE):
						{
							if(strings_are_equal(line, BEGIN_FILE_SIGNATURES))
							{
								current_list_type = LIST_FILE_SIGNATURES;
							}
							else if(strings_are_equal(line, BEGIN_MIME_TYPES))
							{
								current_list_type = LIST_MIME_TYPES;
							}
							else if(strings_are_equal(line, BEGIN_FILE_EXTENSIONS))
							{
								current_list_type = LIST_FILE_EXTENSIONS;
							}
							else
							{
								log_print(LOG_ERROR, "Load Group File: Unknown group file list type '%hs'.", line);
							}
						} break;

						// Add each file signature (one per line).
						case(LIST_FILE_SIGNATURES):
						{
							// File signatures are processed differently from MIME types and file extensions.
							// We only allow one file signature per line, and the way each token (byte) is delimited is different.
							++num_file_signatures;
							if(file_signature_strings == NULL) file_signature_strings = push_arena(temporary_arena, 0, TCHAR);
							copy_utf_8_string_to_tchar(temporary_arena, temporary_arena, line);
						} break;

						// Add each MIME type (multiple per line).
						case(LIST_MIME_TYPES):
						{
							copy_string_from_list_to_group(	permanent_arena, temporary_arena,
															line, TOKEN_DELIMITERS,
															&mime_type_strings, &num_mime_types);
						} break;

						// Add each file extension (multiple per line).
						case(LIST_FILE_EXTENSIONS):
						{
							copy_string_from_list_to_group(	permanent_arena, temporary_arena,
															line, TOKEN_DELIMITERS,
															&file_extension_strings, &num_file_extensions);
						} break;
					}
				} break;

				// Add the lists from a URL group.
				case(GROUP_URL):
				{
					switch(current_list_type)
					{
						// Found the start of a list.
						case(LIST_NONE):
						{
							if(strings_are_equal(line, BEGIN_DOMAINS))
							{
								current_list_type = LIST_DOMAINS;
							}
							else
							{
								log_print(LOG_ERROR, "Load Group File: Unknown group URL list type '%hs'.", line);
							}
						} break;

						// Add each domain (one per line).
						case(LIST_DOMAINS):
						{
							++num_domains;
							if(domain_strings == NULL) domain_strings = push_arena(temporary_arena, 0, TCHAR);
							copy_utf_8_string_to_tchar(temporary_arena, temporary_arena, line);
						} break;
					}
				} break;
			}
		}

		line = strtok_s(NULL, LINE_DELIMITERS, &remaining_lines);
	}

	if(current_list_type != LIST_NONE)
	{
		console_print("Load Group File: Found unterminated list of type '%s' in the group file '%s'.", LIST_TYPE_TO_STRING[current_list_type], group_filename);
		log_print(LOG_WARNING, "Load Group File: Found unterminated list of type '%s' in the group file '%s'.", LIST_TYPE_TO_STRING[current_list_type], group_filename);
	}

	if(current_group_type != GROUP_NONE)
	{
		console_print("Load Group File: Found unterminated group of type '%s' in the group file '%s'.", GROUP_TYPE_TO_STRING[current_group_type], group_filename);
		log_print(LOG_WARNING, "Load Group File: Found unterminated group of type '%s' in the group file '%s'.", GROUP_TYPE_TO_STRING[current_group_type], group_filename);
	}

	safe_unmap_view_of_file((void**) &group_file);
	safe_close_handle(&group_file_handle);
}

// Helper structure to pass some values to and from find_group_files_callback().
struct Find_Group_Files_Result
{
	Exporter* exporter;
	u32 num_group_files;
};

// Called every time a group file is found. Used to make an array of filenames.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_group_files_callback)
{
	Find_Group_Files_Result* result = (Find_Group_Files_Result*) user_data;
	Exporter* exporter = result->exporter;
	TCHAR* filename = find_data->cFileName;

	if(exporter->should_load_specific_groups_files
		&& !should_load_group_with_filename(exporter->group_filenames_to_load, exporter->num_group_filenames_to_load, filename))
	{
		return;
	}

	size_t size = string_size(filename);
	push_and_copy_to_arena(&(exporter->temporary_arena), size, u8, filename, size);

	++(result->num_group_files);
}

// Called by qsort() to sort group file's names alphabetically.
static int compare_filenames(const void* filename_pointer_1, const void* filename_pointer_2)
{
	TCHAR* filename_1 = *((TCHAR**) filename_pointer_1);
	TCHAR* filename_2 = *((TCHAR**) filename_pointer_2);
	return _tcscmp(filename_1, filename_2);
}

// Loads all group files on disk. This function should be called after get_total_group_files_size() and with a memory arena that is
// capable of holding the number of bytes it returned.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the executable, which is used to resolve the group files directory,
// and the permanent memory arena where the group data will be stored. After loading this data, this structure's 'custom_groups' member
// will modified.
// 2. num_groups - The number of groups found by an earlier call to get_total_group_files_size().
//
// @Returns: Nothing.
void load_all_group_files(Exporter* exporter, u32 num_groups)
{
	if(num_groups == 0)
	{
		log_print(LOG_WARNING, "Load All Group Files: Attempted to load zero groups. No groups will be loaded.");
		return;
	}

	// The relevant loaded group data will go to the permanent arena, and any intermediary data to the temporary one.
	Arena* permanent_arena = &(exporter->permanent_arena);
	Arena* temporary_arena = &(exporter->temporary_arena);

	TCHAR group_files_directory_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(group_files_directory_path, exporter->executable_path, GROUP_FILES_DIRECTORY_NAME);
	
	// Find each group file on disk. We'll keep track of their filenames by storing them contiguously in memory.
	TCHAR* first_group_filename = push_arena(temporary_arena, 0, TCHAR);
	Find_Group_Files_Result result = {};
	result.exporter = exporter;
	traverse_directory_objects(	group_files_directory_path, GROUP_FILES_SEARCH_QUERY, TRAVERSE_FILES, false,
								find_group_files_callback, &result);

	u32 num_group_files = result.num_group_files;
	if(num_group_files == 0)
	{
		log_print(LOG_ERROR, "Load All Group Files: Expected to load %I32u groups from at least one file on disk, but no files were found. No groups will be loaded.", num_groups);
		_ASSERT(false);
		return;
	}

	// Build an array with the filenames so we can sort them alphabetically. FindFirstFile() doesn't guarantee any specific order,
	// and we want the way the group files are loaded to be deterministic.
	TCHAR** group_filenames_array = build_array_from_contiguous_strings(temporary_arena, first_group_filename, num_group_files);
	qsort(group_filenames_array, num_group_files, sizeof(TCHAR*), compare_filenames);

	// Build an array of group structures, each containing 
	// @Note: Don't confuse 'num_group_files' with 'num_groups'. The former is the number of .group files on disk, and the latter
	// the total number of groups that are defined in them. Each group file may define zero or more groups.
	// The number of groups is always greater than zero here.
	size_t custom_groups_size = sizeof(Custom_Groups) + sizeof(Group) * (num_groups - 1);
	Custom_Groups* custom_groups = push_arena(permanent_arena, custom_groups_size, Custom_Groups);
	custom_groups->num_groups = num_groups;

	// The global group counter that is used to keep track of each group's place in the array.
	u32 num_processed_groups = 0;
	u32 max_num_file_signature_bytes = 0;
	
	for(u32 i = 0; i < num_group_files; ++i)
	{
		TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
		PathCombine(group_file_path, group_files_directory_path, group_filenames_array[i]);

		load_group_file(permanent_arena, temporary_arena,
						group_file_path, custom_groups->groups,
						&num_processed_groups, &max_num_file_signature_bytes);
	}

	if(num_processed_groups != num_groups)
	{
		log_print(LOG_WARNING, "Load All Group Files: Loaded %I32u groups when %I32u were expected.", num_processed_groups, num_groups);
		_ASSERT(false);
	}

	log_print(LOG_INFO, "Load All Group Files: Allocating %I32u bytes for the file signature buffer.", max_num_file_signature_bytes);
	custom_groups->file_signature_buffer = push_arena(permanent_arena, max_num_file_signature_bytes, u8);
	custom_groups->file_signature_buffer_size = max_num_file_signature_bytes;

	exporter->custom_groups = custom_groups;
}

// Compares a file signature to an array of bytes and wildcards.
//
// @Parameters:
// 1. file_buffer - The buffer that contains the first bytes from a file. 
// 2. file_buffer_size - The size of this buffer in bytes.
// 3. bytes_to_compare - The file signature byte array to compare.
// 4. is_byte_wildcard - The wildcard byte array to used during this comparison.
// 5. bytes_to_compare_size - The size of this signature and wildcard array in bytes.
//
// @Returns: True if the file signature matched 
static bool compare_file_bytes_using_wildcards(	u8* file_buffer, u32 file_buffer_size,
												u8* bytes_to_compare, bool* is_byte_wildcard, u32 bytes_to_compare_size)
{
	u32 num_bytes = MIN(file_buffer_size, bytes_to_compare_size);

	for(u32 i = 0; i < num_bytes; ++i)
	{
		if(!is_byte_wildcard[i] && file_buffer[i] != bytes_to_compare[i])
		{
			return false;
		}
	}

	return true;
}

// Attempts to match a cached file to any previously loaded groups.
//
// @Parameters:
// 1. temporary_arena - The Arena structure where any intermediary strings are stored.
// 2. custom_groups - The group data previously loaded from files on disk.
// 3. entry_to_match - The Matchable_Cache_Entry structure that takes any parameters that should be matched to the groups.
// The matched file and/or URL group names are returned in this same structure.
//
// @Returns: True if the cached file matched at least one group. Otherwise, false.
bool match_cache_entry_to_groups(Arena* temporary_arena, Custom_Groups* custom_groups, Matchable_Cache_Entry* entry_to_match)
{
	// If no groups were loaded.
	if(custom_groups == NULL) return false;

	bool should_match_file_group = entry_to_match->should_match_file_group;
	bool should_match_url_group = entry_to_match->should_match_url_group;
	TCHAR* matched_file_group_name = NULL;
	TCHAR* matched_url_group_name = NULL;
	
	TCHAR* full_file_path = entry_to_match->full_file_path;
	TCHAR* mime_type_to_match = entry_to_match->mime_type_to_match;
	TCHAR* file_extension_to_match = entry_to_match->file_extension_to_match;

	// Read the cached file's signature, taking into account empty files and file's smaller than the signature buffer (reading
	// less bytes than the ones requested).
	u32 file_signature_size = 0;
	bool read_file_signature_successfully = read_first_file_bytes(	full_file_path,
																	custom_groups->file_signature_buffer,
																	custom_groups->file_signature_buffer_size,
																	true, &file_signature_size) && file_signature_size > 0;

	Url_Parts url_parts_to_match = {};
	bool partioned_url_successfully = should_match_url_group
									&& partition_url(temporary_arena, entry_to_match->url_to_match, &url_parts_to_match);

	for(u32 i = 0; i < custom_groups->num_groups; ++i)
	{
		Group group = custom_groups->groups[i];

		// If we have yet to match a file group ('matched_file_group_name' is NULL).
		if(group.type == GROUP_FILE && should_match_file_group)
		{
			// Match a file signature by comparing each individual byte while taking into account wildcards, which match any byte.
			if(matched_file_group_name == NULL && read_file_signature_successfully)
			{
				for(u32 j = 0; j < group.file_info.num_file_signatures; ++j)
				{
					File_Signature* signature = group.file_info.file_signatures[j];
					_ASSERT(signature->num_bytes <= custom_groups->file_signature_buffer_size);

					// Skip invalid file signatures.
					if(signature->bytes != NULL
						&& compare_file_bytes_using_wildcards(	custom_groups->file_signature_buffer, file_signature_size,
																signature->bytes, signature->is_wildcard, signature->num_bytes))
					{
						matched_file_group_name = group.name;
					}
				}
			}

			// Match a MIME type by comparing the beginning of the string (case insensitive).
			if(matched_file_group_name == NULL && mime_type_to_match != NULL)
			{
				for(u32 j = 0; j < group.file_info.num_mime_types; ++j)
				{
					TCHAR* mime_type_in_group = group.file_info.mime_types[j];
					if(string_starts_with(mime_type_to_match, mime_type_in_group, true))
					{
						matched_file_group_name = group.name;
					}
				}		
			}

			// Match a file extension by comparing strings (case insensitive).
			if(matched_file_group_name == NULL && file_extension_to_match != NULL)
			{
				for(u32 j = 0; j < group.file_info.num_file_extensions; ++j)
				{
					TCHAR* file_extension_in_group = group.file_info.file_extensions[j];
					if(strings_are_equal(file_extension_to_match, file_extension_in_group, true))
					{
						matched_file_group_name = group.name;
					}
				}
			}
		}
		// If we have yet to match a URL group ('matched_url_group_name' is NULL).
		else if(group.type == GROUP_URL && should_match_url_group)
		{
			// Match a URL by comparing the ending of the host and beginning of the path components (case insensitive).
			if(matched_url_group_name == NULL && partioned_url_successfully)
			{
				for(u32 j = 0; j < group.url_info.num_domains; ++j)
				{
					Domain* domain = group.url_info.domains[j];
					// The URL we partioned always has a 'path', but the 'host' might be NULL.
					// The opposite is true for a URL group: the 'path' might be NULL, but the host always exists.
					bool urls_match = (url_parts_to_match.host != NULL) && string_ends_with(url_parts_to_match.host, domain->host, true);

					if(domain->path != NULL)
					{
						urls_match = urls_match && string_starts_with(url_parts_to_match.path, domain->path, true);
					}

					if(urls_match)
					{
						matched_url_group_name = group.name;
					}
				}
			}
		}

		// If we matched the groups we wanted, we don't need to continue checking.
		// We either don't want to match a given group type (meaning we can exit if we got the other),
		// or we do want to match it (meaning we need to check if we got it).
		if( (!should_match_file_group || matched_file_group_name != NULL)
			&& (!should_match_url_group || matched_url_group_name != NULL) )
		{
			break;
		}
	}

	entry_to_match->matched_file_group_name = matched_file_group_name;
	entry_to_match->matched_url_group_name = matched_url_group_name;

	// If we matched at least one group.
	return (matched_file_group_name != NULL) || (matched_url_group_name != NULL);
}
