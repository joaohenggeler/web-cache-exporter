#include "web_cache_exporter.h"
#include "shockwave_plugin.h"

/*
	This file defines how the exporter processes the Adobe (previously Macromedia) Shockwave Player's web plugin cache.
	For this type of cache, we'll work directly with the files stored on disk instead of parsing a database with metadata
	about each file.

	@SupportedFormats: Director 6 and later.

	@DefaultCacheLocations: The Temporary Files directory. This location is specified in the TEMP or TMP environment variables.
	- 98, ME 				C:\WINDOWS\TEMP
	- 2000, XP 				C:\Documents and Settings\<Username>\Local Settings\Temp
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Local\Temp

	The names of these cached files start with "mp", followed by at least six more characters (e.g. mpb02684.w3d).

	This exporter will also copy any Xtras (.x32 files) in the Temporary Files directory, AppData, LocalLow AppData, and their
	subdirectories.

	@Resources: TOMYSSHADOW's extensive knowledge of Macromedia / Adobe Director: https://github.com/tomysshadow

	@Tools: Some utilities that can be used to process certain Director file formats that are found in the plugin's cache.

	[MRX] "Movie Restorer Xtra 1.4.5"
	--> https://github.com/tomysshadow/Movie-Restorer-Xtra
	--> Can be used to open Shockwave movies in Director.

	[VU] "Valentin's Unpack"
	--> https://valentin.dasdeck.com/lingo/unpack/
	--> Can be used to extract Xtras from Xtra-Packages.

	I also used the Director game "Adventure Elf" (developed by Blockdot and published by Kewlbox.com) to test the Xtras export
	feature for the Temporary Files directory. This was done by executing the exporter while the game was running.
*/

static const TCHAR* OUTPUT_NAME = TEXT("SW");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
	CSV_DIRECTOR_FILE_TYPE, CSV_XTRA_DESCRIPTION, CSV_XTRA_VERSION,
	CSV_LOCATION_ON_CACHE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_SHA_256
};

static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Since cached Shockwave files can be stored on disk without a file extension, we'll make it easier to tell what kind of file was
// found by reading and interpreting their first bytes.

/*
	struct Partial_Rifx_Chunk
	{
		u32 id;
		u32 size;
		u32 format;
	};
*/

static const u32 MIN_RIFX_CHUNK_READ_SIZE = 12;

// Possible values for the first four bytes.
enum Chunk_Id
{
	CHUNK_RIFX_BIG_ENDIAN = 0x52494658, // "RIFX"
	CHUNK_RIFX_LITTLE_ENDIAN = 0x58464952, // "XFIR"
	
	CHUNK_RIFF_BIG_ENDIAN = 0x52494646 // "RIFF"
};

// Possible values for the last four bytes.
enum Chunk_Format
{
	// Director Movie or Cast - DIR, CST, DXR, or CXT files.
	FORMAT_DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN = 0x4D563933, // "MV93"
	FORMAT_DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN = 0x3339564D, // "39VM"
	
	// Shockwave Movie - DCR files.
	FORMAT_SHOCKWAVE_MOVIE_BIG_ENDIAN = 0x4647444D, // "FGDM"
	FORMAT_SHOCKWAVE_MOVIE_LITTLE_ENDIAN = 0x4D444746, // "MDGF"
	
	// Shockwave Cast - CCT files.
	FORMAT_SHOCKWAVE_CAST_BIG_ENDIAN = 0x46474443, // "FGDC"
	FORMAT_SHOCKWAVE_CAST_LITTLE_ENDIAN = 0x43444746, // "CDGF"
	
	// Xtra-Package - W32 files.
	FORMAT_XTRA_PACKAGE_BIG_ENDIAN = 0x50434B32 // "PCK2"
};

// Shockwave 3D World - W3D files.
static const u32 SHOCKWAVE_3D_WORLD_SIGNATURE = 0x49465800; // "IFX."

// Shockwave Audio - SWA files. This signature follows a different structure, and appears at a certain offset in the file.
static const u32 SHOCKWAVE_AUDIO_SIGNATURE_OFFSET = 0x24;
static const char SHOCKWAVE_AUDIO_SIGNATURE[] = "MACR";
static const u32 SHOCKWAVE_AUDIO_SIGNATURE_SIZE = sizeof(SHOCKWAVE_AUDIO_SIGNATURE) - 1;
static const u32 MIN_SHOCKWAVE_AUDIO_READ_SIZE = SHOCKWAVE_AUDIO_SIGNATURE_OFFSET + SHOCKWAVE_AUDIO_SIGNATURE_SIZE;

