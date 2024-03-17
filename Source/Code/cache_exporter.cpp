#include "cache.h"
#include "common.h"

static const TCHAR* SHORT_NAMES[] =
{
	T("WALK"),
	T("IE"), T("MZ"),
	T("FL"), T("SW"), T("JV"), T("UN"),
};

static const TCHAR* LONG_NAMES[] =
{
	T("Walk"),
	T("WinINet"), T("Mozilla"),
	T("Flash"), T("Shockwave"), T("Java"), T("Unity"),
};

static Array_View<Csv_Column> COLUMNS[] =
{
	{},
	{}, MOZILLA_COLUMNS,
	{}, SHOCKWAVE_COLUMNS, {}, {},
};

_STATIC_ASSERT(_countof(SHORT_NAMES) == MAX_CACHE);
_STATIC_ASSERT(_countof(LONG_NAMES) == MAX_CACHE);
_STATIC_ASSERT(_countof(COLUMNS) == MAX_CACHE);

bool cache_flags_from_names(const TCHAR* names, u32* flags)
{
	bool success = true;

	*flags = 0;

	Split_State state = {};
	state.view = view_from_c(names);
	state.delimiters = T(",");

	String_View name = {};
	while(string_split(&state, &name))
	{
		if(string_is_equal(name, T("browsers"), IGNORE_CASE))
		{
			*flags |= CACHE_BROWSERS;
		}
		else if(string_is_equal(name, T("plugins"), IGNORE_CASE))
		{
			*flags |= CACHE_PLUGINS;
		}
		else if(string_is_equal(name, T("all"), IGNORE_CASE))
		{
			*flags |= CACHE_ALL;
		}
		else
		{
			bool found = false;

			for(int i = 0; i < MAX_CACHE; i += 1)
			{
				if(string_is_equal(name, SHORT_NAMES[i], IGNORE_CASE)
				|| string_is_equal(name, LONG_NAMES[i], IGNORE_CASE))
				{
					*flags |= (1 << i);
					found = true;
					break;
				}
			}

			if(!found)
			{
				console_error("Unknown cache format '%.*s'", name.code_count, name.data);
				log_error("Unknown cache format '%.*s' in '%s'", name.code_count, name.data, names);
				success = false;
				break;
			}
		}
	}

	if(success && *flags == 0)
	{
		console_error("No cache formats found");
		log_error("No cache formats found in '%s'", names);
		success = false;
	}

	return success;
}

Key_Paths default_key_paths(void)
{
	Key_Paths key_paths = {};
	key_paths.name = EMPTY_STRING;

	String_Builder* builder = builder_create(MAX_PATH_COUNT);

	// - 98, ME, XP, Vista, 7, 8, 8.1, 10, 11	C:\WINDOWS
	// - 2000									C:\WINNT
	if(GetWindowsDirectory(builder->data, builder->capacity) != 0)
	{
		// The drive path used to be determined using GetVolumeInformation,
		// but that function started returning names like "Windows-SSD" in
		// newer Windows versions.
		key_paths.windows = builder_to_string(builder);
		key_paths.drive = string_from_view(string_slice(key_paths.windows, 0, 3));
	}
	else
	{
		log_error("Failed to get the Windows path with the error: %s", last_error_message());
		key_paths.windows = NO_PATH;
		key_paths.drive = NO_PATH;
	}

	// - 98, ME									C:\WINDOWS\TEMP
	// - 2000, XP								C:\Documents and Settings\<User>\Local Settings\Temp
	// - Vista, 7, 8, 8.1, 10, 11				C:\Users\<User>\AppData\Local\Temp
	if(GetTempPath(builder->capacity, builder->data) != 0)
	{
		key_paths.temporary = builder_to_string(builder);
	}
	else
	{
		log_error("Failed to get the temporary path with the error: %s", last_error_message());
		key_paths.temporary = NO_PATH;
	}

	// - 98, ME									<None>
	// - 2000, XP								C:\Documents and Settings\<User>
	// - Vista, 7, 8, 8.1, 10, 11				C:\Users\<User>
	if(!path_from_csidl(CSIDL_PROFILE, &key_paths.user)) key_paths.user = NO_PATH;


	// - 98, ME									C:\WINDOWS\Application Data
	// - 2000, XP								C:\Documents and Settings\<User>\Application Data
	// - Vista, 7, 8, 8.1, 10, 11				C:\Users\<User>\AppData\Roaming
	if(!path_from_csidl(CSIDL_APPDATA, &key_paths.appdata)) key_paths.appdata = NO_PATH;

	// - 98, ME									<None>
	// - 2000, XP								C:\Documents and Settings\<User>\Local Settings\Application Data
	// - Vista, 7, 8, 8.1, 10, 11				C:\Users\<User>\AppData\Local
	if(!path_from_csidl(CSIDL_LOCAL_APPDATA, &key_paths.local_appdata)) key_paths.local_appdata = NO_PATH;

	// - 98, ME									<None>
	// - 2000, XP								<None>
	// - Vista, 7, 8, 8.1, 10, 11				C:\Users\<User>\AppData\LocalLow
	if(!path_from_kfid(KFID_LOCAL_LOW_APPDATA, &key_paths.local_low_appdata)) key_paths.local_low_appdata = NO_PATH;

	// - 98, ME									C:\WINDOWS\Temporary Internet Files
	// - 2000, XP								C:\Documents and Settings\<User>\Local Settings\Temporary Internet Files
	// - Vista, 7								C:\Users\<User>\AppData\Local\Microsoft\Windows\Temporary Internet Files
	// - 8, 8.1, 10, 11							C:\Users\<User>\AppData\Local\Microsoft\Windows\INetCache
	if(!path_from_csidl(CSIDL_INTERNET_CACHE, &key_paths.wininet)) key_paths.wininet = NO_PATH;

	return key_paths;
}

