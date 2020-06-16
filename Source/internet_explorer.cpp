#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "internet_explorer.h"
#ifndef BUILD_9X
	#include <esent.h>
#endif

/*
	File Format Documentation:
	- http://www.geoffchappell.com/studies/windows/ie/wininet/api/urlcache/indexdat.htm
	- https://github.com/libyal/libmsiecf/blob/master/documentation/MSIE%20Cache%20File%20(index.dat)%20format.asciidoc
	- https://github.com/csuft/QIECacheViewer/blob/master/README.md
	- http://en.verysource.com/code/4134901_1/cachedef.h.html
*/

static const size_t SIGNATURE_SIZE = 28;
static const size_t NUM_CACHE_DIRECTORY_NAME_CHARS = 8;
static const size_t MAX_NUM_CACHE_DIRECTORIES = 32;
static const size_t HEADER_DATA_LENGTH = 32;
static const size_t BLOCK_SIZE = 128;
static const size_t ALLOCATION_BITMAP_SIZE = 0x3DB0;

enum Internet_Explorer_Index_Entry_Signature
{
	ENTRY_URL = 0x204C5255, // "URL "
	ENTRY_REDIRECT = 0x52444552, // "REDR"
	ENTRY_LEAK = 0x4B41454C, // "LEAK"
	ENTRY_HASH = 0x48534148, // "HASH"

	// @Investigate
	ENTRY_DELETED = 0x204C4544, // "DEL "
	ENTRY_UPDATED = 0x20445055, // "UPD "

	ENTRY_NEWLY_ALLOCATED = 0xDEADBEEF,
	ENTRY_DEALLOCATED = 0x0BADF00D
};

#pragma pack(push, 1)

struct Internet_Explorer_Index_Header
{
	s8 signature[SIGNATURE_SIZE]; // Including null terminator.
	u32 file_size;
	u32 file_offset_to_first_hash_table_page;

	u32 num_blocks;
	u32 num_allocated_blocks;
	u32 _reserved_1;
	
	u32 max_size;
	u32 _reserved_2;
	u32 cache_size;
	u32 _reserved_3;
	u32 sticky_cache_size;
	u32 _reserved_4;
	
	u32 num_directories;
	struct
	{
		u32 num_files;
		s8 name[NUM_CACHE_DIRECTORY_NAME_CHARS]; // Without null terminator.
	} cache_directories[MAX_NUM_CACHE_DIRECTORIES];

	u32 header_data[HEADER_DATA_LENGTH];
	
	u32 _reserved_5;
};

struct Internet_Explorer_Index_File_Map_Entry
{
	u32 signature;
	u32 num_allocated_blocks;
};

struct Internet_Explorer_Index_Url_Entry
{
	FILETIME last_modified_time;
	FILETIME last_access_time;
	Dos_Date_Time expiry_time;
	u32 _reserved_1;

	u32 cached_file_size;
	u32 _reserved_2; // @Check: use as high part of cached_file_size?

	u32 file_offset_to_group_or_group_list;
	
	union
	{
		u32 sticky_time_delta; // For a URL entry: number of seconds before the cached item may be released (relative to the last access time).
		u32 file_offset_to_next_leak_entry; // For a LEAK entry.
	};

	u32 _reserved_3;
	u32 entry_offset_to_url;

	u8 cache_directory_index; // 0xFE = special type (cookie/iecompat/iedownload)?, 0xFF = ?
	u8 sync_count;
	u8 format_version; // 0x00 =  IE5_URL_FILEMAP_ENTRY, 0x10 = IE6_URL_FILEMAP_ENTRY
	u8 format_version_copy;

	u32 entry_offset_to_filename;
	u32 cache_flags;
	u32 entry_offset_to_headers;
	u32 headers_size;
	u32 entry_offset_to_file_extension;

	Dos_Date_Time last_sync_time;
	u32 num_entry_locks; // Number of hits
	u32 level_of_entry_lock_nesting;
	Dos_Date_Time creation_time;

	u32 _reserved_4;
	u32 _reserved_5;
};

#pragma pack(pop)

