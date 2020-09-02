#include "web_cache_exporter.h"
#include "java_plugin.h"

/*
	@Format: IDX files.

	@ByteOrder: Big Endian

	Primitive Types in Java:
	- byte = 1 byte = s8
	- char = 2 bytes = u16
	- short = 2 bytes = s16
	- int = 4 bytes = s32
	- long = 8 bytes = s64

	@Decompiled: "com.sun.deploy.cache.CacheEntry" from "jre\lib\deploy.jar" in JDK 8 update 181

	int INCOMPLETE_FALSE = 0;
	int INCOMPLETE_TRUE = 1;
	int INCOMPLETE_ONHOLD = 2;

	int BUSY_FALSE = 0;
	int BUSY_TRUE = 1;

	readIndexFile(boolean paramBoolean)
	{
		RandomAccessFile file = this.openLockIndexFile("r", false);
		byte[] section1 = new byte[32];
		int numBytesRead = file.read(section1);
		DataInputStream stream = new DataInputStream(new ByteArrayInputStream(section1, 0, numBytesRead));

		byte busy = stream1.readByte(); // @Struct: s8

		// Missing respective cached file.
		int incomplete = stream1.readByte(); // @Struct: s8
		if(paramBoolean && incomplete == INCOMPLETE_TRUE)
		{
			incomplete = INCOMPLETE_ONHOLD;
		}

		int cacheVersion = stream1.readInt(); // @Struct: s32
		// Upgrade the index file if an older file format is detected.
		// @TODO: Document this.
		if(cacheVersion != Cache.getCacheVersion())
		{
			this.readIndexFileOld(stream, file);
        	this.saveUpgrade();
        	return;
		}

		readSection1Remaining(stream); // @Struct: Next 95 bytes.
		{
			byte isShortcutImage = stream1.readBytes(); // @Struct: s8
			int contentLength = stream1.readInt(); // @Struct: s32
			long lastModified = stream1.readLong(); // @Struct: s64
			long expirationDate = stream1.readLong(); // @Struct: s64

			long <unused> = stream1.readLong(); // @Struct: s64
			byte <unused> = stream1.readByte(); // @Struct: s8

		    int section2Length = stream1.readInt(); // @Struct: s32
		    int section3Length = stream1.readInt(); // @Struct: s32
		    int section4Length = stream1.readInt(); // @Struct: s32
		    int section5Length = stream1.readInt(); // @Struct: s32
		    
		    long <unused> = stream1.readLong(); // @Struct: s64
		    long <unused> = stream1.readLong(); // @Struct: s64
		    byte <unused> = stream1.readByte(); // @Struct: s8

			int reducedManifestLength = stream1.readInt(); // @Struct: s32
			int section4Pre15Length = stream1.readInt(); // @Struct: s32

		    byte <unused> = stream1.readByte(); // @Struct: s8
		    byte <unused> = stream1.readByte(); // @Struct: s8

		    int section4CertsLength = stream1.readInt(); // @Struct: s32
		    int section4SignersLength = stream1.readInt(); // @Struct: s32
		    
		    byte <unused> = stream1.readByte(); // @Struct: s8
		    long <unused> = stream1.readLong(); // @Struct: s64
		    
		    int reducedManifest2Length = stream1.readInt(); // @Struct: s32
		    byte IsProxied = stream1.readByte(); // @Struct: s8
		}

        readSection2(file);
        {
			if(section2Length > 0)
			{
				byte[] section2 = new byte[section2Length];
				file.read(section2);
				DataInputStream stream2 = new DataInputStream(new ByteArrayInputStream(section2));

				// Strings are stored in modified UTF-8.
				// @Docs: See writeUTF() in java.io.DataOutputStream and java.io.DataOutput
				// --> https://docs.oracle.com/javase/7/docs/api/java/io/DataOutput.html#writeUTF(java.lang.String)

				String version = stream2.readUTF(); // @Struct: Variable Length
				String url = stream2.readUTF(); // @Struct: Variable Length
				String namespaceId = stream2.readUTF(); // @Struct: Variable Length
				String codebaseIp = stream2.readUTF(); // @Struct: Variable Length

				readHeaders(stream2);
				{
					int numHeaders = stream2.readInt(); // @Struct: s32
					for(int i = 0; i < numHeaders; ++i)
					{
						String headerKey = stream2.readUTF(); // @Struct: Variable Length
						String headerValue = stream2.readUTF(); // @Struct: Variable Length
						
						// <null> corresponds to the server response, i.e., the first line in the HTTP headers.
						if(headerKey.equals("<null>"))
						{
							headerKey = null;
						}
						
						this.headerFields.add(headerKey, headerValue);
					}
				}
			}
        }
	}

*/

