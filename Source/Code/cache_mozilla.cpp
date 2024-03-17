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
	CSV_FOUND, CSV_INDEXED, CSV_INPUT_PATH, CSV_INPUT_SIZE,
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

	Array<String*>* result = array_create<String*>(prefs_count);

	for(int i = 0; i < prefs_count; i += 1)
	{
		String* prefs_path = path_build(CANY(directory_path), CANY(prefs[i]));

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

	Array<String*>* paths = array_create<String*>(base_count * browsers_count * profiles_count);

	for(int i = 0; i < base_count; i += 1)
	{
		for(int j = 0; j < browsers_count; j += 1)
		{
			for(int k = 0; k < profiles_count; k += 1)
			{
				String* profiles_path = path_build(CANY(base_paths[i]), CANY(browsers[j]), CANY(profiles[k]));

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
			String* cache_path = path_build(CANY(path), CANY(cache[j]));
			array_add(&result, cache_path);
		}
	}

	// Filtering duplicate paths is required here because the prefs can contain both the cache paths and their parent paths.
	// This means we could be creating duplicates when generating all possible combinations.
	return path_unique_directories(result);
}

static void mozilla_browser_and_profile(Exporter* exporter, String* path, String** browser, String** profile)
{
	*browser = EMPTY_STRING;
	*profile = EMPTY_STRING;

	if(exporter->current_batch)
	{
		String_View profile_view = path_component_end(path, 1);
		bool salt = path_ends_with(profile_view, T(".slt"));

		if(salt) profile_view = path_component_end(path, 2);
		*profile = string_from_view(profile_view);

		if(salt) *browser = string_from_view(path_component_end(path, 4));
		else *browser = string_from_view(path_component_end(path, 3));
	}
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

static String* mozilla_v1_data_path(String* base_path, bool v19_or_newer, u32 hash_number, bool metadata, u8 generation)
{
	String_Builder* builder = builder_create(base_path->code_count + 14);

	builder_append_format(&builder, T("%08X"), hash_number);
	String* hash = builder_to_string(builder);

	builder_clear(builder);
	builder_append_path(&builder, base_path);
	builder_append_path(&builder, T(""));

	const TCHAR* id = (metadata) ? (T("m")) : (T("d"));

	if(v19_or_newer)
	{
		// E.g. "0\E0\A6E00d01" (hash = 0E0A6E00, metadata = false, generation = 1)
		builder_append_format(&builder, T("%.1s\\") T("%.2s\\") T("%s") T("%s") T("%02X"), hash->data, hash->data + 1, hash->data + 3, id, generation);
	}
	else
	{
		// E.g. "0E0A6E00d01" (hash = 0E0A6E00, metadata = false, generation = 1)
		builder_append_format(&builder, T("%s") T("%s") T("%02X"), hash->data, id, generation);
	}

	return builder_terminate(&builder);
}

static void mozilla_v1_cache_export(Exporter* exporter, String* path)
{
	log_info("Exporting from '%s'", path->data);

	// @FormatVersion: Mozilla 0.9.5 to Firefox 31
	// @ByteOrder: Big Endian
	// @CharacterEncoding: ASCII
	// @DateTimeFormat: Unix time

	#pragma pack(push, 1)

	// Mozilla Version		Header Version
	// Mozilla 0.9.5	 	1.3
	// Mozilla 1.2			1.5
	// Mozilla 1.7.13 		1.5 (last Mozilla Suite version)
	// Firefox 1.5 			1.6 (map header format change)
	// Firefox 2.0 			1.8
	// Firefox 3.0 			1.11
	// Firefox 4.0 			1.19
	// Firefox 31 			1.19

	const int MAX_BUCKETS = 32;

	// See nsDiskCacheHeader in nsDiskCacheMap.h (https://www-archive.mozilla.org/releases/old-releases-0.9.2-1.0rc3)
	// The version is defined in nsDiskCache.h.
	struct Map_Header_3_To_5
	{
		u16 major_version;
		u16 minor_version;
		s32 data_size; // Signed.
		s32 entry_count; // Signed.
		u32 dirty_flag;
		u32 eviction_ranks[MAX_BUCKETS];
	};

	// See nsDiskCacheHeader in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheMap.h
	// The version is defined in https://hg.mozilla.org/mozilla-central/log/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCache.h?patch=&linerange=20:22
	struct Map_Header_6_To_19
	{
		u16 major_version;
		u16 minor_version;
		u32 data_size;
		s32 entry_count; // Signed.
		u32 dirty_flag;
		s32 record_count; // Signed.
		u32 eviction_ranks[MAX_BUCKETS];
		u32 bucket_usage[MAX_BUCKETS];
	};

	// See nsDiskCacheRecord in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheMap.h
	struct Map_Record
	{
		u32 hash_number;
		u32 eviction_rank;
		u32 data_location;
		u32 metadata_location;
	};

	// Padded to the block size: sizeof(nsDiskCacheBucket) - sizeof(Previous Members Of nsDiskCacheHeader).
	// Where sizeof(nsDiskCacheBucket) = kRecordsPerBucket * sizeof(nsDiskCacheRecord).
	const size_t MAP_HEADER_3_TO_5_PADDING = 256 * sizeof(Map_Record) - sizeof(Map_Header_3_To_5);

	// See the enum in nsDiskCacheRecord in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheMap.h
	enum
	{
		DATA_LOCATION_INITIALIZED_MASK = 0x80000000,
		DATA_LOCATION_SELECTOR_MASK = 0x30000000,
		DATA_LOCATION_SELECTOR_OFFSET = 28,
		DATA_EXTRA_BLOCKS_MASK = 0x03000000,
		DATA_EXTRA_BLOCKS_OFFSET = 24,
		DATA_RESERVED_MASK = 0x4C000000,
		DATA_BLOCK_NUMBER_MASK = 0x00FFFFFF,
		DATA_FILE_SIZE_MASK = 0x00FFFF00,
		DATA_FILE_SIZE_OFFSET = 8,
		DATA_data_generation_MASK = 0x000000FF,
		DATA_FILE_RESERVED_MASK = 0x4F000000,
	};

	// See nsDiskCacheEntry in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheEntry.h
	// The version should be the same as Map_Header_*.
	struct Metadata
	{
		u16 header_major_version;
		u16 header_minor_version;
		u32 meta_location;
		s32 access_count; // Signed.
		u32 last_access_time;
		u32 last_modified_time;
		u32 expiry_time;
		u32 data_size;
		u32 key_size; // Includes the null terminator.
		u32 elements_size; // Includes the null terminator.
	};

	#pragma pack(pop)

	_STATIC_ASSERT(sizeof(Map_Header_3_To_5) == 144);
	_STATIC_ASSERT(sizeof(Map_Header_6_To_19) == 276);
	_STATIC_ASSERT(sizeof(Map_Record) == 16);
	_STATIC_ASSERT(sizeof(Metadata) == 36);

	ARENA_SAVEPOINT()
	{
		REPORT_DEFER(exporter, path)
		{
			int file_count = walk_file_count(path, RECURSIVE);
			Map<Sha256, bool>* index = map_create<Sha256, bool>(file_count);

			String* browser = NULL;
			String* profile = NULL;
			mozilla_browser_and_profile(exporter, path, &browser, &profile);

			String* map_path = path_build(CANY(path), CANY(T("_CACHE_MAP_")));
			File map_file = {};

			if(file_read_all(map_path, &map_file))
			{
				if(map_file.size >= sizeof(Map_Header_6_To_19))
				{
					Cursor map_cursor = {};
					map_cursor.data = map_file.data;
					map_cursor.size = map_file.size;

					// Versions 6 to 19 include every member from the previous versions.
					Map_Header_6_To_19 header = {};

					cursor_big_endian_read(&map_cursor, &header.major_version);
					cursor_big_endian_read(&map_cursor, &header.minor_version);

					bool v5_or_older = (header.major_version <= 1 && header.minor_version <= 5);
					bool v19_or_newer = (header.major_version >= 1 && header.minor_version >= 19);

					if(v5_or_older)
					{
						cursor_big_endian_read(&map_cursor, &header.data_size);
						cursor_big_endian_read(&map_cursor, &header.entry_count);
						cursor_big_endian_read(&map_cursor, &header.dirty_flag);

						for(int i = 0; i < _countof(header.eviction_ranks); i += 1)
						{
							cursor_big_endian_read(&map_cursor, &header.eviction_ranks[i]);
						}
					}
					else
					{
						cursor_big_endian_read(&map_cursor, &header.data_size);
						cursor_big_endian_read(&map_cursor, &header.entry_count);
						cursor_big_endian_read(&map_cursor, &header.dirty_flag);
						cursor_big_endian_read(&map_cursor, &header.record_count);

						for(int i = 0; i < _countof(header.eviction_ranks); i += 1)
						{
							cursor_big_endian_read(&map_cursor, &header.eviction_ranks[i]);
						}

						for(int i = 0; i < _countof(header.bucket_usage); i += 1)
						{
							cursor_big_endian_read(&map_cursor, &header.bucket_usage[i]);
						}
					}

					if(header.dirty_flag != 0) log_warning("The dirty flag is set");

					struct Block_File
					{
						String* path;
						bool exists;
						size_t header_size; // Bitmap.
						size_t block_size;
						size_t max_entry_size;
					};

					const int MIN_RECORD_BLOCKS = 1;
					const int MAX_RECORD_BLOCKS = 4;
					const int MAX_BLOCK_FILES = 3;

					Block_File block_files[MAX_BLOCK_FILES + 1] = {};
					String_Builder* builder = builder_create(MAX_PATH_COUNT);

					for(int i = 1; i < MAX_BLOCK_FILES + 1; i += 1)
					{
						Block_File* block = &block_files[i];

						builder_clear(builder);
						builder_append_path(&builder, path);
						builder_append_path(&builder, T(""));
						builder_append_format(&builder, T("_CACHE_00%d_"), i);

						block->path = (i == MAX_BLOCK_FILES) ? (builder_terminate(&builder)) : (builder_to_string(builder));
						block->exists = path_is_file(block->path);
						if(!block->exists) log_error("Missing block file %d '%s'", i, block->path->data);

						// - Mozilla 0.9.5 (1.3) to Firefox 4.0 (1.19)
						// Block Size = 256 << (2 * (index - 1))
						// Block File 1: 256 << 0 = 256
						// Block File 2: 256 << 2 = 1024
						// Block File 3: 256 << 4 = 4096
						//
						// - Mozilla 0.9.5 (1.3)
						// Block File Header Size = kBitMapBytes = 4096
						//
						// - Firefox 4.0 (1.19)
						// Number of Blocks = (131072 >> (2 * (index - 1)))
						// Number of Words = Number of Blocks / 32
						// Number of Bytes = Number of Words * 4
						// Block File Header Size = (131072 >> (2 * (index - 1))) / 32 * 4
						// Block File 1: 131072 >> 0 = 131072 / 32 * 4 = 16384
						// Block File 2: 131072 >> 2 = 32768 / 32 * 4 = 4096
						// Block File 3: 131072 >> 4 = 8192 / 32 * 4 = 1024

						if(i == 1)
						{
							block->header_size = (v19_or_newer) ? (16384) : (4096);
							block->block_size = 256;
						}
						else if(i == 2)
						{
							block->header_size = 4096;
							block->block_size = 1024;
						}
						else if(i == 3)
						{
							block->header_size = (v19_or_newer) ? (1024) : (4096);
							block->block_size = 4096;
						}
						else
						{
							ASSERT(false, "Unhandled block file");
						}

						block->max_entry_size = MAX_RECORD_BLOCKS * block->block_size;
					}

					const size_t MAP_HEADER_SIZE = (v5_or_older) ? (sizeof(Map_Header_3_To_5) + MAP_HEADER_3_TO_5_PADDING) : (sizeof(Map_Header_6_To_19));
					map_cursor.data = advance(map_file.data, MAP_HEADER_SIZE);
					map_cursor.size = size_clamp((s64) map_file.size - MAP_HEADER_SIZE);

					ARENA_SAVEPOINT()
					{
						int record_count = (int) (map_cursor.size / sizeof(Map_Record));
						for(int r = 0; r < record_count; r += 1)
						{
							Map_Record record = {};

							// For versions between 1.3 and 1.5, the records appear to be stored in little endian,
							// even though the header and cache entries are in big endian. The data for versions 1.6
							// and newer are stored in big endian. This has been tested with versions 1.3, 1.5, 1.6,
							// 1.11, and 1.19.
							if(v5_or_older)
							{
								cursor_little_endian_read(&map_cursor, &record.hash_number);
								cursor_little_endian_read(&map_cursor, &record.eviction_rank);
								cursor_little_endian_read(&map_cursor, &record.data_location);
								cursor_little_endian_read(&map_cursor, &record.metadata_location);
							}
							else
							{
								cursor_big_endian_read(&map_cursor, &record.hash_number);
								cursor_big_endian_read(&map_cursor, &record.eviction_rank);
								cursor_big_endian_read(&map_cursor, &record.data_location);
								cursor_big_endian_read(&map_cursor, &record.metadata_location);
							}

							if(record.hash_number == 0) continue;

							u32 data_initialized = (record.data_location & DATA_LOCATION_INITIALIZED_MASK);
							u32 data_selector = (record.data_location & DATA_LOCATION_SELECTOR_MASK) >> DATA_LOCATION_SELECTOR_OFFSET;
							u8 data_generation = (u8) (record.data_location & DATA_data_generation_MASK);
							u32 data_first_block = (record.data_location & DATA_BLOCK_NUMBER_MASK);
							u32 data_block_count = ((record.data_location & DATA_EXTRA_BLOCKS_MASK) >> DATA_EXTRA_BLOCKS_OFFSET) + 1;

							u32 metadata_initialized = (record.metadata_location & DATA_LOCATION_INITIALIZED_MASK);
							u32 metadata_selector = (record.metadata_location & DATA_LOCATION_SELECTOR_MASK) >> DATA_LOCATION_SELECTOR_OFFSET;
							u8 metadata_generation = (u8) (record.metadata_location & DATA_data_generation_MASK);
							u32 metadata_first_block = (record.metadata_location & DATA_BLOCK_NUMBER_MASK);
							u32 metadata_block_count = ((record.metadata_location & DATA_EXTRA_BLOCKS_MASK) >> DATA_EXTRA_BLOCKS_OFFSET) + 1;

							if(!data_initialized && !metadata_initialized) continue;

							if(data_block_count < MIN_RECORD_BLOCKS || data_block_count > MAX_RECORD_BLOCKS)
							{
								log_error("The number of data blocks is out of range (%d <= %I32u <= %d)", r, MIN_RECORD_BLOCKS, data_block_count, MAX_RECORD_BLOCKS);
								continue;
							}

							if(metadata_block_count < MIN_RECORD_BLOCKS || metadata_block_count > MAX_RECORD_BLOCKS)
							{
								log_error("The number of metadata blocks is out of range (%d <= %I32u <= %d)", r, MIN_RECORD_BLOCKS, metadata_block_count, MAX_RECORD_BLOCKS);
								continue;
							}

							Cursor metadata_cursor = {};

							if(metadata_initialized)
							{
								if(metadata_selector == 0)
								{
									String* metadata_path = mozilla_v1_data_path(path, v19_or_newer, record.hash_number, true, metadata_generation);
									File file = {};
									if(file_read_all(metadata_path, &file))
									{
										if(file.size >= sizeof(Metadata))
										{
											metadata_cursor.data = file.data;
											metadata_cursor.size = file.size;
										}
										else
										{
											log_error("The metadata file '%s' is smaller than expected (%Iu < %Iu)", metadata_path->data, file.size, sizeof(Metadata));
										}
									}
									else
									{
										log_error("Failed to read the metadata file '%s'", metadata_path->data);
									}
								}
								else if(metadata_selector <= MAX_BLOCK_FILES)
								{
									Block_File* block = &block_files[metadata_selector];
									if(block->exists)
									{
										size_t offset = block->header_size + metadata_first_block * block->block_size;
										size_t size = metadata_block_count * block->block_size;
										ASSERT(sizeof(Metadata) <= size && size <= block->max_entry_size, "Metadata size is out of range");

										void* data = arena_push(context.current_arena, size, char);
										size_t bytes_read = 0;

										if(file_read_at_most(block->path, data, size, offset, &bytes_read))
										{
											if(bytes_read >= sizeof(Metadata))
											{
												metadata_cursor.data = data;
												metadata_cursor.size = bytes_read;
											}
											else
											{
												log_warning("The metadata from block file '%s' is smaller than expected (%Iu < %Iu)", block->path->data, bytes_read, sizeof(Metadata));
											}
										}
										else
										{
											log_error("Failed to read the metadata from block file '%s'", block->path->data);
										}
									}
								}
								else
								{
									log_error("The metadata selector is out of range (0 <= %I32u <= %d)", metadata_selector, MAX_BLOCK_FILES);
								}
							}

							Map<Csv_Column, String*>* row = map_create<Csv_Column, String*>(MOZILLA_COLUMNS.count);
							Export_Params params = {};
							params.index = &index;
							params.row = row;

							map_put(&row, CSV_BROWSER, browser);
							map_put(&row, CSV_PROFILE, profile);

							{
								String_Builder* builder = builder_create(5);
								builder_append_format(&builder, T("%hu.%hu"), header.major_version, header.minor_version);
								map_put(&row, CSV_VERSION, builder_terminate(&builder));
							}

							Metadata metadata = {};
							bool read_metadata = (metadata_cursor.data != NULL);

							if(read_metadata)
							{
								cursor_big_endian_read(&metadata_cursor, &metadata.header_major_version);
								cursor_big_endian_read(&metadata_cursor, &metadata.header_minor_version);
								cursor_big_endian_read(&metadata_cursor, &metadata.meta_location);
								cursor_big_endian_read(&metadata_cursor, &metadata.access_count);
								cursor_big_endian_read(&metadata_cursor, &metadata.last_access_time);
								cursor_big_endian_read(&metadata_cursor, &metadata.last_modified_time);
								cursor_big_endian_read(&metadata_cursor, &metadata.expiry_time);
								cursor_big_endian_read(&metadata_cursor, &metadata.data_size);
								cursor_big_endian_read(&metadata_cursor, &metadata.key_size);
								cursor_big_endian_read(&metadata_cursor, &metadata.elements_size);

								if(!metadata_cursor.end && metadata_cursor.size >= metadata.key_size)
								{
									// The key contains two values separated by a colon, where the URL is the second one.
									// For example: "HTTP:http://www.example.com/index.html"
									//
									// See:
									// - ClientKeyFromCacheKey() in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsCache.cpp
									// - nsCacheService::CreateRequest() in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsCacheService.cpp

									String* key = string_from_utf_8((char*) metadata_cursor.data);

									Split_State state = {};
									state.str = key;
									state.delimiters = T(":");

									String_View protocol = {};
									String_View url = {};
									if(string_partition(&state, &protocol, &url))
									{
										params.url = string_from_view(url);
									}
									else
									{
										log_warning("The key '%s' does not contain the URL", key->data);
									}

									metadata_cursor.data = advance(metadata_cursor.data, metadata.key_size);
									metadata_cursor.size -= metadata.key_size;

									if(metadata_cursor.size >= metadata.elements_size)
									{
										Metadata_Elements elements = mozilla_cache_elements_parse(metadata_cursor.data, metadata.elements_size);
										params.http_headers = elements.http_headers;
										params.origin = elements.request_origin;
									}
									else
									{
										log_warning("Skipping the metadata elements since the remaining size is smaller than expected (%Iu < %I32u)", metadata_cursor.size, metadata.elements_size);
									}
								}
								else
								{
									log_warning("Skipping the metadata key and elements since the remaining size is smaller than expected (%Iu < %I32u)", metadata_cursor.size, metadata.key_size);
								}

								map_put(&row, CSV_LAST_MODIFIED_TIME, unix_time_format(metadata.last_modified_time));
								map_put(&row, CSV_LAST_ACCESS_TIME, unix_time_format(metadata.last_access_time));
								map_put(&row, CSV_EXPIRY_TIME, unix_time_format(metadata.expiry_time));
								map_put(&row, CSV_ACCESS_COUNT, string_from_num(metadata.access_count));

								{
									String_Builder* builder = builder_create(5);
									builder_append_format(&builder, T("%hu.%hu"), metadata.header_major_version, metadata.header_minor_version);
									map_put(&row, CSV_VERSION, builder_terminate(&builder));
								}
							}

							bool exported = false;

							if(data_initialized)
							{
								if(data_selector == 0)
								{
									exported = true;
									params.data_path = mozilla_v1_data_path(path, v19_or_newer, record.hash_number, false, data_generation);
									exporter_next(exporter, params);
								}
								else if(data_selector <= MAX_BLOCK_FILES)
								{
									Block_File* block = &block_files[data_selector];
									if(block->exists)
									{
										size_t offset = block->header_size + data_first_block * block->block_size;
										size_t allocated_size = data_block_count * block->block_size;
										ASSERT(allocated_size <= block->max_entry_size, "Data size is out of range");

										char* data = arena_push(context.current_arena, allocated_size, char);
										size_t bytes_read = 0;

										if(file_read_at_most(block->path, data, allocated_size, offset, &bytes_read))
										{
											File_Writer writer = {};
											TEMPORARY_FILE_DEFER(&writer)
											{
												size_t write_size = 0;

												if(read_metadata)
												{
													if(metadata.data_size > bytes_read)
													{
														log_warning("The data size in '%s' is larger than expected (%I32u > %Iu)", block->path->data, metadata.data_size, bytes_read);
													}
													write_size = MIN(metadata.data_size, bytes_read);
												}
												else
												{
													// Try to guess the data size if there's no metadata.
													// The data in a block file is padded with null bytes, unless it's the last entry.
													int null_count = 0;
													for(ssize_t i = bytes_read - 1; i >= 0; i -= 1)
													{
														if(data[i] != '\0') break;
														null_count += 1;
													}

													write_size = bytes_read - null_count;
													log_warning("Guessed data size in '%s' (%Iu - %d = %Iu)", block->path->data, bytes_read, null_count, write_size);
												}

												String_Builder* builder = builder_create(block->path->code_count + 20);
												builder_append_format(&builder, T("%s") T("@%08X") T("#%08X"), block->path->data, offset, write_size);
												map_put(&row, CSV_INPUT_PATH, builder_terminate(&builder));

												if(file_write_next(&writer, data, write_size))
												{
													exported = true;
													params.data_path = writer.path;
													exporter_next(exporter, params);
												}
												else
												{
													log_error("Failed to extract the cached file");
												}
											}
										}
										else
										{
											log_error("Failed to read the data from block file '%s'", block->path->data);
										}

										if(!exported && read_metadata)
										{
											// Default values for cases where extracting the cached file fails.
											String_Builder* builder = builder_create(block->path->code_count + 20);
											builder_append_format(&builder, T("%s") T("@%08X") T("#%08X"), block->path->data, offset, metadata.data_size);
											map_put(&row, CSV_INPUT_PATH, builder_terminate(&builder));
											map_put(&row, CSV_INPUT_SIZE, string_from_num(metadata.data_size));
										}
									}
								}
								else
								{
									log_error("The data selector is out of range (0 <= %I32u <= %d)", data_selector, MAX_BLOCK_FILES);
								}
							}

							// The arena is cleared after exporting.
							if(!exported)
							{
								params.data_path = NO_PATH;
								exporter_next(exporter, params);
							}
						}
					}
				}
				else
				{
					log_error("The map file '%s' is smaller than expected (%Iu < %Iu)", map_path->data, map_file.size, sizeof(Map_Header_6_To_19));
				}
			}
			else
			{
				log_error("Failed to read the map file '%s'", map_path->data);
			}

			// Since we only want data files, we can use a query that excludes map, block, and metadata files.
			Walk_State state = {};
			state.base_path = path;
			state.query = T("*d??");
			state.files = true;
			state.max_depth = -1;
			state.copy = true;

			WALK_DEFER(&state)
			{
				Walk_Info info = {};
				while(walk_next(&state, &info))
				{
					ARENA_SAVEPOINT()
					{
						if(!exporter_index_has(index, info.path))
						{
							Export_Params params = {};
							params.info = &info;
							params.unindexed = true;
							params.index = &index;
							params.row = map_create<Csv_Column, String*>(MOZILLA_COLUMNS.count);
							exporter_next(exporter, params);
						}
					}
				}
			}
		}
	}
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

	// @Version: Firefox 32 and newer
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
			String* browser = NULL;
			String* profile = NULL;
			mozilla_browser_and_profile(exporter, path, &browser, &profile);

			Index_Header index_header = {};

			{
				String* index_path = path_build(CANY(path), CANY(T("index")));

				if(file_read_first_chunk(index_path, &index_header, sizeof(index_header)))
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
						log_warning("The dirty flag is set");
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
						log_error("Failed to read the metadata offset from '%s'", info.path->data);
						continue;
					}

					if(metadata_offset >= info.size)
					{
						log_error("The metadata offset 0x%08X goes past the end of '%s'", metadata_offset, info.path->data);
						continue;
					}

					// See CacheFileMetadata::OnDataRead() in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileMetadata.cpp
					const size_t HASH_CHUNK_SIZE = 256 * 1024;
					size_t hash_count = (metadata_offset == 0) ? (0) : ((metadata_offset - 1) / HASH_CHUNK_SIZE + 1);
					size_t hash_size = sizeof(u32) + hash_count * sizeof(u16);

					size_t metadata_size = size_clamp((s64) info.size - metadata_offset - hash_size - sizeof(metadata_offset));
					if(metadata_size < sizeof(Metadata_Header_1_And_2))
					{
						log_error("The metadata in '%s' is smaller than expected (%Iu < %Iu)", info.path->data, metadata_size, sizeof(Metadata_Header_1_And_2));
						continue;
					}

					Map<Csv_Column, String*>* row = map_create<Csv_Column, String*>(MOZILLA_COLUMNS.count);
					Export_Params params = {};
					params.row = row;

					void* metadata = arena_push(context.current_arena, metadata_size, char);
					if(file_read_chunk(info.path, metadata, metadata_size, metadata_offset + hash_size))
					{
						Cursor cursor = {};
						cursor.data = metadata;
						cursor.size = metadata_size;

						// Version 3 includes every member from the previous versions.
						Metadata_Header_3 metadata_header = {};
						cursor_big_endian_read(&cursor, &metadata_header.version);

						if(metadata_header.version <= 2)
						{
							cursor_big_endian_read(&cursor, &metadata_header.access_count);
							cursor_big_endian_read(&cursor, &metadata_header.last_access_time);
							cursor_big_endian_read(&cursor, &metadata_header.last_modified_time);
							cursor_big_endian_read(&cursor, &metadata_header.expiry_time);
							cursor_big_endian_read(&cursor, &metadata_header.key_length);
						}
						else if(metadata_header.version == 3)
						{
							cursor_big_endian_read(&cursor, &metadata_header.access_count);
							cursor_big_endian_read(&cursor, &metadata_header.last_access_time);
							cursor_big_endian_read(&cursor, &metadata_header.last_modified_time);
							cursor_big_endian_read(&cursor, &metadata_header.frecency);
							cursor_big_endian_read(&cursor, &metadata_header.expiry_time);
							cursor_big_endian_read(&cursor, &metadata_header.key_length);
							cursor_big_endian_read(&cursor, &metadata_header.flags);
						}
						else
						{
							log_warning("Skipping unsupported metadata version %I32u in '%s'", metadata_header.version, info.path->data);
							ASSERT(false, "Unsupported metadata version");
							goto skip;
						}

						size_t key_size = metadata_header.key_length + 1;
						if(!cursor.end && cursor.size >= key_size)
						{
							String* key = string_from_utf_8((char*) cursor.data);
							Metadata_2_Key parts = mozilla_v2_key_parse(key);
							params.url = parts.url;

							cursor.data = advance(cursor.data, key_size);
							cursor.size -= key_size;

							Metadata_Elements elements = mozilla_cache_elements_parse(cursor.data, cursor.size);
							params.http_headers = elements.http_headers;
							params.origin = (elements.request_origin != NULL) ? (elements.request_origin) : (parts.partition_key);
						}
						else
						{
							log_warning("Skipping the metadata key and elements in '%s' since the remaining size is smaller than expected (%Iu < %Iu)", info.path->data, cursor.size, key_size);
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
						log_error("Failed to read the metadata from '%s'", info.path->data);
					}

					bool exported = false;

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
							exported = true;
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

					// The arena is cleared after exporting.
					if(!exported)
					{
						params.data_path = NO_PATH;
						exporter_next(exporter, params);
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
		Exporter exporter = {};
		exporter.current_batch = true;

		#define TEST_BROWSER_AND_PROFILE(path, expected_browser, expected_profile) \
			do \
			{ \
				String* browser = NULL; \
				String* profile = NULL; \
				mozilla_browser_and_profile(&exporter, CSTR(path), &browser, &profile); \
				TEST(browser, T(expected_browser)); \
				TEST(profile, T(expected_profile)); \
			} while(false)

		TEST_BROWSER_AND_PROFILE("C:\\WINDOWS\\Application Data\\<Vendor>\\<Browser>\\Profiles\\<Profile>\\<Cache>", "<Browser>", "<Profile>");
		TEST_BROWSER_AND_PROFILE("C:\\Documents and Settings\\<User>\\Local Settings\\Application Data\\<Vendor>\\<Browser>\\Profiles\\<Profile>\\<Cache>", "<Browser>", "<Profile>");
		TEST_BROWSER_AND_PROFILE("C:\\Users\\<User>\\AppData\\Local\\<Vendor>\\<Browser>\\Profiles\\<Profile>\\<Cache>", "<Browser>", "<Profile>");

		TEST_BROWSER_AND_PROFILE("C:\\WINDOWS\\Application Data\\<Vendor>\\<Browser>\\Profiles\\<Profile>\\<8 Characters>.slt\\<Cache>", "<Browser>", "<Profile>");
		TEST_BROWSER_AND_PROFILE("C:\\Documents and Settings\\<User>\\Application Data\\<Vendor>\\<Browser>\\Profiles\\<Profile>\\<8 Characters>.slt\\<Cache>", "<Browser>", "<Profile>");
		TEST_BROWSER_AND_PROFILE("C:\\Users\\<User>\\AppData\\Roaming\\<Vendor>\\<Browser>\\Profiles\\<Profile>\\<8 Characters>.slt\\<Cache>", "<Browser>", "<Profile>");

		#undef TEST_BROWSER_AND_PROFILE
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
		String* path = CSTR("C:\\Path");
		TEST(mozilla_v1_data_path(path, false, 0x0E0A6E00U, false, 1U), T("C:\\Path\\0E0A6E00d01"));
		TEST(mozilla_v1_data_path(path, false, 0x0E0A6E00U, true, 1U), T("C:\\Path\\0E0A6E00m01"));
		TEST(mozilla_v1_data_path(path, true, 0x0E0A6E00U, false, 1U), T("C:\\Path\\0\\E0\\A6E00d01"));
		TEST(mozilla_v1_data_path(path, true, 0x0E0A6E00U, true, 1U), T("C:\\Path\\0\\E0\\A6E00m01"));
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