_STATIC_ASSERT(sizeof(Internet_Explorer_Index_Header) == 0x0250);
_STATIC_ASSERT(sizeof(Internet_Explorer_Index_File_Map_Entry) == 0x08);
_STATIC_ASSERT(sizeof(Internet_Explorer_Index_Url_Entry) == 0x60);

// ----------------------------------------------------------------------------------------------------

bool find_internet_explorer_version(TCHAR* ie_version, DWORD ie_version_size)
{
	return query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "svcVersion", ie_version, ie_version_size)
		|| query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "Version", ie_version, ie_version_size);
}


bool find_internet_explorer_cache(TCHAR* cache_path)
{
	#ifdef BUILD_9X
		return SHGetSpecialFolderPath(NULL, cache_path, CSIDL_INTERNET_CACHE, FALSE) == TRUE;
	#else
		return SUCCEEDED(SHGetFolderPath(NULL, CSIDL_INTERNET_CACHE, NULL, SHGFP_TYPE_CURRENT, cache_path));
	#endif
}

void export_specific_or_default_internet_explorer_cache(Exporter* exporter)
{
	if(is_string_empty(exporter->cache_path))
	{
		if(find_internet_explorer_cache(exporter->cache_path))
		{
			const size_t ie_version_size = 32;
			TCHAR ie_version[ie_version_size] = TEXT("");
			find_internet_explorer_version(ie_version, ie_version_size);
			log_print(LOG_INFO, "Internet Explorer: Browser version in the current user is '%s'.", ie_version);
			
			export_specific_internet_explorer_cache(exporter);
		}
		else
		{
			log_print(LOG_ERROR, "Internet Explorer: Failed to get the current Temporary Internet Files directory path.");
		}
		
	}
	else
	{
		export_specific_internet_explorer_cache(exporter);
	}
}

