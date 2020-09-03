#include "web_cache_exporter.h"
#include "java_plugin.h"

/*




	@Decompiled: "sun.plugin.cachescheme.PluginCacheTable" from "jre\lib\jaws.jar" in JDK 1.3.1 update 28

	public static String cacheHome = userHome + File.separator + "java_plugin_AppletStore" + File.separator + System.getProperty("javaplugin.version");

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
			byte isShortcutImage = stream1.readByte(); // @Struct: s8
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
	CSV_CODEBASE_IP, CSV_VERSION,
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

	s8 is_shortcut_image;
	s32 content_length;
	s64 last_modified_time; // In milliseconds.
	s64 expiry_time; // In milliseconds.

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

	TCHAR* version;
	TCHAR* url;
	TCHAR* namespace_id;
	TCHAR* codebase_ip;

	TCHAR* response;
	TCHAR* server;
	TCHAR* cache_control;
	TCHAR* pragma;
	TCHAR* content_type;
	TCHAR* content_encoding;
};

static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index);
static TRAVERSE_DIRECTORY_CALLBACK(find_java_index_files_callback)
{
	Exporter* exporter = (Exporter*) user_data;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* index_filename = find_data->cFileName;
	Index index = {};
	{
		PathCombine(exporter->index_path, directory_path, index_filename);
		read_index_file(arena, exporter->index_path, &index);
	}

	TCHAR* url = index.url;
	TCHAR* filename = PathFindFileName(url);

	// Note that the time information is stored in milliseconds while time_t is measured in seconds.
	TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	format_time64_t_date_time(index.last_modified_time / 1000, last_modified_time);

	TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	format_time64_t_date_time(index.expiry_time / 1000, expiry_time);

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
		{/* Filename */}, {/* URL */}, {/* File Extension */},
		{last_modified_time}, {expiry_time},
		{index.response}, {index.server}, {index.cache_control}, {index.pragma}, {index.content_type}, {content_length}, {index.content_encoding},
		{index.codebase_ip}, {index.version},
		{short_file_path}, {/* Missing File */},
		{/* Custom File Group */}, {/* Custom URL Group */}
	};
	
	export_cache_entry(exporter, csv_row, full_file_path, url, filename);
}

static TCHAR* convert_modified_utf_8_string_to_tchar(Arena* arena, const char* modified_utf_8_string, u16 utf_length)
{
	if(utf_length == 0) return NULL;

	wchar_t* utf_16_string = push_arena(arena, (utf_length + 1) * sizeof(wchar_t), wchar_t);
	u16 utf_16_index = 0;
	
	for(u16 i = 0; i < utf_length; ++i)
	{
		char a = modified_utf_8_string[i];
		// Matches the pattern 0xxx.xxxx, where the mask is 1000.0000 (0x80)
		// and the pattern is 0000.0000 (0x00).
		if((a & 0x80) == 0x00)
		{
			utf_16_string[utf_16_index] = a;
		}
		// Matches the pattern 110x.xxxx, where the mask is 1110.0000 (0xE0)
		// and the pattern is 1100.0000 (0xC0).
		else if((a & 0xE0) == 0xC0)
		{
			if(i+1 < utf_length)
			{
				char b = modified_utf_8_string[i+1];
				// Matches the pattern 10xx.xxxx, where the mask is 1100.0000 (0xC0)
				// and the pattern is 1000.0000 (0x80). 
				if((b & 0xC0) == 0x80)
				{
					utf_16_string[utf_16_index] = ((a & 0x1F) << 6) | (b & 0x3F);
					i += 1;
				}
				else
				{
					log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. The second byte (0x%08X) does not match the pattern.", modified_utf_8_string, b);
					return NULL;
				}
			}
			else
			{
				log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. Missing the second byte in the group.", modified_utf_8_string);
				return NULL;
			}
		}
		// Matches the pattern 1110.xxxx, where the mask is 1111.0000 (0xF0)
		// and the pattern is 1110.0000 (0xE0).
		else if((a & 0xF0) == 0xE0)
		{
			if(i+2 < utf_length)
			{
				char b = modified_utf_8_string[i+1];
				char c = modified_utf_8_string[i+2];
				// Matches the pattern 10xx.xxxx, where the mask is 1100.0000 (0xC0)
				// and the pattern is 1000.0000 (0x80). 
				if( ((b & 0xC0) == 0x80) && ((c & 0xC0) == 0x80) )
				{
					utf_16_string[utf_16_index] = ((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F);
					i += 2;
				}
				else
				{
					log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. The second (0x%08X) or third byte (0x%08X) does not match the pattern.", modified_utf_8_string, b, c);
					return NULL;
				}
			}
			else
			{
				log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. Missing the second or third byte in the group.", modified_utf_8_string);
				return NULL;
			}
		}
		else
		{
			log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. The first byte (0x%08X) does not match any pattern.", modified_utf_8_string, a);
			return NULL;
		}

		++utf_16_index;
	}

	utf_16_string[utf_16_index] = L'\0';

	#ifdef BUILD_9X

		int size_required_ansi = WideCharToMultiByte(CP_ACP, 0, utf_16_string, -1, NULL, 0, NULL, NULL);
		if(size_required_ansi == 0)
		{
			log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Failed to find the number of bytes necessary to represent the intermediate UTF-16 '%ls' as an ANSI string with the error code %lu.", utf_16_string, GetLastError());
			return NULL;
		}

		char* ansi_string = push_arena(arena, size_required_ansi, char);
		if(WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, ansi_string, size_required_ansi, NULL, NULL) == 0)
		{
			log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Failed to convert the intermediate UTF-16 string '%ls' to an ANSI string with the error code %lu.", utf_16_string, GetLastError());
			return NULL;
		}

		return ansi_string;
	#else
		return utf_16_string;
	#endif
}

