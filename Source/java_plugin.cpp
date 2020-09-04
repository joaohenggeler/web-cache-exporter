#include "web_cache_exporter.h"
#include "java_plugin.h"

/*
	@TODO: 
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

// A helper enumeration used to tell what kind of cache location we're in while iterating over the found index files.
// In earlier cache formats there were separate directories for files (images, sounds, and classes) and for archives (ZIPs and JARs).
enum Location_Type
{
	LOCATION_ALL = 0, // For any type of file.

	LOCATION_FILES = 1, // For .class, .gif, .jpg, .au, .wav files only. Used by older cache formats.
	LOCATION_ARCHIVES = 2 // For .zip and .jar files only. Used by older cache formats.
};

enum Cache_Version
{
	// @Decompiled: Taken from sun.plugin.cache.Cache (Java 1.4).
	VERSION_4 = 16,
	// @Decompiled: Taken from XXX (Java 8).
	VERSION_602 = 602,
	VERSION_603 = 603,
	VERSION_604 = 604,
	VERSION_605 = 605
};

static const size_t VERSION_6_HEADER_SIZE = 128;

struct Index
{
	// >>>> Version 4 only.

	s32 file_type;

	// >>>> Version 6 combined with a few attributes from earlier versions.

	s8 busy;
	s8 incomplete;
	s32 cache_version;

	s8 is_shortcut_image;
	s32 content_length;
	s64 last_modified_time; // In milliseconds.
	s64 expiry_time; // In milliseconds.

	// s32 section_1_length = VERSION_6_HEADER_SIZE
	s32 section_2_length;
	s32 section_3_length;
	s32 section_4_length;

	s32 reduced_manifest_length;
	s32 section_4_pre_15_Length;
	s8 has_only_signed_entries;
	s8 has_single_code_source;
	s32 section_4_certs_length;
	s32 section_4_signers_length;

	s32 reduced_manifest_2_length;
	s8 is_proxied_host;

	TCHAR* version;
	TCHAR* url;
	TCHAR* namespace_id;
	TCHAR* codebase_ip;

	// Only the following HTTP headers are allowed: "content-length", "last-modified", "expires", "content-type", "content-encoding",
	// "date", "server". @Decompiled: sun.plugin.cache.CachedFileLoader (Java 1.4).

	TCHAR* response;
	TCHAR* server;
	TCHAR* cache_control;
	TCHAR* pragma;
	TCHAR* content_type;
	TCHAR* content_encoding;
};

struct Find_Filename_Result
{
	bool was_found;
	TCHAR* result_buffer;
};

static TRAVERSE_DIRECTORY_CALLBACK(find_cached_filename_that_starts_with_callback)
{
	Find_Filename_Result* result = (Find_Filename_Result*) user_data;
	result->was_found = !string_ends_with(find_data->cFileName, TEXT(".idx"))
					&& SUCCEEDED(StringCchCopy(result->result_buffer, MAX_PATH_CHARS, find_data->cFileName));
	return !result->was_found;
}

static bool find_cached_filename_that_starts_with(const TCHAR* directory_path, const TCHAR* filename_prefix, TCHAR* result_filename)
{
	TCHAR search_query[MAX_PATH_CHARS] = TEXT("");
	StringCchCopy(search_query, MAX_PATH_CHARS, filename_prefix);
	StringCchCat(search_query, MAX_PATH_CHARS, TEXT("*"));

	Find_Filename_Result result = {};
	result.result_buffer = result_filename;
	traverse_directory_objects(	directory_path, search_query, TRAVERSE_FILES, false,
								find_cached_filename_that_starts_with_callback, &result);
	return result.was_found;
}

static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index, Location_Type location_type);
static TRAVERSE_DIRECTORY_CALLBACK(find_java_index_files_callback)
{
	Exporter* exporter = (Exporter*) user_data;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* directory_name = PathFindFileName(directory_path);
	Location_Type location_type = LOCATION_ALL;

	if(string_starts_with(directory_name, TEXT("file"), true))
	{
		location_type = LOCATION_FILES;
	}
	else if(string_starts_with(directory_name, TEXT("jar"), true))
	{
		location_type = LOCATION_ARCHIVES;
	}

	TCHAR* index_filename = find_data->cFileName;
	Index index = {};
	{
		PathCombine(exporter->index_path, directory_path, index_filename);
		read_index_file(arena, exporter->index_path, &index, location_type);
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

	// How we find the cached filename depends on the cache version.
	//
	// On older format versions where there's separate directories for the type of file, the cached file has the same name as the index
	// but with its original file extension (e.g. ".class") instead of ".idx".
	//
	// On newer versions cache formats where every type of file is allowed, the cached file has the same name as the index but without
	// the ".idx" file extension.
	//
	// Note that the older cache directroy may still exist in a newer Java version. For example, if a user updated their Java version
	// and their cache version was upgraded from one format to the other.
	TCHAR cached_filename[MAX_PATH_CHARS] = TEXT("");
	StringCchCopy(cached_filename, MAX_PATH_CHARS, index_filename);
	{
		// Remove the .idx file extension:
		// - "ABCDEFGH-12345678.idx" -> "ABCDEFGH-12345678".
		// - "file.ext-ABCDEFGH-12345678.idx" -> "file.ext-ABCDEFGH-12345678" (not the actual filename though).
		TCHAR* idx_file_extension = skip_to_file_extension(cached_filename, true);
		*idx_file_extension = TEXT('\0');

		// The above works for the newer version, but for the older ones (the file or archive cache) we still need to determine
		// the actual filename by appending the file extension. Otherwise, we won't be able to copy the file.
		if(location_type != LOCATION_ALL)
		{
			// Attempt to find the file extension using the filename (which is determined using the URL).
			TCHAR* actual_file_extension = skip_to_file_extension(filename, true);

			if(actual_file_extension != NULL)
			{
				// If it worked, add the file extension to build the actual filename.
				// This applies to the older cache directories that exist in their original Java versions.
				StringCchCat(cached_filename, MAX_PATH_CHARS, actual_file_extension);
			}
			else
			{
				// If that fails, take the time to search on disk for the actual filename.
				// This applies to the older cache directories that still exist in newer Java versions.
				TCHAR actual_filename[MAX_PATH_CHARS] = TEXT("");
				if(find_cached_filename_that_starts_with(directory_path, cached_filename, actual_filename))
				{
					StringCchCopy(cached_filename, MAX_PATH_CHARS, actual_filename);
				}
			}

			// Another thing to take care of is that the filename to show in the first column may be NULL if the URL data wasn't
			// stored in the index. Since the cached filename is something like "file.ext-ABCDEFGH-12345678.ext", we can truncate
			// this string to find a good representation of the resource's name.
			// E.g. "file.ext-ABCDEFGH-12345678.ext" -> "file.ext".
			if(filename == NULL)
			{
				// This applies to the older cache directories that still exist in newer Java versions.
				filename = push_string_to_arena(arena, cached_filename);
				TCHAR* last_dash = _tcsrchr(filename, TEXT('-'));
				if(last_dash != NULL)
				{
					*last_dash = TEXT('\0');
					last_dash = _tcsrchr(filename, TEXT('-'));
					
				}
				if(last_dash != NULL)
				{
					*last_dash = TEXT('\0');
				}				
			}
		}
	}

	TCHAR short_file_path[MAX_PATH_CHARS] = TEXT("");
	s32 format_version = index.cache_version;
	if(location_type != LOCATION_ALL)
	{
		format_version = 4;
	}

	StringCchPrintf(short_file_path, MAX_PATH_CHARS, TEXT("v%I32d\\%s\\%s"), format_version, directory_name, cached_filename);

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

	return true;
}

// Converts a modified UTF-8 string to a TCHAR one.
//
// @Docs: The character conversion method was taken from the description of the java.io.DataInput.readUTF() method in the Java API
// Specification: https://docs.oracle.com/javase/8/docs/api/java/io/DataInput.html#readUTF--
//
// @Parameters:
// 1. arena - The Arena structure that will receive the converted TCHAR string.
// 3. modified_utf_8_string - The modified UTF-8 string to convert. This string is not null terminated.
// 3. utf_length - The size of the modified UTF-8 string. This value is an unsigned 16-bit integer that is stored before the string
// data, and is called the 'UTF length' according to the documentation.
//
// @Returns: The pointer to the TCHAR string on success. Otherwise, it returns NULL.
static TCHAR* convert_modified_utf_8_string_to_tchar(Arena* arena, const char* modified_utf_8_string, u16 utf_length)
{
	if(utf_length == 0) return NULL;

	// This UTF-16 string will be the same length or smaller than the modified UTF-8 one. In the worst memory case, all character
	// groups are represented by one byte, meaning the UTF length matches the actual string length.
	wchar_t* utf_16_string = push_arena(arena, (utf_length + 1) * sizeof(wchar_t), wchar_t);
	u16 utf_16_index = 0;
	
	for(u16 i = 0; i < utf_length; ++i)
	{
		// We consider groups of one, two, or three bytes, where each group corresponds to a character.

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
		if(WideCharToMultiByte(CP_UTF8, 0, utf_16_string, -1, ansi_string, size_required_ansi, NULL, NULL) == 0)
		{
			log_print(LOG_ERROR, "Copy Modified Utf-8 String To Tchar: Failed to convert the intermediate UTF-16 string '%ls' to an ANSI string with the error code %lu.", utf_16_string, GetLastError());
			return NULL;
		}

		return ansi_string;
	#else
		return utf_16_string;
	#endif
}

static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index, Location_Type location_type)
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

	/*if(index_file_size < VERSION_6_HEADER_SIZE)
	{
		log_print(LOG_ERROR, "Read Index File: The size of the opened index file is smaller than the file format's header. No files will be exported using this index.");
		safe_unmap_view_of_file((void**) &index_file);
		safe_close_handle(&index_handle);
		return;
	}*/

	// @ByteOrder: Big Endian.

	u32 total_bytes_read = 0;

	// Helper macro function used to skip a number of bytes in the file. This may be used to
	// emulate the behavior of the skipBytes() function from java.io.DataInput, or any other
	// read function that doesn't use its return value.
	#define SKIP_BYTES(num_bytes)\
	do\
	{\
		if(total_bytes_read >= index_file_size) break;\
		\
		index_file = advance_bytes(index_file, num_bytes);\
		total_bytes_read += num_bytes;\
	} while(false, false)

	// Helper macro function used to read an integer of any size from the current file position.
	// This emulates the behavior of the following functions from java.io.DataInput: readByte(),
	// readInt(), readLong(), etc.
	#define READ_INTEGER(variable)\
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

	// Helper macro function used to read a string that was encoded using modified UTF-8 from
	// the current file position. This emulates the behavior of the readUTF() function from
	// java.io.DataInput, where first processes the string's size and then its contents. The
	// variable passed to this macro must be a TCHAR string.
	#define READ_STRING(variable)\
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

	#define READ_HEADERS(codebase_ip_key)\
	do\
	{\
		s32 num_headers = 0;\
		READ_INTEGER(num_headers);\
		\
		for(s32 i = 0; i < num_headers; ++i)\
		{\
			TCHAR* key = NULL;\
			TCHAR* value = NULL;\
			READ_STRING(key);\
			READ_STRING(value);\
			\
			if(key == NULL || value == NULL) continue;\
			\
			if(codebase_ip_key != NULL && strings_are_equal(key, codebase_ip_key, true))\
			{\
				index->codebase_ip = value;\
			}\
			else if(strings_are_equal(key, TEXT("<null>"), true))\
			{\
				index->response = value;\
			}\
			else if(strings_are_equal(key, TEXT("server"), true))\
			{\
				index->server = value;\
			}\
			else if(strings_are_equal(key, TEXT("cache-control"), true))\
			{\
				index->cache_control = value;\
			}\
			else if(strings_are_equal(key, TEXT("pragma"), true))\
			{\
				index->pragma = value;\
			}\
			else if(strings_are_equal(key, TEXT("content-type"), true))\
			{\
				index->content_type = value;\
			}\
			else if(strings_are_equal(key, TEXT("content-encoding"), true))\
			{\
				index->content_encoding = value;\
			}\
		}\
	} while(false, false)

	#define READ_SECTION_2(...)\
	do\
	{\
		if(index->section_2_length > 0)\
		{\
			READ_STRING(index->version);\
			READ_STRING(index->url);\
			READ_STRING(index->namespace_id);\
			READ_STRING(index->codebase_ip);\
			\
			READ_HEADERS(NULL);\
		}\
	} while(false, false)

	// Read the first bytes in the header.

	s8 first_byte = 0;
	READ_INTEGER(first_byte);

	// @Decompiled: In package sun.plugin.cache.* (Java 1.4).
	// See FileCache.verifyFile() -> readHeaderFields() and CachedFileLoader.createCacheFiles() -> writeHeaders().
	// See JarCache.verifyFile() and CachedJarLoader.authenticateFromCache() and authenticate().
	if(first_byte == VERSION_4)
	{
		READ_STRING(index->url);
		READ_INTEGER(index->last_modified_time);
		READ_INTEGER(index->expiry_time);
		READ_INTEGER(index->file_type);

		if(location_type == LOCATION_FILES)
		{
			READ_HEADERS(TEXT("plugin_resource_codebase_ip"));
		}
		else if(location_type == LOCATION_ARCHIVES)
		{
			READ_STRING(index->version);
		}
		else
		{
			// @Assert: We should never get here for the older cache formats.
			_ASSERT(false);
		}
	}
	// @Decompiled: In package com.sun.deploy.cache.* (Java 8).
	else
	{
		index->busy = first_byte;
		READ_INTEGER(index->incomplete);
		READ_INTEGER(index->cache_version);

		// Read the remaining bytes in the header, whose layout depends on the previously read cache version.
		switch(index->cache_version)
		{
			// @Decompiled: See CacheEntry.readIndexFile() -> readSection1Remaining() -> readSection2() -> readHeaders().
			case(VERSION_605):
			{
				READ_INTEGER(index->is_shortcut_image);
				READ_INTEGER(index->content_length);
				READ_INTEGER(index->last_modified_time);
				READ_INTEGER(index->expiry_time);

				SKIP_BYTES(sizeof(s64));
				SKIP_BYTES(sizeof(s8));

				READ_INTEGER(index->section_2_length);
				READ_INTEGER(index->section_3_length);
				READ_INTEGER(index->section_4_length);

				SKIP_BYTES(sizeof(s64));
				SKIP_BYTES(sizeof(s64));
				SKIP_BYTES(sizeof(s8));

				READ_INTEGER(index->reduced_manifest_length);
				READ_INTEGER(index->section_4_pre_15_Length);
				READ_INTEGER(index->has_only_signed_entries);
				READ_INTEGER(index->has_single_code_source);
				READ_INTEGER(index->section_4_certs_length);
				READ_INTEGER(index->section_4_signers_length);

				SKIP_BYTES(sizeof(s8));
				SKIP_BYTES(sizeof(s64));

				READ_INTEGER(index->reduced_manifest_2_length);
				READ_INTEGER(index->is_proxied_host);

				if(total_bytes_read < VERSION_6_HEADER_SIZE)
				{
					u32 header_padding_size = VERSION_6_HEADER_SIZE - total_bytes_read;
					SKIP_BYTES(header_padding_size);		
				}

				READ_SECTION_2();

				_ASSERT( (VERSION_6_HEADER_SIZE + index->section_2_length) == total_bytes_read );

			} break;

			// @Decompiled: See CacheEntry.readIndexFileOld() -> readSection1Remaining604() -> readSection2() -> readHeaders().
			case(VERSION_604):
			case(VERSION_603):
			{
				SKIP_BYTES(sizeof(s8));
				SKIP_BYTES(sizeof(s8));

				READ_INTEGER(index->is_shortcut_image);
				READ_INTEGER(index->content_length);
				READ_INTEGER(index->last_modified_time);
				READ_INTEGER(index->expiry_time);

				SKIP_BYTES(sizeof(s64));
				SKIP_BYTES(sizeof(s8));

				READ_INTEGER(index->section_2_length);
				READ_INTEGER(index->section_3_length);
				READ_INTEGER(index->section_4_length);

				SKIP_BYTES(sizeof(s64));
				SKIP_BYTES(sizeof(s64));
				SKIP_BYTES(sizeof(s8));

				READ_INTEGER(index->reduced_manifest_length);
				READ_INTEGER(index->section_4_pre_15_Length);

				SKIP_BYTES(sizeof(s8));
				SKIP_BYTES(sizeof(s8));

				READ_INTEGER(index->section_4_certs_length);
				READ_INTEGER(index->section_4_signers_length);

				SKIP_BYTES(sizeof(s8));
				SKIP_BYTES(sizeof(s64));

				READ_INTEGER(index->reduced_manifest_2_length);

				READ_SECTION_2();

			} break;

			// @Decompiled: See CacheEntry.readIndexFileOld() -> readIndexFile602() -> readHeaders602().
			case(VERSION_602):
			{
				SKIP_BYTES(sizeof(u8));
				SKIP_BYTES(sizeof(u8));

				READ_INTEGER(index->is_shortcut_image);
				READ_INTEGER(index->content_length);
				READ_INTEGER(index->last_modified_time);
				READ_INTEGER(index->expiry_time);

				READ_STRING(index->version);
				READ_STRING(index->url);
				READ_STRING(index->namespace_id);

				READ_HEADERS(TEXT("deploy_resource_codebase_ip"));

			} break;

			default:
			{
				log_print(LOG_ERROR, "Read Index File: Unsupported cache version %I32d in the index file '%s'.", index->cache_version, index_path);
			} break;
		}
	}

	safe_unmap_view_of_file((void**) &index_file);
	safe_close_handle(&index_handle);
}