void export_specific_internet_explorer_cache(Exporter* exporter)
{
	Arena* arena = &(exporter->arena);

	TCHAR cache_path[MAX_PATH_CHARS];
	GetFullPathName(exporter->cache_path, MAX_PATH_CHARS, cache_path, NULL);
	log_print(LOG_INFO, "Internet Explorer: Exporting the cache from '%s'.", cache_path);

	TCHAR index_path[MAX_PATH_CHARS];
	StringCchCopy(index_path, MAX_PATH_CHARS, cache_path);
	PathAppend(index_path, TEXT("Content.IE5\\index.dat"));

	TCHAR output_copy_path[MAX_PATH_CHARS];
	GetFullPathName(exporter->output_path, MAX_PATH_CHARS, output_copy_path, NULL);
	PathAppend(output_copy_path, TEXT("InternetExplorer"));
	
	TCHAR output_csv_path[MAX_PATH_CHARS];
	GetFullPathName(exporter->output_path, MAX_PATH_CHARS, output_csv_path, NULL);
	PathAppend(output_csv_path, TEXT("InternetExplorer.csv"));

	u64 index_file_size;
	void* index_file = memory_map_entire_file(index_path, &index_file_size);
	bool was_index_copied_to_temporary_file = false;
	TCHAR temporary_index_path[MAX_PATH_CHARS];

	if(index_file == NULL && GetLastError() == ERROR_SHARING_VIOLATION)
	{
		log_print(LOG_INFO, "Internet Explorer: Failed to open the index file since its being used by another process. Attempting to create a copy and opening that one...");
		
		if(copy_to_temporary_file(index_path, temporary_index_path))
		{
			log_print(LOG_INFO, "Internet Explorer: Copied the index file to '%s'.", temporary_index_path);
			was_index_copied_to_temporary_file = true;
			index_file = memory_map_entire_file(temporary_index_path, &index_file_size);
		}
		else
		{
			log_print(LOG_ERROR, "Internet Explorer: Failed to create a copy of the index file.");
		}
	}

	if(index_file == NULL)
	{
		log_print(LOG_ERROR, "Internet Explorer: Failed to open the index file correctly. No files will be exported from this cache.");
		if(was_index_copied_to_temporary_file) DeleteFile(temporary_index_path);
		return;
	}

	if(index_file_size < sizeof(Internet_Explorer_Index_Header))
	{
		log_print(LOG_ERROR, "Internet Explorer: The size of the opened index file is smaller than the file format's header. No files will be exported from this cache.");
		if(was_index_copied_to_temporary_file) DeleteFile(temporary_index_path);
		_ASSERT(false);
		return;
	}

	Internet_Explorer_Index_Header* header = (Internet_Explorer_Index_Header*) index_file;

	if(strncmp(header->signature, "Client UrlCache MMF Ver ", 24) != 0)
	{
		char signature_string[SIGNATURE_SIZE + 1];
		CopyMemory(signature_string, header->signature, SIGNATURE_SIZE);
		signature_string[SIGNATURE_SIZE] = '\0';

		log_print(LOG_ERROR, "Internet Explorer: The index file starts with an invalid signature: '%hs'. No files will be exported from this cache.", signature_string);
		if(was_index_copied_to_temporary_file) DeleteFile(temporary_index_path);
		_ASSERT(false);
		return;
	}

	if(index_file_size != header->file_size)
	{
		log_print(LOG_ERROR, "Internet Explorer: The size of the opened index file is different than the size specified in its header. No files will be exported from this cache.");
		if(was_index_copied_to_temporary_file) DeleteFile(temporary_index_path);
		_ASSERT(false);
		return;
	}

	log_print(LOG_INFO, "Internet Explorer: The index file was opened successfully. Starting the export process.");

	#ifdef DEBUG
		debug_log_print("Internet Explorer: Header->Reserved_1: 0x%08X", header->_reserved_1);
		debug_log_print("Internet Explorer: Header->Reserved_2: 0x%08X", header->_reserved_2);
		debug_log_print("Internet Explorer: Header->Reserved_3: 0x%08X", header->_reserved_3);
		debug_log_print("Internet Explorer: Header->Reserved_4: 0x%08X", header->_reserved_4);
		debug_log_print("Internet Explorer: Header->Reserved_5: 0x%08X", header->_reserved_5);
	#endif

	const size_t CSV_NUM_COLUMNS = 16;
	const Csv_Type csv_header[CSV_NUM_COLUMNS] =
	{
		CSV_FILENAME, CSV_URL, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
		CSV_LAST_MODIFIED_TIME, CSV_LAST_ACCESS_TIME, CSV_CREATION_TIME, CSV_EXPIRY_TIME,
		CSV_SERVER_RESPONSE, CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_ENCODING, 
		CSV_HITS, CSV_LOCATION_ON_CACHE, CSV_MISSING_FILE, CSV_LEAK_ENTRY
	};
	
	HANDLE csv_file = INVALID_HANDLE_VALUE;
	if(exporter->should_create_csv)
	{
		csv_file = create_csv_file(output_csv_path);
		csv_print_header(arena, csv_file, csv_header, CSV_NUM_COLUMNS);
		clear_arena(arena);
	}

	unsigned char* allocation_bitmap = (unsigned char*) advance_bytes(header, sizeof(Internet_Explorer_Index_Header));
	void* blocks = advance_bytes(allocation_bitmap, ALLOCATION_BITMAP_SIZE);
	
	for(u32 i = 0; i < header->num_blocks; ++i)
	{
		size_t byte_index = i / CHAR_BIT;
		size_t block_index_in_byte = i % CHAR_BIT;

		bool is_block_allocated = ( allocation_bitmap[byte_index] & (1 << block_index_in_byte) ) != 0;

		if(is_block_allocated)
		{
			void* current_block = advance_bytes(blocks, i * BLOCK_SIZE);
			Internet_Explorer_Index_File_Map_Entry* entry = (Internet_Explorer_Index_File_Map_Entry*) current_block;

			switch(entry->signature)
			{
				case(ENTRY_URL):
				case(ENTRY_LEAK):
				{
					Internet_Explorer_Index_Url_Entry* url_entry = (Internet_Explorer_Index_Url_Entry*) advance_bytes(entry, sizeof(Internet_Explorer_Index_File_Map_Entry));

					const char* filename_in_mmf = (char*) advance_bytes(entry, url_entry->entry_offset_to_filename);
					TCHAR* decorated_filename = copy_ansi_string_to_tchar(arena, filename_in_mmf);
					TCHAR* filename = copy_ansi_string_to_tchar(arena, filename_in_mmf);
					PathUndecorate(filename);

					TCHAR* file_extension_start = skip_to_file_extension(filename);
					TCHAR* file_extension = push_and_copy_to_arena(arena, string_size(file_extension_start), TCHAR, file_extension_start, string_size(file_extension_start));

					const char* url_in_mmf = (char*) advance_bytes(entry, url_entry->entry_offset_to_url);
					TCHAR* url = copy_ansi_string_to_tchar(arena, url_in_mmf);
					// @TODO: Change decode_url() so it decodes the URL in-place.
					TCHAR* url_copy = push_and_copy_to_arena(arena, string_size(url), TCHAR, url, string_size(url));
					if(!decode_url(url_copy, url)) url = NULL;

					const char* headers_in_mmf = (char*) advance_bytes(entry, url_entry->entry_offset_to_headers);
					char* headers = push_and_copy_to_arena(arena, url_entry->headers_size, char, headers_in_mmf, url_entry->headers_size);
					
					const char* line_delimiters = "\r\n";
					char* next_headers_token = NULL;
					char* line = strtok_s(headers, line_delimiters, &next_headers_token);
					bool is_first_line = true;
					
					TCHAR* server_response = NULL;
					TCHAR* content_type = NULL;
					TCHAR* content_length = NULL;
					TCHAR* content_encoding = NULL;

					// Parse these specific HTTP headers.
					while(line != NULL)
					{
						// Keep the first line intact since it's the server's response (e.g. "HTTP/1.1 200 OK"),
						// and not a key-value pair.
						if(is_first_line)
						{
							is_first_line = false;
							server_response = copy_ansi_string_to_tchar(arena, line);
						}
						// Handle some specific HTTP header response fields (e.g. "Content-Type: text/html",
						// where "Content-Type" is the key, and "text/html" the value).
						else
						{
							char* next_field_token = NULL;
							char* key = strtok_s(line, ":", &next_field_token);
							char* value = skip_leading_whitespace(next_field_token);
							
							if(key != NULL && value != NULL)
							{
								if(lstrcmpiA(key, "content-type") == 0)
								{
									content_type = copy_ansi_string_to_tchar(arena, value);
								}
								else if(lstrcmpiA(key, "content-length") == 0)
								{
									content_length = copy_ansi_string_to_tchar(arena, value);
								}
								else if(lstrcmpiA(key, "content-encoding") == 0)
								{
									content_encoding = copy_ansi_string_to_tchar(arena, value);
								}
							}
							else
							{
								_ASSERT(false);
							}

						}

						line = strtok_s(NULL, line_delimiters, &next_headers_token);
					}

					TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS];
					format_filetime_date_time(url_entry->last_modified_time, last_modified_time);
					
					TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS];
					format_filetime_date_time(url_entry->last_access_time, last_access_time);

					TCHAR creation_time[MAX_FORMATTED_DATE_TIME_CHARS];
					format_dos_date_time(url_entry->creation_time, creation_time);

					TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS];
					format_dos_date_time(url_entry->expiry_time, expiry_time);

					TCHAR short_file_path[MAX_PATH_CHARS] = TEXT("");
					bool file_exists = true;
					if(url_entry->cache_directory_index < MAX_NUM_CACHE_DIRECTORIES)
					{
						// Build the short file path by using the cached file's directory and its (decorated) filename.
						// E.g. "ABCDEFGH\image[1].gif". The cache directory's name doesn't include the null terminator.
						const char* cache_directory_name_in_mmf = header->cache_directories[url_entry->cache_directory_index].name;
						char cache_directory_ansi_name[NUM_CACHE_DIRECTORY_NAME_CHARS + 1];
						CopyMemory(cache_directory_ansi_name, cache_directory_name_in_mmf, NUM_CACHE_DIRECTORY_NAME_CHARS * sizeof(char));
						cache_directory_ansi_name[NUM_CACHE_DIRECTORY_NAME_CHARS] = '\0';

						TCHAR* cache_directory_name = copy_ansi_string_to_tchar(arena, cache_directory_ansi_name);
						StringCchCopy(short_file_path, MAX_PATH_CHARS, cache_directory_name);
						PathAppend(short_file_path, decorated_filename);

						// Build the absolute file path to the cache file. The cache directories are next to the index file
						// in this version of Internet Explorer.
						TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
						StringCchCopy(full_file_path, MAX_PATH_CHARS, index_path);
						PathAppend(full_file_path, TEXT(".."));
						PathAppend(full_file_path, short_file_path);
						// Since GetFullPathName() is called at the beginning of this function for cache_path, the index_path
						// will also hold the full path.

						file_exists = PathFileExists(full_file_path) == TRUE;

						// Build the absolute file path to the destination file. The directory structure will be the same as
						// the path on the original website.
						if(file_exists && exporter->should_copy_files)
						{
							copy_file_using_url_directory_structure(arena, full_file_path, output_copy_path, url, filename);
						}
					}
			
					TCHAR cached_file_size[MAX_UINT32_CHARS];
					convert_u32_to_string(url_entry->cached_file_size, cached_file_size);
					
					TCHAR num_hits[MAX_UINT32_CHARS];
					convert_u32_to_string(url_entry->num_entry_locks, num_hits);

					TCHAR* is_file_missing = (file_exists) ? (TEXT("No")) : (TEXT("Yes"));
					TCHAR* is_leak_entry = (entry->signature == ENTRY_LEAK) ? (TEXT("Yes")) : (TEXT("No"));

					if(exporter->should_create_csv)
					{
						Csv_Entry csv_row[CSV_NUM_COLUMNS] =
						{
							{filename}, {url}, {file_extension}, {cached_file_size},
							{last_modified_time}, {last_access_time}, {creation_time}, {expiry_time},
							{server_response}, {content_type}, {content_length}, {content_encoding},
							{num_hits}, {short_file_path}, {is_file_missing}, {is_leak_entry}
						};

						csv_print_row(arena, csv_file, csv_header, csv_row, CSV_NUM_COLUMNS);
					}

					clear_arena(arena);
				} // Intentional fallthrough.

				case(ENTRY_REDIRECT):
				case(ENTRY_HASH):
				case(ENTRY_DELETED):
				case(ENTRY_UPDATED):
				case(ENTRY_NEWLY_ALLOCATED):
				{
					// Skip to the last allocated block so we move to a new entry on the next iteration.
					i += entry->num_allocated_blocks - 1;
				} break;

				default:
				{
					char signature_string[5];
					CopyMemory(signature_string, &(entry->signature), 4);
					signature_string[4] = '\0';
					log_print(LOG_INFO, "Internet Explorer: Found unknown entry signature at (%Iu, %Iu): 0x%08X (%hs) with %I32u blocks allocated.", block_index_in_byte, byte_index, entry->signature, signature_string, entry->num_allocated_blocks);
				} break;

			}

		}
	}

	close_csv_file(csv_file);
	csv_file = INVALID_HANDLE_VALUE;

	UnmapViewOfFile(index_file);
	index_file = NULL;

	if(was_index_copied_to_temporary_file) DeleteFile(temporary_index_path);

	log_print(LOG_INFO, "Internet Explorer: Finished exporting the cache.");
}

/*
#ifndef BUILD_9X
	void windows_nt_test_ie10(char* index_path)
	{
		JetInit(NULL);

		JET_SESID session_id = JET_sesidNil;
		JetBeginSession(NULL, &session_id, 0, 0);

		JET_DBID database_id = JET_dbidNil;
		JET_ERR error_code = JetOpenDatabaseA(session_id, index_path, NULL, &database_id, JET_bitDbReadOnly);
		log_print(LOG_INFO, "Database error code: %ld", error_code);
	}
#endif
*/

/*
https://www.microsoft.com/en-eg/download/details.aspx?id=1919

Setup: http://go.microsoft.com/fwlink/?LinkId=82431
Says: Microsoft® Windows® Software Development Kit (SDK) for Windows Vista™.
Installs to: C:\Program Files\Microsoft SDKs\Windows\v6.0
Check this option: Install the Windows Vista Headers and Libraries.
*/
