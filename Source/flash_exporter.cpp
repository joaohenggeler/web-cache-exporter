#include "web_cache_exporter.h"
#include "flash_exporter.h"

/*
	This file defines how the exporter processes the Adobe (previously Macromedia) Flash Player's cache. Note that this cache doesn't
	contain actual Flash movies (SWF files) and is instead used for other types of files, like shared library code (SWZ files). This
	might not be useful when looking for lost web game assets, but these SWZ files could potentially be used to get specific Flash
	games working (e.g. their files were found but they require a currently missing library). These SWZ files are located in the Asset
	Cache and each one is associated with a HEU metadata file that contains a few strings of information (like the packaged library's
	SHA-256 value).

	@SupportedFormats: Flash Player 9.0.115.0 and later.

	@DefaultCacheLocations:
	- 98, ME 				C:\WINDOWS\Application Data\Adobe\Flash Player
	- 2000, XP 				C:\Documents and Settings\<Username>\Application Data\Adobe\Flash Player
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Roaming\Adobe\Flash Player

	The previously mentioned Asset Cache is in: <Cache Location>\AssetCache\<8 Character Directory>

	This exporter will also look for FLV video files in the Temporary Files directory. These were cached by Flash video players (e.g. 
	YouTube's old player) when they were watched in a browser.

	@SupportsCustomCacheLocations:
	- Same Machine: Unknown if this location can be changed by the user.
	- External Locations: Unknown, see above.

	@Resources: A few pages of interest:

	- https://community.ccleaner.com/topic/24286-a-new-plague-of-flash-trash-on-the-way/
	- https://web.archive.org/web/20090306164003/http://www.adobe.com/devnet/flashplayer/articles/flash_player_admin_guide/flash_player_8_9_admin_guide.pdf
	- https://web.archive.org/web/20090206112134/http://www.adobe.com/devnet/flashplayer/articles/flash_player_admin_guide/flash_player_admin_guide.pdf

	@Tools: None for the SWZ files. But the following NirSoft tool is very useful if you only want to recover video files from the
	web cache:

	[NS-T1] "VideoCacheView v3.05"
	--> https://www.nirsoft.net/utils/video_cache_view.html
*/

