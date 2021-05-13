#include "web_cache_exporter.h"
#include "unity_exporter.h"

/*
	This file defines how the exporter processes the Unity Web Player's cache. This location includes cached AssetBundle files whose
	assets (models, textures, audio, etc) can be extracted using other tools.

	@SupportedFormats: Not yet determined.

	@DefaultCacheLocations:
	- 98, ME 				<None>
	- 2000, XP 				C:\Documents and Settings\<Username>\Local Settings\Application Data\Unity\WebPlayer\Cache
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\LocalLow\Unity\WebPlayer\Cache

	@SupportsCustomCacheLocations:
	- Same Machine: Unknown if this location can be changed by the user.
	- External Locations: Unknown, see above.

	@Resources: A few pages of interest:

	- https://answers.unity.com/questions/983035/where-is-the-asset-bundle-cache-folder-in-windows.html
	- https://docs.unity3d.com/ScriptReference/WWW.LoadFromCacheOrDownload.html
	- https://docs.unity3d.com/Manual/AssetBundlesIntro.html

	@Tools: None for exporting the cache, but the following can be used to extract assets from the cached files:

	[UTR] "uTinyRipper (2020-11-02 17-59-48)"
	--> https://github.com/mafaca/UtinyRipper

	[UABE] "Unity Assets Bundle Extractor 2.2"
	--> https://github.com/DerPopo/UABE
*/

static const TCHAR* OUTPUT_NAME = TEXT("UN");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_CREATION_TIME, CSV_LAST_WRITE_TIME, CSV_LAST_ACCESS_TIME, CSV_EXPIRY_TIME,
	CSV_LOCATION_ON_CACHE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_SHA_256
};

static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Entry point for the Unity Web Player's cache exporter. This function will determine where to look for the cache before
// processing its contents.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the cache should be exported.
// If the path to this location isn't defined, this function will look in the current AppData directory.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_unity_cache_files_callback);
void export_default_or_specific_unity_cache(Exporter* exporter)
{
	console_print("Exporting the Unity Web Player's cache...");
	
	initialize_cache_exporter(exporter, CACHE_UNITY, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		if(exporter->is_exporting_from_default_locations)
		{
			TCHAR* unity_appdata_path = exporter->local_low_appdata_path;
			if(string_is_empty(unity_appdata_path)) unity_appdata_path = exporter->local_appdata_path;
			// This will be an empty string in Windows 98 and ME.

			PathCombine(exporter->cache_path, unity_appdata_path, TEXT("Unity\\WebPlayer\\Cache"));
		}

		log_print(LOG_INFO, "Unity Web Player: Exporting the cache from '%s'.", exporter->cache_path);
		
		traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, true, find_unity_cache_files_callback, exporter);
		
		log_print(LOG_INFO, "Unity Web Player: Finished exporting the cache.");
	}
	terminate_cache_exporter(exporter);
}

// Called every time a file is found in the Unity Web Player's cache. Used to export every cache entry.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_unity_cache_files_callback)
{
	Exporter* exporter = (Exporter*) callback_info->user_data;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* filename = callback_info->object_name;

	// Skip the metadata and lock files.
	if(strings_are_equal(filename, TEXT("__info"), true) || strings_are_equal(filename, TEXT("__lock"), true))
	{
		return true;
	}

	TCHAR* full_file_path = callback_info->object_path;
	TCHAR* short_location_on_cache = skip_to_last_path_components(full_file_path, 3);
	{
		TCHAR copy_subdirectory[MAX_PATH_CHARS] = TEXT("");
		PathCombine(copy_subdirectory, short_location_on_cache, TEXT(".."));
		set_exporter_output_copy_subdirectory(exporter, copy_subdirectory);
	}

	TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	{
		TCHAR metadata_file_path[MAX_PATH_CHARS] = TEXT("");
		PathCombine(metadata_file_path, callback_info->directory_path, TEXT("__info"));

		u64 metadata_file_size = 0;
		char* metadata_file = (char*) read_entire_file(arena, metadata_file_path, &metadata_file_size, true);
		if(metadata_file != NULL)
		{
			// @FormatVersion: Not yet determined.
			// @ByteOrder: None. The data is stored as multiple lines of ASCII strings.
			// @CharacterEncoding: ASCII.
			// @DateTimeFormat: Unix time (_time32 or _time64).

			/*
				For example:

				-1
				1442863210
				1
				CAB-4ebad34d111aff249881a8de4b590a07
			*/

			String_Array<char>* split_lines = split_string(arena, metadata_file, "\r\n");

			#define GET_VALUE_OR_EMPTY_STRING(index) (split_lines->num_strings > index + 1) ? (split_lines->strings[index]) : ("")

			char* first_in_file = GET_VALUE_OR_EMPTY_STRING(0);
			char* expiry_time_in_file = GET_VALUE_OR_EMPTY_STRING(1);
			char* third_in_file = GET_VALUE_OR_EMPTY_STRING(2);
			char* filename_in_file = GET_VALUE_OR_EMPTY_STRING(3);

			first_in_file; third_in_file; filename_in_file;

			#undef GET_VALUE_OR_EMPTY_STRING

			u64 expiry_time_value = 0;
			if(convert_string_to_u64(expiry_time_in_file, &expiry_time_value))
			{
				format_time64_t_date_time(expiry_time_value, expiry_time);
			}
		}
		else
		{
			log_print(LOG_ERROR, "Unity Web Player: Failed to open the metadata file '%s'. No additional information about this file will be extracted.", metadata_file_path);
		}
	}

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Creation Time */}, {/* Last Write Time */}, {/* Last Access Time */}, {expiry_time}, 
		{/* Location On Cache */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter_Params params = {};
	params.copy_source_path = full_file_path;
	params.url = NULL;
	params.filename = filename;
	params.short_location_on_cache = short_location_on_cache;

	export_cache_entry(exporter, csv_row, &params, callback_info);

	return true;
}
