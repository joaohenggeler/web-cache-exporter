#include "web_cache_exporter.h"
#include "custom_groups.h"

/*
	This file defines the necessary functions used to load groups files, and match their contents to cache entries. Groups files are
	text files that define zero or more groups, each one specifying a list of attributes to match to files found on a web browser or
	plugin's cache.

	To learn how these files are supposed to work, refer to the "About Groups.txt" help file in "Source\Groups".
*/

static const TCHAR* GROUP_FILES_SEARCH_QUERY = T("*.group");

// Various keywords and delimiters for the group file syntax.
static const char COMMENT = ';';
static const char* LINE_DELIMITERS = "\r\n";
static const char* TOKEN_DELIMITERS = " \t";
static const char* BEGIN_FILE_GROUP = "BEGIN_FILE_GROUP";
static const char* BEGIN_URL_GROUP = "BEGIN_URL_GROUP";
static const char* END_GROUP = "END";

static const char* BEGIN_FILE_SIGNATURES = "BEGIN_FILE_SIGNATURES";
static const TCHAR* BYTE_DELIMITERS = T(" ");
static const char* BEGIN_MIME_TYPES = "BEGIN_MIME_TYPES";
static const char* BEGIN_FILE_EXTENSIONS = "BEGIN_FILE_EXTENSIONS";
static const char* DEFAULT_FILE_EXTENSION = "DEFAULT_FILE_EXTENSION";
static const char* BEGIN_DOMAINS = "BEGIN_DOMAINS";
static const TCHAR* ANY_TOP_OR_SECOND_LEVEL_DOMAIN = T(".*");
static const TCHAR* URL_PATH_DELIMITERS = T("/");

static const int MAX_FILE_SIGNATURE_BUFFER_SIZE = (int) kilobytes_to_bytes(1);

// Finds each group file on disk and retrieves the number of groups and how many bytes are (roughly) required to store them.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the executable, which is used to resolve the group files directory.
// 2. result_num_groups - The number of groups found.
//
// @Returns: The total size in bytes required to store the groups found.
size_t get_total_group_files_size(Exporter* exporter, int* result_num_groups)
{
	Arena* temporary_arena = &(exporter->temporary_arena);
	lock_arena(temporary_arena);

	Traversal_Result* group_files = find_objects_in_directory(temporary_arena, exporter->group_files_path, GROUP_FILES_SEARCH_QUERY, TRAVERSE_FILES, false);
	
	size_t total_group_size = 0;
	int num_groups = 0;

	for(int i = 0; i < group_files->num_objects; ++i)
	{
		Traversal_Object_Info file_info = group_files->object_info[i];
		TCHAR* file_path = file_info.object_path;

		lock_arena(temporary_arena);

		u64 file_size = 0;
		char* file = (char*) read_entire_file(temporary_arena, file_path, &file_size, true);
	
		if(file != NULL)
		{
			String_Array<char>* split_lines = split_string(temporary_arena, file, LINE_DELIMITERS);
		
			for(int j = 0; j < split_lines->num_strings; ++j)
			{
				char* line = split_lines->strings[j];
				line = skip_leading_whitespace(line);

				if(*line == COMMENT || string_is_empty(line))
				{
					// Skip comments and empty lines.
				}
				else
				{
					// Keep track of the total group file string data. We're essentially getting a single byte character
					// string's length plus the null terminator here. At the end, we'll multiply this value by sizeof(TCHAR).
					// This should guarantee enough memory (in excess) for both types of build (char vs wchar_t).
					total_group_size += string_size(line);

					String_Array<char>* split_tokens = split_string(temporary_arena, line, TOKEN_DELIMITERS, 1);
					
					if(split_tokens->num_strings == 2)
					{
						char* group_type = split_tokens->strings[0];
						char* group_name = split_tokens->strings[1];

						if( (strings_are_equal(group_type, BEGIN_FILE_GROUP) || strings_are_equal(group_type, BEGIN_URL_GROUP))
							&& !string_is_empty(group_name) )
						{
							++num_groups;
						}
					}
				}
			}
		}
		else
		{
			log_error("Load Group File: Failed to read the group file.");
		}

		clear_arena(temporary_arena);
		unlock_arena(temporary_arena);
	}

	clear_arena(temporary_arena);
	unlock_arena(temporary_arena);

	*result_num_groups = num_groups;
	
	// Total Size = Size for the Group array + Size for the string data + Size for the file signature buffer.
	return 	sizeof(Custom_Groups) + MAX(num_groups - 1, 0) * sizeof(Group)
			+ total_group_size * sizeof(TCHAR)
			+ MAX_FILE_SIGNATURE_BUFFER_SIZE;
}

