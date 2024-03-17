#include "cache.h"
#include "common.h"

static Csv_Column _SHOCKWAVE_COLUMNS[] =
{
	CSV_FILENAME, CSV_EXTENSION,
	CSV_CREATION_TIME, CSV_LAST_WRITE_TIME, CSV_LAST_ACCESS_TIME,
	CSV_DIRECTOR_FORMAT, CSV_XTRA_DESCRIPTION, CSV_XTRA_VERSION, CSV_XTRA_COPYRIGHT,
	CSV_INPUT_PATH, CSV_INPUT_SIZE,
	CSV_EXPORTED, CSV_OUTPUT_PATH, CSV_OUTPUT_SIZE,
	CSV_MAJOR_FILE_LABEL, CSV_MINOR_FILE_LABEL,
	CSV_SHA_256,
};

Array_View<Csv_Column> SHOCKWAVE_COLUMNS = ARRAY_VIEW_FROM_C(_SHOCKWAVE_COLUMNS);

static Array<String*>* shockwave_paths(Key_Paths key_paths)
{
	String* base_paths[] = {key_paths.appdata, key_paths.local_low_appdata};
	const TCHAR* vendors[] = {T("Macromedia"), T("Adobe")};

	int base_count = _countof(base_paths);
	int vendor_count = _countof(vendors);

	Array<String*>* result = array_create<String*>(base_count * vendor_count);

	for(int i = 0; i < base_count; i += 1)
	{
		for(int j = 0; j < vendor_count; j += 1)
		{
			String* parent_path = path_build(CANY(base_paths[i]), CANY(vendors[j]));

			Walk_State state = {};
			state.base_path = parent_path;
			state.query = T("*Shockwave*");
			state.directories = true;
			state.copy = true;

			WALK_DEFER(&state)
			{
				Walk_Info info = {};
				while(walk_next(&state, &info))
				{
					array_add(&result, info.path);
				}
			}
		}
	}

	return path_unique_directories(result);
}

static String* shockwave_director_format(String* path)
{
	if(path_has_extension(path, T("x32"))) return CSTR("Xtra");

	const size_t RIFX_CHUNK_BUFFER_SIZE = 12;

	const size_t SHOCKWAVE_AUDIO_MAGIC_OFFSET = 0x24;
	const char SHOCKWAVE_AUDIO_MAGIC[] = "MACR";
	const size_t SHOCKWAVE_AUDIO_MAGIC_SIZE = sizeof(SHOCKWAVE_AUDIO_MAGIC) - 1;
	const size_t SHOCKWAVE_AUDIO_BUFFER_SIZE = SHOCKWAVE_AUDIO_MAGIC_OFFSET + SHOCKWAVE_AUDIO_MAGIC_SIZE;

	const size_t BUFFER_SIZE = MAX(RIFX_CHUNK_BUFFER_SIZE, SHOCKWAVE_AUDIO_BUFFER_SIZE);

	u8 buffer[BUFFER_SIZE] = "";
	size_t bytes_read = 0;
	bool success = file_read_first_at_most(path, buffer, sizeof(buffer), &bytes_read);

	String* result = EMPTY_STRING;

	if(success)
	{
		if(bytes_read >= RIFX_CHUNK_BUFFER_SIZE)
		{
			/*
				@ByteOrder: Big and Little Endian.

				struct Partial_Rifx_Chunk
				{
					u32 id;
					u32 size;
					u32 format;
				};
			*/

			enum Chunk_Id
			{
				ID_RIFX_BIG_ENDIAN = 0x52494658, // "RIFX"
				ID_RIFX_LITTLE_ENDIAN = 0x58464952, // "XFIR"
				ID_RIFF_BIG_ENDIAN = 0x52494646, // "RIFF"
			};

			enum Chunk_Format
			{
				// Director Movie or Cast (DIR, CST, DXR, CXT)
				FORMAT_DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN = 0x4D563933, // "MV93"
				FORMAT_DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN = 0x3339564D, // "39VM"

				// Shockwave Movie (DCR)
				FORMAT_SHOCKWAVE_MOVIE_BIG_ENDIAN = 0x4647444D, // "FGDM"
				FORMAT_SHOCKWAVE_MOVIE_LITTLE_ENDIAN = 0x4D444746, // "MDGF"

				// Shockwave Cast (CCT)
				FORMAT_SHOCKWAVE_CAST_BIG_ENDIAN = 0x46474443, // "FGDC"
				FORMAT_SHOCKWAVE_CAST_LITTLE_ENDIAN = 0x43444746, // "CDGF"

				// Xtra-Package (W32)
				FORMAT_XTRA_PACKAGE_BIG_ENDIAN = 0x50434B32, // "PCK2"
			};

			const u32 SHOCKWAVE_3D_WORLD_MAGIC = 0x49465800; // "IFX."

			u32 chunk_id = 0;
			u32 chunk_format = 0;

			CopyMemory(&chunk_id, buffer, sizeof(chunk_id));
			CopyMemory(&chunk_format, buffer + 8, sizeof(chunk_format));

			BIG_ENDIAN_TO_HOST(chunk_id);
			BIG_ENDIAN_TO_HOST(chunk_format);

			// This would work without swapping the byte order because we check both big and
			// little endian format signatures. But it'll be useful for the other file formats.
			if(chunk_id == ID_RIFX_BIG_ENDIAN || chunk_id == ID_RIFX_LITTLE_ENDIAN)
			{
				switch(chunk_format)
				{
					case(FORMAT_DIRECTOR_MOVIE_OR_CAST_BIG_ENDIAN):
					case(FORMAT_DIRECTOR_MOVIE_OR_CAST_LITTLE_ENDIAN):
					{
						result = CSTR("Director Movie or Cast");
					} break;

					case(FORMAT_SHOCKWAVE_MOVIE_BIG_ENDIAN):
					case(FORMAT_SHOCKWAVE_MOVIE_LITTLE_ENDIAN):
					{
						result = CSTR("Shockwave Movie");
					} break;

					case(FORMAT_SHOCKWAVE_CAST_BIG_ENDIAN):
					case(FORMAT_SHOCKWAVE_CAST_LITTLE_ENDIAN):
					{
						result = CSTR("Shockwave Cast");
					} break;
				}
			}
			else if(chunk_id == ID_RIFF_BIG_ENDIAN && chunk_format == FORMAT_XTRA_PACKAGE_BIG_ENDIAN)
			{
				result = CSTR("Xtra-Package");
			}
			// This isn't a RIFF or RIFX container, but we can still take advantage of the layout.
			else if(chunk_id == SHOCKWAVE_3D_WORLD_MAGIC)
			{
				result = CSTR("Shockwave 3D World");
			}
		}

		if(result == EMPTY_STRING && bytes_read >= SHOCKWAVE_AUDIO_BUFFER_SIZE)
		{
			if(memory_is_equal(buffer + SHOCKWAVE_AUDIO_MAGIC_OFFSET, SHOCKWAVE_AUDIO_MAGIC, SHOCKWAVE_AUDIO_MAGIC_SIZE))
			{
				result = CSTR("Shockwave Audio");
			}
		}
	}
	else
	{
		log_warning("Could not read the file signature from '%s'", path->data);
	}

	return result;
}

