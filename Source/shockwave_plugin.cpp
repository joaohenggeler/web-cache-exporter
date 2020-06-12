#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "shockwave_plugin.h"

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

static char* get_director_file_type(char* file_path)
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
					return "DIR / CST / DXR / CXT";
				} break;

				case(SHOCKWAVE_MOVIE_BIG_ENDIAN):
				case(SHOCKWAVE_MOVIE_LITTLE_ENDIAN):
				{
					return "DCR";
				} break;

				case(SHOCKWAVE_CAST_BIG_ENDIAN):
				case(SHOCKWAVE_CAST_LITTLE_ENDIAN):
				{
					return "CCT";
				} break;
			}
		}
	}

	return "";
}

void export_specific_or_default_shockwave_plugin_cache(Exporter* exporter)
{
	if(is_string_empty(exporter->cache_path))
	{
		if(GetTempPathA(MAX_PATH_CHARS, exporter->cache_path) != 0)
		{
			export_specific_shockwave_plugin_cache(exporter);
		}
		else
		{
			log_print(LOG_ERROR, "Shockwave Plugin: Failed to get the temporary files directory path.");
		}
	}
	else
	{
		export_specific_shockwave_plugin_cache(exporter);
	}
}

void export_specific_shockwave_plugin_cache(Exporter* exporter)
{
	char cache_path[MAX_PATH_CHARS];
	GetFullPathNameA(exporter->cache_path, MAX_PATH_CHARS, cache_path, NULL);
	log_print(LOG_INFO, "Shockwave Plugin: Exporting the cache from '%s'.", cache_path);

	char output_copy_path[MAX_PATH_CHARS];
	GetFullPathNameA(exporter->output_path, MAX_PATH_CHARS, output_copy_path, NULL);
	PathAppendA(output_copy_path, "ShockwavePlugin");
	
	char output_csv_path[MAX_PATH_CHARS];
	GetFullPathNameA(exporter->output_path, MAX_PATH_CHARS, output_csv_path, NULL);
	PathAppendA(output_csv_path, "ShockwavePlugin.csv");

	const char* CSV_HEADER = "Filename,File Extension,File Size,Last Write Time,Last Access Time,Creation Time,Director File Type\r\n";
	const size_t CSV_NUM_COLUMNS = 7;
	HANDLE csv_file = INVALID_HANDLE_VALUE;
	if(exporter->should_create_csv)
	{
		csv_file = create_csv_file(output_csv_path);
		csv_print_header(csv_file, CSV_HEADER);
	}

	char search_cache_path[MAX_PATH_CHARS];
	StringCchCopyA(search_cache_path, MAX_PATH_CHARS, cache_path);
	PathAppendA(search_cache_path, "mp*");

	WIN32_FIND_DATAA file_find_data;
	HANDLE search_handle = FindFirstFileA(search_cache_path, &file_find_data);
	bool found_file = search_handle != INVALID_HANDLE_VALUE;
	while(found_file)
	{
		// Ignore directories.
		if((file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			found_file = FindNextFileA(search_handle, &file_find_data) == TRUE;
			continue;
		}

		char* filename = file_find_data.cFileName;

		char full_file_path[MAX_PATH_CHARS];
		StringCchCopyA(full_file_path, MAX_PATH_CHARS, cache_path);
		PathAppendA(full_file_path, filename);

		if(exporter->should_create_csv)
		{
			char* file_extension = skip_to_file_extension(filename);

			u64 file_size = combine_high_and_low_u32s(file_find_data.nFileSizeHigh, file_find_data.nFileSizeLow);
			char file_size_string[MAX_UINT64_CHARS];
			_ui64toa_s(file_size, file_size_string, MAX_UINT64_CHARS, INT_FORMAT_RADIX);

			char last_write_time[MAX_FORMATTED_DATE_TIME_CHARS];
			format_filetime_date_time(file_find_data.ftLastWriteTime, last_write_time);

			char creation_time[MAX_FORMATTED_DATE_TIME_CHARS];
			format_filetime_date_time(file_find_data.ftCreationTime, creation_time);
			
			char last_access_time[MAX_FORMATTED_DATE_TIME_CHARS];
			format_filetime_date_time(file_find_data.ftLastAccessTime, last_access_time);

			char* director_file_type = get_director_file_type(full_file_path);

			char* csv_row[CSV_NUM_COLUMNS] = {filename, file_extension, file_size_string,
											last_write_time, creation_time, last_access_time,
											director_file_type};

			csv_print_row(&exporter->arena, csv_file, csv_row, CSV_NUM_COLUMNS);
		}

		if(exporter->should_copy_files)
		{
			copy_file_using_url_directory_structure(&exporter->arena, full_file_path, output_copy_path, NULL, filename);
		}

		found_file = FindNextFileA(search_handle, &file_find_data) == TRUE;
	}

	close_csv_file(csv_file);
}
