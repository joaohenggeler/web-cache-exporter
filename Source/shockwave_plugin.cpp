#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "shockwave_plugin.h"

enum Shockwave_Plugin_Cache_Version
{
	SHOCKWAVE_CACHE_UNKNOWN = 0,
	SHOCKWAVE_CACHE = 1,
	NUM_SHOCKWAVE_CACHE_VERSIONS = 2
};
TCHAR* SHOCKWAVE_CACHE_CACHE_VERSION_TO_STRING[NUM_SHOCKWAVE_CACHE_VERSIONS] =
{
	TEXT("Shockwave-Plugin-Unknown"),
	TEXT("Shockwave-Plugin")
};

#pragma pack(push, 1)
struct Partial_Director_Rifx_Chunk
{
	u32 id;
	u32 size;
	u32 codec;
};
#pragma pack(pop)

const u32 RIFX_BIG_ENDIAN = 0x52494658; // "RIFX"
const u32 RIFX_LITTLE_ENDIAN = 0x58464952; // "XFIR"

enum Director_Codec
{
	DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN = 0x4D563933, // "MV93"
	DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN = 0x3339564D, // "39VM"
	SHOCKWAVE_MOVIE_BIG_ENDIAN = 0x4647444D, // "FGDM"
	SHOCKWAVE_MOVIE_LITTLE_ENDIAN = 0x4D444746, // "MDGF"
	SHOCKWAVE_CAST_BIG_ENDIAN = 0x46474443, // "FGDC"
	SHOCKWAVE_CAST_LITTLE_ENDIAN = 0x43444746 // "CDGF"
};

static TCHAR* get_director_file_type(TCHAR* file_path)
{
	Partial_Director_Rifx_Chunk chunk;

	if(read_first_file_bytes(file_path, &chunk, sizeof(Partial_Director_Rifx_Chunk)))
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

	return TEXT("");
}

void export_specific_or_default_shockwave_plugin_cache(Exporter* exporter)
{
	if(is_string_empty(exporter->cache_path))
	{
		if(GetTempPath(MAX_PATH_CHARS, exporter->cache_path) == 0)
		{
			log_print(LOG_ERROR, "Shockwave Plugin: Failed to get the temporary files directory path.");
			return;
		}
	}

	get_full_path_name(exporter->cache_path);
	log_print(LOG_INFO, "Shockwave Plugin: Exporting the cache from '%s'.", exporter->cache_path);

	resolve_cache_version_output_paths(exporter, SHOCKWAVE_CACHE, SHOCKWAVE_CACHE_CACHE_VERSION_TO_STRING);
	export_shockwave_plugin_cache(exporter);
	log_print(LOG_INFO, "Shockwave Plugin: Finished exporting the cache.");
}

void export_shockwave_plugin_cache(Exporter* exporter)
{
	Arena* arena = &(exporter->arena);

	const size_t CSV_NUM_COLUMNS = 7;
	const Csv_Type csv_header[CSV_NUM_COLUMNS] =
	{
		CSV_FILENAME, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
		CSV_LAST_WRITE_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME,
		CSV_DIRECTOR_FILE_TYPE
	};

	HANDLE csv_file = INVALID_HANDLE_VALUE;
	if(exporter->should_create_csv)
	{
		csv_file = create_csv_file(exporter->output_csv_path);
		csv_print_header(arena, csv_file, csv_header, CSV_NUM_COLUMNS);
		clear_arena(arena);
	}

	TCHAR search_cache_path[MAX_PATH_CHARS];
	StringCchCopy(search_cache_path, MAX_PATH_CHARS, exporter->cache_path);
	PathAppend(search_cache_path, TEXT("mp*"));

	WIN32_FIND_DATA file_find_data;
	HANDLE search_handle = FindFirstFile(search_cache_path, &file_find_data);
	bool found_file = search_handle != INVALID_HANDLE_VALUE;
	while(found_file)
	{
		// Ignore directories.
		if((file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
			continue;
		}

		TCHAR* filename = file_find_data.cFileName;
		_ASSERT(filename != NULL);

		TCHAR full_file_path[MAX_PATH_CHARS];
		StringCchCopy(full_file_path, MAX_PATH_CHARS, exporter->cache_path);
		PathAppend(full_file_path, filename);

		if(exporter->should_create_csv)
		{
			TCHAR* file_extension = skip_to_file_extension(filename);

			u64 file_size = combine_high_and_low_u32s(file_find_data.nFileSizeHigh, file_find_data.nFileSizeLow);
			TCHAR file_size_string[MAX_INT64_CHARS];
			convert_u64_to_string(file_size, file_size_string);

			TCHAR last_write_time[MAX_FORMATTED_DATE_TIME_CHARS];
			format_filetime_date_time(file_find_data.ftLastWriteTime, last_write_time);

			TCHAR creation_time[MAX_FORMATTED_DATE_TIME_CHARS];
			format_filetime_date_time(file_find_data.ftCreationTime, creation_time);
			
			TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS];
			format_filetime_date_time(file_find_data.ftLastAccessTime, last_access_time);

			TCHAR* director_file_type = get_director_file_type(full_file_path);

			Csv_Entry csv_row[CSV_NUM_COLUMNS] =
			{
				{filename}, {file_extension}, {file_size_string},
				{last_write_time}, {last_access_time}, {creation_time},
				{director_file_type}
			};

			csv_print_row(arena, csv_file, csv_header, csv_row, CSV_NUM_COLUMNS);
		}

		if(exporter->should_copy_files)
		{
			copy_file_using_url_directory_structure(arena, full_file_path, exporter->output_copy_path, NULL, filename);
		}

		clear_arena(arena);

		found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
	}

	FindClose(search_handle);
	search_handle = INVALID_HANDLE_VALUE;

	close_csv_file(csv_file);
	csv_file = INVALID_HANDLE_VALUE;
}
