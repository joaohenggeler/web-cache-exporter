#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "internet_explorer.h"

/*
	File Format Documentation:
	- http://www.geoffchappell.com/studies/windows/ie/wininet/api/urlcache/indexdat.htm
	- https://github.com/libyal/libmsiecf/blob/master/documentation/MSIE%20Cache%20File%20(index.dat)%20format.asciidoc
	- https://github.com/csuft/QIECacheViewer/blob/master/README.md
	- http://en.verysource.com/code/4134901_1/cachedef.h.html

	@TODO:
	- Escape CSV values
	- Decode URLs
	- Resolve paths
	- Check if file exists in cache directory
	- Copy file from cache directory to subdirectories structured like the original URL
	---> scheme:[//authority]path[?query][#fragment], where authority is [userinfo@]host[:port]
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
_STATIC_ASSERT(sizeof(FILETIME) == sizeof(u64));
_STATIC_ASSERT(sizeof(Dos_Date_Time) == sizeof(u32));
_STATIC_ASSERT(sizeof(Internet_Explorer_Index_Url_Entry) == 0x60);

// ----------------------------------------------------------------------------------------------------

// log_print(LOG_INFO, "Internet Explorer: Error %d while querying the registry for the browser's version.\n", GetLastError());
/*bool find_internet_explorer_version(char* ie_version)
{
	return query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "svcVersion", ie_version)
		|| query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "Version", ie_version);
}*/

