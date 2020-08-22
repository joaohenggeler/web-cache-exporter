#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "custom_groups.h"

static const TCHAR* GROUP_FILES_DIRECTORY = TEXT("Groups");
static const TCHAR* GROUP_FILES_SEARCH_PATH = TEXT("Groups\\*.group");

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

static TCHAR* copy_utf_8_string_to_tchar(Arena* permanent_arena, Arena* temporary_arena, const char* utf_8_string)
{
	int num_chars_required_wide = MultiByteToWideChar(CP_UTF8, 0, utf_8_string, -1, NULL, 0);
	if(num_chars_required_wide == 0)
	{
		log_print(LOG_ERROR, "Copy Utf-8 String To Tchar: Failed to find the number of characters necessary to represent the string as a Wide string with the error code %lu.", GetLastError());
		_ASSERT(false);
		return NULL;
	}

	#ifdef BUILD_9X
		Arena* wide_string_arena = temporary_arena;
	#else
		Arena* wide_string_arena = permanent_arena;
		temporary_arena;
	#endif

	int size_required_wide = num_chars_required_wide * sizeof(wchar_t);
	wchar_t* wide_string = push_arena(wide_string_arena, size_required_wide, wchar_t);
	if(MultiByteToWideChar(CP_UTF8, 0, utf_8_string, -1, wide_string, num_chars_required_wide) == 0)
	{
		log_print(LOG_ERROR, "Copy Utf-8 String To Tchar: Failed to convert the string to a Wide string with the error code %lu.", GetLastError());
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

static TCHAR* skip_to_end_of_string(TCHAR* str)
{
	while(*str != TEXT('\0')) ++str;
	return str;
}

static TCHAR** build_array_from_contiguous_strings(Arena* arena, TCHAR* first_string, u32 num_strings)
{
	TCHAR** string_array = push_arena(arena, num_strings * sizeof(TCHAR*), TCHAR*);

	for(u32 i = 0; i < num_strings; ++i)
	{
		string_array[i] = first_string;
		first_string = skip_to_end_of_string(first_string);
		++first_string;
	}

	return string_array;
}

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

size_t get_total_group_files_size(Exporter* exporter, u32* result_num_groups)
{
	size_t total_file_size = 0;
	u32 total_num_groups = 0;

	TCHAR search_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(search_path, exporter->executable_path, GROUP_FILES_SEARCH_PATH);

	WIN32_FIND_DATA file_find_data = {};
	HANDLE search_handle = FindFirstFile(search_path, &file_find_data);
	bool found_file = search_handle != INVALID_HANDLE_VALUE;

	// Find each group file on disk and keep track of their total file disk and number of groups.
	while(found_file)
	{
		// Ignore directories.
		if((file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			#ifdef BUILD_32_BIT
				size_t file_size = file_find_data.nFileSizeLow;
			#else
				size_t file_size = combine_high_and_low_u32s_into_u64(file_find_data.nFileSizeHigh, file_find_data.nFileSizeLow);
			#endif
			
			total_file_size += file_size;
			
			TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
			PathCombine(group_file_path, exporter->executable_path, GROUP_FILES_DIRECTORY);
			PathAppend(group_file_path, file_find_data.cFileName);

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

					char* group_name = NULL;
					char* group_type = strtok_s(line, TOKEN_DELIMITERS, &group_name);
					if(group_type != NULL && group_name != NULL)
					{
						if(strings_are_equal(group_type, BEGIN_FILE_GROUP)
						|| strings_are_equal(group_type, BEGIN_URL_GROUP))
						{
							++total_num_groups;
						}
					}

					line = strtok_s(NULL, LINE_DELIMITERS, &remaining_lines);
				}
			}

			SAFE_UNMAP_VIEW_OF_FILE(group_file);
			safe_close_handle(&group_file_handle);
		}

		found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
	}

	safe_find_close(&search_handle);

	*result_num_groups = total_num_groups;
	return sizeof(Custom_Groups) + MAX(total_num_groups - 1, 0) * sizeof(Group) + total_file_size * sizeof(TCHAR);
}

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
			// Skip comments.
		}
		// Reached the end of a list. Aggregate and perform any last operations on the data before moving on to the next list or group.
		else if(current_list_type != LIST_NONE && strings_are_equal(line, END_GROUP))
		{
			switch(current_list_type)
			{
				case(LIST_FILE_SIGNATURES):
				{
					File_Signature** file_signatures = push_arena(permanent_arena, num_file_signatures * sizeof(File_Signature*), File_Signature*);
					
					for(u32 i = 0; i < num_file_signatures; ++i)
					{
						++(group->file_info.num_file_signatures);
						File_Signature* signature = push_arena(permanent_arena, sizeof(File_Signature), File_Signature);

						u32 num_bytes = count_tokens_delimited_by_spaces(file_signature_strings);

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

				case(LIST_MIME_TYPES):
				{
					group->file_info.mime_types = build_array_from_contiguous_strings(permanent_arena, mime_type_strings, num_mime_types);
					group->file_info.num_mime_types = num_mime_types;

					num_mime_types = 0;
					mime_type_strings = NULL;
				} break;

				case(LIST_FILE_EXTENSIONS):
				{
					group->file_info.file_extensions = build_array_from_contiguous_strings(permanent_arena, file_extension_strings, num_file_extensions);
					group->file_info.num_file_extensions = num_file_extensions;

					num_file_extensions = 0;
					file_extension_strings = NULL;
				} break;

				case(LIST_DOMAINS):
				{
					Domain** domains = push_arena(permanent_arena, num_domains * sizeof(Domain*), Domain*);
					
					for(u32 i = 0; i < num_domains; ++i)
					{
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
				// Add a new group.
				case(GROUP_NONE):
				{
					char* group_name = NULL;
					char* group_type = strtok_s(line, TOKEN_DELIMITERS, &group_name);
					if(group_type != NULL && group_name != NULL)
					{
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
							u32 group_idx = *num_processed_groups;
							++(*num_processed_groups);
							group = &group_array[group_idx];

							group->type = current_group_type;
							group->name = copy_utf_8_string_to_tchar(permanent_arena, temporary_arena, group_name);

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

						case(LIST_FILE_SIGNATURES):
						{
							// File signatures are processed differently from MIME types and file extensions.
							// We only allow one file signature per line, and the way each token (byte) is delimited is different.
							++num_file_signatures;
							if(file_signature_strings == NULL) file_signature_strings = push_arena(temporary_arena, 0, TCHAR);
							copy_utf_8_string_to_tchar(temporary_arena, temporary_arena, line);
						} break;

						case(LIST_MIME_TYPES):
						{
							copy_string_from_list_to_group(	permanent_arena, temporary_arena,
															line, TOKEN_DELIMITERS,
															&mime_type_strings, &num_mime_types);
						} break;

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

						case(LIST_DOMAINS):
						{
							// @TODO
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
		log_print(LOG_WARNING, "Load Group File: Found unterminated list of type '%s' in the group file '%s'.", LIST_TYPE_TO_STRING[current_list_type], group_filename);
		console_print("Load Group File: Found unterminated list of type '%s' in the group file '%s'.", LIST_TYPE_TO_STRING[current_list_type], group_filename);
	}

	if(current_group_type != GROUP_NONE)
	{
		log_print(LOG_WARNING, "Load Group File: Found unterminated group of type '%s' in the group file '%s'.", GROUP_TYPE_TO_STRING[current_group_type], group_filename);
		console_print("Load Group File: Found unterminated group of type '%s' in the group file '%s'.", GROUP_TYPE_TO_STRING[current_group_type], group_filename);
	}

	SAFE_UNMAP_VIEW_OF_FILE(group_file);
	safe_close_handle(&group_file_handle);
}

int compare_filenames(const void* filename_pointer_1, const void* filename_pointer_2)
{
	TCHAR* filename_1 = *((TCHAR**) filename_pointer_1);
	TCHAR* filename_2 = *((TCHAR**) filename_pointer_2);
	return _tcscmp(filename_1, filename_2);
}

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

	// Find each group file on disk.
	u32 num_group_files = 0;
	TCHAR* first_group_filename = push_arena(temporary_arena, 0, TCHAR);
	{
		TCHAR search_path[MAX_PATH_CHARS] = TEXT("");
		PathCombine(search_path, exporter->executable_path, GROUP_FILES_SEARCH_PATH);

		WIN32_FIND_DATA file_find_data = {};
		HANDLE search_handle = FindFirstFile(search_path, &file_find_data);
		
		bool found_file = search_handle != INVALID_HANDLE_VALUE;
		while(found_file)
		{
			// Ignore directories.
			if((file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				size_t size = string_size(file_find_data.cFileName);
				push_and_copy_to_arena(temporary_arena, size, u8, file_find_data.cFileName, size);
				++num_group_files;
			}

			found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
		}

		safe_find_close(&search_handle);
	}

	if(num_group_files == 0)
	{
		log_print(LOG_ERROR, "Load All Group Files: Expected to load %I32u groups from at least one file on disk, but found none. No groups will be loaded.", num_groups);
		_ASSERT(false);
	}

	// Build an array with the filenames so we can sort them alphabetically. FindFirstFile() doesn't guarantee any specific order,
	// and we want the way the group files are loaded to be deterministic.
	TCHAR** group_filenames_array = build_array_from_contiguous_strings(temporary_arena, first_group_filename, num_group_files);
	qsort(group_filenames_array, num_group_files, sizeof(TCHAR*), compare_filenames);

	// Build an array of group structures, each containing 
	// @Note: Don't confuse 'num_group_files' with 'num_groups'. The former is the number of .group files on disk, and the latter
	// the total number of groups that are defined in them. Each group file may defined zero or more groups.
	// The number of groups is greater than zero here.
	size_t custom_groups_size = sizeof(Custom_Groups) + sizeof(Group) * (num_groups - 1);
	Custom_Groups* custom_groups = push_arena(permanent_arena, custom_groups_size, Custom_Groups);
	custom_groups->num_groups = num_groups;

	// The global group counter that is used to keep track of each group's place in the array.
	u32 num_processed_groups = 0;
	u32 max_num_file_signature_bytes = 0;
	
	for(u32 i = 0; i < num_group_files; ++i)
	{
		TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
		PathCombine(group_file_path, exporter->executable_path, GROUP_FILES_DIRECTORY);
		PathAppend(group_file_path, group_filenames_array[i]);

		load_group_file(permanent_arena, temporary_arena,
						group_file_path, custom_groups->groups, &num_processed_groups, &max_num_file_signature_bytes);
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

	// Remember that the custom group data is kept in the permanent arena.
	clear_arena(temporary_arena);

	#if 0
	for(u32 i = 0; i < custom_groups->num_groups; ++i)
	{
		Group group = custom_groups->groups[i];
		debug_log_print("########## Group '%s'", group.name);
		if(group.type == GROUP_FILE)
		{
			if(group.file_info.num_file_extensions > 0) debug_log_print("- File Extensions");
			for(u32 j = 0; j < group.file_info.num_file_extensions; ++j)
			{
				TCHAR* file_extension = group.file_info.file_extensions[j];
				debug_log_print("-> %s", file_extension);
			}

			if(group.file_info.num_mime_types > 0) debug_log_print("- MIME Types");
			for(u32 j = 0; j < group.file_info.num_mime_types; ++j)
			{
				TCHAR* mime_type = group.file_info.mime_types[j];
				debug_log_print("-> %s", mime_type);
			}

			if(group.file_info.num_file_signatures > 0) debug_log_print("- File Signatures");
			for(u32 j = 0; j < group.file_info.num_file_signatures; ++j)
			{
				debug_log_print("-> %I32u", j);
				File_Signature* signature = group.file_info.file_signatures[j];
				for(u32 k = 0; k < signature->num_bytes; ++k)
				{
					char* is_wildcard = (signature->is_wildcard[k]) ? ("true") : ("false");
					debug_log_print("--> 0x%02X %hs", signature->bytes[k], is_wildcard);
				}
			}
		}
		else if(group.type == GROUP_URL)
		{
			if(group.url_info.num_domains > 0) debug_log_print("- Domains");
			for(u32 j = 0; j < group.url_info.num_domains; ++j)
			{
				Domain* domain = group.url_info.domains[j];
				debug_log_print("-> Host = '%s'. Path = '%s'.", domain->host, (domain->path != NULL) ? (domain->path) : (TEXT("-")));
			}
		}
	}

	debug_log_print("END");

	#endif
}

static bool compare_file_bytes_using_wildcards(const TCHAR* file_path, u8* file_buffer, u32 num_bytes, u8* bytes, bool* is_wildcard)
{
	if(!read_first_file_bytes(file_path, file_buffer, num_bytes))
	{
		return false;
	}

	for(u32 i = 0; i < num_bytes; ++i)
	{
		if(!is_wildcard[i] && file_buffer[i] != bytes[i])
		{
			return false;
		}
	}

	return true;
}

bool match_cache_entry_to_groups(Arena* temporary_arena, Custom_Groups* custom_groups, Matchable_Cache_Entry* entry_to_match)
{
	bool should_match_file_group = entry_to_match->should_match_file_group;
	bool should_match_url_group = entry_to_match->should_match_url_group;
	TCHAR* matched_file_group_name = NULL;
	TCHAR* matched_url_group_name = NULL;
	
	TCHAR* full_file_path = entry_to_match->full_file_path;
	TCHAR* mime_type_to_match = entry_to_match->mime_type_to_match;
	TCHAR* file_extension_to_match = entry_to_match->file_extension_to_match;

	Url_Parts url_parts_to_match = {};
	bool partioned_url_successfully = should_match_url_group
									&& partition_url(temporary_arena, entry_to_match->url_to_match, &url_parts_to_match);

	for(u32 i = 0; i < custom_groups->num_groups; ++i)
	{
		Group group = custom_groups->groups[i];

		// If we have yet to match a file group ('matched_file_group_name' is NULL).
		if(group.type == GROUP_FILE && should_match_file_group)
		{
			// Match a file signature by comparing each individual byte while taking into account wildcards,
			// which match any byte.
			if(matched_file_group_name == NULL && full_file_path != NULL)
			{
				for(u32 j = 0; j < group.file_info.num_file_signatures; ++j)
				{
					File_Signature* signature = group.file_info.file_signatures[j];
					_ASSERT(signature->num_bytes <= custom_groups->file_signature_buffer_size);

					if(compare_file_bytes_using_wildcards(	full_file_path, custom_groups->file_signature_buffer,
															signature->num_bytes, signature->bytes, signature->is_wildcard))
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
		if 	( 	( !should_match_file_group || (should_match_file_group && matched_file_group_name != NULL) )
			&& 	( !should_match_url_group || (should_match_url_group && matched_url_group_name != NULL))
			)
		{
			break;
		}
	}

	entry_to_match->matched_file_group_name = matched_file_group_name;
	entry_to_match->matched_url_group_name = matched_url_group_name;

	// If we matched at least one group.
	return (matched_file_group_name != NULL) || (matched_url_group_name != NULL);
}