static void exporter_begin(Exporter* exporter, u32 flag, String* subdirectory = NULL)
{
	exporter->current_found = 0;
	exporter->current_exported = 0;
	exporter->current_excluded = 0;

	u32 index = flag_to_index(flag);
	ASSERT(0 <= index && index < MAX_CACHE, "Flag index out of range");

	exporter->current_flag = flag;
	exporter->current_short = string_from_c(SHORT_NAMES[index]);
	exporter->current_long = string_from_c(LONG_NAMES[index]);

	if(exporter->copy_files)
	{
		builder_clear(exporter->builder);
		builder_append_path(&exporter->builder, exporter->output_path);
		if(subdirectory != NULL) builder_append_path(&exporter->builder, subdirectory);
		builder_append_path(&exporter->builder, exporter->current_short);
		exporter->current_output = builder_to_string(exporter->builder);
	}

	if(exporter->create_csvs)
	{
		builder_clear(exporter->builder);
		builder_append_path(&exporter->builder, exporter->output_path);
		if(subdirectory != NULL) builder_append_path(&exporter->builder, subdirectory);
		builder_append_path(&exporter->builder, exporter->current_short);
		builder_append(&exporter->builder, T(".csv"));

		String* path = builder_to_string(exporter->builder);
		Array_View<Csv_Column> columns = COLUMNS[index];
		csv_begin(&exporter->current_csv, path, columns);
	}

	#ifdef WCE_DEBUG
		context.debug_exporter_balance += 1;
	#endif

	ASSERT(exporter->builder != NULL, "Terminated builder");
}

static void exporter_end(Exporter* exporter)
{
	ASSERT(exporter->current_flag != 0, "Missing current flag");
	ASSERT(exporter->current_short != NULL, "Missing current short");
	ASSERT(exporter->current_long != NULL, "Missing current long");
	ASSERT(!exporter->copy_files || exporter->current_output != NULL, "Missing current output");
	ASSERT(!exporter->current_batch || exporter->current_key_paths.name != NULL, "Missing current key paths");

	if(exporter->create_csvs && exporter->current_csv.created)
	{
		csv_end(&exporter->current_csv);
		if(file_is_empty(exporter->current_csv.path)) file_delete(exporter->current_csv.path);
	}

	if(exporter->current_found > 0)
	{
		console_progress_end();
		console_info("%s: Exported %d of %d files (%d excluded)", exporter->current_long->data, exporter->current_exported, exporter->current_found, exporter->current_excluded);
		log_info("%s: Exported %d of %d files (%d excluded)", exporter->current_long->data, exporter->current_exported, exporter->current_found, exporter->current_excluded);
	}
	else
	{
		console_info("%s: No files found", exporter->current_long->data);
		log_info("%s: No files found", exporter->current_long->data);
	}

	exporter->current_flag = 0;
	exporter->current_short = NULL;
	exporter->current_long = NULL;
	exporter->current_output = NULL;
	exporter->current_batch = false;
	ZeroMemory(&exporter->current_key_paths, sizeof(exporter->current_key_paths));

	#ifdef WCE_DEBUG
		context.debug_exporter_balance -= 1;
	#endif
}