void export_internet_explorer_cache(Arena* arena, char* index_path)
{
	char* base_export_path = "ExportedCache\\InternetExplorer";

	// @TODO: Handle "Content.IE5" and other known locations by default to make it so we can just pass the normal Temporary Internet Files path.
	void* index_file = memory_map_entire_file(index_path);

	if(index_file != NULL)
	{
		Internet_Explorer_Index_Header* header = (Internet_Explorer_Index_Header*) index_file;

		_ASSERT(strncmp(header->signature, "Client UrlCache MMF Ver ", 24) == 0);
		_ASSERT(header->_reserved_1 == 0);
		_ASSERT(header->_reserved_2 == 0);
		_ASSERT(header->_reserved_3 == 0);
		_ASSERT(header->_reserved_4 == 0);
		_ASSERT(header->_reserved_5 == 0);
		
		unsigned char* allocation_bitmap = (unsigned char*) advance_bytes(header, sizeof(Internet_Explorer_Index_Header));
		void* blocks = advance_bytes(allocation_bitmap, ALLOCATION_BITMAP_SIZE);
		
		const char* CSV_HEADER = "Filename,URL,File Extension,File Size,Last Modified Time,Last Access Time,Creation Time,Expiry Time,Server Response,Content-Type,Content-Length,Content-Encoding,Hits,Location On Cache,Missing File,Leak Entry\r\n";
		const size_t CSV_NUM_COLUMNS = 16;
		HANDLE csv_file = create_csv_file("ExportedCache\\InternetExplorer.csv");
		csv_print_header(csv_file, CSV_HEADER);

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
						bool is_leak = (entry->signature == ENTRY_LEAK);

						const char* filename_in_mmf = (char*) advance_bytes(entry, url_entry->entry_offset_to_filename);
						char* filename = push_and_copy_to_arena(arena, string_size(filename_in_mmf), char, filename_in_mmf, string_size(filename_in_mmf));
						PathUndecorateA(filename);

						char* file_extension_start = skip_to_file_extension(filename);
						char* file_extension = push_and_copy_to_arena(arena, string_size(file_extension_start), char, file_extension_start, string_size(file_extension_start));

						const char* url_in_mmf = (char*) advance_bytes(entry, url_entry->entry_offset_to_url);
						char* url = push_arena(arena, string_size(url_in_mmf), char);
						if(!decode_url(url_in_mmf, url)) url = NULL;

						const char* headers_in_mmf = (char*) advance_bytes(entry, url_entry->entry_offset_to_headers);
						char* headers = push_and_copy_to_arena(arena, url_entry->headers_size, char, headers_in_mmf, url_entry->headers_size);
						
						const char* line_delimiters = "\r\n";
						char* next_headers_token = NULL;
						char* line = strtok_s(headers, line_delimiters, &next_headers_token);
						bool is_first_line = true;
						
						char* server_response = NULL;
						char* content_type = NULL;
						char* content_length = NULL;
						char* content_encoding = NULL;

						// Parse these specific HTTP headers.
						while(line != NULL)
						{
							// Keep the first line intact since it's the server's response (e.g. "HTTP/1.1 200 OK"),
							// and not a key-value pair.
							if(is_first_line)
							{
								is_first_line = false;
								server_response = line;
							}
							// Handle some specific HTTP header response fields (e.g. "Content-Type: text/html",
							// where "Content-Type" is the key, and "text/html" the value).
							else
							{
								char* next_field_token = NULL;
								char* key = strtok_s(line, ":", &next_field_token);
								char* value = skip_leading_whitespace(next_field_token);
								//debug_log_print("Field: '%s: %s'\n", key, value);
								
								if(key != NULL && value != NULL)
								{
									if(lstrcmpiA(key, "content-type") == 0)
									{
										content_type = push_and_copy_to_arena(arena, string_size(value), char, value, string_size(value));
									}
									else if(lstrcmpiA(key, "content-length") == 0)
									{
										content_length = push_and_copy_to_arena(arena, string_size(value), char, value, string_size(value));
									}
									else if(lstrcmpiA(key, "content-encoding") == 0)
									{
										content_encoding = push_and_copy_to_arena(arena, string_size(value), char, value, string_size(value));
									}
								}
								else
								{
									_ASSERT(false);
								}

							}

							line = strtok_s(NULL, line_delimiters, &next_headers_token);
						}

						filename = (filename != NULL) ? (filename) : ("");
						url = (url != NULL) ? (url) : ("");
						file_extension = (file_extension != NULL) ? (file_extension) : ("");
						server_response = (server_response != NULL) ? (server_response) : ("");
						content_type = (content_type != NULL) ? (content_type) : ("");
						content_length = (content_length != NULL) ? (content_length) : ("");
						content_encoding = (content_encoding != NULL) ? (content_encoding) : ("");

						char creation_time[MAX_FORMATTED_DATE_TIME_CHARS];
						format_dos_date_time(url_entry->creation_time, creation_time);

						char last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS];
						format_filetime_date_time(url_entry->last_modified_time, last_modified_time);
						
						char last_access_time[MAX_FORMATTED_DATE_TIME_CHARS];
						format_filetime_date_time(url_entry->last_access_time, last_access_time);

						char expiry_time[MAX_FORMATTED_DATE_TIME_CHARS];
						format_dos_date_time(url_entry->expiry_time, expiry_time);

						char short_file_path[MAX_PATH_CHARS] = "";
						//char* short_file_path = push_arena(arena, max_string_chars_for_csv(MAX_PATH_CHARS) * sizeof(char), char);
						bool file_exists = true;
						if(url_entry->cache_directory_index < MAX_NUM_CACHE_DIRECTORIES)
						{
							// Build the short file path by using the cached file's directory and its filename.
							// E.g. "ABCDEFGH\image[1].gif".
							const char* cache_directory_name_in_mmf = header->cache_directories[url_entry->cache_directory_index].name;
							CopyMemory(short_file_path, cache_directory_name_in_mmf, NUM_CACHE_DIRECTORY_NAME_CHARS);
							short_file_path[NUM_CACHE_DIRECTORY_NAME_CHARS] = '\0';
							PathAppendA(short_file_path, filename_in_mmf);

							// Build the absolute file path to the cache file. The cache directories are next to the index file.
							char full_file_path[MAX_PATH_CHARS] = "";
							CopyMemory(full_file_path, index_path, string_size(index_path));
							PathAppendA(full_file_path, "..");
							PathAppendA(full_file_path, short_file_path);
							GetFullPathNameA(full_file_path, MAX_PATH_CHARS, full_file_path, NULL);

							file_exists = PathFileExistsA(full_file_path) == TRUE;

							// Build the absolute file path to the destination file. The directory structure will be the same as
							// the path on the original website.
							if(file_exists)
							{
								copy_file_using_url_directory_structure(arena, full_file_path, base_export_path, url, filename);
							}
						}

						// _ui64toa						
						char cached_file_size[MAX_UINT32_CHARS];
						_ultoa_s(url_entry->cached_file_size, cached_file_size, MAX_UINT32_CHARS, INT_FORMAT_RADIX);
						char num_hits[MAX_UINT32_CHARS];
						_ultoa_s(url_entry->num_entry_locks, num_hits, MAX_UINT32_CHARS, INT_FORMAT_RADIX);
						char* is_file_missing = (file_exists) ? ("No") : ("Yes");
						char* is_leak_entry = (is_leak) ? ("Yes") : ("No");

						char* csv_row[CSV_NUM_COLUMNS] = {filename, url, file_extension, cached_file_size,
														  last_modified_time, last_access_time, creation_time, expiry_time,
														  server_response, content_type, content_length, content_encoding,
														  num_hits, short_file_path, is_file_missing, is_leak_entry};

						csv_print_row(arena, csv_file, csv_row, CSV_NUM_COLUMNS);

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
						log_print(LOG_INFO, "Internet Explorer: Found unknown entry signature at (%u, %u): 0x%08X (%s) with %u blocks allocated.\n", block_index_in_byte, byte_index, entry->signature, signature_string, entry->num_allocated_blocks);
					} break;

				}

			}
		}

		if(csv_file != INVALID_HANDLE_VALUE) CloseHandle(csv_file);
		csv_file = NULL;

		UnmapViewOfFile(index_file);
		index_file = NULL;
	}
	else
	{
		log_print(LOG_INFO, "Internet Explorer: Error while trying to create the file mapping for '%s'.\n", index_path);
	}	
}
