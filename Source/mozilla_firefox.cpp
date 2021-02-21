#include "web_cache_exporter.h"
#include "mozilla_firefox.h"

/*
	@TODO

	@DefaultCacheLocations:
	- 95, 98, ME 			C:\WINDOWS\Application Data\Mozilla\Firefox\Profiles\<Profile Name>\Cache
	- 2000, XP 				C:\Documents and Settings\<Username>\Local Settings\Application Data\Mozilla\Firefox\Profiles\<Profile Name>\Cache
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Local\Mozilla\Firefox\Profiles\<Profile Name>\Cache



*/

static const TCHAR* OUTPUT_NAME = TEXT("MZ");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_URL, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_FIRST_ACCESS_TIME, CSV_LAST_ACCESS_TIME, CSV_EXPIRY_TIME, CSV_ACCESS_COUNT,
	CSV_RESPONSE, CSV_SERVER, CSV_CACHE_CONTROL, CSV_PRAGMA, CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_ENCODING, 
	CSV_LOCATION_ON_CACHE, CSV_CACHE_VERSION,
	CSV_MISSING_FILE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_CUSTOM_URL_GROUP, CSV_SHA_256
};

static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// @TODO

static void export_mozilla_cache_version_1(Exporter* exporter);

void export_default_or_specific_mozilla_firefox_cache(Exporter* exporter)
{
	console_print("Exporting Mozilla browsers' cache...");

	initialize_cache_exporter(exporter, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		if(exporter->is_exporting_from_default_locations)
		{
			TCHAR* mozilla_appdata_path = exporter->local_appdata_path;
			if(string_is_empty(mozilla_appdata_path)) mozilla_appdata_path = exporter->appdata_path;

			TCHAR mozilla_profile_path[MAX_PATH_CHARS] = TEXT("");
			PathCombine(mozilla_profile_path, mozilla_appdata_path, TEXT("Mozilla\\Firefox\\Profiles"));
			set_exporter_output_copy_subdirectory(exporter, TEXT("FF"));

			Arena* arena = &(exporter->temporary_arena);
			Traversal_Result* profiles = find_objects_in_directory(arena, mozilla_profile_path, TEXT("*"), TRAVERSE_DIRECTORIES, false);
			lock_arena(arena);

			for(int i = 0; i < profiles->num_objects; ++i)
			{
				Traversal_Object_Info profile_info = profiles->object_info[i];
				PathCombine(exporter->cache_path, profile_info.object_path, TEXT("Cache"));
				exporter->cache_profile = profile_info.object_name;

				export_mozilla_cache_version_1(exporter);
			}

			unlock_arena(arena);
		}
		else
		{
			export_mozilla_cache_version_1(exporter);
		}
	}
	terminate_cache_exporter(exporter);
}

// @ByteOrder: Big Endian.

static const size_t NUM_BUCKETS = 32;

#pragma pack(push, 1)
struct Mozilla_Map_Header
{
	u16 major_version;
	u16 minor_version;
	u32 data_size;
	u32 num_entries;
	u32 dirty_flag;

	u32 num_records;
	u32 eviction_ranks[NUM_BUCKETS];
	u32 bucket_usage[NUM_BUCKETS];
};

struct Mozilla_Map_Record
{
	u32 hash_number;
	u32 eviction_rank;
	u32 data_location;
	u32 metadata_location;
};

struct Mozilla_Metadata_Entry
{
	u16 major_version;
	u16 minor_version;
	u32 metadata_location;
	s32 access_count; // @Format: Signed integer.
	u32 first_access_time;

	u32 last_access_time;
	u32 expiry_time;
	u32 data_size;
	u32 url_size; // @Format: Includes the null terminator.

	u32 headers_size; // @Format: Includes the null terminator.
};

enum Mozilla_Data_Mask_Or_Offset
{
	LOCATION_INITIALIZED_MASK = 0x80000000,

	LOCATION_SELECTOR_MASK = 0x30000000,
	LOCATION_SELECTOR_OFFSET = 28,