static const bool MP_CACHE = true;

static void shockwave_cache_export(Exporter* exporter, String* path, bool mp_cache = false)
{
	log_info("Exporting from '%s'", path->data);

	ARENA_SAVEPOINT()
	{
		REPORT_DEFER(exporter, path)
		{
			Walk_State state = {};
			state.base_path = path;
			state.query = (mp_cache) ? (T("mp*")) : (T("*"));
			state.files = true;
			state.max_depth = (mp_cache) ? (0) : (-1);
			state.copy = true;

			WALK_DEFER(&state)
			{
				Walk_Info info = {};
				while(walk_next(&state, &info))
				{
					Map<Csv_Column, String*>* row = map_create<Csv_Column, String*>(SHOCKWAVE_COLUMNS.count);

					{
						String* format = shockwave_director_format(info.path);
						map_put(&row, CSV_DIRECTOR_FORMAT, format);
					}

					{
						File_Info file_info = file_info_get(info.path);
						map_put(&row, CSV_XTRA_DESCRIPTION, file_info.file_description);
						map_put(&row, CSV_XTRA_VERSION, file_info.product_version);
						map_put(&row, CSV_XTRA_COPYRIGHT, file_info.legal_copyright);
					}

					map_put(&row, CSV_INPUT_PATH, info.path);

					bool xtra = path_has_extension(info.path, T("x32"));

					Export_Params params = {};
					params.info = &info;
					params.subdirectory = (xtra) ? (CSTR("Xtras")) : (CSTR("Cache"));
					params.row = row;
					exporter_next(exporter, params);
				}
			}
		}
	}
}

static void shockwave_dswmedia_xtras_export(Exporter* exporter, String* path)
{
	shockwave_cache_export(exporter, path);
}

static void shockwave_appdata_cache_export(Exporter* exporter, String* path)
{
	ARENA_SAVEPOINT()
	{
		String* dswmedia_path = path_build(CANY(path), CANY(T("DswMedia")));
		shockwave_dswmedia_xtras_export(exporter, dswmedia_path);

		String* xtras_path = path_build(CANY(path), CANY(T("Xtras")));
		shockwave_dswmedia_xtras_export(exporter, xtras_path);
	}
}

static void shockwave_temporary_cache_export(Exporter* exporter, String* path)
{
	shockwave_cache_export(exporter, path, MP_CACHE);
}

void shockwave_single_export(Exporter* exporter, String* path)
{
	shockwave_appdata_cache_export(exporter, path);
}

void shockwave_batch_export(Exporter* exporter, Key_Paths key_paths)
{
	ARENA_SAVEPOINT()
	{
		Array<String*>* paths = shockwave_paths(key_paths);

		for(int i = 0; i < paths->count; i += 1)
		{
			String* path = paths->data[i];
			shockwave_appdata_cache_export(exporter, path);
		}

		shockwave_temporary_cache_export(exporter, key_paths.temporary);
	}
}

void shockwave_tests(void)
{
	console_info("Running Shockwave tests");
	log_info("Running Shockwave tests");

	{
		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\movie_be.dir")), T("Director Movie or Cast"));
		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\movie_le.dir")), T("Director Movie or Cast"));

		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\movie_be.dcr")), T("Shockwave Movie"));
		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\movie_le.dcr")), T("Shockwave Movie"));

		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\cast_be.cct")), T("Shockwave Cast"));
		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\cast_le.cct")), T("Shockwave Cast"));

		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\world.w3d")), T("Shockwave 3D World"));
		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\audio.swa")), T("Shockwave Audio"));

		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\xtra.x32")), T("Xtra"));
		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\xtra_package.w32")), T("Xtra-Package"));

		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\file.txt")), T(""));
		TEST(shockwave_director_format(CSTR("Tests\\Shockwave\\empty.txt")), T(""));
	}
}