String* exporter_path_localize(Exporter* exporter, String* path)
{
	ASSERT(exporter->current_batch, "Localizing path in single mode");
	ASSERT(exporter->current_key_paths.drive != NULL, "Missing drive");
	ASSERT(path_is_absolute(path), "Path is relative");

	String* path_without_drive = string_from_view(string_slice(path, 3, path->char_count));
	return path_build(CANY(exporter->current_key_paths.drive), CANY(path_without_drive));
}

void exporter_index_put(Map<Sha256, bool>** index_ptr, String* path)
{
	Sha256 sha256 = sha256_bytes_from_file(path, TEMPORARY);
	map_put(index_ptr, sha256, true);
}

bool exporter_index_has(Map<Sha256, bool>* index, String* path)
{
	Sha256 sha256 = sha256_bytes_from_file(path, TEMPORARY);
	return map_has(index, sha256);
}

static String* exporter_yes_or_no(bool yes)
{
	return (yes) ? (CSTR("Yes")) : (CSTR("No"));
}

static String* exporter_filename(Exporter* exporter)
{
	String_Builder* builder = builder_create(8);
	exporter->filename_count += 1;
	builder_append_format(&builder, T("~WCE%04d"), exporter->filename_count);
	return builder_terminate(&builder);
}

struct Copy_Params
{
	String* from_path;
	String* to_path;
	String* fallback_path;
	String* extension;
};

