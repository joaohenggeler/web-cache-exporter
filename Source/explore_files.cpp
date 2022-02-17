#include "web_cache_exporter.h"
#include "explore_files.h"

/*
	This file defines a simple cache exporter that processes any files in a given directory and its subdirectories. It does not
	correspond to any specfic web browser or plugin, and is instead used to explore the contents of directories that may contain
	relevant files formats. For example, a directory that may contain the cache of an obscure web plugin. This is useful when
	combined with group files, which allow you to potentially identify file formats based on file signatures.
*/

static const TCHAR* OUTPUT_NAME = T("EXPLORE");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_CREATION_TIME, CSV_LAST_WRITE_TIME, CSV_LAST_ACCESS_TIME,
	CSV_FILE_DESCRIPTION, CSV_FILE_VERSION, CSV_PRODUCT_NAME, CSV_PRODUCT_VERSION, CSV_COPYRIGHT,
	CSV_LOCATION_ON_DISK, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_SHA_256
};

static const int CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Called every time a file is found in the specified directory and subdirectories. Used to export every file.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(explore_files_callback)
{
	Exporter* exporter = (Exporter*) callback_info->user_data;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* full_location_on_cache = callback_info->object_path;

	const u32 SIGNATURE_BUFFER_SIZE = 2;
	u8 signature_buffer[SIGNATURE_BUFFER_SIZE] = {};
	bool is_executable = read_first_file_bytes(full_location_on_cache, signature_buffer, SIGNATURE_BUFFER_SIZE)
						&& memory_is_equal(signature_buffer, "MZ", SIGNATURE_BUFFER_SIZE);

	TCHAR* file_description = NULL;
	TCHAR* file_version = NULL;
	TCHAR* product_name = NULL;
	TCHAR* product_version = NULL;
	TCHAR* copyright = NULL;

	if(is_executable)
	{
		get_file_info(arena, full_location_on_cache, INFO_FILE_DESCRIPTION, &file_description);
		get_file_info(arena, full_location_on_cache, INFO_FILE_VERSION, &file_version);
		get_file_info(arena, full_location_on_cache, INFO_PRODUCT_NAME, &product_name);
		get_file_info(arena, full_location_on_cache, INFO_PRODUCT_VERSION, &product_version);
		get_file_info(arena, full_location_on_cache, INFO_LEGAL_COPYRIGHT, &copyright);
	}

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Creation Time */}, {/* Last Write Time */}, {/* Last Access Time */},
		{file_description}, {file_version}, {product_name}, {product_version}, {copyright},
		{/* Location On Disk */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter_Params params = {};
	params.copy_source_path = full_location_on_cache;
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
	console_print("Exploring the files in '%s'...", exporter->cache_path);

	initialize_cache_exporter(exporter, CACHE_EXPLORE, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		log_info("Explore Files: Exploring the files in '%s'.", exporter->cache_path);
		traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, true, explore_files_callback, exporter);
		log_info("Explore Files: Finished exploring the files.");
	}
	terminate_cache_exporter(exporter);
}