// Copies the current string tokens from a line of delimited values, and converts it from UTF-8 to a TCHAR (ANSI or UTF-16 string
// depending on the build target). This function is meant to be called consecutively until the current group list type ends. It will
// keep track of currently processed elements and the number of tokens found.
//
// @Parameters:
// 1. permanent_arena - The Arena structure that will receive the final converted TCHAR string.
// 2. temporary_arena - The Arena structure that will be used to store an intermediary UTF-16 string on Windows 98 and ME.
// See: convert_utf_8_string_to_tchar().
// 3. list_line - The current line of delimited values.
// 4. token_delimiters - The character delimiters.
// 5. token_strings - The currently processed strings tokens stored contiguously in memory. You must pass NULL when calling this
// function for the first time.
// 6. token_counter - The current number of processed tokens. You must pass zero when calling this function for the first time.
//
// @Returns: Nothing.
static void copy_string_from_list_to_group(	Arena* permanent_arena, Arena* temporary_arena,
											char* list_line, const char* token_delimiters,
											TCHAR** token_strings, int* token_counter)
{
	if(*token_strings == NULL) *token_strings = push_arena(permanent_arena, 0, TCHAR);

	String_Array<char>* split_tokens = split_string(temporary_arena, list_line, token_delimiters);

	for(int i = 0; i < split_tokens->num_strings; ++i)
	{
		char* token = split_tokens->strings[i];
		convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, token);
	}

	*token_counter += split_tokens->num_strings;
}