static bool exporter_copy(Exporter* exporter, Copy_Params params, String** final_path)
{
	// Any path that is used to build the destination must be
	// absolute so we can check the count against MAX_PATH.
	params.to_path = path_absolute(params.to_path);
	params.fallback_path = path_absolute(params.fallback_path);

	bool copy_success = true;

	String_Builder* builder = builder_create(MAX_PATH_COUNT);
	bool fallback = false;

	if(params.to_path->code_count > MAX_PATH_COUNT)
	{
		fallback = true;

		if(!directory_create(params.fallback_path))
		{
			log_error("Failed to create the fallback directory '%s'", params.fallback_path->data);
			copy_success = false;
		}

		Path_Parts parts = path_parse(params.to_path);
		builder_append_path(&builder, params.fallback_path);
		builder_append_path(&builder, parts.name);
	}
	else
	{
		ARENA_SAVEPOINT()
		{
			Path_Parts parts = path_parse(params.to_path);

			String* directory_path = EMPTY_STRING;
			String_Builder* parent_builder = builder_create(parts.parent.code_count);
			String_Builder* collision_builder = builder_create(params.to_path->code_count + 5);

			Split_State state = {};
			state.view = parts.parent;
			state.delimiters = PATH_DELIMITERS;

			String_View component = {};
			while(string_split(&state, &component))
			{
				builder_append_path(&parent_builder, component);
				directory_path = builder_to_string(parent_builder);

				int collisions = 0;
				bool directory_success = CreateDirectory(directory_path->data, NULL) != FALSE;
				#define COLLISION() (GetLastError() == ERROR_ALREADY_EXISTS && path_is_file(directory_path))

				while(!directory_success && COLLISION())
				{
					collisions += 1;

					builder_clear(collision_builder);
					builder_append_path(&collision_builder, path_parent(directory_path));
					builder_append_path(&collision_builder, component);
					builder_append_format(&collision_builder, T("~%d"), collisions);

					directory_path = builder_to_string(collision_builder);
					directory_success = CreateDirectory(directory_path->data, NULL) != FALSE;
				}

				#undef COLLISION

				if(!directory_success && GetLastError() != ERROR_ALREADY_EXISTS)
				{
					log_error("Failed to create '%s' of '%.*s' with the error: %s", directory_path->data, parts.parent.code_count, parts.parent.data, last_error_message());
					copy_success = false;
					break;
				}
			}

			builder_append_path(&builder, directory_path);
			builder_append_path(&builder, parts.name);
		}
	}

	if(!copy_success) return copy_success;

	String* file_path = builder_to_string(builder);
	Path_Parts parts = path_parse(file_path);

	if(file_path->code_count > MAX_PATH_COUNT)
	{
		String* filename = exporter_filename(exporter);

		// We only want to use the parent path for URLs.
		if(!fallback)
		{
			builder_clear(builder);
			builder_append_path(&builder, parts.parent);
			builder_append_path(&builder, filename);

			if(params.extension->char_count > 0)
			{
				builder_append(&builder, T("."));
				builder_append(&builder, params.extension);
			}

			file_path = builder_to_string(builder);
			parts = path_parse(file_path);
		}

		if(file_path->code_count > MAX_PATH_COUNT)
		{
			builder_clear(builder);
			builder_append_path(&builder, params.fallback_path);
			builder_append_path(&builder, filename);

			if(params.extension->char_count > 0)
			{
				builder_append(&builder, T("."));
				builder_append(&builder, params.extension);
			}

			file_path = builder_to_string(builder);
			parts = path_parse(file_path);
		}
	}

	int collisions = 0;
	#ifdef WCE_DEBUG
		copy_success = (exporter->empty_copy) ? (file_empty_create(file_path)) : (file_copy_try(params.from_path, file_path));
	#else
		copy_success = file_copy_try(params.from_path, file_path);
	#endif
	#define COLLISION() ( GetLastError() == ERROR_FILE_EXISTS || (GetLastError() == ERROR_ACCESS_DENIED && path_is_directory(file_path)) )

	while(!copy_success && COLLISION())
	{
		collisions += 1;

		builder_clear(builder);
		builder_append_path(&builder, parts.parent);
		builder_append_path(&builder, parts.stem);
		const TCHAR* format = (parts.extension.char_count > 0) ? (T("~%d.")) : (T("~%d"));
		builder_append_format(&builder, format, collisions);
		builder_append(&builder, parts.extension);

		file_path = builder_to_string(builder);
		#ifdef WCE_DEBUG
			copy_success = (exporter->empty_copy) ? (file_empty_create(file_path)) : (file_copy_try(params.from_path, file_path));
		#else
			copy_success = file_copy_try(params.from_path, file_path);
		#endif
	}

	// For rare error cases.
	if(!copy_success)
	{
		String* filename = exporter_filename(exporter);

		builder_clear(builder);
		builder_append_path(&builder, params.fallback_path);
		builder_append_path(&builder, filename);

		if(params.extension->char_count > 0)
		{
			builder_append(&builder, T("."));
			builder_append(&builder, params.extension);
		}

		file_path = builder_to_string(builder);

		#ifdef WCE_DEBUG
			copy_success = (exporter->empty_copy) ? (file_empty_create(file_path)) : (file_copy_try(params.from_path, file_path));
		#else
			copy_success = file_copy_try(params.from_path, file_path);
		#endif
	}

	if(copy_success) *final_path = file_path;
	else log_error("Failed to copy '%s' to '%s' with the error: %s", params.from_path->data, file_path->data, last_error_message());

	#undef COLLISION

	return copy_success;
}

