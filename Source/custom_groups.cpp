#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "custom_groups.h"

static const TCHAR* GROUP_FILES_DIRECTORY = TEXT("Groups");
static const TCHAR* GROUP_FILES_SEARCH_PATH = TEXT("Groups\\*.group");
static const char* LINE_DELIMITERS = "\r\n";
static const char* TOKEN_DELIMITERS = " \t";

size_t get_total_group_files_size(Exporter* exporter, u32* result_num_groups)
{
	size_t total_file_size = 0;
	u32 num_groups = 0;

	TCHAR search_path[MAX_PATH_CHARS] = TEXT("");
	shell_copy_and_append_path(search_path, exporter->executable_path, GROUP_FILES_SEARCH_PATH);

	WIN32_FIND_DATA file_find_data = {};
	HANDLE search_handle = FindFirstFile(search_path, &file_find_data);
	bool found_file = search_handle != INVALID_HANDLE_VALUE;

	while(found_file)
	{
		// Only handle files (skip directories).
		if((file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			#ifdef BUILD_32_BIT
				size_t file_size = file_find_data.nFileSizeLow;
			#else
				size_t file_size = combine_high_and_low_u32s_into_u64(file_find_data.nFileSizeHigh, file_find_data.nFileSizeLow);
			#endif
			
			total_file_size += file_size;
			
			TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
			shell_copy_and_append_path(group_file_path, exporter->executable_path, GROUP_FILES_DIRECTORY);
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
						if(strings_are_equal(group_type, "BEGIN_FILE_GROUP")
						|| strings_are_equal(group_type, "BEGIN_URL_GROUP"))
						{
							++num_groups;
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

	*result_num_groups = num_groups;
	return sizeof(Custom_Groups) + MAX(num_groups - 1, 0) * sizeof(Group) + total_file_size * sizeof(TCHAR);
}

void load_group_file(Arena* permanent_arena, Arena* temporary_arena,
					 const TCHAR* file_path, Group* groups, u32* num_processed_groups)
{
	permanent_arena;
	temporary_arena;

	log_print(LOG_INFO, "Load Group File: Loading file at '%s'.", file_path);

	HANDLE group_file_handle = INVALID_HANDLE_VALUE;
	u64 group_file_size = 0;
	char* group_file = (char*) memory_map_entire_file(file_path, &group_file_handle, &group_file_size, false);
	
	if(group_file == NULL)
	{
		log_print(LOG_ERROR, "Load Group File: Failed to load '%s'.", file_path);
		safe_close_handle(&group_file_handle);
		return;
	}

	char* remaining_lines = NULL;
	char* line = strtok_s(group_file, LINE_DELIMITERS, &remaining_lines);

	Group_Type current_group_type = GROUP_NONE;
	List_Type current_list_type = LIST_NONE;
	u32 current_group_index = 0;

	while(line != NULL)
	{
		line = skip_leading_whitespace(line);

		if(*line == ';')
		{
			// Skip comments.
		}
		else if(current_list_type != LIST_NONE && strings_are_equal(line, "END"))
		{
			current_list_type = LIST_NONE;	
		}
		else if(current_group_type != GROUP_NONE && strings_are_equal(line, "END"))
		{
			current_group_type = GROUP_NONE;
		}
		else
		{
			switch(current_group_type)
			{
				case(GROUP_NONE):
				{
					char* group_name = NULL;
					char* group_type = strtok_s(line, TOKEN_DELIMITERS, &group_name);
					if(group_type != NULL && group_name != NULL)
					{
						if(strings_are_equal(group_type, "BEGIN_FILE_GROUP"))
						{
							current_group_type = GROUP_FILE_GROUP;
						}
						else if(strings_are_equal(group_type, "BEGIN_URL_GROUP"))
						{
							current_group_type = GROUP_URL_GROUP;
						}
						else
						{
							// Unknown block name
						}

						if(current_group_type != GROUP_NONE)
						{
							current_group_index = *num_processed_groups;
							++(*num_processed_groups);

							groups[current_group_index].type = current_group_type;
							groups[current_group_index].name = copy_utf_8_string_to_tchar(permanent_arena, group_name);	
						}
					}
				} break;

				case(GROUP_FILE_GROUP):
				{
					switch(current_list_type)
					{
						case(LIST_NONE):
						{
							if(strings_are_equal(line, "BEGIN_MIME_TYPES"))
							{
								current_list_type = LIST_MIME_TYPES;
							}
							else if(strings_are_equal(line, "BEGIN_FILE_EXTENSIONS"))
							{
								current_list_type = LIST_FILE_EXTENSIONS;
							}
						} break;

						case(LIST_FILE_SIGNATURES):
						{
							// @TODO
						} break;

						case(LIST_MIME_TYPES):
						{
							// @TODO
						} break;

						case(LIST_FILE_EXTENSIONS):
						{
							char* remaining_file_extensions = NULL;
							char* file_extension = strtok_s(line, TOKEN_DELIMITERS, &remaining_file_extensions);
							while(file_extension != NULL)
							{
								debug_log_print("%hs", file_extension);

								file_extension = strtok_s(NULL, TOKEN_DELIMITERS, &remaining_file_extensions);
							}
						} break;
					}
				} break;

				case(GROUP_URL_GROUP):
				{
					switch(current_list_type)
					{
						case(LIST_NONE):
						{
							// @TODO
						} break;

						case(LIST_DOMAINS):
						{
							// @TODO
						} break;
					}
				} break;
			}

			//TCHAR* foo = copy_utf_8_string_to_tchar(temporary_arena, line);
			//log_print(LOG_NONE, "-> %s", foo);
		}

		line = strtok_s(NULL, LINE_DELIMITERS, &remaining_lines);
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
		log_print(LOG_WARNING, "Load All Group Files: Attempted to load zero groups.");
		return;
	}

	Arena* permanent_arena = &(exporter->permanent_arena);
	Arena* temporary_arena = &(exporter->temporary_arena);

	u32 num_group_files = 0;
	TCHAR* group_filenames = push_arena(temporary_arena, 0, TCHAR);
	{
		TCHAR search_path[MAX_PATH_CHARS] = TEXT("");
		shell_copy_and_append_path(search_path, exporter->executable_path, GROUP_FILES_SEARCH_PATH);

		WIN32_FIND_DATA file_find_data = {};
		HANDLE search_handle = FindFirstFile(search_path, &file_find_data);
		
		bool found_file = search_handle != INVALID_HANDLE_VALUE;
		while(found_file)
		{
			// Skip directories.
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

	TCHAR** group_filenames_array = push_arena(temporary_arena, num_group_files * sizeof(TCHAR*), TCHAR*);
	for(u32 i = 0; i < num_group_files; ++i)
	{
		group_filenames_array[i] = group_filenames;
		while(*group_filenames != TEXT('\0')) ++group_filenames;
		++group_filenames;

		debug_log_print("%I32u : %s", i, group_filenames_array[i]);
	}

	qsort(group_filenames_array, num_group_files, sizeof(TCHAR*), compare_filenames);

	_ASSERT(num_groups > 0);
	size_t custom_groups_size = sizeof(Custom_Groups) + sizeof(Group) * (num_groups - 1);
	Custom_Groups* custom_groups = push_arena(permanent_arena, custom_groups_size, Custom_Groups);
	custom_groups->num_groups = num_groups;
	u32 num_processed_groups = 0;
	
	for(u32 i = 0; i < num_group_files; ++i)
	{
		TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
		shell_copy_and_append_path(group_file_path, exporter->executable_path, GROUP_FILES_DIRECTORY);
		PathAppend(group_file_path, group_filenames_array[i]);

		load_group_file(permanent_arena, temporary_arena,
						group_file_path, custom_groups->groups, &num_processed_groups);

		debug_log_print("%I32u : %s", i, group_filenames_array[i]);
	}

	_ASSERT(num_processed_groups <= num_groups);

	exporter->custom_groups = custom_groups;

	clear_arena(temporary_arena);
}