/*
	@Resources: The Java API Specification - https://docs.oracle.com/javase/8/docs/api/index.html

	Primitive Types in Java:
	- byte 	= 1 byte (signed) 		= s8
	- short = 2 bytes (signed) 		= s16
	- int 	= 4 bytes (signed) 		= s32
	- long 	= 8 bytes (signed) 		= s64
	- char 	= 2 bytes (unsigned) 	= u16

	Reading and writing these primitives is done using the methods in the java.io.DataInput and DataOutput interfaces.
	Strings are serialized using the modified UTF-8 character encoding.
	- https://docs.oracle.com/javase/8/docs/api/java/io/DataInput.html
	- https://docs.oracle.com/javase/8/docs/api/java/io/DataOutput.html
	
	@ByteOrder: Big Endian.

	@Resources: Decompiled Java classes related to the web plugin and its cache. Note that the Java Plugin and Java Web Start
	are not included in OpenJDK.

	@Decompiled: "jre\lib\jaws.jar" in JDK 1.3.1 update 28.
	{
		@Class: "sun.plugin.cachescheme.PluginCacheTable"
		String userHome = (String) AccessController.doPrivileged(new GetPropertyAction("user.home"));
		String cacheHome = userHome + File.separator + "java_plugin_AppletStore" + File.separator + System.getProperty("javaplugin.version");
	
		@Class: "sun.plugin.cachescheme.PluginJarCacheTable"
		String jarCacheHome = PluginCacheTable.cacheHome + File.separator + "jar";
	  	public static String getCacheHomeDir()
		{
			return jarCacheHome;
		}

		@Class: "sun.plugin.cachescheme.PluginJarCacheHandler"
		private File generateCacheFile(String paramString1, String paramString2) throws JarCacheException
 		{
 			[...]
	  		str1 = str1 + str2.hashCode() + paramString2 + String.valueOf(localRandom.nextLong()) + ".jar";
      		localFile = new File(PluginJarCacheTable.getCacheHomeDir() + File.separator + str1);
      		[...]
      		return localFile;
 		}

		@Docs: The Java 1.3.1's documentation - "Applet Caching in Java Plug-in".

		"Java Plug-in has supported caching in previous versions by using the same cache the browser uses for all other web documents."

		"This release introduces an alternative form of applet caching which allows an applet deployer to decide her applet should be
		"sticky", that is, to stay on the disk in a secondary cache which the browser cannot overwrite. The only time "sticky" applets
		get downloaded after that is when they are updated on their server. Otherwise the applet is always available for quick loading."

		"This new feature is activated by including the new PARAM NAME="cache_option" and PARAM NAME="cache_archive" values in the tag
		that specifies the use of Java Plug-in as below:""
		<OBJECT ....>
			<PARAM NAME="archive" VALUE="...">
			....
			<PARAM NAME="cache_option" VALUE="...">
			<PARAM NAME="cache_archive" VALUE="...">
		</OBJECT>

		"The cache_option attribute can take one of three values:"
		- No: Disable applet installation. Always download the file from the web server.
		- Browser: Run applets from the browser cache (default).
		- Plugin: Run applets from the new Java Plug-in cache.

		"The cache_archive attribute contains a list of the files to be cached:"
			<PARAM NAME="cache_archive" VALUE="a.jar,b.jar,c.jar">

		"Note that the list of JAR files in the cache_archive attribute and those in the archive attribute may be similar but should not
		contain the same JAR files. There are two possible cases:"
		1. A JAR file is listed in cache_archive but not in archive. In this case, the JAR file is cached according to cache_option.
		2. A JAR file is listed in archive but not in cache_archive. In this case, the JAR file is cached using the native browser cache.
		This guarantees a minimum of caching.

		@Docs: The book "Solaris Java Plug-in User's Guide" - Chapter 8 "Applet Caching and Installation in Java Plug-in".

		"The cache_version is an optional attribute. If used, it contains a list of file versions to be cached:"
			<PARAM NAME="cache_version" VALUE="1.2.0.1, 2.1.1.2, 1.1.2.7">

		"In order to allow pre-loading of jar files, HTML parameter cache_archive_ex can be used, This parameter allows you to specify
		whether the jar file needs to be pre-loaded; optionally the version of the jar file can also be specified. The value of
		cache_archive_ex has the following format:"
			cache_archive_ex = "<jar_file_name>;<preload(optional)>;
								<jar_file_version>,<jar_file_name>;
								<preload(optional>;<jar_file_version(optional>,...."

		<OBJECT .... >
			<PARAM NAME="archive" VALUE="a.jar">
			<PARAM NAME="cache_archive" VALUE="b.jar, c.jar, d.jar">
			<PARAM NAME="cache_version" VALUE="0.0.0.1, 0.0.A.B, 0.A.B.C">
			<PARAM NAME="cache_archive_ex" VALUE="applet.jar;preload, util.jar;preload;0.9.0.abc, tools.jar;0.9.8.7">
		</OBJECT>

		Cache Update Algorithm

		"By default, without the cache_version attribute, applet caching will be updated if:"
		- The cache_archive has not been cached before, or
		- The "Last-Modified" value of the cache_archive on the web server is newer than the one stored locally in the applet cache, or
		- The "Content-Length" of the cache_archive on the web server is different than the one stored locally in the applet cache.

	}

	@Decompiled: "jre\lib\plugin.jar" in JDK 1.4.2 update 19. Index files appear to have been introduced in Java 1.4.
	This format looks to be the same in "jre\lib\plugin.jar" in JDK 1.5.0 update 22.
	{
		@Class: "com.sun.deploy.config.Config" in "jre\lib\deploy.jar" in JDK 1.5.0 update 22.
		public static String getCacheDirectory()
		{
		    return getProperty("deployment.user.dir");
		}

		@Class: "sun.plugin.util.PluginConfig"
  		private static final String JAVAPI = "javapi";
		private static final String CACHE_VERSION = "v1.0";

		private String getCacheDirectorySubStructure()
		{
		    return "javapi" + File.separator + "v1.0";
		}

		public String getPluginCacheDirectory()
		{
		    return Config.getCacheDirectory() + File.separator + getCacheDirectorySubStructure();
		}

		@Class: "sun.plugin.cache.Cache"
		protected static String generateCacheFileName(File paramFile, URL paramURL) throws IOException
		{
	  		[...]
			str2 = str1 + Integer.toString(getRandom(), 16);
			localFile1 = new File(paramFile, str2 + getFileExtension(paramURL.toString()));
			localFile2 = new File(paramFile, str2 + ".idx");
	   		return str2;
	 	}

	 	protected static final File getIndexFile(File paramFile, URL paramURL)
		{
			String str = paramFile.getName();
			str = str.substring(0, str.length() - getFileExtension(paramURL.toString()).length());
			str = str + ".idx";
			return new File(paramFile.getParentFile(), str);
		}

		protected static final File getDataFile(File paramFile, String paramString)
		{
			String str = paramFile.getName();
			str = str.substring(0, str.length() - ".idx".length());
			str = str + getFileExtension(paramString);
			return new File(paramFile.getParentFile(), str);
		}

		protected static final String getFileExtension(String paramString)
		{
			String str = "";

			int i = paramString.lastIndexOf('.');
			if (i != -1) {
				str = paramString.substring(i);
			}
			if ((str.equalsIgnoreCase(".jar")) || (str.equalsIgnoreCase(".jarjar"))) {
				str = ".zip";
			}
			return str;
		}
	}

	@Decompiled: "jre\lib\deploy.jar" in JDK 8 update 181.
	{
		@Class: "com.sun.deploy.cache.CacheEntry"
	}
*/