void exporter_next(Exporter* exporter, Export_Params params)
{
	#define CSV_HAS_NOT(column) (array_has(exporter->current_csv.columns, column) && !map_has(params.row, column))

	ASSERT(params.row != NULL, "Missing row");

	ASSERT(CSV_HAS_NOT(CSV_FOUND), "Missing or set Found column");
	ASSERT(!map_has(params.row, CSV_INDEXED), "Set Indexed column");
	ASSERT(!map_has(params.row, CSV_DECOMPRESSED), "Set Decompressed Label column");
	ASSERT(CSV_HAS_NOT(CSV_EXPORTED), "Missing or set Exported column");
	ASSERT(CSV_HAS_NOT(CSV_OUTPUT_PATH), "Missing or set Output Path column");
	ASSERT(CSV_HAS_NOT(CSV_OUTPUT_SIZE), "Missing or set Output Size column");
	ASSERT(CSV_HAS_NOT(CSV_MAJOR_FILE_LABEL), "Missing or set Major File Label column");
	ASSERT(CSV_HAS_NOT(CSV_MINOR_FILE_LABEL), "Missing or set Minor File Label column");
	ASSERT(!map_has(params.row, CSV_MAJOR_URL_LABEL), "Set Major URL Label column");
	ASSERT(!map_has(params.row, CSV_MINOR_URL_LABEL), "Set Minor URL Label column");
	ASSERT(!map_has(params.row, CSV_MAJOR_ORIGIN_LABEL), "Set Major Origin Label column");
	ASSERT(!map_has(params.row, CSV_MINOR_ORIGIN_LABEL), "Set Minor Origin Label column");
	ASSERT(CSV_HAS_NOT(CSV_SHA_256), "Missing or set SHA-256 column");

	if(params.data_path == NULL)
	{
		ASSERT(params.info != NULL, "Missing walk info");
		ASSERT(params.info->_state->copy, "Shallow walk path");
		params.data_path = params.info->path;
	}

	ASSERT(params.data_path != NULL, "Missing data path");

	bool valid_path = !path_is_equal(params.data_path, NO_PATH);

	Url url = {};
	String* filename = NULL;

	if(params.url != NULL)
	{
		url = url_parse(params.url);
		filename = string_from_view(path_name(url.path));
	}
	else if(valid_path)
	{
		filename = string_from_view(path_name(params.data_path));
	}
	else
	{
		filename = EMPTY_STRING;
	}

	ASSERT(filename != NULL, "Missing filename");

	{
		if(CSV_HAS_NOT(CSV_FILENAME)) map_put(&params.row, CSV_FILENAME, filename);
		if(CSV_HAS_NOT(CSV_EXTENSION)) map_put(&params.row, CSV_EXTENSION, string_lower(path_extension(filename)));
	}

	if(filename->char_count == 0)
	{
		filename = exporter_filename(exporter);
	}

	{
		exporter->current_found += 1;
		exporter->total_found += 1;

		String_View short_filename = string_slice(filename, 0, 50);
		console_progress("%s [%04d]: %.*s", exporter->current_long->data, exporter->current_found, short_filename.code_count, short_filename.data);
	}

	Url origin = {};
	if(params.origin != NULL) origin = url_parse(params.origin);

	{
		if(params.url != NULL && CSV_HAS_NOT(CSV_URL)) map_put(&params.row, CSV_URL, params.url);
		if(params.origin != NULL && CSV_HAS_NOT(CSV_ORIGIN)) map_put(&params.row, CSV_ORIGIN, params.origin);
	}

	{
		if(params.info != NULL)
		{
			if(CSV_HAS_NOT(CSV_CREATION_TIME)) map_put(&params.row, CSV_CREATION_TIME, filetime_format(params.info->creation_time));
			if(CSV_HAS_NOT(CSV_LAST_ACCESS_TIME)) map_put(&params.row, CSV_LAST_ACCESS_TIME, filetime_format(params.info->last_access_time));
			if(CSV_HAS_NOT(CSV_LAST_WRITE_TIME)) map_put(&params.row, CSV_LAST_WRITE_TIME, filetime_format(params.info->last_write_time));
		}
	}

	{
		if(params.http_headers != NULL)
		{
			#define HTTP_HEADER_PUT(column, key) \
				do \
				{ \
					if(CSV_HAS_NOT(column)) \
					{ \
						String* value = string_from_view(map_get_or(params.http_headers, T(key), EMPTY_VIEW)); \
						map_put(&params.row, column, value); \
					} \
				} while(false)

			HTTP_HEADER_PUT(CSV_RESPONSE, "");
			HTTP_HEADER_PUT(CSV_SERVER, "server");
			HTTP_HEADER_PUT(CSV_CACHE_CONTROL, "cache-control");
			HTTP_HEADER_PUT(CSV_PRAGMA, "pragma");
			HTTP_HEADER_PUT(CSV_CONTENT_TYPE, "content-type");
			HTTP_HEADER_PUT(CSV_CONTENT_LENGTH, "content-length");
			HTTP_HEADER_PUT(CSV_CONTENT_RANGE, "content-range");
			HTTP_HEADER_PUT(CSV_CONTENT_ENCODING, "content-encoding");

			#undef HTTP_HEADER_PUT
		}
	}

	{
		{
			bool found = valid_path && path_is_file(params.data_path);
			if(CSV_HAS_NOT(CSV_FOUND)) map_put(&params.row, CSV_FOUND, exporter_yes_or_no(found));
		}

		if(CSV_HAS_NOT(CSV_INDEXED)) map_put(&params.row, CSV_INDEXED, exporter_yes_or_no(!params.unindexed));

		if(CSV_HAS_NOT(CSV_INPUT_PATH) && valid_path) map_put(&params.row, CSV_INPUT_PATH, path_absolute(params.data_path));

		u64 size = 0;
		if(CSV_HAS_NOT(CSV_INPUT_SIZE) && valid_path && file_size_get(params.data_path, &size))
		{
			map_put(&params.row, CSV_INPUT_SIZE, string_from_num(size));
		}
	}

	File_Writer decompress_writer = {};

	{
		if(params.index != NULL && valid_path) exporter_index_put(params.index, params.data_path);

		bool decompressed = false;

		String_View encoding_view = {};
		if(exporter->decompress && params.http_headers != NULL
		&& map_get(params.http_headers, T("content-encoding"), &encoding_view)
		&& valid_path
		&& !file_is_empty(params.data_path))
		{
			if(temporary_file_begin(&decompress_writer))
			{
				String* encoding = string_from_view(encoding_view);
				if(decompress_from_content_encoding(params.data_path, encoding, &decompress_writer, TEMPORARY))
				{
					decompressed = true;
					params.data_path = decompress_writer.path;
					if(params.index != NULL) exporter_index_put(params.index, params.data_path);
				}
				else
				{
					log_error("Failed to decompress '%s'", params.data_path->data);
				}
			}
			else
			{
				log_error("Failed to create the temporary file to decompress '%s'", params.data_path->data);
			}
		}

		map_put(&params.row, CSV_DECOMPRESSED, exporter_yes_or_no(decompressed));
	}

	{
		String* sha256 = (valid_path) ? (string_upper(sha256_string_from_file(params.data_path, TEMPORARY))) : (EMPTY_STRING);
		map_put(&params.row, CSV_SHA_256, sha256);
	}

	{
		String_View mime_type_view = {};
		String* mime_type = NULL;
		if(params.http_headers != NULL && map_get(params.http_headers, T("content-type"), &mime_type_view))
		{
			mime_type = string_from_view(mime_type_view);
		}

		String* extension = string_from_view(path_extension(filename));

		Match_Params match_params = {};
		match_params.temporary = true;
		match_params.path = params.data_path;
		match_params.mime_type = mime_type;
		match_params.extension = extension;
		match_params.url = url;

		Label file_label = {};
		bool file_match = label_file_match(exporter, match_params, &file_label);
		if(file_match)
		{
			map_put(&params.row, CSV_MAJOR_FILE_LABEL, file_label.major_name);
			map_put(&params.row, CSV_MINOR_FILE_LABEL, file_label.minor_name);
		}

		Label url_label = {};
		bool url_match = params.url != NULL && label_url_match(exporter, match_params, &url_label);
		if(url_match)
		{
			map_put(&params.row, CSV_MAJOR_URL_LABEL, url_label.major_name);
			map_put(&params.row, CSV_MINOR_URL_LABEL, url_label.minor_name);
		}

		Label origin_label = {};
		match_params.url = origin;
		if(params.origin != NULL && label_url_match(exporter, match_params, &origin_label))
		{
			map_put(&params.row, CSV_MAJOR_ORIGIN_LABEL, origin_label.major_name);
			map_put(&params.row, CSV_MINOR_ORIGIN_LABEL, origin_label.minor_name);
		}

		bool filter = true;

		if(exporter->positive_filter != NULL)
		{
			Compare_Params<String*> params = {};
			params.comparator = string_ignore_case_comparator;
			filter = (file_match && array_has(exporter->positive_filter, file_label.major_name, params))
				  || (file_match && array_has(exporter->positive_filter, file_label.minor_name, params))
				  || (url_match  && array_has(exporter->positive_filter, url_label.major_name, params))
				  || (url_match  && array_has(exporter->positive_filter, url_label.minor_name, params));
		}

		if(exporter->negative_filter != NULL)
		{
			Compare_Params<String*> params = {};
			params.comparator = string_ignore_case_comparator;
			filter = (file_match && array_has(exporter->negative_filter, file_label.major_name, params))
				  || (file_match && array_has(exporter->negative_filter, file_label.minor_name, params))
				  || (url_match  && array_has(exporter->negative_filter, url_label.major_name, params))
				  || (url_match  && array_has(exporter->negative_filter, url_label.minor_name, params));
			filter = !filter;
		}

		if(exporter->ignore_filter != 0)
		{
			filter = (exporter->ignore_filter & exporter->current_flag) != 0;
		}

		if(!filter)
		{
			exporter->current_excluded += 1;
			exporter->total_excluded += 1;
		}

		bool exported = false;

		if(exporter->copy_files && filter && valid_path)
		{
			String_Builder* builder = builder_create(MAX_PATH_COUNT);

			builder_append_path(&builder, exporter->current_output);
			if(params.subdirectory != NULL) builder_append_path(&builder, params.subdirectory);

			String* fallback_path = builder_to_string(builder);

			if(exporter->group_origin && params.origin != NULL)
			{
				builder_append_path(&builder, origin.host);
			}

			if(params.url != NULL)
			{
				builder_append_path(&builder, url.host);
				builder_append_path(&builder, path_parent(url.path));
			}

			builder_append_path(&builder, filename);

			// Note that making the path safe can truncate the filename and remove the extension.
			String* to_path = path_safe(builder_to_string(builder));

			if(extension->char_count == 0)
			{
				if(file_label.default_extension != NULL)
				{
					extension = file_label.default_extension;
					builder_append(&builder, T("."));
					builder_append(&builder, extension);
					to_path = path_safe(builder_terminate(&builder));
				}
				else if(file_label.extensions != NULL && file_label.extensions->count == 1)
				{
					extension = file_label.extensions->data[0];
					builder_append(&builder, T("."));
					builder_append(&builder, extension);
					to_path = path_safe(builder_terminate(&builder));
				}
			}

			Copy_Params copy_params = {};
			copy_params.from_path = params.data_path;
			copy_params.to_path = to_path;
			copy_params.fallback_path = fallback_path;
			copy_params.extension = extension;

			String* final_path = NULL;

			if(exporter_copy(exporter, copy_params, &final_path))
			{
				exported = true;
				exporter->current_exported += 1;
				exporter->total_exported += 1;

				map_put(&params.row, CSV_OUTPUT_PATH, path_absolute(final_path));

				u64 size = 0;
				if(file_size_get(final_path, &size)) map_put(&params.row, CSV_OUTPUT_SIZE, string_from_num(size));
			}
			else
			{
				log_error("Failed to copy '%s' to '%s'", params.data_path->data, to_path->data);
			}
		}

		map_put(&params.row, CSV_EXPORTED, exporter_yes_or_no(exported));

		if(exporter->create_csvs && exporter->current_csv.created && filter)
		{
			csv_next(&exporter->current_csv, params.row);
		}
	}

	if(decompress_writer.opened) temporary_file_end(&decompress_writer);

	arena_clear(context.current_arena);

	#undef CSV_HAS_NOT
}

