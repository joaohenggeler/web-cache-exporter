#include "web_cache_exporter.h"
#include "memory_and_file_io.h"

static const TCHAR* GROUP_FILES_DIRECTORY = TEXT("Groups");
static const TCHAR* GROUP_FILES_SEARCH_PATH = TEXT("Groups\\*.group");

size_t get_total_group_files_size(Exporter* exporter)
{
	size_t total_size = 0;

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
			
			total_size += file_size;
		}

		found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
	}

	safe_find_close(&search_handle);

	return total_size;
}

void load_group_file(Exporter* exporter, const TCHAR* file_path)
{
	Arena* permanent_arena = &(exporter->permanent_arena);
	Arena* temporary_arena = &(exporter->temporary_arena);

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

	const char* LINE_DELIMITERS = "\r\n";
	const char* TOKEN_DELIMITERS = " \t";
	//const char* MULTILINE_TOKEN_DELIMITERS = "\r\n \t";
	char* remaining_lines = NULL;
	char* line = strtok_s(group_file, LINE_DELIMITERS, &remaining_lines);

	enum Group_Block
	{
		BLOCK_NONE = 0,
		BLOCK_FILE_GROUP = 1,
		BLOCK_URL_GROUP = 2
	};

	enum List_Block
	{
		LIST_NONE = 0,

		LIST_FILE_SIGNATURES = 1,
		LIST_MIME_TYPES = 2,
		LIST_FILE_EXTENSIONS = 3,

		LIST_DOMAINS = 4
	};

	Group_Block current_block = BLOCK_NONE;
	List_Block current_list = LIST_NONE;
	
	while(line != NULL)
	{
		line = skip_leading_whitespace(line);

		if(*line == ';')
		{
			// Skip comments.
		}
		else if(current_list != LIST_NONE && strings_are_equal(line, "END"))
		{
			current_list = LIST_NONE;	
		}
		else if(current_block != BLOCK_NONE && strings_are_equal(line, "END"))
		{
			current_block = BLOCK_NONE;
		}
		else
		{
			switch(current_block)
			{
				case(BLOCK_NONE):
				{
					char* block_value = NULL;
					char* block_name = strtok_s(line, TOKEN_DELIMITERS, &block_value);
					if(block_name != NULL && block_value != NULL)
					{
						if(strings_are_equal(block_name, "BEGIN_FILE_GROUP"))
						{
							current_block = BLOCK_FILE_GROUP;
						}
						else if(strings_are_equal(block_name, "BEGIN_URL_GROUP"))
						{
							current_block = BLOCK_URL_GROUP;
						}
						else
						{
							// Unknown block name
						}
					}
				} break;

				case(BLOCK_FILE_GROUP):
				{
					switch(current_list)
					{
						case(LIST_NONE):
						{
							if(strings_are_equal(line, "BEGIN_MIME_TYPES"))
							{
								current_list = LIST_MIME_TYPES;
							}
							else if(strings_are_equal(line, "BEGIN_FILE_EXTENSIONS"))
							{
								current_list = LIST_FILE_EXTENSIONS;
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
							// @TODO
						} break;
					}
				} break;

				case(BLOCK_URL_GROUP):
				{
					switch(current_list)
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

void load_all_group_files(Exporter* exporter)
{
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
			TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
			shell_copy_and_append_path(group_file_path, exporter->executable_path, GROUP_FILES_DIRECTORY);
			PathAppend(group_file_path, file_find_data.cFileName);

			load_group_file(exporter, group_file_path);
		}

		found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
	}

	safe_find_close(&search_handle);
}
