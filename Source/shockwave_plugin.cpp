#include "web_cache_exporter.h"
#include "shockwave_plugin.h"

/*
	This file defines how the exporter processes the Adobe (previously Macromedia) Shockwave Player's web plugin cache.
	For this type of cache, we'll work directly with the files stored on disk instead of parsing a database with metadata
	on each file.

	@SupportedFormats: Unsure, likely Shockwave Player 7 to 12.

	@DefaultCacheLocations: The Temporary Files directory. This location is specified in the TEMP or TMP environment variables.
	- 98, ME 				C:\WINDOWS\TEMP
	- 2000, XP 				C:\Documents and Settings\<Username>\Local Settings\Temp
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Local\Temp

	The names of these cached files start with "mp", followed by at least six more characters (e.g. mpb02684.w3d).

	This exporter will also copy any Xtras (.x32 files) in the Temporary Files directory, AppData, LocalLow AppData, and their
	subdirectories.

	@Resources: TOMYSSHADOW's extensive knowledge of Macromedia / Adobe Director: https://github.com/tomysshadow

	@Tools: None.

	But I did use the Director game "Adventure Elf" (developed by Blockdot and published by Kewlbox.com) to test the Xtras export
	feature in the Temporary Files directory.
*/

static const TCHAR* OUTPUT_NAME = TEXT("SW");

static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
	CSV_DIRECTOR_FILE_TYPE,
	CSV_LOCATION_ON_CACHE,
	CSV_CUSTOM_FILE_GROUP
};

static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// A structure that defines the first 12 bytes of Director files (movies or external casts). Since cached Shockwave files can be
// stored on disk without a file extension, we'll make it easier to tell what kind of file was found in the generated CSV file.
// This exists purely for convenience and does not represent any type of database that contains metadata about each cached file.
#pragma pack(push, 1)
struct Partial_Director_Chunk
{
	u32 id;
	u32 size;
	u32 codec;
};
#pragma pack(pop)

// Possible values for the 'id' member of this structure.
enum Chunk_Id
{
	CHUNK_RIFX_BIG_ENDIAN = 0x52494658, // "RIFX"
	CHUNK_RIFX_LITTLE_ENDIAN = 0x58464952, // "XFIR"
	CHUNK_RIFF_BIG_ENDIAN = 0x52494646, // "RIFF"

	// W3D files.
	CHUNK_SHOCKWAVE_3D_WORLD_BIG_ENDIAN = 0x49465800 // "IFX."
};

// Possible values for the 'codec' member of this structure.
enum Director_Codec
{
	// DIR, CST, DXR, or CXT files.
	CODEC_DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN = 0x4D563933, // "MV93"
	CODEC_DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN = 0x3339564D, // "39VM"
	
	// DCR files.
	CODEC_SHOCKWAVE_MOVIE_BIG_ENDIAN = 0x4647444D, // "FGDM"
	CODEC_SHOCKWAVE_MOVIE_LITTLE_ENDIAN = 0x4D444746, // "MDGF"
	
	// CCT files.
	CODEC_SHOCKWAVE_CAST_BIG_ENDIAN = 0x46474443, // "FGDC"
	CODEC_SHOCKWAVE_CAST_LITTLE_ENDIAN = 0x43444746, // "CDGF"
	
	// W32 files.
	CODEC_XTRA_PACKAGE_BIG_ENDIAN = 0x50434B32 // "PCK2"
};

// Retrieves the type of a Director file from its first bytes.
//
// @Parameters:
// 1. file_path - The path of the file to check.
//
// @Returns: The Director file type as a constant string. If this file doesn't match any known Director type, this function returns NULL.
static TCHAR* get_director_file_type_from_file_signature(const TCHAR* file_path)
{
	Partial_Director_Chunk chunk = {};

	if(read_first_file_bytes(file_path, &chunk, sizeof(chunk)))
	{
		// This works without swapping the byte order because we check both big and little endian format signatures.
		if(chunk.id == CHUNK_RIFX_BIG_ENDIAN || chunk.id == CHUNK_RIFX_LITTLE_ENDIAN)
		{
			// @ByteOrder: Big or Little Endian.
			switch(chunk.codec)
			{
				case(CODEC_DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN):
				case(CODEC_DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN):	return TEXT("Director Movie or Cast");

				case(CODEC_SHOCKWAVE_MOVIE_BIG_ENDIAN):
				case(CODEC_SHOCKWAVE_MOVIE_LITTLE_ENDIAN):			return TEXT("Shockwave Movie");

				case(CODEC_SHOCKWAVE_CAST_BIG_ENDIAN):
				case(CODEC_SHOCKWAVE_CAST_LITTLE_ENDIAN):			return TEXT("Shockwave Cast");
			}
		}
		else
		{
			// @ByteOrder: Big Endian.
			chunk.id = swap_byte_order(chunk.id);
			chunk.codec = swap_byte_order(chunk.codec);

			if(chunk.id == CHUNK_RIFF_BIG_ENDIAN && chunk.codec == CODEC_XTRA_PACKAGE_BIG_ENDIAN)
			{
				return TEXT("Xtra-Package");
			}
			else if(chunk.id == CHUNK_SHOCKWAVE_3D_WORLD_BIG_ENDIAN)
			{
				return TEXT("Shockwave 3D World");
			}
		}
	}

	return NULL;
}