void exporter_main(Exporter* exporter)
{
	log_info("Processing %d single and %d key paths", exporter->single_paths->count, exporter->key_paths->count);

	report_begin(exporter);

	#define SINGLE_EXPORT(cache_flag, export_function) \
		do \
		{ \
			if((exporter->cache_flags & cache_flag) && (single.flag & cache_flag)) \
			{ \
				exporter_begin(exporter, cache_flag); \
				exporter->current_batch = false; \
				ZeroMemory(&exporter->current_key_paths, sizeof(exporter->current_key_paths)); \
				console_info("%s (Single): '%s'", exporter->current_long->data, single.path->data); \
				log_info("%s (Single): '%s'", exporter->current_long->data, single.path->data); \
				export_function(exporter, single.path); \
				exporter_end(exporter); \
			} \
		} while(false);

	for(int i = 0; i < exporter->single_paths->count; i += 1)
	{
		Single_Path single = exporter->single_paths->data[i];
		ASSERT(flag_has_one(single.flag), "More than one single flag set");
		SINGLE_EXPORT(CACHE_MOZILLA, mozilla_single_export);
		SINGLE_EXPORT(CACHE_SHOCKWAVE, shockwave_single_export);
	}

	#undef SINGLE_EXPORT

	#define BATCH_EXPORT(cache_flag, export_function) \
		do \
		{ \
			if(exporter->cache_flags & cache_flag) \
			{ \
				exporter_begin(exporter, cache_flag, key_paths.name); \
				exporter->current_batch = true; \
				exporter->current_key_paths = key_paths; \
				exporter->filename_count = 0; \
				if(key_paths.name->char_count != 0) \
				{ \
					console_info("%s (Batch): '%s'", exporter->current_long->data, key_paths.name->data); \
					log_info("%s (Batch): '%s'", exporter->current_long->data, key_paths.name->data); \
				} \
				else \
				{ \
					console_info("%s (Default)", exporter->current_long->data); \
					log_info("%s (Default)", exporter->current_long->data); \
				} \
				export_function(exporter, key_paths); \
				exporter_end(exporter); \
			} \
		} while(false);

	for(int i = 0; i < exporter->key_paths->count; i += 1)
	{
		Key_Paths key_paths = exporter->key_paths->data[i];

		log_info("Name: '%s'", key_paths.name->data);
		log_info("Drive: '%s'", key_paths.drive->data);
		log_info("Windows: '%s'", key_paths.windows->data);
		log_info("Temporary: '%s'", key_paths.temporary->data);
		log_info("User: '%s'", key_paths.user->data);
		log_info("AppData: '%s'", key_paths.appdata->data);
		log_info("Local AppData: '%s'", key_paths.local_appdata->data);
		log_info("LocalLow AppData: '%s'", key_paths.local_low_appdata->data);
		log_info("WinINet: '%s'", key_paths.wininet->data);

		BATCH_EXPORT(CACHE_MOZILLA, mozilla_batch_export);
		BATCH_EXPORT(CACHE_SHOCKWAVE, shockwave_batch_export);
	}

	#undef BATCH_EXPORT

	report_end(exporter);

	if(exporter->total_found > 0)
	{
		console_info("Total: Exported %d of %d files (%d excluded)", exporter->total_exported, exporter->total_found, exporter->total_excluded);
		log_info("Total: Exported %d of %d files (%d excluded)", exporter->total_exported, exporter->total_found, exporter->total_excluded);
	}
	else
	{
		console_info("Total: No files found");
		log_info("Total: No files found");
		directory_delete(exporter->output_path);
	}
}

