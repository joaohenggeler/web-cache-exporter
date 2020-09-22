#include "web_cache_exporter.h"
#include "flash_plugin.h"

/*
	This file defines how the exporter processes the Adobe (previously Macromedia) Flash Player's cache. Note that this cache doesn't
	contain actual Flash movies (SWF files) and is instead used for other types of files, like shared library code (SWZ files). This
	might not be useful when looking for lost web game assets, but these SWZ files could potentially be used to get specific Flash
	games working (e.g. their files were found but they require a currently missing library).

	These SWZ files are located in the Asset Cache and each one has a HEU metadata file associated with it. Unlike other cache metadata
	files (like the Java Plugin IDX files), we won't extract any information from these since there doesn't seem to be much relevant
	stuff to show. We'll perform a naive export and copy these files directly. There are other cache subdirectories in the main cache
	location so we'll cover these too for good measure. This export process might change if new information is found.

	@SupportedFormats: Flash Player 9.0.115.0 and later.

	@DefaultCacheLocations:
	- 98, ME 				C:\WINDOWS\Application Data\Adobe\Flash Player
	- 2000, XP 				C:\Documents and Settings\<Username>\Application Data\Adobe\Flash Player
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Roaming\Adobe\Flash Player

	The previously mentioned Asset Cache is in: <Cache Location>\AssetCache\<8 Character Directory>

	@Resources: A few pages of interest:

	- https://community.ccleaner.com/topic/24286-a-new-plague-of-flash-trash-on-the-way/
	- https://web.archive.org/web/20090306164003/http://www.adobe.com/devnet/flashplayer/articles/flash_player_admin_guide/flash_player_8_9_admin_guide.pdf
	- https://web.archive.org/web/20090206112134/http://www.adobe.com/devnet/flashplayer/articles/flash_player_admin_guide/flash_player_admin_guide.pdf

	@Tools: None.

*/

static const TCHAR* OUTPUT_NAME = TEXT("FL");

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
	console_print("Exporting the Flash Plugin's cache...");
	
	if(exporter->is_exporting_from_default_locations)
	{
		PathCombine(exporter->cache_path, exporter->appdata_path, TEXT("Adobe\\Flash Player"));
	}

	initialize_cache_exporter(exporter, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
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
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_files_callback)
{
	TCHAR* filename = find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, directory_path, filename);

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Last Write Time */}, {/* Creation Time */}, {/* Last Access Time */},
		{/* Custom File Group */}
	};

	Exporter* exporter = (Exporter*) user_data;
	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, find_data);

	return true;
}
