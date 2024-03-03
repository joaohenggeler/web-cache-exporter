#include "cache.h"
#include "common.h"

/*
	- 95, 98, ME 					C:\WINDOWS\Application Data\<Vendor + Browser>\Profiles\<Profile>\<Cache>
	- 2000, XP 						C:\Documents and Settings\<User>\Local Settings\Application Data\<Vendor + Browser>\Profiles\<Profile>\<Cache>
	- Vista, 7, 8, 8.1, 10, 11		C:\Users\<User>\AppData\Local\<Vendor + Browser>\Profiles\<Profile>\<Cache>

	<Vendor + Browser>
	- Firefox						Mozilla\Firefox
	- SeaMonkey						Mozilla\SeaMonkey
	- Pale Moon						Moonchild Productions\Pale Moon
	- Basilisk						Moonchild Productions\Basilisk
	- Waterfox						Waterfox
	- K-Meleon						K-Meleon
	- IceDragon						Comodo\IceDragon
	- Netscape 8.x					Netscape\NSB
	- Netscape 9.x					Netscape\Navigator

	<Cache>
	- v1							Cache
	- v2							cache2

	For Phoenix, Firebird, the Mozilla Suite, and Netscape 6.1 to 7.x, the paths are slighty different.
	Note the use of AppData instead of Local Appdata and the extra subdirectory between the <Profile> and <Cache>.

	- 95, 98, ME 					C:\WINDOWS\Application Data\<Vendor + Browser>\Profiles\<Profile>\<8 Characters>.slt\<Cache>
	- 2000, XP 						C:\Documents and Settings\<User>\Application Data\<Vendor + Browser>\Profiles\<Profile>\<8 Characters>.slt\<Cache>
	- Vista, 7, 8, 8.1, 10, 11		C:\Users\<User>\AppData\Roaming\<Vendor + Browser>\Profiles\<Profile>\<8 Characters>.slt\<Cache>

	<Vendor + Browser>
	- Phoenix						Phoenix
	- Firebird						Phoenix
	- Mozilla Suite					Mozilla
	- Netscape (6.1 to 7.x) 		Mozilla

	<Cache>
	- v1							Cache
	- v1 (Netscape 6.1)				NewCache

	When upgrading Netscape 6.0 (which uses a different format) to 6.1 (which uses the Mozilla format), the "Profiles" directory is called "Users50".
*/

static Csv_Column _MOZILLA_COLUMNS[] =
{
	CSV_FILENAME, CSV_EXTENSION,
	CSV_URL, CSV_ORIGIN,
	CSV_LAST_MODIFIED_TIME, CSV_LAST_ACCESS_TIME, CSV_EXPIRY_TIME,
	CSV_ACCESS_COUNT,
	CSV_RESPONSE, CSV_SERVER, CSV_CACHE_CONTROL, CSV_PRAGMA,
	CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_RANGE, CSV_CONTENT_ENCODING,
	CSV_BROWSER, CSV_PROFILE, CSV_VERSION,
	CSV_FOUND, CSV_INPUT_PATH, CSV_INPUT_SIZE,
	CSV_DECOMPRESSED, CSV_EXPORTED, CSV_OUTPUT_PATH, CSV_OUTPUT_SIZE,
	CSV_MAJOR_FILE_LABEL, CSV_MINOR_FILE_LABEL,
	CSV_MAJOR_URL_LABEL, CSV_MINOR_URL_LABEL,
	CSV_MAJOR_ORIGIN_LABEL, CSV_MINOR_ORIGIN_LABEL,
	CSV_SHA_256,
};

Array_View<Csv_Column> MOZILLA_COLUMNS = ARRAY_VIEW_FROM_C(_MOZILLA_COLUMNS);

static String* mozilla_string_unescape(String* str)
{
	String_Builder* builder = builder_create(str->code_count);

	for(int i = 0; i < str->char_count; i += 1)
	{
		String_View chars = string_slice(str, i, i + 2);

		if(string_is_equal(chars, T("\\\\")))
		{
			builder_append(&builder, T("\\"));
			i += 1;
		}
		else if(string_is_equal(chars, T("\\\"")))
		{
			builder_append(&builder, T("\""));
			i += 1;
		}
		else
		{
			String_View chr = string_char_at(chars, 0);
			builder_append(&builder, chr);
		}
	}

	return builder_terminate(&builder);
}

