#include "web_cache_exporter.h"
#include "explore_files.h"

/*
	This file defines a simple cache exporter that processes any files in a given directory and its subdirectories. It does not
	correspond to any specfic web browser or plugin, and is instead used to explore the contents of directories that may contain
	relevant files formats. For example, a directory that may contain the cache of an obscure web plugin. This is useful when
	combined with group files, which allow you to potentially find the types of files based on their file signatures.
*/

// The name of the CSV file and the directory where the cached files will be copied to.
static const TCHAR* OUTPUT_DIRECTORY_NAME = TEXT("EXPLORE");

// The order and type of each column in the CSV file.
static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
	CSV_LOCATION_ON_DISK,
	CSV_CUSTOM_FILE_GROUP
};
static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Called every time a file is found in the specified directory and subdirectories. Used to export every file.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(explore_files_callback)
{
	TCHAR* filename = find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, directory_path, filename);

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Last Write Time */}, {/* Creation Time */}, {/* Last Access Time */},
		{/* Location On Disk */},
		{/* Custom File Group */}
	};

	Exporter* exporter = (Exporter*) user_data;
	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, find_data);

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
	initialize_cache_exporter(exporter, OUTPUT_DIRECTORY_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		log_print(LOG_INFO, "Explore Files: Exporting the files from '%s'.", exporter->cache_path);
		traverse_directory_objects(exporter->cache_path, TEXT("*"), TRAVERSE_FILES, true, explore_files_callback, exporter);
		log_print(LOG_INFO, "Explore Files: Finished exporting the files.");
	}
	terminate_cache_exporter(exporter);
}