static const TCHAR* OUTPUT_NAME = T("FL");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_MODIFIED_TIME, CSV_CREATION_TIME, CSV_LAST_WRITE_TIME, CSV_LAST_ACCESS_TIME,
	CSV_ACCESS_COUNT, CSV_LIBRARY_SHA_256,
	CSV_LOCATION_ON_CACHE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_SHA_256
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
void export_default_or_specific_flash_cache(Exporter* exporter)
{
	console_print("Exporting the Flash Player's cache...");
	
	initialize_cache_exporter(exporter, CACHE_FLASH, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		if(exporter->is_exporting_from_default_locations)
		{
			PathCombine(exporter->cache_path, exporter->appdata_path, T("Adobe\\Flash Player"));
		}

		log_print(LOG_INFO, "Flash Player: Exporting the cache and videos from '%s'.", exporter->cache_path);
		
		set_exporter_output_copy_subdirectory(exporter, T("Cache"));
		traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, true, find_flash_cache_files_callback, exporter);
		
		if(exporter->is_exporting_from_default_locations)
		{
			// This is currently only checked when using default locations since the previous
			// traversal already includes these video files.
			StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, exporter->windows_temporary_path);
			set_exporter_output_copy_subdirectory(exporter, T("Videos"));
			traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, false, find_flash_video_files_callback, exporter);
		}

		log_print(LOG_INFO, "Flash Player: Finished exporting the cache.");
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
	Exporter* exporter = (Exporter*) callback_info->user_data;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* filename = callback_info->object_name;
	TCHAR* file_extension = skip_to_file_extension(filename, true);

	// Skip the HEU metadata files.
	if(filenames_are_equal(file_extension, T(".heu")))
	{
		return true;
	}

	TCHAR* full_file_path = callback_info->object_path;
	TCHAR* short_location_on_cache = skip_to_last_path_components(full_file_path, 3);

	TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
	TCHAR* access_count = NULL;
	TCHAR* library_sha_256 = NULL;
	{
		if(filenames_are_equal(file_extension, T(".swz")))
		{
			TCHAR previous_char = *file_extension;
			*file_extension = T('\0');

			TCHAR metadata_file_path[MAX_PATH_CHARS] = T("");
			PathCombine(metadata_file_path, callback_info->directory_path, filename);
			StringCchCat(metadata_file_path, MAX_PATH_CHARS, T(".heu"));

			u64 metadata_file_size = 0;
			char* metadata_file = (char*) read_entire_file(arena, metadata_file_path, &metadata_file_size, true);
			if(metadata_file != NULL)
			{
				// @FormatVersion: Flash Player 9 and later.
				// @ByteOrder: None. The data is stored as null terminated ASCII strings.
				// @CharacterEncoding: ASCII.
				// @DateTimeFormat: Unix time in milliseconds (_time32 or _time64 * 1000).

				/*
					Each HEU metadata file contains a few null terminated strings with information about
					its respective SWZ file (which packages a shared Flash library). For example:
					
					0<Null>
					1226440693312<Null>
					20<Null>
					AF62E91CD3379900D89DDF6A3E235D6FADB952B74A00F19CE4E3DCE8630B110A<Null>
					E389BAC057BA2167FC68536A1032CED6723901C01B6B4A4427AEB576E5E13085<Null>
				*/

				char* first_in_file 				= metadata_file;
				char* last_modified_time_in_file 	= skip_to_next_string(first_in_file);
				char* access_count_in_file 			= skip_to_next_string(last_modified_time_in_file);
				char* library_sha_256_in_file 		= skip_to_next_string(access_count_in_file);
				char* fifth_in_file 				= skip_to_next_string(library_sha_256_in_file);
				
				fifth_in_file;

				u64 last_modified_time_value = 0;
				if(convert_string_to_u64(last_modified_time_in_file, &last_modified_time_value))
				{
					last_modified_time_value /= 1000;
					format_time64_t_date_time(last_modified_time_value, last_modified_time);
				}

				access_count = convert_ansi_string_to_tchar(arena, access_count_in_file);
				library_sha_256 = convert_ansi_string_to_tchar(arena, library_sha_256_in_file);
				
				// @Assert: Each SWZ's filename should be the first 40 character of its packaged library's SHA-256.
				_ASSERT(strings_are_at_most_equal(filename, library_sha_256, 40, true));
			}
			else
			{
				log_print(LOG_ERROR, "Flash Player: Failed to open the metadata file '%s'. No additional information about this library will be extracted.", metadata_file_path);
			}

			*file_extension = previous_char;
		}
	}

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{last_modified_time}, {/* Creation Time */}, {/* Last Write Time */}, {/* Last Access Time */},
		{access_count}, {library_sha_256},
		{/* Location On Cache */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter_Params params = {};
	params.copy_source_path = full_file_path;
	params.filename = filename;
	params.short_location_on_cache = short_location_on_cache;
	params.file_info = callback_info;

	export_cache_entry(exporter, csv_row, &params);

	return true;
}

// Called every time a file is found in the Temporary Files directory. Used to export every FLV file.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_flash_video_files_callback)
{
	TCHAR* filename = callback_info->object_name;
	TCHAR* full_file_path = callback_info->object_path;

	const u32 SIGNATURE_BUFFER_SIZE = 3;
	u8 signature_buffer[SIGNATURE_BUFFER_SIZE] = {};
	bool is_flv_file = 	read_first_file_bytes(full_file_path, signature_buffer, SIGNATURE_BUFFER_SIZE)
						&& memory_is_equal(signature_buffer, "FLV", SIGNATURE_BUFFER_SIZE);

	// Skip non-FLV files.
	if(!is_flv_file) return true;

	TCHAR short_location_on_cache[MAX_PATH_CHARS] = T("");
	PathCombine(short_location_on_cache, T("<Temporary>"), filename);

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Last Modified Time */}, {/* Creation Time */}, {/* Last Write Time */}, {/* Last Access Time */},
		{/* Access Count */}, {/* Library SHA-256 */},
		{/* Location On Cache */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter* exporter = (Exporter*) callback_info->user_data;

	Exporter_Params params = {};
	params.copy_source_path = full_file_path;
	params.filename = filename;
	params.short_location_on_cache = short_location_on_cache;
	params.file_info =callback_info;

	export_cache_entry(exporter, csv_row, &params);	

	return true;
}
