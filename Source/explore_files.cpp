#include "web_cache_exporter.h"
#include "explore_files.h"

/*
	This file defines a simple cache exporter that processes any files in a given directory and its subdirectories. It does not
	correspond to any specfic web browser or plugin, and is instead used to explore the contents of directories that may contain
	relevant files formats. For example, a directory that may contain the cache of an obscure web plugin. This is useful when
	combined with group files, which allow you to potentially find the types of files based on their file signatures.
*/

static const TCHAR* OUTPUT_NAME = T("EXPLORE");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_CREATION_TIME, CSV_LAST_WRITE_TIME, CSV_LAST_ACCESS_TIME,
	CSV_LOCATION_ON_DISK, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_SHA_256
};

static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Called every time a file is found in the specified directory and subdirectories. Used to export every file.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(explore_files_callback)
{
	TCHAR* filename = callback_info->object_name;
	TCHAR* full_file_path = callback_info->object_path;

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Creation Time */}, {/* Last Write Time */}, {/* Last Access Time */},
		{/* Location On Disk */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter* exporter = (Exporter*) callback_info->user_data;

	Exporter_Params params = {};
	params.copy_source_path = full_file_path;
	params.filename = filename;
	params.file_info = callback_info;

	export_cache_entry(exporter, csv_row, &params);

	return true;
}

// Entry point for the file explorer exporter. This function assumes that the exporter's cache location was passed via the
// command line arguments.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the files should be exported.
//
// @Returns: Nothing.
void export_explored_files(Exporter* exporter)
{
	console_print("Exploring and exporting the files from '%s'...", exporter->cache_path);

	initialize_cache_exporter(exporter, CACHE_EXPLORE, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		log_print(LOG_INFO, "Explore Files: Exporting the files from '%s'.", exporter->cache_path);
		traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, true, explore_files_callback, exporter);
		log_print(LOG_INFO, "Explore Files: Finished exporting the files.");
	}
	terminate_cache_exporter(exporter);
}
