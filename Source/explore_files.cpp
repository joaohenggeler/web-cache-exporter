#include "web_cache_exporter.h"
#include "explore_files.h"

/*
	TODO
*/

// The name of the CSV file and the directory where the cached files will be copied to.
static const TCHAR* OUTPUT_DIRECTORY_NAME = TEXT("EXPLORE");

// The order and type of each column in the CSV file.
static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
	CSV_LOCATION_ON_CACHE,
	CSV_CUSTOM_FILE_GROUP
};
static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

static TRAVERSE_DIRECTORY_CALLBACK(explore_files_callback)
{
	TCHAR* filename = find_data->cFileName;

	TCHAR* file_extension = skip_to_file_extension(filename);

	u64 file_size = combine_high_and_low_u32s_into_u64(find_data->nFileSizeHigh, find_data->nFileSizeLow);
	TCHAR file_size_string[MAX_INT64_CHARS] = TEXT("");
	convert_u64_to_string(file_size, file_size_string);

	TCHAR last_write_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	format_filetime_date_time(find_data->ftLastWriteTime, last_write_time);

	TCHAR creation_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	format_filetime_date_time(find_data->ftCreationTime, creation_time);
	
	TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	format_filetime_date_time(find_data->ftLastAccessTime, last_access_time);

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, directory_path, filename);

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{filename}, {file_extension}, {file_size_string},
		{last_write_time}, {creation_time}, {last_access_time},
		{full_file_path},
		NULL_CSV_ENTRY
	};

	Exporter* exporter = (Exporter*) user_data;
	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename);
}

void export_explored_files(Exporter* exporter)
{
	get_full_path_name(exporter->cache_path);
	log_print(LOG_INFO, "Explore Files: Exporting the files from '%s'.", exporter->cache_path);

	resolve_exporter_output_paths_and_create_csv_file(exporter, OUTPUT_DIRECTORY_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	traverse_directory_objects(exporter->cache_path, TEXT("*"), TRAVERSE_FILES, true, explore_files_callback, exporter);
	close_exporter_csv_file(exporter);
}