// Loads a specific group file on disk, while also keeping track of the total number of processed groups and the maximum file signature
// size in bytes.
//
// @Parameters:
// 1. permanent_arena - The Arena structure that will receive the loaded group data.
// 2. temporary_arena - The Arena structure where any intermediary strings are stored.
// 3. secondary_temporary_arena - The secondary Arena structure where any intermediary strings are stored. This is only in the Windows 98
// and ME builds. On the Windows 2000 to 10 builds, this parameter is unused.
// 4. file_path - The path of the group file to load.
// 5. group_array - The preallocated group array. Each group loads its data to a specific index, which is tracked by 'num_processed_groups'.
// 6. num_processed_groups - The current total number of processed groups.
// 7. max_num_file_signature_bytes - The current maximum file signature size. This is later used to allocate an array that is just large
// enough to load each processed file signature.
//
// @Returns: Nothing.
static void load_group_file(Arena* permanent_arena, Arena* temporary_arena, Arena* secondary_temporary_arena,
							const TCHAR* file_path, Group* group_array, bool enabled_for_filtering,
							int* num_processed_groups, int* max_num_file_signature_bytes)
{
	lock_arena(temporary_arena);

	TCHAR* group_filename = PathFindFileName(file_path);
	log_info("Load Group File: Loading the group file '%s'.", group_filename);

	u64 file_size = 0;
	char* file = (char*) read_entire_file(temporary_arena, file_path, &file_size, true);
	
	if(file == NULL)
	{
		log_error("Load Group File: Failed to read the group file '%s'.", group_filename);
		clear_arena(temporary_arena);
		unlock_arena(temporary_arena);
		return;
	}

	String_Array<char>* split_lines = split_string(temporary_arena, file, LINE_DELIMITERS);

	// Keep track of which group we're loading data to.
	Group_Type current_group_type = GROUP_NONE;
	Group* group = NULL;

	// Keep track of the strings that are loaded from each list type. These are kept contiguously in memory, so this address points
	// to the first string.
	// These counters and addresses are set back to zero and NULL after processing their respective list type.
	List_Type current_list_type = LIST_NONE;
	
	int num_file_signatures = 0;
	TCHAR* file_signature_strings = NULL;

	int num_mime_types = 0;
	TCHAR* mime_type_strings = NULL;
	
	int num_file_extensions = 0;
	TCHAR* file_extension_strings = NULL;
	
	int num_domains = 0;
	TCHAR* domain_strings = NULL;

	for(int i = 0; i < split_lines->num_strings; ++i)
	{
		char* line = split_lines->strings[i];
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
				case(LIST_INVALID):
				{
					// Do nothing.
				} break;

				// Aggregate the group's file signatures.
				case(LIST_FILE_SIGNATURES):
				{
					// Create a file signature array.
					File_Signature** file_signatures = push_arena(permanent_arena, num_file_signatures * sizeof(File_Signature*), File_Signature*);
					
					for(int j = 0; j < num_file_signatures; ++j)
					{
						// Create each file signature by converting each space delimited string to a byte or wildcard.
						++(group->file_info.num_file_signatures);
						File_Signature* signature = push_arena(permanent_arena, sizeof(File_Signature), File_Signature);

						TCHAR* next_file_signature_string = skip_to_next_string(file_signature_strings);
						String_Array<TCHAR>* split_bytes = split_string(temporary_arena, file_signature_strings, BYTE_DELIMITERS);
						int num_bytes = split_bytes->num_strings;

						if(num_bytes > MAX_FILE_SIGNATURE_BUFFER_SIZE)
						{
							log_warning("Load Group File: The file signature number %d in the group '%s' has %d bytes when the maximum is %d. These extra bytes will be ignored.", j+1, group->name, num_bytes, MAX_FILE_SIGNATURE_BUFFER_SIZE);
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
						
						for(int k = 0; k < num_bytes; ++k)
						{
							TCHAR* byte_string = split_bytes->strings[k];

							if(strings_are_equal(byte_string, T("__")))
							{
								is_wildcard[k] = true;
							}
							else if(convert_hexadecimal_string_to_byte(byte_string, &bytes[k]))
							{
								is_wildcard[k] = false;
							}
							else
							{
								log_error("Load Group File: The string '%s' cannot be converted into a byte. The file signature number %d in the group '%s' will be skipped.", byte_string, j+1, group->name);
								num_bytes = 0;
								bytes = NULL;
								is_wildcard = NULL;
								break;
							}
						}

						signature->num_bytes = num_bytes;
						signature->bytes = bytes;
						signature->is_wildcard = is_wildcard;
						file_signatures[j] = signature;

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
					group->file_info.num_mime_types += num_mime_types;

					num_mime_types = 0;
					mime_type_strings = NULL;
				} break;

				// Aggregate the group's file extensions.
				case(LIST_FILE_EXTENSIONS):
				{
					// Create a file extension array.
					group->file_info.file_extensions = build_array_from_contiguous_strings(permanent_arena, file_extension_strings, num_file_extensions);
					group->file_info.num_file_extensions += num_file_extensions;

					num_file_extensions = 0;
					file_extension_strings = NULL;
				} break;

				// Aggregate the group's domains.
				case(LIST_DOMAINS):
				{
					// Create a domain array.
					Domain** domains = push_arena(permanent_arena, num_domains * sizeof(Domain*), Domain*);
					
					for(int j = 0; j < num_domains; ++j)
					{
						// Create each domain by splitting the host and path URL components.
						++(group->url_info.num_domains);
						Domain* domain = push_arena(permanent_arena, sizeof(Domain), Domain);

						TCHAR* next_domain_string = skip_to_next_string(domain_strings);
						String_Array<TCHAR>* split_url = split_string(temporary_arena, domain_strings, URL_PATH_DELIMITERS, 1);

						TCHAR* host = split_url->strings[0];
						TCHAR* path = (split_url->num_strings == 2) ? (split_url->strings[1]) : (NULL);
						
						if(string_ends_with(host, ANY_TOP_OR_SECOND_LEVEL_DOMAIN))
						{
							domain->match_any_top_or_second_level_domain = true;
							TCHAR* last_period = _tcsrchr(host, T('.'));
							if(last_period != NULL) *last_period = T('\0');
						}
						else
						{
							domain->match_any_top_or_second_level_domain = false;
						}

						domain->host = push_string_to_arena(permanent_arena, host);
						if(path != NULL && !string_is_empty(path))
						{
							domain->path = push_string_to_arena(permanent_arena, path);
						}
						else
						{
							domain->path = NULL;
						}

						domains[j] = domain;

						domain_strings = next_domain_string;
					}

					group->url_info.domains = domains;

					num_domains = 0;
					domain_strings = NULL;
				} break;

				default:
				{
					_ASSERT(false);
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
					String_Array<char>* split_tokens = split_string(temporary_arena, line, TOKEN_DELIMITERS, 1);

					if(split_tokens->num_strings == 2)
					{
						char* group_type = split_tokens->strings[0];
						char* group_name = split_tokens->strings[1];

						// Find the group's type.
						if(strings_are_equal(group_type, BEGIN_FILE_GROUP))
						{
							current_group_type = GROUP_FILE;
						}
						else if(strings_are_equal(group_type, BEGIN_URL_GROUP))
						{
							current_group_type = GROUP_URL;
						}

						if(current_group_type != GROUP_NONE && !string_is_empty(group_name))
						{
							// Get this group's index in the global custom groups array.
							// This will allow us to use this group's preallocated Group structure.
							int group_idx = *num_processed_groups;
							++(*num_processed_groups);
							group = &group_array[group_idx];

							// @Assert: Make sure we handle each group type.
							_ASSERT( (current_group_type == GROUP_FILE) || (current_group_type == GROUP_URL) );

							group->type = current_group_type;
							group->name = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, group_name);
							group->enabled_for_filtering = enabled_for_filtering;
						}
						else
						{
							// Set it to a type different than GROUP_NONE so we can handle it properly when we reach the END keyword.
							// Nothing else will be done for this invalid group.
							current_group_type = GROUP_INVALID;
							log_error("Load Group Files: Skipping invalid group of type '%hs' and name '%hs'.", group_type, group_name);
						}
					}
					else
					{
						log_error("Load Group Files: Found %d tokens while looking for a new group when two were expected.", split_tokens->num_strings);
					}
				} break;

				case(GROUP_INVALID):
				{
					// Set it to a type different than LIST_NONE so we can handle it properly when we reach the END keyword.
					// No lists will be loaded for this invalid group.
					current_list_type = LIST_INVALID;
				} break;

				// Add the lists from a file group.
				case(GROUP_FILE):
				{
					switch(current_list_type)
					{
						// Found the start of a list.
						case(LIST_NONE):
						{
							String_Array<char>* split_tokens = split_string(temporary_arena, line, TOKEN_DELIMITERS, 1);
							_ASSERT(split_tokens->num_strings > 0);
							char* first_token = split_tokens->strings[0];

							if(strings_are_equal(first_token, BEGIN_FILE_SIGNATURES))
							{
								current_list_type = LIST_FILE_SIGNATURES;
							}
							else if(strings_are_equal(first_token, BEGIN_MIME_TYPES))
							{
								current_list_type = LIST_MIME_TYPES;
							}
							else if(strings_are_equal(first_token, BEGIN_FILE_EXTENSIONS))
							{
								current_list_type = LIST_FILE_EXTENSIONS;
							}
							else if(strings_are_equal(first_token, DEFAULT_FILE_EXTENSION))
							{
								if(split_tokens->num_strings == 2)
								{
									char* second_token = split_tokens->strings[1];
									group->file_info.default_file_extension = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, second_token);
								}
								else
								{
									log_error("Load Group Files: Found %d tokens while looking for a default file extension when two were expected.", split_tokens->num_strings);
								}
							}
							else
							{
								log_error("Load Group File: Unknown group file list type '%hs'.", first_token);
							}
						} break;

						// Add each file signature (one per line).
						case(LIST_FILE_SIGNATURES):
						{
							// File signatures are processed differently from MIME types and file extensions.
							// We only allow one file signature per line, and the way each token (byte) is delimited is different.
							++num_file_signatures;
							if(file_signature_strings == NULL) file_signature_strings = push_arena(temporary_arena, 0, TCHAR);
							convert_utf_8_string_to_tchar(temporary_arena, secondary_temporary_arena, line);
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

						default:
						{
							_ASSERT(false);
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
								log_error("Load Group File: Unknown group URL list type '%hs'.", line);
							}
						} break;

						// Add each domain (one per line).
						case(LIST_DOMAINS):
						{
							++num_domains;
							if(domain_strings == NULL) domain_strings = push_arena(temporary_arena, 0, TCHAR);
							convert_utf_8_string_to_tchar(temporary_arena, secondary_temporary_arena, line);
						} break;

						default:
						{
							_ASSERT(false);
						} break;
					}
				} break;

				default:
				{
					_ASSERT(false);
				} break;
			}
		}
	}

	if(current_list_type != LIST_NONE)
	{
		log_warning("Load Group File: Found unterminated list of type '%s' in the group file '%s'.", LIST_TYPE_TO_STRING[current_list_type], group_filename);
	}

	if(current_group_type != GROUP_NONE)
	{
		log_warning("Load Group File: Found unterminated group of type '%s' in the group file '%s'.", GROUP_TYPE_TO_STRING[current_group_type], group_filename);
	}

	clear_arena(temporary_arena);
	unlock_arena(temporary_arena);
}