// The name of the CSV file and the directory where the cached files will be copied to.
static const TCHAR* OUTPUT_DIRECTORY_NAME = TEXT("JV");

// The order and type of each column in the CSV file.
static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_URL, CSV_FILE_EXTENSION,
	CSV_LAST_MODIFIED_TIME, CSV_EXPIRY_TIME,
	CSV_RESPONSE, CSV_SERVER, CSV_CACHE_CONTROL, CSV_PRAGMA, CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_ENCODING,
	// CSV_CODEBASE_IP, CSV_VERSION, CACHE_SIZE?, INVALID_CACHE_ENTRY?
	CSV_LOCATION_ON_CACHE, CSV_MISSING_FILE,
	CSV_CUSTOM_FILE_GROUP, CSV_CUSTOM_URL_GROUP
};
static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

static TRAVERSE_DIRECTORY_CALLBACK(find_java_index_files_callback);
void export_specific_or_default_java_plugin_cache(Exporter* exporter)
{
	if(exporter->is_exporting_from_default_locations)
	{
		if(!PathCombine(exporter->cache_path, exporter->local_low_appdata_path, TEXT("Sun\\Java\\Deployment\\cache")))
		{
			log_print(LOG_ERROR, "Java Plugin: Failed to determine the cache directory path.");
			return;
		}
	}

	initialize_cache_exporter(exporter, OUTPUT_DIRECTORY_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		log_print(LOG_INFO, "Java Plugin: Exporting the cache from '%s'.", exporter->cache_path);
		traverse_directory_objects(exporter->cache_path, TEXT("*.idx"), TRAVERSE_FILES, true, find_java_index_files_callback, exporter);
		log_print(LOG_INFO, "Java Plugin: Finished exporting the cache.");	
	}
	terminate_cache_exporter(exporter);
}

enum Busy_Status
{
	BUSY_FALSE = 0,
	BUSY_TRUE = 1
};

enum Incomplete_Status
{
	INCOMPLETE_FALSE = 0,
	INCOMPLETE_TRUE = 1,
	INCOMPLETE_ONHOLD = 2
};

enum Cache_Version
{
	VERSION_602 = 602,
	VERSION_603 = 603,
	VERSION_604 = 604,
	VERSION_605 = 605
};

static const size_t HEADER_SIZE = 128;

struct Index
{
	s8 busy;
	s8 incomplete;
	s32 cache_version;

	// @TODO: union based on the cache_version?
	s8 is_shortcut_image;
	s32 content_length;
	s64 last_modified_time;
	s64 expiry_time;

	s64 _reserved_1;
	s8 _reserved_2;

	s32 section_1_length;
	s32 section_2_length;
	s32 section_3_length;
	s32 section_4_length;

	s64 _reserved_3;
	s64 _reserved_4;
	s8 _reserved_5;

	s32 reduced_manifest_length;
	s32 section_4_pre_15_Length;
	s8 has_only_signed_entries;
	s8 has_single_code_source;
	s32 section_4_certs_length;
	s32 section_4_signers_length;

	s8 _reserved_6;
	s64 _reserved_7;

	s32 reduced_manifest_2_length;
	s8 is_proxied_host;

	u16 version_utf_length;
	TCHAR* version;
	TCHAR* url;
	TCHAR* namespace_id;
	TCHAR* codebase_ip;
};