void exporter_tests(void)
{
	console_info("Running exporter tests");
	log_info("Running exporter tests");

	{
		bool success = false;
		u32 flags = 0;

		success = cache_flags_from_names(T("walk,ie,mz,fl,sw,jv,un"), &flags);
		TEST(success, true);
		TEST(flags, (u32) CACHE_WALK | CACHE_PLUGINS | CACHE_BROWSERS);

		success = cache_flags_from_names(T("all"), &flags);
		TEST(success, true);
		TEST(flags, (u32) CACHE_ALL);
		TEST(flags & CACHE_WALK, 0U);

		success = cache_flags_from_names(T("browsers,plugins"), &flags);
		TEST(success, true);
		TEST(flags, (u32) CACHE_PLUGINS | CACHE_BROWSERS);

		success = cache_flags_from_names(T("wrong"), &flags);
		TEST(success, false);

		success = cache_flags_from_names(T(""), &flags);
		TEST(success, false);
	}

	{
		Exporter exporter = {};
		exporter.current_batch = true;

		exporter.current_key_paths.drive = CSTR("C:\\OldDrive");
		TEST(exporter_path_localize(&exporter, CSTR("C:\\Path\\file.ext")), T("C:\\OldDrive\\Path\\file.ext"));

		exporter.current_key_paths.drive = CSTR("D:\\");
		TEST(exporter_path_localize(&exporter, CSTR("C:\\Path\\file.ext")), T("D:\\Path\\file.ext"));
	}
}