// Determines the type of a Director file from its first bytes.
//
// @Parameters:
// 1. file_path - The path of the file to check.
//
// @Returns: The Director file type as a constant string. If this file doesn't match any known Director type, this function returns NULL.
static TCHAR* get_director_file_type_from_file_signature(const TCHAR* file_path)
{
	TCHAR* file_type = NULL;

	const u32 MAX_READ_SIZE = MAX(MIN_RIFX_CHUNK_READ_SIZE, MIN_SHOCKWAVE_AUDIO_READ_SIZE);
	u8 file_buffer[MAX_READ_SIZE] = {};

	u32 num_bytes_read = 0;
	if(read_first_file_bytes(file_path, file_buffer, MAX_READ_SIZE, true, &num_bytes_read))
	{
		if(num_bytes_read >= MIN_RIFX_CHUNK_READ_SIZE)
		{
			u32 chunk_id = 0;
			u32 chunk_format = 0;

			CopyMemory(&chunk_id, &file_buffer[0], sizeof(chunk_id));
			CopyMemory(&chunk_format, &file_buffer[8], sizeof(chunk_format));
			
			chunk_id = swap_byte_order(chunk_id);
			chunk_format = swap_byte_order(chunk_format);

			// This would work without swapping the byte order because we check both big and little endian format signatures.
			// But it'll be useful for the other file types.
			if(chunk_id == CHUNK_RIFX_BIG_ENDIAN || chunk_id == CHUNK_RIFX_LITTLE_ENDIAN)
			{
				switch(chunk_format)
				{
					case(FORMAT_DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN):
					case(FORMAT_DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN):
					{
						file_type = TEXT("Director Movie or Cast");
					} break;

					case(FORMAT_SHOCKWAVE_MOVIE_BIG_ENDIAN):
					case(FORMAT_SHOCKWAVE_MOVIE_LITTLE_ENDIAN):
					{
						file_type = TEXT("Shockwave Movie");
					} break;

					case(FORMAT_SHOCKWAVE_CAST_BIG_ENDIAN):
					case(FORMAT_SHOCKWAVE_CAST_LITTLE_ENDIAN):
					{
						file_type = TEXT("Shockwave Cast");
					} break;
				}				
			}
			else if(chunk_id == CHUNK_RIFF_BIG_ENDIAN && chunk_format == FORMAT_XTRA_PACKAGE_BIG_ENDIAN)
			{
				file_type = TEXT("Xtra-Package");
			}
			// This isn't a RIFF or RIFX container, but we'll take advantage of this structure to check this file signature.
			else if(chunk_id == SHOCKWAVE_3D_WORLD_SIGNATURE)
			{
				file_type = TEXT("Shockwave 3D World");
			}
		}

		if(file_type == NULL && num_bytes_read >= MIN_SHOCKWAVE_AUDIO_READ_SIZE)
		{
			if(memory_is_equal(	&file_buffer[SHOCKWAVE_AUDIO_SIGNATURE_OFFSET],
								SHOCKWAVE_AUDIO_SIGNATURE, SHOCKWAVE_AUDIO_SIGNATURE_SIZE))
			{
				file_type = TEXT("Shockwave Audio");
			}
		}
	}

	return file_type;
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
void export_default_or_specific_shockwave_plugin_cache(Exporter* exporter)
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
	Find_Shockwave_Files_Params* params = (Find_Shockwave_Files_Params*) callback_user_data;

	Exporter* exporter = params->exporter;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* filename = callback_find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, callback_directory_path, filename);

	TCHAR* director_file_type = (params->is_xtra) ? (TEXT("Xtra")) : (get_director_file_type_from_file_signature(full_file_path));

	TCHAR short_file_path[MAX_PATH_CHARS] = TEXT("");
	TCHAR* xtra_description = NULL;
	TCHAR* xtra_version = NULL;

	if(params->is_xtra)
	{
		PathCombine(short_file_path, params->location_identifier, TEXT("[...]"));
		PathAppend(short_file_path, skip_to_last_path_components(full_file_path, 3));

		if(!get_file_info(arena, full_file_path, INFO_FILE_DESCRIPTION, &xtra_description))
		{
			log_print(LOG_WARNING, "Shockwave Plugin: No file description found for the Xtra '%s'.", filename);
		}

		if(!get_file_info(arena, full_file_path, INFO_PRODUCT_VERSION, &xtra_version))
		{
			log_print(LOG_WARNING, "Shockwave Plugin: No product version found for the Xtra '%s'.", filename);
		}
	}
	else
	{
		PathCombine(short_file_path, params->location_identifier, filename);
	}

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Last Write Time */}, {/* Creation Time */}, {/* Last Access Time */},
		{director_file_type}, {xtra_description}, {xtra_version},
		{short_file_path}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, callback_find_data);

	return true;
}