static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index);
static TRAVERSE_DIRECTORY_CALLBACK(find_java_index_files_callback)
{
	Exporter* exporter = (Exporter*) user_data;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* index_filename = find_data->cFileName;
	Index index = {};
	{
		TCHAR index_path[MAX_PATH_CHARS] = TEXT("");
		PathCombine(index_path, directory_path, index_filename);
		read_index_file(arena, index_path, &index);
	}

	TCHAR content_length[MAX_INT32_CHARS] = TEXT("");
	{
		convert_s32_to_string(index.content_length, content_length);
	}

	TCHAR* cached_filename = push_string_to_arena(arena, index_filename);
	{
		TCHAR* idx_file_extension = skip_to_file_extension(cached_filename, true);
		*idx_file_extension = TEXT('\0');		
	}

	TCHAR* cache_directory_name = PathFindFileName(directory_path);
	TCHAR short_file_path[MAX_PATH_CHARS] = TEXT("");
	StringCchPrintf(short_file_path, MAX_PATH_CHARS, TEXT("%I32d\\%s\\%s"), index.cache_version, cache_directory_name, cached_filename);

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, directory_path, cached_filename);

	Csv_Entry csv_row[CSV_NUM_COLUMNS] =
	{
		{TEXT("TODO")}, {NULL}, {NULL},
		{NULL}, {NULL},
		{NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {content_length}, {NULL},
		// CSV_CODEBASE_IP, CSV_VERSION, CACHE_SIZE?, INVALID_CACHE_ENTRY?
		{short_file_path}, {NULL},
		{NULL}, {NULL}
	};
	
	export_cache_entry(exporter, csv_row, full_file_path, NULL, cached_filename/*, find_data*/);
}

/*void copy_and_advance_bytes(void* destination, void** source, size_t num_bytes)
{
	CopyMemory(destination, *source, num_bytes);
	*source = advance_bytes(*source, num_bytes);
}*/

