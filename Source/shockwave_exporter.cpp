#include "web_cache_exporter.h"
#include "shockwave_exporter.h"

/*
	This file defines how the exporter processes the Adobe (previously Macromedia) Shockwave Player's web plugin cache.
	For this type of cache, we'll work directly with the files stored on disk instead of parsing a database with metadata
	about each file.

	@SupportedFormats: Director 6 and later.

	@DefaultCacheLocations: The Temporary Files directory. This location is specified in the TEMP or TMP environment variables.
	- 98, ME 				C:\WINDOWS\TEMP
	- 2000, XP 				C:\Documents and Settings\<Username>\Local Settings\Temp
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Local\Temp

	The names of these cached files start with "mp", followed by at least six more characters (e.g. mpb02684.w3d). The exporter
	will also copy any Xtras (.x32 files) in the Temporary Files directory and its subdirectories.

	There are some other locations in the AppData and Local Low AppData directories that should be checked for cached files
	and Xtras:
	- 98, ME 				<AppData or Local Low AppData>\<Macromedia or Adobe>\<Shockwave Version>\<Cache Type>
	- 2000, XP 				<AppData or Local Low AppData>\<Macromedia or Adobe>\<Shockwave Version>\<Cache Type>
	- Vista, 7, 8.1, 10	 	<AppData or Local Low AppData>\<Macromedia or Adobe>\<Shockwave Version>\<Cache Type>

	The first two identifiers represent directory names as is (e.g. C:\Users\<Username>\AppData\LocalLow\Adobe), but the other two
	require some additional explanation:
	- <Shockwave Version> is the directory used for the Shockwave Player version that cached the files. The actual names
	depend on some factors (e.g. downloading compatibility component Xtras), but here are some observed names: "Shockwave Player",
	"Shockwave Player 11", "Shockwave Player 12".
	- <Cache Type> can be either "DswMedia" (cached files), "Prefs" (text files that could be used to store	user data locally,
	similarly to Flash cookies), or "Xtras".

	For this last cache location, we'll export everything in "DswMedia", "Xtras", and any of their subdirectories.

	@SupportsCustomCacheLocations:
	- Same Machine: Unknown if this location can be changed by the user.
	- External Locations: Unknown, see above.

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

static const TCHAR* OUTPUT_NAME = T("SW");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_CREATION_TIME, CSV_LAST_WRITE_TIME, CSV_LAST_ACCESS_TIME,
	CSV_DIRECTOR_FILE_TYPE, CSV_XTRA_DESCRIPTION, CSV_XTRA_VERSION, CSV_XTRA_COPYRIGHT,
	CSV_LOCATION_ON_CACHE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_SHA_256
};

static const int CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Since cached Shockwave files can be stored on disk without a file extension, we'll make it easier to tell what kind of file was
// found by reading and interpreting their first bytes.

/*
	@ByteOrder: Big and Little Endian.

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
			
			BIG_ENDIAN_TO_HOST(chunk_id);
			BIG_ENDIAN_TO_HOST(chunk_format);

			// This would work without swapping the byte order because we check both big and little endian format signatures.
			// But it'll be useful for the other file types.
			if(chunk_id == CHUNK_RIFX_BIG_ENDIAN || chunk_id == CHUNK_RIFX_LITTLE_ENDIAN)
			{
				switch(chunk_format)
				{
					case(FORMAT_DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN):
					case(FORMAT_DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN):
					{
						file_type = T("Director Movie or Cast");
					} break;

					case(FORMAT_SHOCKWAVE_MOVIE_BIG_ENDIAN):
					case(FORMAT_SHOCKWAVE_MOVIE_LITTLE_ENDIAN):
					{
						file_type = T("Shockwave Movie");
					} break;

					case(FORMAT_SHOCKWAVE_CAST_BIG_ENDIAN):
					case(FORMAT_SHOCKWAVE_CAST_LITTLE_ENDIAN):
					{
						file_type = T("Shockwave Cast");
					} break;
				}				
			}
			else if(chunk_id == CHUNK_RIFF_BIG_ENDIAN && chunk_format == FORMAT_XTRA_PACKAGE_BIG_ENDIAN)
			{
				file_type = T("Xtra-Package");
			}
			// This isn't a RIFF or RIFX container, but we'll take advantage of this structure to check this file signature.
			else if(chunk_id == SHOCKWAVE_3D_WORLD_SIGNATURE)
			{
				file_type = T("Shockwave 3D World");
			}
		}

		if(file_type == NULL && num_bytes_read >= MIN_SHOCKWAVE_AUDIO_READ_SIZE)
		{
			if(memory_is_equal(	&file_buffer[SHOCKWAVE_AUDIO_SIGNATURE_OFFSET],
								SHOCKWAVE_AUDIO_SIGNATURE, SHOCKWAVE_AUDIO_SIGNATURE_SIZE))
			{
				file_type = T("Shockwave Audio");
			}
		}
	}
	else
	{
		log_warning("Get Director File Type From File Signature: Could not read the file signature.");
	}

	return file_type;
}

struct Find_Shockwave_Files_Params
{
	Exporter* exporter;
	bool is_appdata_cache;
	const TCHAR* location_identifier;
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
void export_default_or_specific_shockwave_cache(Exporter* exporter)
{
	Arena* arena = &(exporter->temporary_arena);

	console_print("Exporting the Shockwave Player's cache...");

	initialize_cache_exporter(exporter, CACHE_SHOCKWAVE, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		if(exporter->is_exporting_from_default_locations)
		{
			StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, exporter->windows_temporary_path);
		}

		log_info("Shockwave Player: Exporting the cache and Xtras from '%s'.", exporter->cache_path);

		Find_Shockwave_Files_Params file_params = {};
		file_params.exporter = exporter;
		file_params.is_appdata_cache = false;
		file_params.location_identifier = T("<Temporary>");

		set_exporter_output_copy_subdirectory(exporter, T("Cache"));
		traverse_directory_objects(exporter->cache_path, T("mp*"), TRAVERSE_FILES, false, find_shockwave_files_callback, &file_params);
		
		set_exporter_output_copy_subdirectory(exporter, T("Xtras"));
		traverse_directory_objects(exporter->cache_path, T("*.x32"), TRAVERSE_FILES, true, find_shockwave_files_callback, &file_params);

		if(exporter->is_exporting_from_default_locations)
		{
			const TCHAR* base_paths[] = {exporter->appdata_path, exporter->local_low_appdata_path};
			const TCHAR* base_identifiers[] = {T("<AppData>"), T("<Local Low AppData>")};
			_STATIC_ASSERT(_countof(base_paths) == _countof(base_identifiers));

			const TCHAR* vendor_directories[] = {T("Macromedia"), T("Adobe")};

			for(int i = 0; i < _countof(base_paths); ++i)
			{
				const TCHAR* base_path = base_paths[i];
				if(strings_are_equal(base_path, PATH_NOT_FOUND)) continue;
				// Local Low AppData is skipped for Windows 98 through XP.
			
				file_params.is_appdata_cache = true;
				file_params.location_identifier = base_identifiers[i];
			
				for(int j = 0; j < _countof(vendor_directories); ++j)
				{
					TCHAR vendor_directory_path[MAX_PATH_CHARS] = T("");
					PathCombine(vendor_directory_path, base_path, vendor_directories[j]);
					Traversal_Result* version_directories = find_objects_in_directory(arena, vendor_directory_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_DIRECTORIES, false);
					lock_arena(arena);

					log_info("Shockwave Player: Exporting additional cached files and Xtras from '%s'.", vendor_directory_path);
					for(int k = 0; k < version_directories->num_objects; ++k)
					{
						Traversal_Object_Info directory_info = version_directories->object_info[k];
						
						set_exporter_output_copy_subdirectory(exporter, T("Cache"));
						PathCombine(exporter->cache_path, directory_info.object_path, T("DswMedia"));
						traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, true, find_shockwave_files_callback, &file_params);

						set_exporter_output_copy_subdirectory(exporter, T("Xtras"));
						PathCombine(exporter->cache_path, directory_info.object_path, T("Xtras"));
						traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, true, find_shockwave_files_callback, &file_params);
					}

					unlock_arena(arena);
				}
			}
		}

		log_info("Shockwave Player: Finished exporting the cache.");	
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
	Find_Shockwave_Files_Params* file_params = (Find_Shockwave_Files_Params*) callback_info->user_data;

	Exporter* exporter = file_params->exporter;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* filename = callback_info->object_name;

	TCHAR full_location_on_cache[MAX_PATH_CHARS] = T("");
	PathCombine(full_location_on_cache, callback_info->directory_path, filename);

	bool is_xtra = filename_ends_with(filename, T(".x32"));
	TCHAR* director_file_type = (is_xtra) ? (T("Xtra")) : (get_director_file_type_from_file_signature(full_location_on_cache));

	TCHAR* xtra_description = NULL;
	TCHAR* xtra_version = NULL;
	TCHAR* xtra_copyright = NULL;

	if(is_xtra)
	{
		if(!get_file_info(arena, full_location_on_cache, INFO_FILE_DESCRIPTION, &xtra_description))
		{
			log_warning("Shockwave Player: No file description found for the Xtra '%s'.", filename);
		}

		if(!get_file_info(arena, full_location_on_cache, INFO_PRODUCT_VERSION, &xtra_version))
		{
			log_warning("Shockwave Player: No product version found for the Xtra '%s'.", filename);
		}

		if(!get_file_info(arena, full_location_on_cache, INFO_LEGAL_COPYRIGHT, &xtra_copyright))
		{
			log_warning("Shockwave Player: No copyright found for the Xtra '%s'.", filename);
		}		
	}

	TCHAR short_location_on_cache[MAX_PATH_CHARS] = T("");

	if(file_params->is_appdata_cache)
	{
		PathCombine(short_location_on_cache, file_params->location_identifier, skip_to_last_path_components(full_location_on_cache, 3));
	}
	else
	{
		PathCombine(short_location_on_cache, file_params->location_identifier, filename);
	}

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* File Extension */}, {/* File Size */},
		{/* Creation Time */}, {/* Last Write Time */}, {/* Last Access Time */},
		{director_file_type}, {xtra_description}, {xtra_version}, {xtra_copyright},
		{/* Location On Cache */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter_Params exporter_params = {};
	exporter_params.copy_source_path = full_location_on_cache;
	exporter_params.short_location_on_cache = short_location_on_cache;
	exporter_params.file_info = callback_info;

	export_cache_entry(exporter, csv_row, &exporter_params);

	return true;
}