static Array<String*>* mozilla_paths_from_prefs(Exporter* exporter, String* prefs_path)
{
	// @Future: clear arena if nothing was found.
	File file = {};
	Array<String*>* result = array_create<String*>(0);

	if(file_read_all(prefs_path, &file))
	{
		// @Future: check if this is the correct character encoding.
		String* content = string_from_utf_8((char*) file.data);

		Split_State line_state = {};
		line_state.str = content;
		line_state.delimiters = LINE_DELIMITERS;

		String_View line = {};
		while(string_split(&line_state, &line))
		{
			if(string_begins_with(line, T("user_pref")))
			{
				line = string_remove_prefix(line, T("user_pref("));
				line = string_remove_suffix(line, T(");"));

				Split_State quote_state = {};
				quote_state.view = line;
				quote_state.delimiters = T("\"");

				// This assumes that the key and value don't have quotes,
				// which should always be true on Windows.
				Array<String_View>* pair = string_split_all(&quote_state);
				if(pair->count >= 3)
				{
					String_View key = pair->data[0];
					String_View value = pair->data[pair->count - 1];

					if(string_is_equal(key, T("browser.cache.disk.parent_directory"))
					|| string_is_equal(key, T("browser.cache.disk.directory"))
					|| string_is_equal(key, T("browser.cache.directory"))
					|| string_is_equal(key, T("browser.newcache.directory")))
					{
						String* external_path = mozilla_string_unescape(string_from_view(value));
						String* local_path = exporter_path_localize(exporter, external_path);
						array_add(&result, local_path);

						log_info("Localized '%s' to '%s'", external_path->data, local_path->data);
					}
				}
			}
		}
	}
	else
	{
		log_error("Failed to read '%s'", prefs_path->data);
	}

	return result;
}

static Array<String*>* mozilla_paths_from_prefs_directory(Exporter* exporter, String* directory_path)
{
	const TCHAR* prefs[] = {T("prefs.js"), T("user.js")};
	int prefs_count = _countof(prefs);

	String_Builder* builder = builder_create(MAX_PATH_COUNT);
	Array<String*>* result = array_create<String*>(prefs_count);

	for(int i = 0; i < prefs_count; i += 1)
	{
		builder_clear(builder);
		builder_append_path(&builder, directory_path);
		builder_append_path(&builder, prefs[i]);

		String* prefs_path = (i == prefs_count - 1) ? (builder_terminate(&builder)) : (builder_to_string(builder));

		if(path_is_file(prefs_path))
		{
			Array<String*>* paths = mozilla_paths_from_prefs(exporter, prefs_path);
			array_merge(&result, paths);
		}
	}

	return result;
}

