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

	This exporter will also look for FLV video files in the Temporary Files directory. These were cached by Flash video players, like
	YouTube, when these type of files were played in the browser. Note that these videos may also exist in the browser's cache, and
	should be handled by that specific cache exporter. The Flash Plugin exporter only checks the Temporary Files directory.

	@Resources: A few pages of interest:

	- https://community.ccleaner.com/topic/24286-a-new-plague-of-flash-trash-on-the-way/
	- https://web.archive.org/web/20090306164003/http://www.adobe.com/devnet/flashplayer/articles/flash_player_admin_guide/flash_player_8_9_admin_guide.pdf
	- https://web.archive.org/web/20090206112134/http://www.adobe.com/devnet/flashplayer/articles/flash_player_admin_guide/flash_player_admin_guide.pdf

	@Tools: None for the SWZ files. But the following NirSoft tool is very useful if you only want to recover video files from the
	web cache:

	[NS-T1] "VideoCacheView v3.05"
	--> https://www.nirsoft.net/utils/video_cache_view.html
*/

static const TCHAR* OUTPUT_NAME = TEXT("FL");

static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
	CSV_LIBRARY_SHA_256,
	CSV_LOCATION_ON_CACHE,
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
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_cache_files_callback);
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_video_files_callback);
void export_specific_or_default_flash_plugin_cache(Exporter* exporter)
{
	console_print("Exporting the Flash Plugin's cache...");
	
	initialize_cache_exporter(exporter, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		if(exporter->is_exporting_from_default_locations)
		{
			PathCombine(exporter->cache_path, exporter->appdata_path, TEXT("Adobe\\Flash Player"));
		}

		log_print(LOG_INFO, "Flash Plugin: Exporting the cache and videos from '%s'.", exporter->cache_path);
		
		set_exporter_output_copy_subdirectory(exporter, TEXT("Cache"));
		traverse_directory_objects(exporter->cache_path, TEXT("*"), TRAVERSE_FILES, true, find_flash_cache_files_callback, exporter);
		
		if(exporter->is_exporting_from_default_locations)
		{
			// This is currently only checked when using default locations since the previous
			// traversal already includes these video files.
			StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, exporter->windows_temporary_path);
			set_exporter_output_copy_subdirectory(exporter, TEXT("Videos"));
			traverse_directory_objects(exporter->cache_path, TEXT("*"), TRAVERSE_FILES, false, find_flash_video_files_callback, exporter);
		}

		log_print(LOG_INFO, "Flash Plugin: Finished exporting the cache.");
	}
	terminate_cache_exporter(exporter);
}

// Called every time a file is found in the Flash Player's cache. Used to export every cache entry.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_cache_files_callback)
{
	Exporter* exporter = (Exporter*) callback_user_data;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* filename = callback_find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, callback_directory_path, filename);

	TCHAR* short_file_path = find_last_path_components(full_file_path, 3);

	TCHAR* library_sha_256 = NULL;
	{
		TCHAR* file_extension = skip_to_file_extension(filename, true);
		if(strings_are_equal(file_extension, TEXT(".swz"), true))
		{
			TCHAR previous_char = *file_extension;
			*file_extension = TEXT('\0');

			TCHAR metadata_file_path[MAX_PATH_CHARS] = TEXT("");
			PathCombine(metadata_file_path, callback_directory_path, filename);
			StringCchCat(metadata_file_path, MAX_PATH_CHARS, TEXT(".heu"));

			u64 metadata_file_size = 0;
			char* metadata_file = (char*) read_entire_file(arena, metadata_file_path, &metadata_file_size, true);
			if(metadata_file != NULL)
			{
				for(int i = 0; i < 3; ++i)
				{
					metadata_file = skip_to_end_of_string(metadata_file);
					++metadata_file;
				}

				library_sha_256 = convert_ansi_string_to_tchar(arena, metadata_file);
				// @Assert: Each SWZ's filename should be the first 40 character of its packaged library's SHA-256.
				_ASSERT(strings_are_at_most_equal(filename, library_sha_256, 40, true));
			}

			*file_extension = previous_char;
		}
	}

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Last Write Time */}, {/* Creation Time */}, {/* Last Access Time */},
		{library_sha_256},
		{short_file_path},
		{/* Custom File Group */}
	};

	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, callback_find_data);

	return true;
}

// Called every time a file is found in the Temporary Files directory. Used to export every FLV file.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_video_files_callback)
{
	TCHAR* filename = callback_find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, callback_directory_path, filename);

	const u32 SIGNATURE_BUFFER_SIZE = 3;
	u8 signature_buffer[SIGNATURE_BUFFER_SIZE] = {};
	bool is_flv_file = 	read_first_file_bytes(full_file_path, signature_buffer, SIGNATURE_BUFFER_SIZE)
						&& memory_is_equal(signature_buffer, "FLV", SIGNATURE_BUFFER_SIZE);

	// Skip non-FLV files.
	if(is_flv_file)
	{
		TCHAR short_file_path[MAX_PATH_CHARS] = TEXT("");
		PathCombine(short_file_path, TEXT("<Temporary>"), filename);

		Csv_Entry csv_row[CSV_NUM_COLUMNS] =
		{
			{/* Filename */}, {/* File Extension */}, {/* File Size */},
			{/* Last Write Time */}, {/* Creation Time */}, {/* Last Access Time */},
			{/* Library SHA-256 */},
			{short_file_path},
			{/* Custom File Group */}
		};

		Exporter* exporter = (Exporter*) callback_user_data;
		export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, callback_find_data);	
	}

	return true;
}
