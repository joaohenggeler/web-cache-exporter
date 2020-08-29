#include "web_cache_exporter.h"
#include "shockwave_plugin.h"

/*
	This file defines how the exporter processes the Adobe (previously Macromedia) Shockwave Player's web plugin cache.
	For this type of cache, we'll work directly with the files stored on disk instead of parsing a database with metadata
	on each file.

	@SupportedFormats: Unknown, likely Shockwave 8 to 12.

	@DefaultCacheLocations: The Temporary Files directory. This location is specified in the TEMP or TMP environment variables.
	- 98, ME 				C:\WINDOWS\TEMP
	- 2000, XP 				C:\Documents and Settings\<Username>\Local Settings\Temp
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Local\Temp

	The names of these cached files start with "mp", followed by at least six more characters (e.g. mpb02684.w3d).

	This exporter will also copy any Xtras (.x32 files) in Temporary Files directory and its subdirectories.

	@Resources: TOMYSSHADOW's extensive knowledge of Macromedia / Adobe Director: https://github.com/tomysshadow

	@Tools: None.

	But I did use the Director game "Adventure Elf" (developed by Blockdot and published by Kewlbox.com) to test the Xtras export
	feature.
*/

// The name of the CSV file and the directory where the cached files will be copied to.
static const TCHAR* OUTPUT_DIRECTORY_NAME = TEXT("SW");

// The order and type of each column in the CSV file.
static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
	CSV_DIRECTOR_FILE_TYPE,
	CSV_CUSTOM_FILE_GROUP
};
static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// A structure that defines the first 12 bytes of Director files (movies or external casts). Since cached Shockwave files can be
// stored on disk without a file extension, we'll make it easier to tell what kind of file was found in the generated CSV file.
// This exists purely for convenience and does not represent any type of database that contains metadata about each cached file.
#pragma pack(push, 1)
struct Partial_Director_Rifx_Chunk
{
	u32 id;
	u32 size;
	u32 codec;
};
#pragma pack(pop)

// Possible values for the 'id' member of this structure.
static const u32 RIFX_BIG_ENDIAN = 0x52494658; // "RIFX"
static const u32 RIFX_LITTLE_ENDIAN = 0x58464952; // "XFIR"

// Possible values for the 'codec' member of this structure.
enum Director_Codec
{
	// DIR, CST, DXR, or CXT files.
	DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN = 0x4D563933, // "MV93"
	DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN = 0x3339564D, // "39VM"
	// DCR files.
	SHOCKWAVE_MOVIE_BIG_ENDIAN = 0x4647444D, // "FGDM"
	SHOCKWAVE_MOVIE_LITTLE_ENDIAN = 0x4D444746, // "MDGF"
	// CCT files.
	SHOCKWAVE_CAST_BIG_ENDIAN = 0x46474443, // "FGDC"
	SHOCKWAVE_CAST_LITTLE_ENDIAN = 0x43444746 // "CDGF"
};

// Retrieves the type of a Director file from its first bytes.
//
// @Parameters:
// 1. file_path - The path of the file to check.
//
// @Returns: The Director file type as a string. If this file doesn't match any known Director type, this function returns NULL.
static TCHAR* get_director_file_type_from_file_signature(const TCHAR* file_path)
{
	Partial_Director_Rifx_Chunk chunk = {};

	if(read_first_file_bytes(file_path, &chunk, sizeof(chunk)))
	{
		if(chunk.id == RIFX_BIG_ENDIAN || chunk.id == RIFX_LITTLE_ENDIAN)
		{
			switch(chunk.codec)
			{
				case(DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN):
				case(DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN):
				{
					return TEXT("DIR / CST / DXR / CXT");
				} break;

				case(SHOCKWAVE_MOVIE_BIG_ENDIAN):
				case(SHOCKWAVE_MOVIE_LITTLE_ENDIAN):
				{
					return TEXT("DCR");
				} break;

				case(SHOCKWAVE_CAST_BIG_ENDIAN):
				case(SHOCKWAVE_CAST_LITTLE_ENDIAN):
				{
					return TEXT("CCT");
				} break;
			}
		}
	}

	return NULL;
}

struct Find_Shockwave_Files_Params
{
	Exporter* exporter;
	bool is_xtra;
};

// Entry point for the Shockwave Player's cache exporter. This function will determine where to look for the cache before
// processing its contents.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the cache should be exported.
// If the path to this location isn't defined, this function will look in the current Temporary Files directory.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_shockwave_files_callback);
void export_specific_or_default_shockwave_plugin_cache(Exporter* exporter)
{
	if(exporter->is_exporting_from_default_locations)
	{
		if(FAILED(StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, exporter->windows_temporary_path)))
		{
			log_print(LOG_ERROR, "Shockwave Plugin: Failed to get the Temporary Files directory path.");
			return;
		}
	}

	initialize_cache_exporter(exporter, OUTPUT_DIRECTORY_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		log_print(LOG_INFO, "Shockwave Plugin: Exporting the cache from '%s'.", exporter->cache_path);
		Find_Shockwave_Files_Params params = {};
		params.exporter = exporter;

		params.is_xtra = false;
		traverse_directory_objects(exporter->cache_path, TEXT("mp*"), TRAVERSE_FILES, false, find_shockwave_files_callback, &params);
		
		params.is_xtra = true;
		traverse_directory_objects(exporter->cache_path, TEXT("*.x32"), TRAVERSE_FILES, true, find_shockwave_files_callback, &params);		
	}
	terminate_cache_exporter(exporter);
	
	log_print(LOG_INFO, "Shockwave Plugin: Finished exporting the cache.");
}

// Called every time a file is found in the Shockwave Player's cache. Used to export every cache entry.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_shockwave_files_callback)
{
	Find_Shockwave_Files_Params* params = (Find_Shockwave_Files_Params*) user_data;

	TCHAR* filename = find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, directory_path, filename);

	TCHAR* director_file_type = (params->is_xtra) ? (TEXT("Xtra")) : (get_director_file_type_from_file_signature(full_file_path));

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{NULL}, {NULL}, {NULL},
		{NULL}, {NULL}, {NULL},
		{director_file_type},
		{NULL}
	};

	Exporter* exporter = params->exporter;
	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, find_data);
}