static Array<String*>* mozilla_paths(Exporter* exporter, Key_Paths key_paths)
{
	String* base_paths[] = {key_paths.appdata, key_paths.local_appdata};

	const TCHAR* browsers[] =
	{
		T("Mozilla\\Firefox"), T("Mozilla\\SeaMonkey"),
		T("Moonchild Productions\\Pale Moon"), T("Moonchild Productions\\Basilisk"),
		T("Waterfox"), T("K-Meleon"), T("Comodo\\IceDragon"),
		T("Netscape\\NSB"), T("Netscape\\Navigator"),

		T("Phoenix"), T("Mozilla"),
	};

	const TCHAR* profiles[] = {T("Profiles"), T("Users50")};

	int base_count = _countof(base_paths);
	int browsers_count = _countof(browsers);
	int profiles_count = _countof(profiles);

	String_Builder* builder = builder_create(MAX_PATH_COUNT);
	Array<String*>* paths = array_create<String*>(base_count * browsers_count * profiles_count);

	for(int i = 0; i < base_count; i += 1)
	{
		for(int j = 0; j < browsers_count; j += 1)
		{
			for(int k = 0; k < profiles_count; k += 1)
			{
				builder_clear(builder);
				builder_append_path(&builder, base_paths[i]);
				builder_append_path(&builder, browsers[j]);
				builder_append_path(&builder, profiles[k]);

				String* profiles_path = builder_to_string(builder);

				Walk_State state = {};
				state.base_path = profiles_path;
				state.query = T("*");
				state.directories = true;
				state.copy = true;

				WALK_DEFER(&state)
				{
					Walk_Info info = {};
					while(walk_next(&state, &info))
					{
						array_add(&paths, info.path);

						Array<String*>* prefs_paths = mozilla_paths_from_prefs_directory(exporter, info.path);
						array_merge(&paths, prefs_paths);

						Walk_State salt_state = {};
						salt_state.base_path = info.path;
						salt_state.query = T("*.slt");
						salt_state.directories = true;
						salt_state.copy = true;

						WALK_DEFER(&salt_state)
						{
							Walk_Info salt_info = {};
							while(walk_next(&salt_state, &salt_info))
							{
								array_add(&paths, salt_info.path);

								Array<String*>* salt_prefs_paths = mozilla_paths_from_prefs_directory(exporter, salt_info.path);
								array_merge(&paths, salt_prefs_paths);
							}
						}
					}
				}
			}
		}
	}

	const TCHAR* cache[] = {T("Cache"), T("cache2"), T("NewCache")};
	int cache_count = _countof(cache);

	Array<String*>* result = array_create<String*>(paths->count * cache_count);

	for(int i = 0; i < paths->count; i += 1)
	{
		String* path = paths->data[i];

		for(int j = 0; j < cache_count; j += 1)
		{
			builder_clear(builder);
			builder_append_path(&builder, path);
			builder_append_path(&builder, cache[j]);

			bool last = (i == paths->count - 1) && (j == cache_count - 1);
			String* cache_path = (last) ? (builder_terminate(&builder)) : (builder_to_string(builder));

			array_add(&result, cache_path);
		}
	}

	// Filtering duplicate paths is required here because the prefs can contain both the cache paths and their parent paths.
	// This means we could be creating duplicates when generating all possible combinations.
	return path_unique_directories(result);
}

struct Metadata_Elements
{
	Map<const TCHAR*, String_View>* http_headers;
	String* request_origin;
};

static Metadata_Elements mozilla_cache_elements_parse(void* elements, size_t size)
{
	// The elements are contiguous key-value pairs of null-terminated strings.
	Metadata_Elements result = {};

	char* key = (char*) elements;
	void* end = advance(elements, size);

	while(key < end)
	{
		char* value = key;
		while(*value != '\0') value += 1;
		value += 1;

		if(value >= end) break;

		String* key_str = string_from_utf_8(key);

		if(string_is_equal(key_str, T("response-head")))
		{
			String* headers = string_from_utf_8(value);
			result.http_headers = http_headers_parse(headers);
		}
		else if(string_is_equal(key_str, T("request-origin")))
		{
			result.request_origin = string_from_utf_8(value);
		}

		key = value;
		while(*key != '\0') key += 1;
		key += 1;
	}

	return result;
}

static void mozilla_v1_cache_export(Exporter* exporter, String* path)
{
	exporter;
	log_info("v1: '%s'", path->data);
}

struct Metadata_2_Key
{
	String* url;
	String* partition_key;
};

