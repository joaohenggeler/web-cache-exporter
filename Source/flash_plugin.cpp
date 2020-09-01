#include "web_cache_exporter.h"
#include "flash_plugin.h"

/*
	@TODO: This file is still under construction. Right now it doesn't read any useful data from the HEU Flash Player Cache Metadata
	files (though there doesn't seem to be too much useful stuff to show anyways, so we might never read anything from it).
*/

// The name of the CSV file and the directory where the cached files will be copied to.
static const TCHAR* OUTPUT_DIRECTORY_NAME = TEXT("FL");

// The order and type of each column in the CSV file.
static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
	CSV_CUSTOM_FILE_GROUP
};
static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Entry point for the Flash Player's cache exporter. This function will determine where to look for the cache before
// processing its contents.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the cache should be exported.
// If the path to this location isn't defined, this function will look in the current AppData directory.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_files_callback);
void export_specific_or_default_flash_plugin_cache(Exporter* exporter)
{
	if(exporter->is_exporting_from_default_locations)
	{
		if(!PathCombine(exporter->cache_path, exporter->roaming_appdata_path, TEXT("Adobe\\Flash Player")))
		{
			log_print(LOG_ERROR, "Flash Plugin: Failed to determine the cache directory path.");
			return;
		}
	}

	initialize_cache_exporter(exporter, OUTPUT_DIRECTORY_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		log_print(LOG_INFO, "Flash Plugin: Exporting the cache from '%s'.", exporter->cache_path);
		traverse_directory_objects(exporter->cache_path, TEXT("*"), TRAVERSE_FILES, true, find_flash_files_callback, exporter);
		log_print(LOG_INFO, "Flash Plugin: Finished exporting the cache.");
	}
	terminate_cache_exporter(exporter);
}

// Called every time a file is found in the Flash Player's cache. Used to export every cache entry.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_files_callback)
{
	TCHAR* filename = find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, directory_path, filename);

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{NULL}, {NULL}, {NULL},
		{NULL}, {NULL}, {NULL},
		{NULL}
	};

	Exporter* exporter = (Exporter*) user_data;
	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, find_data);
}
