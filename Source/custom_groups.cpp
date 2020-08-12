#include "web_cache_exporter.h"
#include "memory_and_file_io.h"

static const TCHAR* GROUP_FILES_DIRECTORY = TEXT("Groups");
static const TCHAR* GROUP_FILES_SEARCH_PATH = TEXT("Groups\\*.group");

size_t get_total_group_files_size(Exporter* exporter)
{
	size_t total_size = 0;

	TCHAR search_path[MAX_PATH_CHARS] = TEXT("");
	copy_and_append_path(search_path, exporter->executable_path, GROUP_FILES_SEARCH_PATH);

	WIN32_FIND_DATA file_find_data = {};
	HANDLE search_handle = FindFirstFile(search_path, &file_find_data);
	bool found_file = search_handle != INVALID_HANDLE_VALUE;

	while(found_file)
	{
		// Only handle files (skip directories).
		if((file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			// @TODO: This is dumb
			size_t file_size = (size_t) combine_high_and_low_u32s_into_u64(file_find_data.nFileSizeHigh, file_find_data.nFileSizeLow);
			total_size += file_size;
		}

		found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
	}

	safe_find_close(&search_handle);

	return total_size;
}

void load_group_file(Exporter* exporter, const TCHAR* file_path)
{
	exporter;
	log_print(LOG_INFO, "Load Group File: Loading file at '%s'.", file_path);

	HANDLE group_file_handle = INVALID_HANDLE_VALUE;
	u64 group_file_size = 0;
	// http://web.archive.org/web/20021222022224/http://msdn.microsoft.com/library/en-us/fileio/base/mapviewoffile.asp
	// @TODO: FILE_MAP_COPY + PAGE_WRITECOPY. Boolean as a default argument? memory_map_entire_file(..., bool read_only = true)
	char* group_file = (char*) memory_map_entire_file(file_path, &group_file_handle, &group_file_size);
	
	if(group_file == NULL)
	{
		// @TODO: Log
		return;
	}

	SAFE_UNMAP_VIEW_OF_FILE(group_file);
	safe_close_handle(&group_file_handle);
}

void load_all_group_files(Exporter* exporter)
{
	TCHAR search_path[MAX_PATH_CHARS] = TEXT("");
	copy_and_append_path(search_path, exporter->executable_path, GROUP_FILES_SEARCH_PATH);

	WIN32_FIND_DATA file_find_data = {};
	HANDLE search_handle = FindFirstFile(search_path, &file_find_data);
	bool found_file = search_handle != INVALID_HANDLE_VALUE;

	while(found_file)
	{
		// Only handle files (skip directories).
		if((file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			TCHAR group_file_path[MAX_PATH_CHARS] = TEXT("");
			copy_and_append_path(group_file_path, exporter->executable_path, GROUP_FILES_DIRECTORY);
			PathAppend(group_file_path, file_find_data.cFileName);

			load_group_file(exporter, group_file_path);
		}

		found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
	}

	safe_find_close(&search_handle);
}