static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index)
{
	HANDLE index_handle = INVALID_HANDLE_VALUE;
	u64 index_file_size = 0;
	void* index_file = memory_map_entire_file(index_path, &index_handle, &index_file_size);
	
	if(index_file == NULL)
	{
		log_print(LOG_ERROR, "Read Index File: Failed to open the index file with the error code %lu. No files will be exported using this index.", GetLastError());
		safe_close_handle(&index_handle);
		return;		
	}

	if(index_file_size < HEADER_SIZE)
	{
		log_print(LOG_ERROR, "Read Index File: The size of the opened index file is smaller than the file format's header. No files will be exported using this index.");
		safe_unmap_view_of_file((void**) &index_file);
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

	#define COPY_INTEGER_AND_ADVANCE_BYTES(variable)\
	do\
	{\
		if(total_bytes_read >= index_file_size) break;\
		\
		CopyMemory(&variable, index_file, sizeof(variable));\
		variable = swap_byte_order(variable);\
		\
		index_file = advance_bytes(index_file, sizeof(variable));\
		total_bytes_read += sizeof(variable);\
	} while(false, false)

	#define COPY_STRING_AND_ADVANCE_BYTES(variable)\
	do\
	{\
		if(total_bytes_read >= index_file_size) break;\
		\
		u16 utf_length = 0;\
		CopyMemory(&utf_length, index_file, sizeof(utf_length));\
		utf_length = swap_byte_order(utf_length);\
		index_file = advance_bytes(index_file, sizeof(utf_length));\
		\
		char* modified_utf_8_string = (char*) index_file;\
		variable = convert_modified_utf_8_string_to_tchar(arena, modified_utf_8_string, utf_length);\
		index_file = advance_bytes(index_file, utf_length);\
		\
		total_bytes_read += sizeof(utf_length) + utf_length;\
	} while(false, false)

	// Read the first bytes in section 1.
	COPY_INTEGER_AND_ADVANCE_BYTES(index->busy);
	COPY_INTEGER_AND_ADVANCE_BYTES(index->incomplete);
	COPY_INTEGER_AND_ADVANCE_BYTES(index->cache_version);

	// Read the remaining bytes in section 1, whose layout depends
	// on the current cache format version.
	switch(index->cache_version)
	{
		case(VERSION_605):
		{
			COPY_INTEGER_AND_ADVANCE_BYTES(index->is_shortcut_image);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->content_length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->last_modified_time);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->expiry_time);

			COPY_INTEGER_AND_ADVANCE_BYTES(index->_reserved_1);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->_reserved_2);

			COPY_INTEGER_AND_ADVANCE_BYTES(index->section_1_length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->section_2_length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->section_3_length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->section_4_length);

			COPY_INTEGER_AND_ADVANCE_BYTES(index->_reserved_3);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->_reserved_4);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->_reserved_5);

			COPY_INTEGER_AND_ADVANCE_BYTES(index->reduced_manifest_length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->section_4_pre_15_Length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->has_only_signed_entries);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->has_single_code_source);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->section_4_certs_length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->section_4_signers_length);

			COPY_INTEGER_AND_ADVANCE_BYTES(index->_reserved_6);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->_reserved_7);

			COPY_INTEGER_AND_ADVANCE_BYTES(index->reduced_manifest_2_length);
			COPY_INTEGER_AND_ADVANCE_BYTES(index->is_proxied_host);

			if(total_bytes_read < HEADER_SIZE)
			{
				u32 header_padding_size = HEADER_SIZE - total_bytes_read;
				ADVANCE_BYTES(header_padding_size);		
			}

			// Read section 1 if it exists.
			if(index->section_1_length > 0)
			{
				// @Docs: https://docs.oracle.com/javase/7/docs/api/java/io/DataInput.html#readUTF()
				COPY_STRING_AND_ADVANCE_BYTES(index->version);
				COPY_STRING_AND_ADVANCE_BYTES(index->url);
				COPY_STRING_AND_ADVANCE_BYTES(index->namespace_id);
				COPY_STRING_AND_ADVANCE_BYTES(index->codebase_ip);

				s32 num_headers = 0;
				COPY_INTEGER_AND_ADVANCE_BYTES(num_headers);

				for(s32 i = 0; i < num_headers; ++i)
				{
					TCHAR* key = NULL;
					TCHAR* value = NULL;
					COPY_STRING_AND_ADVANCE_BYTES(key);
					COPY_STRING_AND_ADVANCE_BYTES(value);

					if(key == NULL || value == NULL) continue;

					if(strings_are_equal(key, TEXT("<null>"), true))
					{
						index->response = value;
					}
					else if(strings_are_equal(key, TEXT("server"), true))
					{
						index->server = value;
					}
					else if(strings_are_equal(key, TEXT("cache-control"), true))
					{
						index->cache_control = value;
					}
					else if(strings_are_equal(key, TEXT("pragma"), true))
					{
						index->pragma = value;
					}
					else if(strings_are_equal(key, TEXT("content-type"), true))
					{
						index->content_type = value;
					}
					else if(strings_are_equal(key, TEXT("content-encoding"), true))
					{
						index->content_encoding = value;
					}
				}
			}

			_ASSERT( (HEADER_SIZE + index->section_1_length) == total_bytes_read );

		} break;

		default:
		{
			_ASSERT(false);
		} break;
	}

	safe_unmap_view_of_file((void**) &index_file);
	safe_close_handle(&index_handle);
}