struct Find_Shockwave_Files_Params
{
	Exporter* exporter;
	bool is_xtra;
	TCHAR* location_identifier;
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
	console_print("Exporting the Shockwave Plugin's cache...");

	initialize_cache_exporter(exporter, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		if(exporter->is_exporting_from_default_locations)
		{
			StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, exporter->windows_temporary_path);
		}

		log_print(LOG_INFO, "Shockwave Plugin: Exporting the cache and Xtras from '%s'.", exporter->cache_path);

		Find_Shockwave_Files_Params params = {};
		params.exporter = exporter;
		params.location_identifier = TEXT("<Temporary>");

		params.is_xtra = false;
		set_exporter_output_copy_subdirectory(exporter, TEXT("Cache"));
		traverse_directory_objects(exporter->cache_path, TEXT("mp*"), TRAVERSE_FILES, false, find_shockwave_files_callback, &params);
		
		params.is_xtra = true;
		set_exporter_output_copy_subdirectory(exporter, TEXT("Xtras"));
		traverse_directory_objects(exporter->cache_path, TEXT("*.x32"), TRAVERSE_FILES, true, find_shockwave_files_callback, &params);

		if(exporter->is_exporting_from_default_locations)
		{
			#define TRAVERSE_APPDATA_XTRA_FILES(path, identifier)\
			do\
			{\
				params.location_identifier = identifier;\
				log_print(LOG_INFO, "Shockwave Plugin: Exporting Xtras from '%s'.", path);\
				\
				PathCombine(exporter->cache_path, path, TEXT("Macromedia"));\
				traverse_directory_objects(exporter->cache_path, TEXT("*.x32"), TRAVERSE_FILES, true, find_shockwave_files_callback, &params);\
				\
				PathCombine(exporter->cache_path, path, TEXT("Adobe"));\
				traverse_directory_objects(exporter->cache_path, TEXT("*.x32"), TRAVERSE_FILES, true, find_shockwave_files_callback, &params);\
			} while(false, false)

			TRAVERSE_APPDATA_XTRA_FILES(exporter->appdata_path, TEXT("<AppData>"));
			TRAVERSE_APPDATA_XTRA_FILES(exporter->local_low_appdata_path, TEXT("<Local Low AppData>"));
		}

		log_print(LOG_INFO, "Shockwave Plugin: Finished exporting the cache.");	
	}
	terminate_cache_exporter(exporter);
}

// Called every time a file is found in the Shockwave Player's cache. Used to export every cache entry.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_shockwave_files_callback)
{
	Find_Shockwave_Files_Params* params = (Find_Shockwave_Files_Params*) user_data;

	TCHAR* filename = find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, directory_path, filename);

	TCHAR* director_file_type = (params->is_xtra) ? (TEXT("Xtra")) : (get_director_file_type_from_file_signature(full_file_path));

	TCHAR short_file_path[MAX_PATH_CHARS] = TEXT("");

	if(params->is_xtra)
	{
		PathCombine(short_file_path, params->location_identifier, TEXT("[...]"));
		PathAppend(short_file_path, find_last_path_components(full_file_path, 3));
	}
	else
	{
		StringCchCopy(short_file_path, MAX_PATH_CHARS, params->location_identifier);
	}

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Last Write Time */}, {/* Creation Time */}, {/* Last Access Time */},
		{director_file_type},
		{short_file_path},
		{/* Custom File Group */}
	};

	Exporter* exporter = params->exporter;
	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, find_data);

	return true;
}