	EXTRA_BLOCKS_MASK = 0x03000000,
	EXTRA_BLOCKS_OFFSET = 24,

	RESERVED_MASK = 0x4C000000,

	BLOCK_NUMBER_MASK = 0x00FFFFFF,

	FILE_SIZE_MASK = 0x00FFFF00,
	FILE_SIZE_OFFSET = 8,
	FILE_GENERATION_MASK = 0x000000FF,
	FILE_RESERVED_MASK = 0x4F000000,	
};
#pragma pack(pop)

_STATIC_ASSERT(sizeof(Mozilla_Map_Header) == 276);

static void export_mozilla_cache_version_1(Exporter* exporter)
{
	// @TODO
	Arena* arena = &(exporter->temporary_arena);

	PathCombine(exporter->index_path, exporter->cache_path, TEXT("_CACHE_MAP_"));
	u64 map_file_size = 0;
	void* map_file = read_entire_file(arena, exporter->index_path, &map_file_size);

	if(map_file == NULL)
	{
		DWORD error_code = GetLastError();
		if( (error_code == ERROR_FILE_NOT_FOUND) || (error_code == ERROR_PATH_NOT_FOUND) )
		{
			log_print(LOG_ERROR, "Mozilla Cache Version 1: The map file was not found. No files will be exported from this cache.");
		}
		else
		{
			log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to open the map file with the error code %lu. No files will be exported from this data file.", error_code);
		}

		return;
	}

	if(map_file_size < sizeof(Mozilla_Map_Header))
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 1: The size of the map file is smaller than the file format's header. No files will be exported from this cache.");
		return;
	}

	// @TODO

	Mozilla_Map_Header* header = (Mozilla_Map_Header*) map_file;

	SWAP_BYTE_ORDER(header->major_version);
	SWAP_BYTE_ORDER(header->minor_version);
	SWAP_BYTE_ORDER(header->data_size);
	SWAP_BYTE_ORDER(header->num_entries);
	SWAP_BYTE_ORDER(header->dirty_flag);
	SWAP_BYTE_ORDER(header->num_records);
	for(u32 i = 0; i < NUM_BUCKETS; ++i)
	{
		SWAP_BYTE_ORDER(header->eviction_ranks[i]);
		SWAP_BYTE_ORDER(header->bucket_usage[i]);
	}

	const size_t MAX_CACHE_VERSION_CHARS = MAX_INT16_CHARS + 1 + MAX_INT16_CHARS;
	TCHAR cache_version[MAX_CACHE_VERSION_CHARS] = TEXT("");
	StringCchPrintf(cache_version, MAX_CACHE_VERSION_CHARS, TEXT("%hu.%hu"), header->major_version, header->minor_version);

	bool is_mozilla_2_or_later = (header->major_version >= 1 && header->minor_version >= 19);
	log_print(LOG_INFO, "Mozilla Cache Version 1: The map file (version %s) was opened successfully.", cache_version);

	if(header->dirty_flag != 0)
	{
		log_print(LOG_WARNING, "Mozilla Cache Version 1: The map file's dirty flag is set to 0x%08X.", header->dirty_flag);
	}

	u32 num_records = header->num_records;
	{
		u32 expected_num_records = ((u32) map_file_size - sizeof(Mozilla_Map_Header)) / sizeof(Mozilla_Map_Record);
		if(expected_num_records != num_records)
		{
			log_print(LOG_WARNING, "Mozilla Cache Version 1: The map file has %I32u records when %I32u were expected. Only this last number of records will be processed.", num_records, expected_num_records);
			num_records = expected_num_records;
		}
	}

	const size_t MAX_BLOCK_FILENAME_CHARS = 12;
	struct Block_File
	{
		TCHAR filename[MAX_BLOCK_FILENAME_CHARS];
		TCHAR file_path[MAX_PATH_CHARS];		
		HANDLE file_handle;

		u32 header_size;
		u32 block_size;
		u32 max_entry_size;
	};

	const int MAX_BLOCK_FILE_NUM = 3;
	Block_File block_file_array[MAX_BLOCK_FILE_NUM + 1] = {};

	for(int i = 1; i <= MAX_BLOCK_FILE_NUM; ++i)
	{
		Block_File* block_file = &block_file_array[i];

		StringCchPrintf(block_file->filename, MAX_BLOCK_FILENAME_CHARS, TEXT("_CACHE_00%d_"), i);
		PathCombine(block_file->file_path, exporter->cache_path, block_file->filename);

		block_file->file_handle = CreateFile(block_file->file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

		if(block_file->file_handle != INVALID_HANDLE_VALUE)
		{
			u64 file_size = 0;
			if(get_file_size(block_file->file_handle, &file_size))
			{
				log_print(LOG_INFO, "Mozilla Cache Version 1: The block file '%s' has a size of %I64u bytes.", block_file->filename, file_size);
			}
			else
			{
				log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to find the size of block file '%s' with the error code %lu.", block_file->filename, GetLastError());
			}
		}
		else
		{
			DWORD error_code = GetLastError();
			if( (error_code == ERROR_FILE_NOT_FOUND) || (error_code == ERROR_PATH_NOT_FOUND) )
			{
				log_print(LOG_ERROR, "Mozilla Cache Version 1: The block file '%s' was not found. No files will be exported from this block file.", block_file->filename);
			}
			else
			{
				log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to open block file '%s' with the error code %lu. No files will be exported from this block file.", block_file->filename, error_code);
			}
		}
		
		if(i == 1)
		{
			block_file->header_size = (is_mozilla_2_or_later) ? (16384) : (4096);
			block_file->block_size = 256;
		}
		else if(i == 2)
		{
			block_file->header_size = 4096;
			block_file->block_size = 1024;
		}
		else if(i == 3)
		{
			block_file->header_size = (is_mozilla_2_or_later) ? (1024) : (4096);
			block_file->block_size = 4096;
		}
		else
		{
			_ASSERT(false);
		}

		block_file->max_entry_size = 4 * block_file->block_size;
	}

	lock_arena(arena);

	for(u32 i = 0; i < num_records; ++i)
	{
		Mozilla_Map_Record* record = (Mozilla_Map_Record*) advance_bytes(header, sizeof(Mozilla_Map_Header) + i * sizeof(Mozilla_Map_Record));

		SWAP_BYTE_ORDER(record->hash_number);
		SWAP_BYTE_ORDER(record->eviction_rank);
		SWAP_BYTE_ORDER(record->data_location);
		SWAP_BYTE_ORDER(record->metadata_location);
	
		if(record->hash_number == 0) continue;

		u32 file_initialized = (record->data_location & LOCATION_INITIALIZED_MASK);
		u32 file_selector = (record->data_location & LOCATION_SELECTOR_MASK) >> LOCATION_SELECTOR_OFFSET;
		u32 file_generation = (record->data_location & FILE_GENERATION_MASK);
		u32 file_first_block = (record->data_location & BLOCK_NUMBER_MASK);
		u32 file_num_blocks = ((record->data_location & EXTRA_BLOCKS_MASK) >> EXTRA_BLOCKS_OFFSET) + 1;

		u32 metadata_initialized = (record->metadata_location & LOCATION_INITIALIZED_MASK);
		u32 metadata_selector = (record->metadata_location & LOCATION_SELECTOR_MASK) >> LOCATION_SELECTOR_OFFSET;
		u32 metadata_generation = (record->metadata_location & FILE_GENERATION_MASK);
		u32 metadata_first_block = (record->metadata_location & BLOCK_NUMBER_MASK);
		u32 metadata_num_blocks = ((record->metadata_location & EXTRA_BLOCKS_MASK) >> EXTRA_BLOCKS_OFFSET) + 1;	

		bool is_file_initialized = (file_initialized != 0);
		bool is_metadata_initialized = (metadata_initialized != 0);

		if(!is_file_initialized && !is_metadata_initialized) continue;

		// @TODO
		#define GET_EXTERNAL_DATA_FILE_PATH(is_metadata, result_path)\
		do\
		{\
			TCHAR hash[MAX_INT32_CHARS] = TEXT("");\
			StringCchPrintf(hash, MAX_INT32_CHARS, TEXT("%08X"), record->hash_number);\
			const TCHAR* identifier = (is_metadata) ? (TEXT("m")) : (TEXT("d"));\
			u32 generation = (is_metadata) ? (metadata_generation) : (file_generation);\
			/* @TODO: e.g. */\
			if(is_mozilla_2_or_later)\
			{\
				StringCchPrintf(result_path, MAX_PATH_CHARS, TEXT("%.1s\\") TEXT("%.2s\\") TEXT("%s") TEXT("%s") TEXT("%02X"), hash, hash + 1, hash + 3, identifier, generation);\
			}\
			/* @TODO: e.g. */\
			else\
			{\
				StringCchPrintf(result_path, MAX_PATH_CHARS, TEXT("%s") TEXT("%s") TEXT("%02X"), hash, identifier, generation);\
			}\
		} while(false, false)

		Mozilla_Metadata_Entry* metadata = NULL;

		if(is_metadata_initialized)
		{
			if(metadata_selector <= MAX_BLOCK_FILE_NUM)
			{
				if(metadata_selector == 0)
				{				
					TCHAR full_metadata_path[MAX_PATH_CHARS] = TEXT("");
					GET_EXTERNAL_DATA_FILE_PATH(true, full_metadata_path);
					PathCombine(full_metadata_path, exporter->cache_path, full_metadata_path);
					
					u64 metadata_file_size = 0;
					metadata = (Mozilla_Metadata_Entry*) read_entire_file(arena, full_metadata_path, &metadata_file_size);
					if(metadata != NULL)
					{
						if(metadata_file_size < sizeof(Mozilla_Metadata_Entry))
						{
							metadata = NULL;
							log_print(LOG_WARNING, "Mozilla Cache Version 1: Skipping the read metadata file for record %I32u in '%s' since its size of %I64u is smaller than the minimum possible entry size.", i, full_metadata_path, metadata_file_size);
						}
					}
					else
					{
						log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to read the metadata file for record %I32u in '%s' with the error code %lu.", i, full_metadata_path, GetLastError());
					}
				}
				else
				{
					Block_File block_file = block_file_array[metadata_selector];
					if(block_file.file_handle != INVALID_HANDLE_VALUE)
					{
						u32 offset_in_block_file = block_file.header_size + metadata_first_block * block_file.block_size;
						u32 total_metadata_size = metadata_num_blocks * block_file.block_size;
						_ASSERT(sizeof(Mozilla_Metadata_Entry) <= total_metadata_size);
						_ASSERT(total_metadata_size <= block_file.max_entry_size);

						metadata = push_arena(arena, total_metadata_size, Mozilla_Metadata_Entry);

						u32 read_metadata_size = 0;
						if(read_file_chunk(block_file.file_handle, metadata, total_metadata_size, offset_in_block_file, true, &read_metadata_size))
						{
							if(read_metadata_size < sizeof(Mozilla_Metadata_Entry))
							{
								metadata = NULL;
								log_print(LOG_WARNING, "Mozilla Cache Version 1: Skipping the read metadata for record %I32u in block file '%s' at the offset %I32u since the read size of %I32u is smaller than the minimum possible entry size.", i, block_file.filename, offset_in_block_file, read_metadata_size);
							}
						}
						else
						{
							metadata = NULL;
							log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to read the metadata for record %I32u in block file '%s' at the offset %I32u and with a total size of %I32u.", i, block_file.filename, offset_in_block_file, total_metadata_size);
						}
					}
				}				
			}
			else
			{
				log_print(LOG_WARNING, "Mozilla Cache Version 1: Skipping the unknown metadata selector %I32u in record %I32u.", metadata_selector, i);
			}	
		}

		TCHAR cached_file_size_string[MAX_INT32_CHARS] = TEXT("");
		TCHAR access_count[MAX_INT32_CHARS] = TEXT("");

		TCHAR first_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
		TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
		TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
		
		TCHAR* url = NULL;
		Http_Headers headers = {};

		if(metadata != NULL)
		{
			SWAP_BYTE_ORDER(metadata->major_version);
			SWAP_BYTE_ORDER(metadata->minor_version);
			SWAP_BYTE_ORDER(metadata->metadata_location);
			SWAP_BYTE_ORDER(metadata->access_count);
			SWAP_BYTE_ORDER(metadata->first_access_time);
			SWAP_BYTE_ORDER(metadata->last_access_time);
			SWAP_BYTE_ORDER(metadata->expiry_time);
			SWAP_BYTE_ORDER(metadata->data_size);
			SWAP_BYTE_ORDER(metadata->url_size);
			SWAP_BYTE_ORDER(metadata->headers_size);

			convert_u32_to_string(metadata->data_size, cached_file_size_string);
			convert_s32_to_string(metadata->access_count, access_count);
		
			format_time32_t_date_time(metadata->first_access_time, first_access_time);
			format_time32_t_date_time(metadata->last_access_time, last_access_time);
			format_time32_t_date_time(metadata->expiry_time, expiry_time);

			Block_File block_file = block_file_array[metadata_selector];
			u32 remaining_metadata_size = (metadata_num_blocks * block_file.block_size) - sizeof(Mozilla_Metadata_Entry);
			_ASSERT( (metadata_num_blocks * block_file.block_size) >= sizeof(Mozilla_Metadata_Entry) );

			#define TRUNCATE_SIZES(size_variable, string_variable)\
			do\
			{\
				if(size_variable > remaining_metadata_size)\
				{\
					log_print(LOG_WARNING, "Mozilla Cache Version 1: Truncating '%hs' since its value (%I32u) exceeds the remaining metadata size (%I32u).", #size_variable, size_variable, remaining_metadata_size);\
					size_variable = remaining_metadata_size;\
					char* end_of_string = (char*) advance_bytes(string_variable, size_variable - 1);\
					*end_of_string = '\0';\
				}\
				remaining_metadata_size -= size_variable;\
			} while(false, false)

			// @Format: The URL and headers are null terminated.

			u32 url_size = metadata->url_size;
			char* url_in_metadata = (char*) advance_bytes(metadata, sizeof(Mozilla_Metadata_Entry));
			TRUNCATE_SIZES(url_size, url_in_metadata);

			url = convert_ansi_string_to_tchar(arena, url_in_metadata);
			url = skip_url_scheme(url);
			url = decode_url(arena, url);

			u32 headers_size = metadata->headers_size;
			char* headers_in_metadata = (char*) advance_bytes(url_in_metadata, url_size);
			TRUNCATE_SIZES(headers_size, headers_in_metadata);

			parse_http_headers(arena, headers_in_metadata, headers_size, &headers);
		}

		HANDLE temporary_file_handle = INVALID_HANDLE_VALUE;
		TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");

		TCHAR short_location_on_cache[MAX_PATH_CHARS] = TEXT("");
		TCHAR full_location_on_cache[MAX_PATH_CHARS] = TEXT("");

		if(is_file_initialized)
		{
			if(file_selector <= MAX_BLOCK_FILE_NUM)
			{
				if(file_selector == 0)
				{				
					GET_EXTERNAL_DATA_FILE_PATH(false, short_location_on_cache);
					PathCombine(full_file_path, exporter->cache_path, short_location_on_cache);
					PathCombine(short_location_on_cache, exporter->cache_profile, short_location_on_cache);
				}
				else
				{
					// @TODO
					Block_File block_file = block_file_array[file_selector];
					if(block_file.file_handle != INVALID_HANDLE_VALUE)
					{
						u32 offset_in_block_file = block_file.header_size + file_first_block * block_file.block_size;
						u32 total_file_size = file_num_blocks * block_file.block_size;

						_ASSERT(total_file_size <= block_file.max_entry_size);
						
						void* cached_file_in_block_file = push_arena(arena, total_file_size, u8);
						u32 read_cached_file_size = 0;

						if(read_file_chunk(block_file.file_handle, cached_file_in_block_file, total_file_size, offset_in_block_file, true, &read_cached_file_size))
						{
							if(metadata != NULL)
							{
								read_cached_file_size = MIN(read_cached_file_size, metadata->data_size);
							}
							else
							{
								u32 num_null_bytes = 0;
								u8* last_cached_file_byte = (u8*) advance_bytes(cached_file_in_block_file, read_cached_file_size - 1);

								while(*last_cached_file_byte == 0 && num_null_bytes < read_cached_file_size)
								{
									++num_null_bytes;
									--last_cached_file_byte;
								}

								_ASSERT(num_null_bytes <= read_cached_file_size);
								read_cached_file_size -= num_null_bytes;
								log_print(LOG_WARNING, "Mozilla Cache Version 1: Attempted to find the cached file's size in record %I32u since the metadata was missing. Reduced the size to %I32u after finding %I32u null bytes. The exported file may be corrupted.", i, read_cached_file_size, num_null_bytes);
							}

							bool write_success = create_temporary_exporter_file(exporter, full_file_path, &temporary_file_handle)
												&& write_to_file(temporary_file_handle, cached_file_in_block_file, read_cached_file_size);

							if(!write_success)
							{
								log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to write the cached file (%I32u) in record %I32u from block file '%s' to the temporary exporter directory.", read_cached_file_size, i, block_file.filename);
							}

							const size_t MAX_LOCATION_IN_FILE_CHARS = MAX_INT32_CHARS * 2 + 2;
							TCHAR location_in_file[MAX_LOCATION_IN_FILE_CHARS] = TEXT("");
							StringCchPrintf(location_in_file, MAX_LOCATION_IN_FILE_CHARS, TEXT("@%08X") TEXT("#%08X"), offset_in_block_file, read_cached_file_size);

							PathCombine(short_location_on_cache, exporter->cache_profile, block_file.filename);
							StringCchCat(short_location_on_cache, MAX_PATH_CHARS, location_in_file);

							StringCchCopy(full_location_on_cache, MAX_PATH_CHARS, block_file.file_path);
							StringCchCat(full_location_on_cache, MAX_PATH_CHARS, location_in_file);							
						}
						else
						{
							log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to read the file for record %I32u in block file '%s' at the offset %I32u and with a total size of %I32u.", i, block_file.filename, offset_in_block_file, total_file_size);
						}
					}
				}				
			}
			else
			{
				log_print(LOG_WARNING, "Mozilla Cache Version 1: Skipping the unknown file selector %I32u in record %I32u.", file_selector, i);
			}
		}

		Csv_Entry csv_row[] =
		{
			{/* Filename */}, {url}, {/* File Extension */}, {cached_file_size_string},
			{first_access_time}, {last_access_time}, {expiry_time}, {access_count},
			{headers.response}, {headers.server}, {headers.cache_control}, {headers.pragma},
			{headers.content_type}, {headers.content_length}, {headers.content_encoding},
			{/* Location On Cache */}, {cache_version},
			{/* Missing File */}, {/* Location In Output */}, {/* Copy Error */},
			{/* Custom File Group */}, {/* Custom URL Group */}, {/* SHA-256 */}
		};
		_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

		Exporter_Params params = {};
		params.full_file_path = full_file_path;
		params.url = url;
		params.filename = NULL;
		params.short_location_on_cache = short_location_on_cache;
		params.full_location_on_cache = full_location_on_cache;

		export_cache_entry(exporter, csv_row, &params);

		safe_close_handle(&temporary_file_handle);
	}

	unlock_arena(arena);

	for(int i = 1; i <= MAX_BLOCK_FILE_NUM; ++i)
	{
		Block_File* block_file = &block_file_array[i];
		safe_close_handle(&block_file->file_handle);
	}
}