static Metadata_2_Key mozilla_v2_key_parse(String* key)
{
	// The key is a comma separated list of properties, where the first character specifies their type.
	// E.g. "O" = origin attributes, "a" = anonymous, ":" = the URL (and the last value).
	// Any ":" character before this last value is replaced with the "+" character.
	//
	// We only want the URL (which always appears at the end), and the partition key (which is part of the
	// origin attributes). These origin attributes start with the "^" character and are followed by a list
	// of URL parameters (e.g. "param1=value1&param2=value2"). The partition key is one of these key-value
	// pairs, and takes the form of "partitionKey=(scheme,host)" or "partitionKey=(scheme,host,port)".
	// These characters "()," are percent-encoded.
	//
	// For example:
	// "a,~1614704371,:https://cdn.expl.com/path/file.ext"
	// "O^partitionKey=%28https%2Cexample.com%29,a,:https://cdn.expl.com/path/file.ext"
	//
	// See:
	// - KeyParser::ParseTags() in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileUtils.cpp
	// - OriginAttributes::PopulateFromSuffix() in https://hg.mozilla.org/mozilla-central/file/tip/caps/OriginAttributes.cpp
	//
	// And also:
	// - GetOriginAttributesWithScheme() in https://hg.mozilla.org/mozilla-central/file/tip/toolkit/components/antitracking/StoragePrincipalHelper.cpp
	// - URLParams::Serialize() in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/base/nsURLHelper.cpp

	Metadata_2_Key result = {};

	Split_State property_state = {};
	property_state.str = key;
	property_state.delimiters = T(",");

	String_View property = {};
	while(string_split(&property_state, &property))
	{
		if(string_begins_with(property, T("O^")))
		{
			String_View attributes = string_remove_prefix(property, T("O^"));

			Split_State attribute_state = {};
			attribute_state.view = attributes;
			attribute_state.delimiters = T("&");

			String_View attribute = {};
			while(string_split(&attribute_state, &attribute))
			{
				Split_State key_state = {};
				key_state.view = attribute;
				key_state.delimiters = T("=");

				String_View key = {};
				String_View value = {};

				if(string_partition(&key_state, &key, &value))
				{
					if(string_is_equal(key, T("partitionKey")))
					{
						String* decoded = url_decode(value);
						String_View values = string_remove_prefix(decoded, T("("));
						values = string_remove_suffix(values, T(")"));

						Split_State value_state = {};
						value_state.view = values;
						value_state.delimiters = T(",");

						Array<String_View>* split_values = string_split_all(&value_state);
						if(split_values->count >= 2)
						{
							String_View scheme = split_values->data[0];
							String_View host = split_values->data[1];
							String_Builder* builder = builder_create(scheme.code_count + 3 + host.code_count);
							builder_append_format(&builder, T("%.*s://%.*s"), scheme.code_count, scheme.data, host.code_count, host.data);
							result.partition_key = builder_terminate(&builder);
						}
					}
				}
			}
		}
		else if(string_begins_with(property, T(":")))
		{
			String_View url = string_remove_prefix(property, T(":"));
			result.url = string_from_view(url);
		}
	}

	return result;
}

