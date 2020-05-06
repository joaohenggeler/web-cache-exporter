#include "web_cache_exporter.h"
#include "file_io.h"
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
static const size_t CACHE_DIRECTORY_NAME_CHARS = 8;
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

	ENTRY_NEWLY_ALLOCATED = 0xDEADBEEF,
	ENTRY_DEALLOCATED = 0x0BADF00D
};

#pragma pack(push, 1)

struct Dos_Date_Time
{
	u16 date;
	u16 time;
};

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
		s8 name[CACHE_DIRECTORY_NAME_CHARS]; // Without null terminator.
	} cache_directories[MAX_NUM_CACHE_DIRECTORIES];

	u32 header_data[HEADER_DATA_LENGTH];
	
	u32 _reserved_5;
};

struct Internet_Explorer_Index_Block
{
	u8 data[BLOCK_SIZE];
};

struct Internet_Explorer_Index_File_Map_Entry
{
	u32 signature;
	u32 num_allocated_blocks;
};

struct Internet_Explorer_Index_Url_Entry : Internet_Explorer_Index_File_Map_Entry
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

static bool format_date_time(Dos_Date_Time* date_time, char* formatted_string)
{
	if(date_time->date == 0 && date_time->time == 0)
	{
		*formatted_string = '\0';
		return true;
	}

	FILETIME filetime;
	DosDateTimeToFileTime(date_time->date, date_time->time, &filetime);
	return format_date_time(&filetime, formatted_string);
}

// log_print("Internet Explorer: Error %d while querying the registry for the browser's version.\n", GetLastError());
bool find_internet_explorer_version(char* ie_version)
{
	return query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "svcVersion", ie_version)
		|| query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "Version", ie_version);
}

void read_internet_explorer_cache(Arena* arena, char* cache_path)
{
	// @TODO: Handle "Content.IE5" and other known locations by default to make it so we can just pass the normal Temporary Internet Files path.
	void* index_file = memory_map_entire_file(cache_path);

	if(index_file != NULL)
	{
		_ASSERT(sizeof(Internet_Explorer_Index_Header) == 0x0250);
		_ASSERT(sizeof(Internet_Explorer_Index_Block) == BLOCK_SIZE);
		_ASSERT(sizeof(Internet_Explorer_Index_File_Map_Entry) == sizeof(u64));
		_ASSERT(sizeof(Internet_Explorer_Index_Url_Entry) == 0x68);
		_ASSERT(sizeof(FILETIME) == sizeof(u64));
		_ASSERT(sizeof(Dos_Date_Time) == sizeof(u32));

		Internet_Explorer_Index_Header* header = (Internet_Explorer_Index_Header*) index_file;

		_ASSERT(strncmp(header->signature, "Client UrlCache MMF Ver ", 24) == 0);
		_ASSERT(header->_reserved_1 == 0);
		_ASSERT(header->_reserved_2 == 0);
		_ASSERT(header->_reserved_3 == 0);
		_ASSERT(header->_reserved_4 == 0);
		_ASSERT(header->_reserved_5 == 0);
		
		u8* allocation_bitmap = (u8*) advance_bytes(header, sizeof(Internet_Explorer_Index_Header));
		Internet_Explorer_Index_Block* blocks = (Internet_Explorer_Index_Block*) advance_bytes(allocation_bitmap, ALLOCATION_BITMAP_SIZE);
		
		const char* CSV_HEADER = "Filename,URL,File Extension,File Size,Creation Time,Last Modified Time,Last Access Time,Expiry Time,Server Response,Content-Type,Content-Length,Content-Encoding,Hits,Location On Cache,Missing File,Leak Entry\n";
		const char* CSV_FORMAT = "%s,%s,%s,%d,%s,%s,%s,%s,%s,%s,%d,%s,%d,TODO,TODO,TODO\n";
		log_print(CSV_HEADER);

		for(size_t i = 0; i < header->num_blocks; ++i)
		{
			size_t byte_index = i / CHAR_BIT;
			size_t block_index_in_byte = i % CHAR_BIT;

			bool is_block_allocated = ( allocation_bitmap[byte_index] & (1 << block_index_in_byte) ) != 0;

			if(is_block_allocated)
			{
				Internet_Explorer_Index_File_Map_Entry* entry = (Internet_Explorer_Index_File_Map_Entry*) &blocks[i];

				switch(entry->signature)
				{
					case(ENTRY_URL):
					{
						Internet_Explorer_Index_Url_Entry* url_entry = (Internet_Explorer_Index_Url_Entry*) entry;

						const char* filename_in_mmf = (char*) advance_bytes(url_entry, url_entry->entry_offset_to_filename);
						char* filename = push_and_copy_to_arena(arena, filename_in_mmf, string_size(filename_in_mmf), char);
						PathUndecorateA(filename);
						char* file_extension = skip_to_file_extension(filename);

						const char* url_in_mmf = (char*) advance_bytes(url_entry, url_entry->entry_offset_to_url);
						char* url = push_arena(arena, string_size(url_in_mmf), char);
						if(!decode_url(url_in_mmf, url)) url = NULL;

						const char* headers_in_mmf = (char*) advance_bytes(url_entry, url_entry->entry_offset_to_headers);
						char* headers = push_and_copy_to_arena(arena, headers_in_mmf, url_entry->headers_size, char);
						
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
							//debug_log_print("Field: '%s'\n", line);

							// Keep the first line intact since it's the server's response (e.g. "HTTP/1.1 200 OK"),
							// and not a key-value pair.
							if(is_first_line)
							{
								is_first_line = false;
								//server_response = (char*) push_and_copy_to_arena(arena, line, string_size(line));
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
										content_type = value;
									}
									else if(lstrcmpiA(key, "content-length") == 0)
									{
										content_length = value;
									}
									else if(lstrcmpiA(key, "content-encoding") == 0)
									{
										content_encoding = value;
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
						format_date_time(&(url_entry->creation_time), creation_time);

						char last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS];
						format_date_time(&(url_entry->last_modified_time), last_modified_time);
						
						char last_access_time[MAX_FORMATTED_DATE_TIME_CHARS];
						format_date_time(&(url_entry->last_access_time), last_access_time);

						char expiry_time[MAX_FORMATTED_DATE_TIME_CHARS];
						format_date_time(&(url_entry->expiry_time), expiry_time);

						log_print(CSV_FORMAT, filename, url, file_extension, url_entry->cached_file_size,
											  creation_time, last_modified_time, last_access_time, expiry_time,
											  server_response, content_type, content_length, content_encoding,
											  url_entry->num_entry_locks);

						clear_arena(arena);
					} // Intentional fallthrough.

					case(ENTRY_REDIRECT):
					case(ENTRY_LEAK):
					case(ENTRY_HASH):
					case(ENTRY_NEWLY_ALLOCATED):
					{
						// Skip to the last allocated block so we move to a new entry on the next iteration.
						i += entry->num_allocated_blocks - 1;
					} break;

					#ifdef DEBUG
					default:
					{
						char signature_string[5];
						CopyMemory(signature_string, &(entry->signature), 4);
						signature_string[4] = '\0';
						debug_log_print("Found unknown entry signature at (%d, %d): 0x%08X (%s) with %d blocks allocated.\n", block_index_in_byte, byte_index, entry->signature, signature_string, entry->num_allocated_blocks);
					} break;
					#endif

				}

			}
		}

		UnmapViewOfFile(index_file);
		index_file = NULL;
	}
	else
	{
		log_print("Internet Explorer: Error while trying to create the file mapping for '%s'.\n", cache_path);
	}	
}