static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index)
{
	HANDLE index_handle = INVALID_HANDLE_VALUE;
	u64 index_file_size = 0;
	void* index_file = memory_map_entire_file(index_path, &index_handle, &index_file_size);
	
	if(index_file == NULL)
	{
		safe_close_handle(&index_handle);
		return;		
	}

	u32 total_bytes_read = 0;

	#define ADVANCE_BYTES(num_bytes)\
	do\
	{\
		if(total_bytes_read >= index_file_size) break;\
		\
		index_file = advance_bytes(index_file, num_bytes);\
		total_bytes_read += num_bytes;\
	} while(false, false)

	#define COPY_AND_ADVANCE_BYTES(member)\
	do\
	{\
		if(total_bytes_read >= index_file_size) break;\
		\
		CopyMemory(&(index->member), index_file, sizeof(index->member));\
		index->member = swap_byte_order(index->member);\
		\
		index_file = advance_bytes(index_file, sizeof(index->member));\
		total_bytes_read += sizeof(index->member);\
	} while(false, false)

	#define COPY_MODIFIED_UTF_8_STRING_AND_ADVANCE_BYTES(member)\
	do\
	{\
		\
	} while(false, false)

	// Read the first bytes in section 1.
	COPY_AND_ADVANCE_BYTES(busy);
	COPY_AND_ADVANCE_BYTES(incomplete);
	COPY_AND_ADVANCE_BYTES(cache_version);

	// Read the remaining bytes in section 1, whose layout depends
	// on the current cache format version.
	switch(index->cache_version)
	{
		case(VERSION_605):
		{
			COPY_AND_ADVANCE_BYTES(is_shortcut_image);
			COPY_AND_ADVANCE_BYTES(content_length);
			COPY_AND_ADVANCE_BYTES(last_modified_time);
			COPY_AND_ADVANCE_BYTES(expiry_time);

			COPY_AND_ADVANCE_BYTES(_reserved_1);
			COPY_AND_ADVANCE_BYTES(_reserved_2);

			COPY_AND_ADVANCE_BYTES(section_1_length);
			COPY_AND_ADVANCE_BYTES(section_2_length);
			COPY_AND_ADVANCE_BYTES(section_3_length);
			COPY_AND_ADVANCE_BYTES(section_4_length);

			COPY_AND_ADVANCE_BYTES(_reserved_3);
			COPY_AND_ADVANCE_BYTES(_reserved_4);
			COPY_AND_ADVANCE_BYTES(_reserved_5);

			COPY_AND_ADVANCE_BYTES(reduced_manifest_length);
			COPY_AND_ADVANCE_BYTES(section_4_pre_15_Length);
			COPY_AND_ADVANCE_BYTES(has_only_signed_entries);
			COPY_AND_ADVANCE_BYTES(has_single_code_source);
			COPY_AND_ADVANCE_BYTES(section_4_certs_length);
			COPY_AND_ADVANCE_BYTES(section_4_signers_length);

			COPY_AND_ADVANCE_BYTES(_reserved_6);
			COPY_AND_ADVANCE_BYTES(_reserved_7);

			COPY_AND_ADVANCE_BYTES(reduced_manifest_2_length);
			COPY_AND_ADVANCE_BYTES(is_proxied_host);

			u32 header_padding_size = MIN(HEADER_SIZE - total_bytes_read, 0);
			ADVANCE_BYTES(header_padding_size);

			// Read section 2 if it exists.
			if(index->section_2_length > 0)
			{
				COPY_AND_ADVANCE_BYTES(version_utf_length);
				if(index->version_utf_length > 0)
				{
					// @Docs: https://docs.oracle.com/javase/7/docs/api/java/io/DataInput.html#readUTF()
					char* modified_utf_8_str = (char*) index_file;
					char* utf_8_str = push_arena(arena, index->version_utf_length + 1, char);
					u16 actual_length = 0;
					for(u16 i = 0; i < index->version_utf_length; ++i)
					{
						char a = modified_utf_8_str[i];
						// Matches the pattern 0xxx.xxxx, where the mask is 1000.0000 (0x80)
						// and the pattern is 0000.0000 (0x00).
						if((a & 0x80) == 0x00)
						{
							utf_8_str[actual_length] = a;
						}
						// Matches the pattern 110x.xxxx, where the mask is 0010.0000 (0x20)
						// and the pattern is 1100.0000 (0xC0).
						else if((a & 0x20) == 0xC0)
						{
							if(i+1 < index->version_utf_length)
							{
								char b = modified_utf_8_str[i+1];
								// Matches the pattern 10xx.xxxx, where the mask is 0100.0000 (0x40)
								// and the pattern is 1000.0000 (0x80). 
								if((b & 0x40) == 0x80)
								{
									utf_8_str[actual_length] = ((a & 0x1F) << 6) | (b & 0x3F);
									i += 1;
								}
								else
								{
									// Error
									_ASSERT(false);
								}
							}
							else
							{
								// Error
								_ASSERT(false);
							}
						}
						// Matches the pattern 1110.xxxx, where the mask is 0001.0000 (0x10)
						// and the pattern is 1110.0000 (0xE0).
						else if((a & 0x10) == 0xE0)
						{
							if(i+2 < index->version_utf_length)
							{
								char b = modified_utf_8_str[i+1];
								char c = modified_utf_8_str[i+2];
								// Matches the pattern 10xx.xxxx, where the mask is 0100.0000 (0x40)
								// and the pattern is 1000.0000 (0x80). 
								if( ((b & 0x40) == 0x80) && ((c & 0x40) == 0x80) )
								{
									utf_8_str[actual_length] = ((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F);
									i += 2;
								}
								else
								{
									// Error
									_ASSERT(false);
								}
							}
							else
							{
								// Error
								_ASSERT(false);
							}
						}
						else
						{
							// Error
							_ASSERT(false);
						}

						++actual_length;
					}
					utf_8_str[actual_length] = '\0';

					index->version = convert_utf_8_string_to_tchar(arena, utf_8_str);
				}
			}

		} break;

		default:
		{
			_ASSERT(false);
		} break;
	}

	safe_unmap_view_of_file((void**) &index_file);
	safe_close_handle(&index_handle);
}