// Determines if the groups present in a given group file should be enabled for filtering.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the group files should be loaded.
// 2. filename - The current name of the group file on disk that should be checked. This filename may contain a .group extension,
// though this will be ignored when performing comparisons.
//
// @Returns: True if the groups in this file should be enabled for filtering. Otherwise, false. This function always returns false
// if the '-filter-by-groups' command line option is not used.
static bool should_groups_in_file_be_enabled_for_filtering(Exporter* exporter, TCHAR* filename)
{
	if(!exporter->should_filter_by_groups) return false;

	String_Array<TCHAR>* group_files_for_filtering = exporter->group_files_for_filtering;

	// Temporarily remove the .group file extension.
	TCHAR* file_extension = skip_to_file_extension(filename, true);
	TCHAR previous_char = *file_extension;
	*file_extension = T('\0');
		
	bool enabled = false;

	for(int i = 0; i < group_files_for_filtering->num_strings; ++i)
	{
		TCHAR* filename_for_filtering = group_files_for_filtering->strings[i];
		if(filenames_are_equal(filename, filename_for_filtering))
		{
			enabled = true;
			break;
		}
	}

	*file_extension = previous_char;

	return enabled;
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
void load_all_group_files(Exporter* exporter, int num_groups)
{
	if(num_groups == 0)
	{
		log_warning("Load All Group Files: Attempted to load zero groups. No groups will be loaded.");
		return;
	}

	// The relevant loaded group data will go to the permanent arena, and any intermediary data to the temporary one.
	Arena* permanent_arena = &(exporter->permanent_arena);
	Arena* temporary_arena = &(exporter->temporary_arena);
	
	// Find each group file on disk. We'll keep track of their filenames by storing them contiguously in memory.
	Traversal_Result* group_files = find_objects_in_directory(temporary_arena, exporter->group_files_path, GROUP_FILES_SEARCH_QUERY, TRAVERSE_FILES, false);
	TCHAR* first_group_filename = push_arena(temporary_arena, 0, TCHAR);
	
	int num_group_files = 0;
	for(int i = 0; i < group_files->num_objects; ++i)
	{
		Traversal_Object_Info file_info = group_files->object_info[i];
		TCHAR* filename = file_info.object_name;
		size_t filename_size = string_size(filename);
		push_and_copy_to_arena(temporary_arena, filename_size, u8, filename, filename_size);
		++num_group_files;
	}

	if(num_group_files == 0)
	{
		log_error("Load All Group Files: Expected to load %d groups from at least one file on disk, but no files were found. No groups will be loaded.", num_groups);
		return;
	}

	// Build an array with the filenames so we can sort them alphabetically. We'll do this because find_objects_in_directory()
	// doesn't guarantee any specific order, and we want the way the group files are loaded to be deterministic.
	TCHAR** group_filenames_array = build_array_from_contiguous_strings(temporary_arena, first_group_filename, num_group_files);
	qsort(group_filenames_array, num_group_files, sizeof(TCHAR*), compare_filenames);

	// Build an array of group structures, each containing 
	// @Note: Don't confuse 'num_group_files' with 'num_groups'. The former is the number of .group files on disk, and the latter
	// the total number of groups that are defined in them. Each group file may define zero or more groups.
	// The number of groups is always greater than zero here.
	size_t custom_groups_size = sizeof(Custom_Groups) + sizeof(Group) * (num_groups - 1);
	Custom_Groups* custom_groups = push_arena(permanent_arena, custom_groups_size, Custom_Groups);

	// The global group counter that is used to keep track of each group's place in the array.
	int num_processed_groups = 0;
	int max_num_file_signature_bytes = 0;
	
	for(int i = 0; i < num_group_files; ++i)
	{
		TCHAR* group_filename = group_filenames_array[i];
		TCHAR group_file_path[MAX_PATH_CHARS] = T("");
		PathCombine(group_file_path, exporter->group_files_path, group_filename);
		bool enabled_for_filtering = should_groups_in_file_be_enabled_for_filtering(exporter, group_filename);

		load_group_file(permanent_arena, temporary_arena, &(exporter->secondary_temporary_arena),
						group_file_path, custom_groups->groups, enabled_for_filtering,
						&num_processed_groups, &max_num_file_signature_bytes);
	}

	custom_groups->num_groups = num_processed_groups;
	if(num_processed_groups != num_groups)
	{
		log_error("Load All Group Files: Loaded %d groups when %d were expected.", num_processed_groups, num_groups);
	}

	log_info("Load All Group Files: Allocating %d bytes for the file signature buffer.", max_num_file_signature_bytes);
	custom_groups->file_signature_buffer = push_arena(permanent_arena, max_num_file_signature_bytes, u8);
	custom_groups->file_signature_buffer_size = max_num_file_signature_bytes;

	exporter->custom_groups = custom_groups;
}

// Compares an array of bytes read from a file to a previously loaded file signature.
//
// @Parameters:
// 1. file_buffer - The buffer that contains the first bytes from a file. 
// 2. file_buffer_size - The size of this buffer in bytes.
// 3. signature - The file signature whose bytes and wildcards will be used during this comparison.
//
// @Returns: True if the file signature matched. Otherwise, false.
static bool bytes_match_file_signature(u8* file_buffer, u32 file_buffer_size, File_Signature* signature)
{
	// Skip invalid file signatures.
	if(signature->bytes == NULL) return false;

	// Skip signatures that can never match due to their size.
	if(file_buffer_size < (u32) signature->num_bytes) return false;

	for(int i = 0; i < signature->num_bytes; ++i)
	{
		if(!signature->is_wildcard[i] && file_buffer[i] != signature->bytes[i])
		{
			return false;
		}
	}

	return true;
}

// Checks if a URL's host ends with a given suffix. This comparison is case insensitive and takes into account the period separator
// between labels.
//
// For example, if the suffix is "go.com", the following hosts match:
// - "go.com"
// - "www.go.com"
// - "disney.go.com"
//
// But the following don't:
// - "lego.com"
// - "www.lego.com"
//
// @Parameters:
// 1. host - The URL's host to check.
// 2. suffix - The suffix to use.
//
// @Returns: True if the host ends with that suffix. Otherwise, false.
static bool url_host_ends_with(const TCHAR* host, const TCHAR* suffix)
{
	size_t host_length = string_length(host);
	size_t suffix_length = string_length(suffix);
	if(suffix_length > host_length) return false;

	if(strings_are_equal(host, suffix, true)) return true;

	const TCHAR* suffix_in_host = host + host_length - suffix_length;
	bool host_has_period_before_suffix = (suffix_length + 1 <= host_length) && ( *(suffix_in_host - 1) == T('.') );

	return host_has_period_before_suffix && strings_are_equal(suffix_in_host, suffix, true);
}

// Compares an array of bytes read from a file to a previously loaded file signature.
//
// @Parameters:
// 1. host - The URL's host to match. This value may be NULL.
// 2. path - The URL's path to match. This value cannot be NULL.
// 3. domain - The domain whose host and path will be used during this comparison. The path may be NULL, but the host
// must always be defined.
//
// @Returns: True if the domain matched. Otherwise, false.
static bool url_host_and_path_match_domain(TCHAR* host, TCHAR* path, Domain* domain)
{
	// The URL we partioned always has a 'path', but the 'host' might be NULL.
	// The opposite is true for a URL group: the 'path' might be NULL, but the host always exists.
	_ASSERT(path != NULL);
	_ASSERT(domain->host != NULL);

	// Match any top level domain if it was requested in the URL group.
	TCHAR* last_period = NULL;
	TCHAR* second_to_last_period = NULL;

	// We'll do this by first removing the current host's top level domain before doing the first comparison.
	if(domain->match_any_top_or_second_level_domain && host != NULL)
	{
		last_period = _tcsrchr(host, T('.'));
		if(last_period != NULL)
		{
			*last_period = T('\0');
			second_to_last_period = _tcsrchr(host, T('.'));
		}
	}

	bool urls_match = (host != NULL) && url_host_ends_with(host, domain->host);

	// Put any removed top level domains back.
	if(last_period != NULL)
	{
		*last_period = T('.');
	}

	// And by then removing the current host's second level domain before doing a second comparison.
	// We only need to do this if there wasn't a previous match.
	if(!urls_match && second_to_last_period != NULL)
	{
		*second_to_last_period = T('\0');
		urls_match = (host != NULL) && url_host_ends_with(host, domain->host);
		// Put any removed second level domains back.
		*second_to_last_period = T('.');
	}

	// If there is also a path to compare, it must match the current URL's path. Otherwise, the whole match fails.
	if(domain->path != NULL)
	{
		urls_match = urls_match && string_begins_with(path, domain->path, true);
	}

	return urls_match;
}

// Attempts to match a cached file to any previously loaded groups.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on the previously group files.
// 2. entry_to_match - The Matchable_Cache_Entry structure that takes any parameters that should be matched to the groups.
// The matched file and/or URL group names are returned in this same structure.
//
// @Returns: True if the cached file matched at least one group. Otherwise, false.
bool match_cache_entry_to_groups(Exporter* exporter, Matchable_Cache_Entry* entry_to_match)
{
	Arena* temporary_arena = &(exporter->temporary_arena);
	Custom_Groups* custom_groups = exporter->custom_groups;

	// If no groups were loaded.
	if(custom_groups == NULL) return false;

	Group* file_group = NULL;
	Group* url_group = NULL;
	
	bool should_match_file_group = entry_to_match->should_match_file_group;
	bool should_match_url_group = entry_to_match->should_match_url_group;

	TCHAR* full_file_path = entry_to_match->full_file_path;
	TCHAR* mime_type_to_match = entry_to_match->mime_type_to_match;
	TCHAR* file_extension_to_match = entry_to_match->file_extension_to_match;

	// Read the cached file's signature, taking into account empty files and file's smaller than the signature buffer (reading
	// less bytes than the ones requested).
	u32 file_signature_size = 0;
	bool read_file_signature_successfully = does_file_exist(full_file_path)
										&& read_first_file_bytes(full_file_path,
																 custom_groups->file_signature_buffer,
																 custom_groups->file_signature_buffer_size,
																 true, &file_signature_size) && file_signature_size > 0;

	Url_Parts url_parts_to_match = {};
	bool partioned_url_successfully = should_match_url_group
									&& partition_url(temporary_arena, entry_to_match->url_to_match, &url_parts_to_match);

	for(int i = 0; i < custom_groups->num_groups; ++i)
	{
		Group* group = &(custom_groups->groups[i]);

		if(group->type == GROUP_FILE && should_match_file_group)
		{
			// Match a file signature by comparing each individual byte while taking into account wildcards, which match any byte.
			if(file_group == NULL && read_file_signature_successfully)
			{
				for(int j = 0; j < group->file_info.num_file_signatures; ++j)
				{
					File_Signature* signature = group->file_info.file_signatures[j];
					_ASSERT(signature->num_bytes <= custom_groups->file_signature_buffer_size);

					if(bytes_match_file_signature(custom_groups->file_signature_buffer, file_signature_size, signature))
					{
						file_group = group;
					}
				}
			}

			// Match a MIME type by comparing the beginning of the string (case insensitive).
			if(file_group == NULL && mime_type_to_match != NULL)
			{
				for(int j = 0; j < group->file_info.num_mime_types; ++j)
				{
					TCHAR* mime_type_in_group = group->file_info.mime_types[j];
					if(string_begins_with(mime_type_to_match, mime_type_in_group, true))
					{
						file_group = group;
					}
				}		
			}

			// Match a file extension by comparing strings (case insensitive).
			if(file_group == NULL && file_extension_to_match != NULL)
			{
				for(int j = 0; j < group->file_info.num_file_extensions; ++j)
				{
					TCHAR* file_extension_in_group = group->file_info.file_extensions[j];
					if(filenames_are_equal(file_extension_to_match, file_extension_in_group))
					{
						file_group = group;
					}
				}
			}
		}
		else if(group->type == GROUP_URL && should_match_url_group)
		{
			// Match a URL by comparing the ending of the host and beginning of the path components (case insensitive).
			if(url_group == NULL && partioned_url_successfully)
			{
				for(int j = 0; j < group->url_info.num_domains; ++j)
				{
					Domain* domain = group->url_info.domains[j];

					if(url_host_and_path_match_domain(url_parts_to_match.host, url_parts_to_match.path, domain))
					{
						url_group = group;
					}
				}
			}
		}

		bool matched_all_requested_group_types = (!should_match_file_group || file_group != NULL) && (!should_match_url_group || url_group != NULL);
		if(matched_all_requested_group_types)
		{
			break;
		}
	}

	entry_to_match->matched_file_group_name = (file_group != NULL) ? (file_group->name) : (NULL);
	entry_to_match->matched_url_group_name = (url_group != NULL) ? (url_group->name) : (NULL);

	entry_to_match->matched_default_file_extension = NULL;

	if(file_group != NULL)
	{
		if(file_group->file_info.default_file_extension != NULL)
		{
			entry_to_match->matched_default_file_extension = file_group->file_info.default_file_extension;
		}
		else if(file_group->file_info.num_file_extensions == 1)
		{
			entry_to_match->matched_default_file_extension = file_group->file_info.file_extensions[0];
		}
	}

	entry_to_match->match_is_enabled_for_filtering = (file_group != NULL && file_group->enabled_for_filtering) || (url_group != NULL && url_group->enabled_for_filtering);

	// If we matched at least one group.
	return (file_group != NULL) || (url_group != NULL);
}