static void mozilla_v2_cache_export(Exporter* exporter, String* path)
{
	log_info("Exporting from '%s'", path->data);

	// @Version: Firefox 32 and later
	// @ByteOrder: Big Endian
	// @CharacterEncoding: ASCII
	// @DateTimeFormat: Unix

	#pragma pack(push, 1)

	// See CacheIndexHeader in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheIndex.h
	// The version is defined in https://hg.mozilla.org/mozilla-central/log/tip/netwerk/cache2/CacheIndex.cpp?patch=&linerange=29:29
	struct Index_Header
	{
		u32 version;
		u32 last_write_time;
		u32 dirty_flag;
		u32 used_cache_size; // In kilobytes. This member did not exist in the oldest header version.
	};

	const u32 MAX_INDEX_VERSION = 10;

	// See CacheFileMetadataHeader in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileMetadata.h
	// The version is defined in https://hg.mozilla.org/mozilla-central/log/tip/netwerk/cache2/CacheFileMetadata.h?patch=&linerange=37:37
	struct Metadata_Header_1_And_2
	{
		u32 version;
		u32 access_count;
		u32 last_access_time;
		u32 last_modified_time;
		u32 expiry_time;
		u32 key_length; // Called mKeySize but is set to mKey.Length(). Does not include the null terminator.
	};

	struct Metadata_Header_3
	{
		u32 version;
		u32 access_count;
		u32 last_access_time;
		u32 last_modified_time;
		u32 frecency;
		u32 expiry_time;
		u32 key_length; // See v1 and v2.
		u32 flags;
	};

	#pragma pack(pop)

	_STATIC_ASSERT(sizeof(Index_Header) == 16);
	_STATIC_ASSERT(sizeof(Metadata_Header_1_And_2) == 24);
	_STATIC_ASSERT(sizeof(Metadata_Header_3) == 32);

	ARENA_SAVEPOINT()
	{
		REPORT_DEFER(exporter, path)
		{
			String* browser = EMPTY_STRING;
			String* profile = EMPTY_STRING;

			if(exporter->current_batch)
			{
				String_View profile_view = path_component_end(path, 1);
				bool salt = path_ends_with(profile_view, T(".slt"));

				if(salt) profile_view = path_component_end(path, 2);
				profile = string_from_view(profile_view);

				if(salt) browser = string_from_view(path_component_end(path, 4));
				else browser = string_from_view(path_component_end(path, 3));
			}

			Index_Header index_header = {};

			{
				String_Builder* builder = builder_create(MAX_PATH_COUNT);
				builder_append_path(&builder, path);
				builder_append_path(&builder, T("index"));
				String* index_path = builder_terminate(&builder);

				if(file_read_first(index_path, &index_header, sizeof(index_header)))
				{
					BIG_ENDIAN_TO_HOST(index_header.version);
					BIG_ENDIAN_TO_HOST(index_header.last_write_time);
					BIG_ENDIAN_TO_HOST(index_header.dirty_flag);
					BIG_ENDIAN_TO_HOST(index_header.used_cache_size);

					if(index_header.version > MAX_INDEX_VERSION)
					{
						log_warning("Found unsupported index version %I32u in '%s'", index_header.version, index_path->data);
						ASSERT(false, "Unsupported index version");
					}

					if(index_header.dirty_flag != 0)
					{
						log_warning("The index dirty flag is set");
					}
				}
				else
				{
					log_warning("Could not read the index header from '%s'", index_path->data);
				}
			}

			Walk_State state = {};
			state.base_path = path;
			state.query = T("*");
			state.files = true;
			state.max_depth = 1;
			state.copy = true;

			WALK_DEFER(&state)
			{
				Walk_Info info = {};
				while(walk_next(&state, &info))
				{
					// Skip the index.
					if(info.depth == 0) continue;

					// This is also the cached file size.
					// See CacheFileMetadata::ReadMetadata() in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileMetadata.cpp
					u32 metadata_offset = 0;
					if(file_read_chunk(info.path, &metadata_offset, sizeof(metadata_offset), info.size - sizeof(metadata_offset)))
					{
						BIG_ENDIAN_TO_HOST(metadata_offset);
					}
					else
					{
						log_error("Failed to read the metadata offset in '%s'", info.path->data);
						continue;
					}

					if(metadata_offset >= info.size)
					{
						log_error("The metadata offset 0x%08X goes past the end of '%s'", metadata_offset, info.path->data);
						continue;
					}

					// See CacheFileMetadata::OnDataRead() in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileMetadata.cpp
					const u32 HASH_CHUNK_SIZE = 256 * 1024;
					u32 num_hashes = (metadata_offset == 0) ? (0) : ((metadata_offset - 1) / HASH_CHUNK_SIZE + 1);
					u32 hash_size = sizeof(u32) + num_hashes * sizeof(u16);

					s64 remaining_size = (s64) info.size - metadata_offset - sizeof(metadata_offset);
					u32 min_size = hash_size + sizeof(Metadata_Header_1_And_2);

					if(remaining_size < min_size)
					{
						log_error("The metadata in '%s' is %I64d bytes when at least %I32u were expected", info.path->data, remaining_size, min_size);
						continue;
					}

					void* metadata = arena_push(context.current_arena, size_clamp(remaining_size), char);
					Map<Csv_Column, String*>* row = map_create<Csv_Column, String*>(MOZILLA_COLUMNS.count);

					Export_Params params = {};
					params.row = row;

					if(file_read_chunk(info.path, metadata, size_clamp(remaining_size), metadata_offset))
					{
						metadata = advance(metadata, hash_size);
						remaining_size -= hash_size;

						bool eof = false;

						#define READ_INTEGER(var) \
							do \
							{ \
								if(remaining_size < sizeof(var)) eof = true; \
								if(eof) break; \
								CopyMemory(&var, metadata, sizeof(var)); \
								BIG_ENDIAN_TO_HOST(var); \
								metadata = advance(metadata, sizeof(var)); \
								remaining_size -= sizeof(var); \
							} while(false)

						// Version 3 includes every member from the previous versions.
						Metadata_Header_3 metadata_header = {};
						READ_INTEGER(metadata_header.version);

						if(metadata_header.version <= 2)
						{
							READ_INTEGER(metadata_header.access_count);
							READ_INTEGER(metadata_header.last_access_time);
							READ_INTEGER(metadata_header.last_modified_time);
							READ_INTEGER(metadata_header.expiry_time);
							READ_INTEGER(metadata_header.key_length);
						}
						else if(metadata_header.version == 3)
						{
							READ_INTEGER(metadata_header.access_count);
							READ_INTEGER(metadata_header.last_access_time);
							READ_INTEGER(metadata_header.last_modified_time);
							READ_INTEGER(metadata_header.frecency);
							READ_INTEGER(metadata_header.expiry_time);
							READ_INTEGER(metadata_header.key_length);
							READ_INTEGER(metadata_header.flags);
						}
						else
						{
							log_warning("Skipping unsupported metadata version %I32u in '%s'", metadata_header.version, info.path->data);
							ASSERT(false, "Unsupported metadata version");
							goto skip;
						}

						#undef READ_INTEGER

						u32 key_size = metadata_header.key_length + 1;
						if(!eof && remaining_size >= key_size)
						{
							String* key = string_from_utf_8((char*) metadata);
							Metadata_2_Key parts = mozilla_v2_key_parse(key);
							params.url = parts.url;

							metadata = advance(metadata, key_size);
							remaining_size -= key_size;

							Metadata_Elements elements = mozilla_cache_elements_parse(metadata, size_clamp(remaining_size));
							params.http_headers = elements.http_headers;
							params.origin = (elements.request_origin != NULL) ? (elements.request_origin) : (parts.partition_key);
						}
						else
						{
							log_warning("Skipping the metadata key in '%s' since the remaining size is too small to contain it (%I64d < %I32u)", info.path->data, remaining_size, key_size);
						}

						map_put(&row, CSV_LAST_MODIFIED_TIME, unix_time_format(metadata_header.last_modified_time));
						map_put(&row, CSV_LAST_ACCESS_TIME, unix_time_format(metadata_header.last_access_time));
						map_put(&row, CSV_EXPIRY_TIME, unix_time_format(metadata_header.expiry_time));
						map_put(&row, CSV_ACCESS_COUNT, string_from_num(metadata_header.access_count));
						map_put(&row, CSV_BROWSER, browser);
						map_put(&row, CSV_PROFILE, profile);

						{
							String_Builder* builder = builder_create(10);
							builder_append_format(&builder, T("2.%I32u.%I32u"), index_header.version, metadata_header.version);
							map_put(&row, CSV_VERSION, builder_terminate(&builder));
						}

						map_put(&row, CSV_INPUT_PATH, info.path);

						skip:;
					}
					else
					{
						log_error("Failed to read the metadata in '%s'", info.path->data);
					}

					File_Writer writer = {};
					TEMPORARY_FILE_DEFER(&writer)
					{
						bool success = false;

						File_Reader reader = {};
						FILE_READ_DEFER(&reader, info.path)
						{
							u64 total = 0;
							while(file_read_next(&reader))
							{
								if(!file_write_next(&writer, reader.data, reader.size)) break;

								total += reader.size;

								if(total >= metadata_offset)
								{
									success = true;
									break;
								}
							}
						}

						if(success && file_write_truncate(&writer, metadata_offset))
						{
							params.data_path = writer.path;
							exporter_next(exporter, params);
						}
						else
						{
							log_error("Failed to extract the cached file from '%s'", info.path->data);
						}
					}

					if(!writer.opened)
					{
						log_error("Failed to extract the cached file from '%s'", info.path->data);
					}
				}
			}
		}
	}
}

static void mozilla_cache_export(Exporter* exporter, String* path)
{
	if(path_has_file(path, T("_CACHE_MAP_"))
	|| path_has_file(path, T("_CACHE_001_"))
	|| path_has_file(path, T("_CACHE_002_"))
	|| path_has_file(path, T("_CACHE_003_")))
	{
		mozilla_v1_cache_export(exporter, path);
	}
	else if(path_has_directory(path, T("entries"))
		 || path_has_directory(path, T("doomed"))
		 || path_has_file(path, T("index")))
	{
		mozilla_v2_cache_export(exporter, path);
	}
}

void mozilla_single_export(Exporter* exporter, String* path)
{
	mozilla_cache_export(exporter, path);
}

void mozilla_batch_export(Exporter* exporter, Key_Paths key_paths)
{
	ARENA_SAVEPOINT()
	{
		Array<String*>* paths = mozilla_paths(exporter, key_paths);

		for(int i = 0; i < paths->count; i += 1)
		{
			String* path = paths->data[i];
			mozilla_cache_export(exporter, path);
		}
	}
}

void mozilla_tests(void)
{
	console_info("Running Mozilla tests");
	log_info("Running Mozilla tests");

	{
		TEST(mozilla_string_unescape(CSTR("C:\\Path\\file.ext")), T("C:\\Path\\file.ext"));
		TEST(mozilla_string_unescape(CSTR("C:\\\\Path\\\\file.ext")), T("C:\\Path\\file.ext"));
		TEST(mozilla_string_unescape(CSTR("{\\\"key\\\": \\\"value\\\"}")), T("{\"key\": \"value\"}"));
		TEST(mozilla_string_unescape(CSTR("")), T(""));
	}

	{
		Exporter exporter = {};
		exporter.current_batch = true;
		exporter.current_key_paths.drive = CSTR("C:\\OldDrive");

		Array<String*>* paths = mozilla_paths_from_prefs_directory(&exporter, CSTR("Tests\\Mozilla"));
		TEST(paths->count, 4);
		TEST(paths->data[0], T("C:\\OldDrive\\Path"));
		TEST(paths->data[1], T("C:\\OldDrive\\Path\\Cache 1"));
		TEST(paths->data[2], T("C:\\OldDrive\\Path\\Cache 2"));
		TEST(paths->data[3], T("C:\\OldDrive\\Path\\Cache 3"));
	}

	{
		Metadata_Elements result = {};

		char elements[] = 	"response-head\0" \
							"HTTP/1.1 200 OK\r\nContent-Type: text/html\0" \
							"request-origin\0" \
							"example.com\0" \
							"key\0" \
							"value\0";

		size_t size = sizeof(elements) - 1;

		result = mozilla_cache_elements_parse(elements, size);

		TEST(result.http_headers->count, 2);

		bool found = false;
		String_View value = {};

		found = map_get(result.http_headers, T(""), &value);
		TEST(found, true);
		TEST(value, T("HTTP/1.1 200 OK"));

		found = map_get(result.http_headers, T("content-type"), &value);
		TEST(found, true);
		TEST(value, T("text/html"));

		TEST(result.request_origin, T("example.com"));

		result = mozilla_cache_elements_parse("", 0);
		TEST((void*) result.http_headers, NULL);
		TEST((void*) result.request_origin, NULL);
	}

	{
		Metadata_2_Key key = {};

		key = mozilla_v2_key_parse(CSTR("a,~1614704371,:https://cdn.expl.com/path/file.ext"));
		TEST(key.url, T("https://cdn.expl.com/path/file.ext"));
		TEST((void*) key.partition_key, NULL);

		key = mozilla_v2_key_parse(CSTR("O^partitionKey=%28https%2Cexample.com%29,a,:https://cdn.expl.com/path/file.ext"));
		TEST(key.url, T("https://cdn.expl.com/path/file.ext"));
		TEST(key.partition_key, T("https://example.com"));

		key = mozilla_v2_key_parse(CSTR(""));
		TEST((void*) key.url, NULL);
		TEST((void*) key.partition_key, NULL);
	}
}