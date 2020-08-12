#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "internet_explorer.h"
#ifndef BUILD_9X
	// Minimum supported version for the JET Blue / ESE API used by the Internet Explorer 10 and 11's exporter:
	// 0x0500  - Windows 2000
	// 0x0501  - Windows XP
	// 0x0502  - Windows 2003
	// 0x0600  - Windows Vista
	// We'll use Windows Vista since Windows 7 is the earliest version that supports IE 10.
	#define JET_VERSION 0x0600
	#include <esent.h>
	// @Note: The Web Cache Exporter is built using Visual Studio 2005 Professional to target Windows 98 and ME.
	// This version doesn't include the ESENT header file, so we got it from the Windows Vista SDK:
	// - Download Page: https://www.microsoft.com/en-eg/download/details.aspx?id=1919
	// - Download Link: http://go.microsoft.com/fwlink/?LinkId=82431
	// - Titled: Microsoft® Windows® Software Development Kit (SDK) for Windows Vista™.
	// - Check this option when installing: Install the Windows Vista Headers and Libraries.
	// - Installs to: C:\Program Files\Microsoft SDKs\Windows\v6.0
	// Newer Visual Studio versions already include this file.
#endif

/*
	This file defines how the exporter processes Internet Explorer (IE)'s cache. Although we use the term "Internet Explorer",
	this actually represents the WinINet (Windows Internet)'s cache database, which will contain more files than the ones cached
	by the IE browser. This database also holds the cache for other web browsers (like Microsoft Edge) and web plugins (like 3DVIA).

	This cache container is the most important one when it comes to recovering lost web media (games, animations, 3D virtual worlds,
	etc) for a few reasons:
	1. Internet Explorer had a large market share in late 1990s and early 2000s, meaning it's more likely that an older web game was
	played in this browser. See: https://en.wikipedia.org/wiki/Usage_share_of_web_browsers#Summary_tables
	2. In practice, Internet Explorer's maximum cache size could hold a number of complete web games (which were sometimes distributed
	across multiple files) since the file formats used by web plugins (like Flash, Shockwave, etc) were often compressed.

	@SupportedFormats:
	- Internet Explorer 4 (index.dat)
	- Internet Explorer 5 to 9 (Content.IE5\index.dat)
	- Internet Explorer 10 and 11 (WebCacheV01.dat and WebCacheV24.dat - JET Blue / ESE databases)

	@Resources: Previous reverse engineering efforts that specify how the INDEX.DAT file format (IE 4 to 9) should be processed.
	
	[GC] "The INDEX.DAT File Format"
	--> http://www.geoffchappell.com/studies/windows/ie/wininet/api/urlcache/indexdat.htm
	
	[JM] "MSIE Cache File (index.dat) format specification"
	--> https://github.com/libyal/libmsiecf/blob/master/documentation/MSIE%20Cache%20File%20(index.dat)%20format.asciidoc
	
	[NS-B1] "A few words about the cache / history on Internet Explorer 10"
	--> https://blog.nirsoft.net/2012/12/08/a-few-words-about-the-cache-history-on-internet-explorer-10/
	
	[NS-B2] "Improved solution for reading the history of Internet Explorer 10"
	--> https://blog.nirsoft.net/2013/05/02/improved-solution-for-reading-the-history-of-internet-explorer-10/

	@Tools: Existing software that also reads IE's cache.
	
	[NS-T1] "IECacheView v1.58 - Internet Explorer Cache Viewer"
	--> https://www.nirsoft.net/utils/ie_cache_viewer.html
	--> Used to validate the output of this application for IE 5 to 11.
	
	[NS-T2] "ESEDatabaseView v1.65"
	--> https://www.nirsoft.net/utils/ese_database_view.html
	--> Used to explore an existing JET Blue / ESE database in order to figure out how to process the cache for IE 10 and 11.
*/

// The order and type of each column in the CSV file. This applies to all supported cache database file format versions.
static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_URL, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_MODIFIED_TIME, CSV_CREATION_TIME, CSV_LAST_ACCESS_TIME, CSV_EXPIRY_TIME,
	CSV_SERVER_RESPONSE, CSV_CACHE_CONTROL, CSV_PRAGMA, CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_ENCODING, 
	CSV_HITS, CSV_LOCATION_ON_CACHE, CSV_MISSING_FILE
};
static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// ----------------------------------------------------------------------------------------------------

// @Format: Various constants for index.dat.
static const size_t NUM_SIGNATURE_CHARS = 28;
static const size_t NUM_CACHE_DIRECTORY_NAME_CHARS = 8;
static const size_t MAX_NUM_CACHE_DIRECTORIES = 32;
static const size_t HEADER_DATA_LENGTH = 32;
static const size_t BLOCK_SIZE = 128;
static const size_t ALLOCATION_BITMAP_SIZE = 0x3DB0;

// @Format: The signature that identifies each entry in index.dat.
// We must be aware of all of them to properly traverse the allocated blocks.
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

// We'll tightly pack the structures that represent different parts of the index.dat file and then access each member directly after
// mapping it into memory. Due to the way the file is designed, there shouldn't be any memory alignment problems when accessing them. 
#pragma pack(push, 1)

// @Format: The header for the index.dat file.
struct Internet_Explorer_Index_Header
{
	s8 signature[NUM_SIGNATURE_CHARS]; // Including the null terminator.
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

// @Format: The beginning of each entry in the index.dat file.
struct Internet_Explorer_Index_File_Map_Entry
{
	u32 signature;
	u32 num_allocated_blocks;
};

// @Format: The body of a URL entry in the index.dat file (IE 4, format version 4.7).
struct Internet_Explorer_4_Index_Url_Entry
{
	FILETIME last_modified_time;
	FILETIME last_access_time;
	FILETIME expiry_time;

	u32 cached_file_size;
	struct
	{
		u32 _field_1;
		u32 _field_2;
		u32 _field_3;
	} _reserved_1;
	u32 _reserved_2;
	
	u32 _reserved_3;
	u32 entry_offset_to_url;

	u8 cache_directory_index;
	struct
	{
		u8 _field_1;
		u8 _field_2;
		u8 _field_3;
	} _reserved_4;

	u32 entry_offset_to_filename;
	u32 cache_flags;
	u32 entry_offset_to_headers;
	u32 headers_size;
	u32 _reserved_5;

	Dos_Date_Time last_sync_time;
	u32 num_entry_locks; // Number of hits
	u32 _reserved_6;
	Dos_Date_Time creation_time;

	u32 _reserved_7;
};

// @Format: The body of a URL entry in the index.dat file (IE 5 to 9, format version 5.2).
struct Internet_Explorer_5_To_9_Index_Url_Entry
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
_STATIC_ASSERT(sizeof(Internet_Explorer_4_Index_Url_Entry) == 0x60);
_STATIC_ASSERT(sizeof(Internet_Explorer_5_To_9_Index_Url_Entry) == 0x60);

// ----------------------------------------------------------------------------------------------------

// Finds the current Internet Explorer version by querying the registry. This method is recommended in the following Windows
// documentation page, @Docs: https://docs.microsoft.com/en-us/troubleshoot/browsers/information-about-ie-version
//
// @Parameters:
// 1. ie_version - The buffer that receives the string with Internet Explorer's version in the form of:
// <major version>.<minor version>.<build number>.<subbuild number>
// 2. ie_version_size - The size of the buffer in bytes.
//
// @Returns: True if it's able to find Internet Explorer's version in the registry. Otherwise, false.
bool find_internet_explorer_version(TCHAR* ie_version, DWORD ie_version_size)
{
	// We'll try "svcVersion" first since that one contains the correct value for the newer IE versions. In older versions this would
	// fails and we would resot to the "Version" key.
	return query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "svcVersion", ie_version, ie_version_size)
		|| query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "Version", ie_version, ie_version_size);
}

// Removes the decoration from a path string. A decoration consists of the last pair square brackets with zero or more digits in
// between them that appear before the (last) file extension, or before the end of the filename if there's no extension.
//
// For example:
// C:\Path\File[5].txt 	-> C:\Path\File.txt
// C:\Path\File[12] 	-> C:\Path\File
// C:\Path\File.txt 	-> C:\Path\File.txt
// C:\Path\[3].txt 		-> C:\Path\[3].txt
//
// This function was created to replace PathUndecorate() from the Shell API since it was only available from version 5.0
// onwards (IE 5.0, Windows 98SE and 2000, or later).
//
// @Parameters:
// 1. path - The path to modify.
//
// @Returns: True if it's able to find Internet Explorer's version in the registry. Otherwise, false.
static void undecorate_path(TCHAR* path)
{
	TCHAR* filename = PathFindFileName(path);
	// PathFindExtension returns the address of the last file extension. E.g: "file.ext1.ext2" -> ".ext2"
	// We'll use the Windows API function instead of our own because we're trying to replace PathUndecorate
	// to maintain compatibility with older Windows versions, and it's best to use what Windows considers
	// the file extension (first vs last). We have our own function but that one's used to display the
	// file extension in the generated CSV files, and its definition of a file extension might change in
	// the future.
	bool is_first_char = true;
	TCHAR* file_extension = PathFindExtension(filename);
	TCHAR* decoration_begin = NULL;
	TCHAR* decoration_end = NULL;

	// A decoration consists of the last pair square brackets with zero or more digits in between them
	// that appear before the file extension (the last file extension as mentioned above), or before
	// the end of the string if there's no extension. If this pattern appears at the beginning of the
	// filename, it's not considered a decoration. E.g:
	// "C:\path\file[1].ext" 		-> 		"C:\path\file.ext"
	// "C:\path\file[].ext" 		-> 		"C:\path\file.ext"
	// "C:\path\file[1]" 			-> 		"C:\path\file"
	// "C:\path\file[1][2].ext" 	-> 		"C:\path\file[1].ext"
	// "C:\path\[1].ext" 			-> 		"C:\path\[1].ext" 		(no change)
	// "C:\path\file.ext[1]" 		-> 		"C:\path\file.ext[1]" 	(no change)
	// "C:\path\file[1].ext[2]" 	-> 		"C:\path\file.ext[2]"
	// "C:\path\file.ext[1].gz" 	-> 		"C:\path\file.ext.gz"
	while(*filename != TEXT('\0'))
	{
		if(*filename == TEXT('[') && !is_first_char && filename < file_extension)
		{
			decoration_begin = filename;
			++filename;

			while(_istdigit(*filename))
			{
				++filename;
			}

			if(*filename == TEXT(']'))
			{
				decoration_end = filename;
			}
		}

		// Check if it's different than NUL for the case where the decoration isn't closed (e.g. "C:\path\file[1"),
		// meaning 'filename' would already point to the end of the string.
		if(*filename != TEXT('\0')) ++filename;
		is_first_char = false;
	}

	if(decoration_begin != NULL && decoration_end != NULL)
	{
		_ASSERT(decoration_begin < decoration_end);
		TCHAR* remaining_path = decoration_end + 1;
		MoveMemory(decoration_begin, remaining_path, string_size(remaining_path));
	}
}

// 
//
// This function is used when processing both index.dat and the ESE databases.
//
// @Parameters:
// 1. arena -  The Arena structure that receives the various headers' values as TCHAR (ANSI or Wide) strings.
// 2. headers_to_copy - A narrow string that contains the HTTP headers. This string isn't necessarily null terminated.
// 3. headers_size - The size of the headers string in bytes.
// The remaining parameters contain the address of the string in the Arena structure that contains the value of their respective
// header value:
// 4. server_response - The first line in the server's response (e.g. "HTTP/1.1 200 OK").
// 5. cache_control - The "Cache-Control" header.
// 6. pragma - The "Pragma" header.
// 7. content_type - The "Content-Type" header.
// 8. content_length - The "Content-Length" header.
// 9. content_encoding - The "Content-Encoding" header.
//
// @Returns: Nothing.
static void parse_cache_headers(Arena* arena, const char* headers_to_copy, size_t headers_size,
								TCHAR** server_response, TCHAR** cache_control, TCHAR** pragma,
								TCHAR** content_type, TCHAR** content_length, TCHAR** content_encoding)
{
	char* headers = push_and_copy_to_arena(arena, headers_size, char, headers_to_copy, headers_size);

	const char* line_delimiters = "\r\n";
	char* next_headers_token = NULL;
	char* line = strtok_s(headers, line_delimiters, &next_headers_token);
	bool is_first_line = true;

	while(line != NULL)
	{
		// Keep the first line intact since it's the server's response (e.g. "HTTP/1.1 200 OK"),
		// and not a key-value pair.
		if(is_first_line)
		{
			is_first_line = false;
			if(server_response != NULL) *server_response = copy_ansi_string_to_tchar(arena, line);
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
				if(cache_control != NULL && lstrcmpiA(key, "cache-control") == 0)
				{
					*cache_control = copy_ansi_string_to_tchar(arena, value);
				}
				else if(pragma != NULL && lstrcmpiA(key, "pragma") == 0)
				{
					*pragma = copy_ansi_string_to_tchar(arena, value);
				}
				else if(content_type != NULL && lstrcmpiA(key, "content-type") == 0)
				{
					*content_type = copy_ansi_string_to_tchar(arena, value);
				}
				else if(content_length != NULL && lstrcmpiA(key, "content-length") == 0)
				{
					*content_length = copy_ansi_string_to_tchar(arena, value);
				}
				else if(content_encoding != NULL && lstrcmpiA(key, "content-encoding") == 0)
				{
					*content_encoding = copy_ansi_string_to_tchar(arena, value);
				}
			}
			else
			{
				_ASSERT(false);
			}

		}

		line = strtok_s(NULL, line_delimiters, &next_headers_token);
	}
}

// Exports the cache from all supported file formats used by Internet Explorer.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how Internet Explorer's cache should be exported.
// If the path to this location isn't defined, this function will try to locate it using the CSIDL value for the Temporary
// Internet Files directory.
//
// @Returns: Nothing.
void export_specific_or_default_internet_explorer_cache(Exporter* exporter)
{
	if(exporter->is_exporting_from_default_locations && !get_special_folder_path(CSIDL_INTERNET_CACHE, exporter->cache_path))
	{
		log_print(LOG_ERROR, "Internet Explorer: Failed to get the current Temporary Internet Files cache directory path. No files will be exported.");
		return;
	}

	get_full_path_name(exporter->cache_path);
	log_print(LOG_INFO, "Internet Explorer 4 to 9: Exporting the cache from '%s'.", exporter->cache_path);

	resolve_exporter_output_paths_and_create_csv_file(exporter, TEXT("IE"), CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);

	log_print_newline();
	StringCchCopy(exporter->index_path, MAX_PATH_CHARS, exporter->cache_path);
	PathAppend(exporter->index_path, TEXT("index.dat"));
	export_internet_explorer_4_to_9_cache(exporter);

	log_print_newline();
	StringCchCopy(exporter->index_path, MAX_PATH_CHARS, exporter->cache_path);
	PathAppend(exporter->index_path, TEXT("Content.IE5\\index.dat"));
	export_internet_explorer_4_to_9_cache(exporter);

	log_print_newline();
	StringCchCopy(exporter->index_path, MAX_PATH_CHARS, exporter->cache_path);
	PathAppend(exporter->index_path, TEXT("Low\\Content.IE5\\index.dat"));
	export_internet_explorer_4_to_9_cache(exporter);

	#ifndef BUILD_9X

		if(exporter->is_exporting_from_default_locations)
		{
			StringCchCopyW(exporter->cache_path, MAX_PATH_CHARS, exporter->local_appdata_path);
			PathAppendW(exporter->cache_path, L"Microsoft\\Windows\\WebCache");
		}

		log_print(LOG_INFO, "Internet Explorer 10 to 11: Exporting the cache from '%s'.", exporter->cache_path);

		log_print_newline();
		StringCchCopyW(exporter->index_path, MAX_PATH_CHARS, exporter->cache_path);
		PathAppendW(exporter->index_path, L"WebCacheV01.dat");
		windows_nt_export_internet_explorer_10_to_11_cache(exporter, L"V01");

		log_print_newline();
		StringCchCopyW(exporter->index_path, MAX_PATH_CHARS, exporter->cache_path);
		PathAppendW(exporter->index_path, L"WebCacheV24.dat");
		windows_nt_export_internet_explorer_10_to_11_cache(exporter, L"V24");
		
	#endif

	close_exporter_csv_file(exporter);

	log_print(LOG_INFO, "Internet Explorer: Finished exporting the cache.");
}

// Exports the cache from Internet Explorer 4 through 9.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how Internet Explorer's cache should be exported.
//
// @Returns: Nothing.
void export_internet_explorer_4_to_9_cache(Exporter* exporter)
{
	Arena* arena = &(exporter->temporary_arena);

	HANDLE index_handle = INVALID_HANDLE_VALUE;
	u64 index_file_size = 0;
	void* index_file = memory_map_entire_file(exporter->index_path, &index_handle, &index_file_size);
	
	if(index_file == NULL)
	{
		DWORD error_code = GetLastError();

		if( (error_code == ERROR_FILE_NOT_FOUND) || (error_code == ERROR_PATH_NOT_FOUND) )
		{
			log_print(LOG_ERROR, "Internet Explorer 4 to 9: The index file was not found.");
		}
		else if(error_code == ERROR_SHARING_VIOLATION)
		{
			log_print(LOG_WARNING, "Internet Explorer 4 to 9: Failed to open the index file since its being used by another process. Attempting to create a copy and opening that one...");
		
			// @TODO: Can we get a handle with CreateFile() that has the DELETE_ON_CLOSE flag set?
			// We wouldn't need to explicitly delete the temporary file + it's deleted if the exporter crashes.
			TCHAR temporary_index_path[MAX_PATH_CHARS] = TEXT("");
			if(copy_to_temporary_file(exporter->index_path, exporter->temporary_path, temporary_index_path, &index_handle))
			{
				log_print(LOG_INFO, "Internet Explorer 4 to 9: Copied the index file to '%s'.", temporary_index_path);
				index_file = memory_map_entire_file(index_handle, &index_file_size);
			}
			else
			{
				log_print(LOG_ERROR, "Internet Explorer 4 to 9: Failed to create a copy of the index file.");
			}
		}
	}

	if(index_file == NULL)
	{
		log_print(LOG_ERROR, "Internet Explorer 4 to 9: Failed to open the index file correctly. No files will be exported from this cache.");
		safe_close_handle(&index_handle);
		return;
	}

	// If we were able to read the file, we'll still want to check for specific error conditions:
	// 1. The file is too small to contain the header (we wouldn't be able to access critical information).
	// 2. The file isn't a valid index file since it has an invalid signature.
	// 3. The file size we read doesn't match the file size stored in the header.

	if(index_file_size < sizeof(Internet_Explorer_Index_Header))
	{
		log_print(LOG_ERROR, "Internet Explorer 4 to 9: The size of the opened index file is smaller than the file format's header. No files will be exported from this cache.");
		safe_close_handle(&index_handle);
		_ASSERT(false);
		return;
	}

	Internet_Explorer_Index_Header* header = (Internet_Explorer_Index_Header*) index_file;

	if(strncmp(header->signature, "Client UrlCache MMF Ver ", 24) != 0)
	{
		char signature_string[NUM_SIGNATURE_CHARS + 1];
		CopyMemory(signature_string, header->signature, NUM_SIGNATURE_CHARS);
		signature_string[NUM_SIGNATURE_CHARS] = '\0';

		log_print(LOG_ERROR, "Internet Explorer 4 to 9: The index file starts with an invalid signature: '%hs'. No files will be exported from this cache.", signature_string);
		safe_close_handle(&index_handle);
		_ASSERT(false);
		return;
	}

	if(index_file_size != header->file_size)
	{
		log_print(LOG_ERROR, "Internet Explorer 4 to 9: The size of the opened index file is different than the size specified in its header. No files will be exported from this cache.");
		safe_close_handle(&index_handle);
		_ASSERT(false);
		return;
	}

	// We only handle two versions of the index file format: 4.7 and 5.2.
	// @TODO: Should this version be checked in the release builds? Right now we only assert it in debug mode.
	char major_version = header->signature[24];
	char minor_version = header->signature[26];
	log_print(LOG_INFO, "Internet Explorer 4 to 9: The index file (version %hc.%hc) was opened successfully. Starting the export process.", major_version, minor_version);
	_ASSERT( (major_version == '4' && minor_version == '7') || (major_version == '5' && minor_version == '2') );

	#ifdef DEBUG
		debug_log_print("Internet Explorer 4 to 9: Header->Reserved_1: 0x%08X", header->_reserved_1);
		debug_log_print("Internet Explorer 4 to 9: Header->Reserved_2: 0x%08X", header->_reserved_2);
		debug_log_print("Internet Explorer 4 to 9: Header->Reserved_3: 0x%08X", header->_reserved_3);
		debug_log_print("Internet Explorer 4 to 9: Header->Reserved_4: 0x%08X", header->_reserved_4);
		debug_log_print("Internet Explorer 4 to 9: Header->Reserved_5: 0x%08X", header->_reserved_5);
	#endif
	
	// Go through each bit to check if a particular block was allocated. If so, we'll skip to that block and handle
	// that specific entry type. If not, we'll ignore it and move to the next one.
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
				// We'll extract information from two similar entry types: URL and Leak entries.
				// If the file associated with a URL entry is marked for deletion, but cannot be deleted by the cache
				// scavenger (e.g. there's a sharing violation because it's being used by another process), then
				// it's changed to a Leak entry which will be deleted at a later time. The index file's header data
				// contains the offset to the first Leak entry, and each entry the offset to the next one.
				case(ENTRY_URL):
				case(ENTRY_LEAK):
				{
					void* url_entry = advance_bytes(entry, sizeof(Internet_Explorer_Index_File_Map_Entry));
					
					// @Aliasing: These two variables point to the same memory but they're never deferenced at the same time.
					Internet_Explorer_4_Index_Url_Entry* url_entry_4 			= (Internet_Explorer_4_Index_Url_Entry*) 		url_entry;
					Internet_Explorer_5_To_9_Index_Url_Entry* url_entry_5_to_9 	= (Internet_Explorer_5_To_9_Index_Url_Entry*) 	url_entry;

					// Helper macro function used to access a given field in the two types of URL entries (versions 4 and 5).
					// These two structs are very similar but still differ in how they're laid out.
					#define GET_URL_ENTRY_FIELD(variable_name, field_name)\
					do\
					{\
						if(major_version == '4')\
						{\
							variable_name = url_entry_4->field_name;\
						}\
						else\
						{\
							variable_name = url_entry_5_to_9->field_name;\
						}\
					}\
					while(false, false)

					u32 entry_offset_to_filename;
					GET_URL_ENTRY_FIELD(entry_offset_to_filename, entry_offset_to_filename);
					_ASSERT(entry_offset_to_filename > 0);
					// We'll keep two versions of the filename: the original decorated name (e.g. image[1].gif)
					// which is the name of the actual cached file on disk, and the undecorated name (e.g. image.gif)
					// which is what we'll show in the CSV.
					const char* filename_in_mmf = (char*) advance_bytes(entry, entry_offset_to_filename);
					TCHAR* decorated_filename = copy_ansi_string_to_tchar(arena, filename_in_mmf);
					TCHAR* filename = copy_ansi_string_to_tchar(arena, filename_in_mmf);
					undecorate_path(filename);

					TCHAR* file_extension = skip_to_file_extension(filename);

					u32 entry_offset_to_url;
					GET_URL_ENTRY_FIELD(entry_offset_to_url, entry_offset_to_url);
					_ASSERT(entry_offset_to_url > 0);
					// @Format: The stored URL is encoded. We'll decode it for the CSV and to correctly create
					// the website's original directory structure when we copy the cached file.
					const char* url_in_mmf = (char*) advance_bytes(entry, entry_offset_to_url);
					TCHAR* url = copy_ansi_string_to_tchar(arena, url_in_mmf);
					// @TODO: Change decode_url() so it decodes the URL in-place.
					//TCHAR* url_copy = push_and_copy_to_arena(arena, string_size(url), TCHAR, url, string_size(url));
					decode_url(url);

					u32 entry_offset_to_headers;
					GET_URL_ENTRY_FIELD(entry_offset_to_headers, entry_offset_to_headers);
					u32 headers_size;
					GET_URL_ENTRY_FIELD(headers_size, headers_size);
					const char* headers_in_mmf = (char*) advance_bytes(entry, entry_offset_to_headers);

					TCHAR* server_response = NULL;
					TCHAR* cache_control = NULL;
					TCHAR* pragma = NULL;
					TCHAR* content_type = NULL;
					TCHAR* content_length = NULL;
					TCHAR* content_encoding = NULL;

					if( (entry_offset_to_headers > 0) && (headers_size > 0) )
					{
						parse_cache_headers(arena, headers_in_mmf, headers_size,
											&server_response, &cache_control, &pragma,
											&content_type, &content_length, &content_encoding);
					}

					TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS];
					FILETIME last_modified_time_value;
					GET_URL_ENTRY_FIELD(last_modified_time_value, last_modified_time);
					format_filetime_date_time(last_modified_time_value, last_modified_time);
					
					TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS];
					FILETIME last_access_time_value;
					GET_URL_ENTRY_FIELD(last_access_time_value, last_access_time);
					format_filetime_date_time(last_access_time_value, last_access_time);

					TCHAR creation_time[MAX_FORMATTED_DATE_TIME_CHARS];
					Dos_Date_Time creation_time_value;
					GET_URL_ENTRY_FIELD(creation_time_value, creation_time);
					format_dos_date_time(creation_time_value, creation_time);

					// @Format: The file's expiry time is stored as two different types depending on the index file's version.
					TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS];
					if(major_version == '4')
					{
						format_filetime_date_time(url_entry_4->expiry_time, expiry_time);
					}
					else
					{
						format_dos_date_time(url_entry_5_to_9->expiry_time, expiry_time);
					}
					
					TCHAR short_file_path[MAX_PATH_CHARS] = TEXT("");
					TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
					bool file_exists = false;

					const u8 CHANNEL_DEFINITION_FORMAT_INDEX = 0xFF;
					// Channel Definition Format (CDF): https://en.wikipedia.org/wiki/Channel_Definition_Format
					u8 cache_directory_index;
					GET_URL_ENTRY_FIELD(cache_directory_index, cache_directory_index);

					if(cache_directory_index < MAX_NUM_CACHE_DIRECTORIES)
					{
						// Build the short file path by using the cached file's directory and its (decorated) filename.
						// E.g. "ABCDEFGH\image[1].gif".
						// @Format: The cache directory's name doesn't include the null terminator.
						const char* cache_directory_name_in_mmf = header->cache_directories[cache_directory_index].name;
						char cache_directory_ansi_name[NUM_CACHE_DIRECTORY_NAME_CHARS + 1];
						CopyMemory(cache_directory_ansi_name, cache_directory_name_in_mmf, NUM_CACHE_DIRECTORY_NAME_CHARS * sizeof(char));
						cache_directory_ansi_name[NUM_CACHE_DIRECTORY_NAME_CHARS] = '\0';

						TCHAR* cache_directory_name = copy_ansi_string_to_tchar(arena, cache_directory_ansi_name);
						StringCchCopy(short_file_path, MAX_PATH_CHARS, cache_directory_name);
						PathAppend(short_file_path, decorated_filename);

						// Build the absolute file path to the cache file. The cache directories are next to the index file
						// in this version of Internet Explorer. Here, exporter->index_path is already a full path.
						StringCchCopy(full_file_path, MAX_PATH_CHARS, exporter->index_path);
						PathAppend(full_file_path, TEXT(".."));
						PathAppend(full_file_path, short_file_path);

						file_exists = does_file_exist(full_file_path);
					}
					else if(cache_directory_index == CHANNEL_DEFINITION_FORMAT_INDEX)
					{
						// CDF files are marked with this special string since they're not stored on disk.
						StringCchCopy(short_file_path, MAX_PATH_CHARS, TEXT("<CDF>"));
					}
					else
					{
						log_print(LOG_WARNING, "Internet Explorer 4 to 9: Unknown cache directory index 0x%02X for file '%s' with the following URL: '%s'.", cache_directory_index, filename, url);
						_ASSERT(false);
					}
			
					TCHAR cached_file_size[MAX_INT32_CHARS];
					u32 cached_file_size_value;
					GET_URL_ENTRY_FIELD(cached_file_size_value, cached_file_size);
					convert_u32_to_string(cached_file_size_value, cached_file_size);
					
					TCHAR num_hits[MAX_INT32_CHARS];
					u32 num_entry_locks;
					GET_URL_ENTRY_FIELD(num_entry_locks, num_entry_locks);
					convert_u32_to_string(num_entry_locks, num_hits);

					TCHAR* is_file_missing = (file_exists) ? (TEXT("No")) : (TEXT("Yes"));

					Csv_Entry csv_row[CSV_NUM_COLUMNS] =
					{
						{filename}, {url}, {file_extension}, {cached_file_size},
						{last_modified_time}, {creation_time}, {last_access_time}, {expiry_time},
						{server_response}, {cache_control}, {pragma}, {content_type}, {content_length}, {content_encoding},
						{num_hits}, {short_file_path}, {is_file_missing}
					};

					export_cache_entry(	exporter,
										CSV_COLUMN_TYPES, csv_row, CSV_NUM_COLUMNS,
										full_file_path, url, filename);

				} // Intentional fallthrough.

				// We won't handle these specific entry types, so we'll always skip them.
				case(ENTRY_REDIRECT):
				case(ENTRY_HASH):
				case(ENTRY_DELETED):
				case(ENTRY_UPDATED):
				case(ENTRY_NEWLY_ALLOCATED):
				{
					// Skip to the last allocated block so we move to a new entry on the next iteration.
					i += entry->num_allocated_blocks - 1;
				} break;

				// Check if we found an unhandled entry type. We'll want to know if these exist because otherwise
				// we could start treating their allocated blocks as the beginning of other entry types.
				default:
				{
					size_t const NUM_ENTRY_SIGNATURE_CHARS = 4;
					char signature_string[NUM_ENTRY_SIGNATURE_CHARS + 1];
					CopyMemory(signature_string, &(entry->signature), NUM_ENTRY_SIGNATURE_CHARS);
					signature_string[NUM_ENTRY_SIGNATURE_CHARS] = '\0';
					log_print(LOG_WARNING, "Internet Explorer 4 to 9: Found unknown entry signature at (%Iu, %Iu): 0x%08X ('%hs') with %I32u blocks allocated.", block_index_in_byte, byte_index, entry->signature, signature_string, entry->num_allocated_blocks);
				} break;

			}

		}
	}

	SAFE_UNMAP_VIEW_OF_FILE(index_file);
	safe_close_handle(&index_handle);

	log_print(LOG_INFO, "Internet Explorer 4 to 9: Finished exporting the cache.");
}


#ifndef BUILD_9X

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_GET_DATABASE_FILE_INFO_W(function_name) JET_ERR JET_API function_name(JET_PCWSTR szDatabaseName, void* pvResult, unsigned long cbMax, unsigned long InfoLevel)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_GET_DATABASE_FILE_INFO_W(stub_jet_get_database_file_info_w)
	{
		log_print(LOG_WARNING, "JetGetDatabaseFileInfoW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_GET_DATABASE_FILE_INFO_W(Jet_Get_Database_File_Info_W);
	static Jet_Get_Database_File_Info_W* dll_jet_get_database_file_info_w = stub_jet_get_database_file_info_w;
	#define JetGetDatabaseFileInfoW dll_jet_get_database_file_info_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_GET_SYSTEM_PARAMETER_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance, JET_SESID sesid, unsigned long paramid, JET_API_PTR* plParam, JET_PWSTR szParam, unsigned long cbMax)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_GET_SYSTEM_PARAMETER_W(stub_jet_get_system_parameter_w)
	{
		log_print(LOG_WARNING, "JetGetSystemParameterW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_GET_SYSTEM_PARAMETER_W(Jet_Get_System_Parameter_W);
	static Jet_Get_System_Parameter_W* dll_jet_get_system_parameter_w = stub_jet_get_system_parameter_w;
	#define JetGetSystemParameterW dll_jet_get_system_parameter_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_SET_SYSTEM_PARAMETER_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance, JET_SESID sesid, unsigned long paramid, JET_API_PTR lParam, JET_PCWSTR szParam)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_SET_SYSTEM_PARAMETER_W(stub_jet_set_system_parameter_w)
	{
		log_print(LOG_WARNING, "JetSetSystemParameterW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_SET_SYSTEM_PARAMETER_W(Jet_Set_System_Parameter_W);
	static Jet_Set_System_Parameter_W* dll_jet_set_system_parameter_w = stub_jet_set_system_parameter_w;
	#define JetSetSystemParameterW dll_jet_set_system_parameter_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_CREATE_INSTANCE_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance, JET_PCWSTR szInstanceName)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_CREATE_INSTANCE_W(stub_jet_create_instance_w)
	{
		log_print(LOG_WARNING, "JetCreateInstanceW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_CREATE_INSTANCE_W(Jet_Create_Instance_W);
	static Jet_Create_Instance_W* dll_jet_create_instance_w = stub_jet_create_instance_w;
	#define JetCreateInstanceW dll_jet_create_instance_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_INIT(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_INIT(stub_jet_init)
	{
		log_print(LOG_WARNING, "JetInit: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_INIT(Jet_Init);
	static Jet_Init* dll_jet_init = stub_jet_init;
	#define JetInit dll_jet_init

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_TERM(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_TERM(stub_jet_term)
	{
		log_print(LOG_WARNING, "JetTerm: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_TERM(Jet_Term);
	static Jet_Term* dll_jet_term = stub_jet_term;
	#define JetTerm dll_jet_term

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_BEGIN_SESSION_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance, JET_SESID* psesid, JET_PCWSTR szUserName, JET_PCWSTR szPassword)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_BEGIN_SESSION_W(stub_jet_begin_session_w)
	{
		log_print(LOG_WARNING, "JetBeginSessionW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_BEGIN_SESSION_W(Jet_Begin_Session_W);
	static Jet_Begin_Session_W* dll_jet_begin_session_w = stub_jet_begin_session_w;
	#define JetBeginSessionW dll_jet_begin_session_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_END_SESSION(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_GRBIT grbit)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_END_SESSION(stub_jet_end_session)
	{
		log_print(LOG_WARNING, "JetEndSession: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_END_SESSION(Jet_End_Session);
	static Jet_End_Session* dll_jet_end_session = stub_jet_end_session;
	#define JetEndSession dll_jet_end_session

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_ATTACH_DATABASE_2_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename, const unsigned long cpgDatabaseSizeMax, JET_GRBIT grbit)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_ATTACH_DATABASE_2_W(stub_jet_attach_database_2_w)
	{
		log_print(LOG_WARNING, "JetAttachDatabase2W: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_ATTACH_DATABASE_2_W(Jet_Attach_Database_2_W);
	static Jet_Attach_Database_2_W* dll_jet_attach_database_2_w = stub_jet_attach_database_2_w;
	#define JetAttachDatabase2W dll_jet_attach_database_2_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_DETACH_DATABASE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_DETACH_DATABASE_W(stub_jet_detach_database_w)
	{
		log_print(LOG_WARNING, "JetDetachDatabaseW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_DETACH_DATABASE_W(Jet_Detach_Database_W);
	static Jet_Detach_Database_W* dll_jet_detach_database_w = stub_jet_detach_database_w;
	#define JetDetachDatabaseW dll_jet_detach_database_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_OPEN_DATABASE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename, JET_PCWSTR szConnect, JET_DBID* pdbid, JET_GRBIT grbit)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_OPEN_DATABASE_W(stub_jet_open_database_w)
	{
		log_print(LOG_WARNING, "JetOpenDatabaseW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_OPEN_DATABASE_W(Jet_Open_Database_W);
	static Jet_Open_Database_W* dll_jet_open_database_w = stub_jet_open_database_w;
	#define JetOpenDatabaseW dll_jet_open_database_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_CLOSE_DATABASE(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_DBID dbid, JET_GRBIT grbit)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_CLOSE_DATABASE(stub_jet_close_database)
	{
		log_print(LOG_WARNING, "JetCloseDatabase: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_CLOSE_DATABASE(Jet_Close_Database);
	static Jet_Close_Database* dll_jet_close_database = stub_jet_close_database;
	#define JetCloseDatabase dll_jet_close_database

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_OPEN_TABLE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_DBID dbid, JET_PCWSTR szTableName, const void* pvParameters, unsigned long cbParameters, JET_GRBIT grbit, JET_TABLEID* ptableid)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_OPEN_TABLE_W(stub_jet_open_table_w)
	{
		log_print(LOG_WARNING, "JetOpenTableW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_OPEN_TABLE_W(Jet_Open_Table_W);
	static Jet_Open_Table_W* dll_jet_open_table_w = stub_jet_open_table_w;
	#define JetOpenTableW dll_jet_open_table_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_CLOSE_TABLE(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_CLOSE_TABLE(stub_jet_close_table)
	{
		log_print(LOG_WARNING, "JetCloseTable: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_CLOSE_TABLE(Jet_Close_Table);
	static Jet_Close_Table* dll_jet_close_table = stub_jet_close_table;
	#define JetCloseTable dll_jet_close_table

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_GET_TABLE_COLUMN_INFO_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_PCWSTR szColumnName, void* pvResult, unsigned long cbMax, unsigned long InfoLevel)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_GET_TABLE_COLUMN_INFO_W(stub_jet_get_table_column_info_w)
	{
		log_print(LOG_WARNING, "JetGetTableColumnInfoW: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_GET_TABLE_COLUMN_INFO_W(Jet_Get_Table_Column_Info_W);
	static Jet_Get_Table_Column_Info_W* dll_jet_get_table_column_info_w = stub_jet_get_table_column_info_w;
	#define JetGetTableColumnInfoW dll_jet_get_table_column_info_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_RETRIEVE_COLUMN(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID columnid, void* pvData, unsigned long cbData, unsigned long* pcbActual, JET_GRBIT grbit, JET_RETINFO* pretinfo)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_RETRIEVE_COLUMN(stub_jet_retrieve_column)
	{
		log_print(LOG_WARNING, "JetRetrieveColumn: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_RETRIEVE_COLUMN(Jet_Retrieve_Column);
	static Jet_Retrieve_Column* dll_jet_retrieve_column = stub_jet_retrieve_column;
	#define JetRetrieveColumn dll_jet_retrieve_column

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_RETRIEVE_COLUMNS(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_RETRIEVECOLUMN* pretrievecolumn, unsigned long cretrievecolumn)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_RETRIEVE_COLUMNS(stub_jet_retrieve_columns)
	{
		log_print(LOG_WARNING, "JetRetrieveColumns: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_RETRIEVE_COLUMNS(Jet_Retrieve_Columns);
	static Jet_Retrieve_Columns* dll_jet_retrieve_columns = stub_jet_retrieve_columns;
	#define JetRetrieveColumns dll_jet_retrieve_columns

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_GET_RECORD_POSITION(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_RECPOS* precpos, unsigned long cbRecpos)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_GET_RECORD_POSITION(stub_jet_get_record_position)
	{
		log_print(LOG_WARNING, "JetGetRecordPosition: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_GET_RECORD_POSITION(Jet_Get_Record_Position);
	static Jet_Get_Record_Position* dll_jet_get_record_position = stub_jet_get_record_position;
	#define JetGetRecordPosition dll_jet_get_record_position

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_MOVE(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, long cRow, JET_GRBIT grbit)
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static JET_MOVE(stub_jet_move)
	{
		log_print(LOG_WARNING, "JetMove: Calling the stub version of this function.");
		_ASSERT(false);
		return JET_wrnNyi;
	}
	#pragma warning(pop)
	typedef JET_MOVE(Jet_Move);
	static Jet_Move* dll_jet_move = stub_jet_move;
	#define JetMove dll_jet_move

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	static HMODULE esent_library = NULL;

	void windows_nt_load_esent_functions(void)
	{
		if(esent_library != NULL)
		{
			log_print(LOG_WARNING, "Load ESENT Functions: The library was already loaded.");
			_ASSERT(false);
			return;
		}

		esent_library = LoadLibraryA("ESENT.dll");
		if(esent_library != NULL)
		{
			GET_FUNCTION_ADDRESS(esent_library, "JetGetDatabaseFileInfoW", Jet_Get_Database_File_Info_W*, JetGetDatabaseFileInfoW);
			GET_FUNCTION_ADDRESS(esent_library, "JetGetSystemParameterW", Jet_Get_System_Parameter_W*, JetGetSystemParameterW);
			GET_FUNCTION_ADDRESS(esent_library, "JetSetSystemParameterW", Jet_Set_System_Parameter_W*, JetSetSystemParameterW);
			GET_FUNCTION_ADDRESS(esent_library, "JetCreateInstanceW", Jet_Create_Instance_W*, JetCreateInstanceW);
			GET_FUNCTION_ADDRESS(esent_library, "JetInit", Jet_Init*, JetInit);
			GET_FUNCTION_ADDRESS(esent_library, "JetTerm", Jet_Term*, JetTerm);
			GET_FUNCTION_ADDRESS(esent_library, "JetBeginSessionW", Jet_Begin_Session_W*, JetBeginSessionW);
			GET_FUNCTION_ADDRESS(esent_library, "JetEndSession", Jet_End_Session*, JetEndSession);
			GET_FUNCTION_ADDRESS(esent_library, "JetAttachDatabase2W", Jet_Attach_Database_2_W*, JetAttachDatabase2W);
			GET_FUNCTION_ADDRESS(esent_library, "JetDetachDatabaseW", Jet_Detach_Database_W*, JetDetachDatabaseW);
			GET_FUNCTION_ADDRESS(esent_library, "JetOpenDatabaseW", Jet_Open_Database_W*, JetOpenDatabaseW);
			GET_FUNCTION_ADDRESS(esent_library, "JetCloseDatabase", Jet_Close_Database*, JetCloseDatabase);
			GET_FUNCTION_ADDRESS(esent_library, "JetOpenTableW", Jet_Open_Table_W*, JetOpenTableW);
			GET_FUNCTION_ADDRESS(esent_library, "JetCloseTable", Jet_Close_Table*, JetCloseTable);
			GET_FUNCTION_ADDRESS(esent_library, "JetGetTableColumnInfoW", Jet_Get_Table_Column_Info_W*, JetGetTableColumnInfoW);
			GET_FUNCTION_ADDRESS(esent_library, "JetRetrieveColumn", Jet_Retrieve_Column*, JetRetrieveColumn);
			GET_FUNCTION_ADDRESS(esent_library, "JetRetrieveColumns", Jet_Retrieve_Columns*, JetRetrieveColumns);
			GET_FUNCTION_ADDRESS(esent_library, "JetGetRecordPosition", Jet_Get_Record_Position*, JetGetRecordPosition);
			GET_FUNCTION_ADDRESS(esent_library, "JetMove", Jet_Move*, JetMove);
		}
		else
		{
			log_print(LOG_ERROR, "Load ESENT Functions: Failed to load the library with error code %lu.", GetLastError());
			_ASSERT(false);
		}
	}

	void windows_nt_free_esent_functions(void)
	{
		if(esent_library == NULL)
		{
			log_print(LOG_ERROR, "Free ESENT: Failed to free the library since it wasn't previously loaded.");
			return;
		}

		if(FreeLibrary(esent_library))
		{
			esent_library = NULL;
			JetGetDatabaseFileInfoW = stub_jet_get_database_file_info_w;
			JetGetSystemParameterW = stub_jet_get_system_parameter_w;
			JetSetSystemParameterW = stub_jet_set_system_parameter_w;
			JetCreateInstanceW = stub_jet_create_instance_w;
			JetInit = stub_jet_init;
			JetTerm = stub_jet_term;
			JetBeginSessionW = stub_jet_begin_session_w;
			JetEndSession = stub_jet_end_session;
			JetAttachDatabase2W = stub_jet_attach_database_2_w;
			JetDetachDatabaseW = stub_jet_detach_database_w;
			JetOpenDatabaseW = stub_jet_open_database_w;
			JetCloseDatabase = stub_jet_close_database;
			JetOpenTableW = stub_jet_open_table_w;
			JetCloseTable = stub_jet_close_table;
			JetGetTableColumnInfoW = stub_jet_get_table_column_info_w;
			JetRetrieveColumn = stub_jet_retrieve_column;
			JetRetrieveColumns = stub_jet_retrieve_columns;
			JetGetRecordPosition = stub_jet_get_record_position;
			JetMove = stub_jet_move;
		}
		else
		{
			log_print(LOG_ERROR, "Free ESENT: Failed to free the library with the error code %lu.", GetLastError());
			_ASSERT(false);
		}
	}

	static void windows_nt_ese_clean_up(TCHAR* temporary_directory_path,
										JET_INSTANCE* instance, JET_SESID* session_id,
										JET_DBID* database_id, JET_TABLEID* containers_table_id)
	{
		JET_ERR error_code = JET_errSuccess;

		if(*containers_table_id != JET_tableidNil)
		{
			error_code = JetCloseTable(*session_id, *containers_table_id);
			if(error_code != JET_errSuccess) log_print(LOG_WARNING, "Error %ld while trying to close the Containers table.", error_code);
			*containers_table_id = JET_tableidNil;
		}

		if(*database_id != JET_dbidNil)
		{
			error_code = JetCloseDatabase(*session_id, *database_id, 0);
			if(error_code != JET_errSuccess) log_print(LOG_WARNING, "Error %ld while trying to close the database.", error_code);
			error_code = JetDetachDatabaseW(*session_id, NULL);
			if(error_code != JET_errSuccess) log_print(LOG_WARNING, "Error %ld while trying to detach the database.", error_code);
			*database_id = JET_dbidNil;
		}

		if(*session_id != JET_sesidNil)
		{
			error_code = JetEndSession(*session_id, 0);
			if(error_code != JET_errSuccess) log_print(LOG_WARNING, "Error %ld while trying to end the session.", error_code);
			*session_id = JET_sesidNil;
		}

		if(*instance != JET_instanceNil)
		{
			error_code = JetTerm(*instance);
			if(error_code != JET_errSuccess) log_print(LOG_WARNING, "Error %ld while trying to terminate the ESE instance.", error_code);
			*instance = JET_instanceNil;
		}

		if(!is_string_empty(temporary_directory_path) && !delete_directory_and_contents(temporary_directory_path))
		{
			log_print(LOG_WARNING, "Error %lu while trying to delete the temporary recovery directory and its contents.", GetLastError());
			_ASSERT(false);
		}
	}

	static wchar_t* windows_nt_get_database_state_string(unsigned long state)
	{
		switch(state)
		{
			case(JET_dbstateJustCreated): 		return L"Just Created";
			case(JET_dbstateDirtyShutdown): 	return L"Dirty Shutdown";
			case(JET_dbstateCleanShutdown): 	return L"Clean Shutdown";
			case(JET_dbstateBeingConverted): 	return L"Being Converted";
			case(JET_dbstateForceDetach): 		return L"Force Detach";
			default: 							return L"Unknown";
		}
	}

	// Exports the cache from Internet Explorer 10 and 11.
	//
	// @Parameters:
	// 1. exporter - The Exporter structure which contains information on how Internet Explorer's cache should be exported.
	// 2. ese_files_prefix - The three character prefix on the transaction logs and any other files that are next to the ESE
	// database. This parameter is required to ensure that the data is recovered correctly.
	//
	// @Returns: Nothing.
	void windows_nt_export_internet_explorer_10_to_11_cache(Exporter* exporter, const wchar_t* ese_files_prefix)
	{
		Arena* arena = &(exporter->temporary_arena);
		wchar_t* index_filename = PathFindFileNameW(exporter->index_path);

		//windows_nt_force_copy_open_file(arena, L"C:\\NonASCIIHaven\\Flashpoint\\Devs\\wordfall.jar", L"C:\\NonASCIIHaven\\Flashpoint\\Devs\\wordfall_copy.jar");

		if(!does_file_exist(exporter->index_path))
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: The ESE database file '%ls' was not found. No files will be exported from this cache.", index_filename);
			return;
		}

		if(!exporter->was_temporary_directory_created)
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: The temporary exporter directory used to recover the ESE database's contents was not previously created. No files will be exported from this cache.");
			return;
		}

		log_print(LOG_INFO, "Internet Explorer 10 to 11: The cache will be exported based on the information in the ESE database file '%ls'.", index_filename);

		/*
		wchar_t database_path[MAX_ESE_PATH_CHARS] = L"";
		StringCchCopyW(database_path, MAX_ESE_PATH_CHARS, index_path);
		PathAppendW(database_path, L"..");
		StringCchCatW(database_path, MAX_ESE_PATH_CHARS, L"\\");*/

		// @TODO:
		// 1. Copy every file in this directory to a temporary location. This will require:
		// 1a. The normal CopyFile function which will be used for files that aren't being used by another
		// process (if we're exporting from a live machine) or for every file (if we're exporting from
		// a backup of another machine).
		// 1b. Our own force_copy_file function which uses the Native API to duplicate the file handle for
		// any file that is being used by another process (sharing violation). We'll map that file into memory
		// and then dump the contents into the temporary location we mentioned above.
		// 2. Regardless of how these files are copied, we'll mark this temporary for deletion.
		
		wchar_t index_directory_path[MAX_PATH_CHARS] = L"";
		StringCchCopyW(index_directory_path, MAX_PATH_CHARS, exporter->index_path);
		PathAppendW(index_directory_path, TEXT(".."));

		wchar_t temporary_directory_path[MAX_TEMPORARY_PATH_CHARS] = L"";
		if(create_temporary_directory(exporter->temporary_path, temporary_directory_path))
		{
			wchar_t search_path[MAX_PATH_CHARS] = L"";
			StringCchCopyW(search_path, MAX_PATH_CHARS, index_directory_path);
			PathAppendW(search_path, L"*");

			WIN32_FIND_DATAW file_find_data = {};
			HANDLE search_handle = FindFirstFileW(search_path, &file_find_data);
			
			bool found_file = search_handle != INVALID_HANDLE_VALUE;
			while(found_file)
			{
				// Ignore directories, "." and ".."
				if( (file_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
					&& wcscmp(file_find_data.cFileName, L".") != 0
					&& wcscmp(file_find_data.cFileName, L"..") != 0)
				{
					wchar_t copy_source_path[MAX_PATH_CHARS] = L"";
					StringCchCopyW(copy_source_path, MAX_PATH_CHARS, index_directory_path);
					PathAppendW(copy_source_path, file_find_data.cFileName);

					wchar_t copy_destination_path[MAX_PATH_CHARS] = L"";
					StringCchCopyW(copy_destination_path, MAX_PATH_CHARS, temporary_directory_path);
					PathAppendW(copy_destination_path, file_find_data.cFileName);

					if(!CopyFile(copy_source_path, copy_destination_path, FALSE))
					{
						if(GetLastError() == ERROR_SHARING_VIOLATION)
						{
							log_print(LOG_ERROR, "Internet Explorer 10 to 11: Failed to copy the database file '%ls' to the temporary recovery directory because it's being used by another process. Attempting to forcibly copy it.", file_find_data.cFileName);

							if(!windows_nt_force_copy_open_file(arena, copy_source_path, copy_destination_path))
							{
								log_print(LOG_ERROR, "Internet Explorer 10 to 11: Failed to forcibly copy the database file '%ls' to the temporary recovery directory.", file_find_data.cFileName);
							}
						}
						else
						{
							log_print(LOG_ERROR, "Internet Explorer 10 to 11: Failed to copy the database file '%ls' to the temporary recovery directory with the error code %lu.", file_find_data.cFileName, GetLastError());
						}
					}
				}

				found_file = FindNextFile(search_handle, &file_find_data) == TRUE;
			}

			if(search_handle != INVALID_HANDLE_VALUE)
			{
				FindClose(search_handle);
				search_handle = INVALID_HANDLE_VALUE;
			}
		}
		else
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %lu while trying to create the temporary recovery directory.", GetLastError());
			return;
		}

		wchar_t temporary_database_path[MAX_PATH_CHARS] = L"";
		StringCchCopyW(temporary_database_path, MAX_PATH_CHARS, temporary_directory_path);
		PathAppendW(temporary_database_path, index_filename);

		JET_ERR error_code = JET_errSuccess;
		JET_INSTANCE instance = JET_instanceNil;
		JET_SESID session_id = JET_sesidNil;
		JET_DBID database_id = JET_dbidNil;
		JET_TABLEID containers_table_id = JET_tableidNil;

		// @PageSize: We need to set the database's page size parameter to the same value that's stored in the database file.
		// Otherwise, we'd get the error JET_errPageSizeMismatch (-1213) when calling JetInit().
		unsigned long page_size = 0;
		error_code = JetGetDatabaseFileInfoW(temporary_database_path, &page_size, sizeof(page_size), JET_DbInfoPageSize);
		if(error_code < 0)
		{
			// Default to this value (taken from sample WebCacheV01.dat files) if we can't get it out of the database for some reason.
			page_size = 32768;
			log_print(LOG_WARNING, "Internet Explorer 10 to 11: Failed to get the ESE database's page size with the error code %ld. This value will default to %lu.", error_code, page_size);
		}
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramDatabasePageSize, page_size, NULL);

		JET_DBINFOMISC database_info = {};
		error_code = JetGetDatabaseFileInfoW(temporary_database_path, &database_info, sizeof(database_info), JET_DbInfoMisc);
		if(error_code == JET_errSuccess)
		{
			log_print(LOG_INFO, "Internet Explorer 10 to 11: The ESE database's state is '%ls'.", windows_nt_get_database_state_string(database_info.dbstate));
		}

		error_code = JetCreateInstanceW(&instance, L"WebCacheExporter");
		if(error_code < 0)
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to create the ESE instance.", error_code);
			windows_nt_ese_clean_up(temporary_directory_path, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}
		
		// The system parameters that use this path require it to end in a backslash.
		StringCchCatW(temporary_directory_path, MAX_TEMPORARY_PATH_CHARS, L"\\");
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramRecovery, 0, L"On");
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramMaxTemporaryTables, 0, NULL);
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramBaseName, 0, ese_files_prefix);
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramLogFilePath, 0, temporary_directory_path);
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramSystemPath, 0, temporary_directory_path);
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramAlternateDatabaseRecoveryPath, 0, temporary_directory_path);
		
		error_code = JetInit(&instance);
		if(error_code < 0)
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to initialize the ESE instance.", error_code);
			windows_nt_ese_clean_up(temporary_directory_path, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}
		
		error_code = JetBeginSessionW(instance, &session_id, NULL, NULL);
		if(error_code < 0)
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to begin the session.", error_code);
			windows_nt_ese_clean_up(temporary_directory_path, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}

		// @PageSize: Passing zero to the page size makes it so no maximum is enforced by the database engine.
		error_code = JetAttachDatabase2W(session_id, temporary_database_path, 0, JET_bitDbReadOnly);
		if(error_code < 0)
		{
			wchar_t error_message[1024];
			JET_API_PTR err = error_code;
			JetGetSystemParameterW(instance, session_id, JET_paramErrorToString, &err, error_message, sizeof(error_message));
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to attach the database '%ls'", error_code, temporary_database_path);
			windows_nt_ese_clean_up(temporary_directory_path, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}
	
		error_code = JetOpenDatabaseW(session_id, temporary_database_path, NULL, &database_id, JET_bitDbReadOnly);
		if(error_code < 0)
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to open the database '%ls'.", error_code, temporary_database_path);
			windows_nt_ese_clean_up(temporary_directory_path, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}

		error_code = JetOpenTableW(session_id, database_id, L"Containers", NULL, 0, JET_bitTableReadOnly | JET_bitTableSequential, &containers_table_id);
		if(error_code < 0)
		{
			log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to open the Containers table.", error_code);
			windows_nt_ese_clean_up(temporary_directory_path, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}

		// Empty or "C:\Users\<Username>\AppData\Local\Microsoft\Windows\WebCache"
		bool set_original_database_path = false;
		wchar_t original_database_path[MAX_PATH_CHARS] = L"";
		if(!exporter->is_exporting_from_default_locations && exporter->should_use_ie_hint)
		{
			set_original_database_path = true;
			StringCchCopyW(original_database_path, MAX_PATH_CHARS, exporter->ie_hint_path);
			PathAppendW(original_database_path, L"Microsoft\\Windows\\WebCache");
		}

		enum Container_Column_Index
		{
			IDX_NAME = 0,
			IDX_CONTAINER_ID = 1,
			IDX_DIRECTORY = 2,
			IDX_SECURE_DIRECTORIES = 3,
			NUM_CONTAINER_COLUMNS = 4
		};

		const wchar_t* CONTAINER_COLUMN_NAMES[NUM_CONTAINER_COLUMNS] =
		{
			L"Name", 				// JET_coltypText		(10)
			L"ContainerId",			// JET_coltypLongLong 	(15)
			L"Directory", 			// JET_coltypLongText 	(12)
			L"SecureDirectories" 	// JET_coltypLongText 	(12)
		};

		JET_COLUMNDEF container_column_info[NUM_CONTAINER_COLUMNS];
		for(size_t i = 0; i < NUM_CONTAINER_COLUMNS; ++i)
		{
			error_code = JetGetTableColumnInfoW(session_id, containers_table_id, CONTAINER_COLUMN_NAMES[i],
												&container_column_info[i], sizeof(container_column_info[i]), JET_ColInfo);
		}

		bool found_container_record = (JetMove(session_id, containers_table_id, JET_MoveFirst, 0) == JET_errSuccess);
		while(found_container_record)
		{
			wchar_t container_name[256];
			unsigned long actual_container_name_size;
			error_code = JetRetrieveColumn(	session_id, containers_table_id, container_column_info[IDX_NAME].columnid,
											container_name, sizeof(container_name), &actual_container_name_size, 0, NULL);
			size_t num_container_name_chars = actual_container_name_size / sizeof(wchar_t);

			// Check if the container record belongs to the cache.
			if(wcsncmp(container_name, L"Content", num_container_name_chars) == 0)
			{
				JET_RETRIEVECOLUMN container_columns[NUM_CONTAINER_COLUMNS]; // "ContainerId", "Directory", and "SecureDirectories".
				for(size_t i = 0; i < NUM_CONTAINER_COLUMNS; ++i)
				{
					container_columns[i].columnid = container_column_info[i].columnid;
					container_columns[i].pvData = NULL;
					container_columns[i].cbData = 0;
					container_columns[i].grbit = 0;
					container_columns[i].ibLongValue = 0;
					container_columns[i].itagSequence = 1;
				}

				s64 container_id;
				container_columns[IDX_CONTAINER_ID].pvData = &container_id;
				container_columns[IDX_CONTAINER_ID].cbData = sizeof(container_id);

				wchar_t directory[MAX_PATH_CHARS];
				container_columns[IDX_DIRECTORY].pvData = directory;
				container_columns[IDX_DIRECTORY].cbData = sizeof(directory);

				wchar_t secure_directories[NUM_CACHE_DIRECTORY_NAME_CHARS * MAX_NUM_CACHE_DIRECTORIES + 1];
				container_columns[IDX_SECURE_DIRECTORIES].pvData = secure_directories;
				container_columns[IDX_SECURE_DIRECTORIES].cbData = sizeof(secure_directories);

				// Skip retrieving the "Name" column and get only "ContainerId" onwards.
				error_code = JetRetrieveColumns(session_id, containers_table_id, &container_columns[IDX_CONTAINER_ID], NUM_CONTAINER_COLUMNS - 1);
				bool retrieval_success = true;
				for(size_t i = IDX_CONTAINER_ID; i < NUM_CONTAINER_COLUMNS; ++i)
				{
					if(container_columns[i].err != JET_errSuccess)
					{
						retrieval_success = false;

						JET_RECPOS record_position = {};
						error_code = JetGetRecordPosition(session_id, containers_table_id, &record_position, sizeof(record_position));
						log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to retrieve column %Iu for Content record %lu in the Containers table.", container_columns[i].err, i, record_position.centriesLT);
					}
				}

				// We'll only handle cache locations (records) whose column values were read correctly. Otherwise, we wouldn't have
				// enough information to properly export them.
				if(retrieval_success)
				{
					// @Assert: The names of the directories that contain the cached files should have exactly this many characters.
					_ASSERT( (wcslen(secure_directories) % NUM_CACHE_DIRECTORY_NAME_CHARS) == 0 );

					size_t num_cache_directories = wcslen(secure_directories) / NUM_CACHE_DIRECTORY_NAME_CHARS;
					wchar_t cache_directory_names[MAX_NUM_CACHE_DIRECTORIES][NUM_CACHE_DIRECTORY_NAME_CHARS + 1];
					size_t cache_directory_name_size = NUM_CACHE_DIRECTORY_NAME_CHARS * sizeof(wchar_t);
					for(size_t i = 0; i < num_cache_directories; ++i)
					{
						wchar_t* name = (wchar_t*) advance_bytes(secure_directories, i * cache_directory_name_size);
						CopyMemory(cache_directory_names[i], name, cache_directory_name_size);
						cache_directory_names[i][NUM_CACHE_DIRECTORY_NAME_CHARS] = L'\0';
					}

					const size_t NUM_CACHE_TABLE_NAME_CHARS = 10 + MAX_INT64_CHARS;
					wchar_t cache_table_name[NUM_CACHE_TABLE_NAME_CHARS]; // "Container_<s64 id>"
					if(SUCCEEDED(StringCchPrintfW(cache_table_name, NUM_CACHE_TABLE_NAME_CHARS, L"Container_%I64d", container_id)))
					{
						JET_TABLEID cache_table_id = JET_tableidNil;
						error_code = JetOpenTableW(session_id, database_id, cache_table_name, NULL, 0, JET_bitTableReadOnly | JET_bitTableSequential, &cache_table_id);
						if(error_code >= 0)
						{
							// >>>>
							// >>>> START EXPORTING
							// >>>>

							if(error_code > 0)
							{
								log_print(LOG_WARNING, "Internet Explorer 10 to 11: Opened the cache table '%ls' with warning %ld.", cache_table_name, error_code);
							}
							
							enum Cache_Column_Index
							{
								IDX_FILENAME = 0,
								IDX_URL = 1,
								IDX_FILE_SIZE = 2,
								IDX_LAST_MODIFIED_TIME = 3,
								IDX_CREATION_TIME = 4,
								IDX_LAST_ACCESS_TIME = 5,
								IDX_EXPIRY_TIME = 6,
								IDX_HEADERS = 7,
								IDX_SECURE_DIRECTORY = 8,
								IDX_ACCESS_COUNT = 9,
								NUM_CACHE_COLUMNS = 10
							};

							const wchar_t* CACHE_COLUMN_NAMES[NUM_CACHE_COLUMNS] =
							{
								L"Filename", 		// JET_coltypLongText 		(12)
								L"Url", 			// JET_coltypLongText 		(12)
								L"FileSize",		// JET_coltypLongLong 		(15)
								L"ModifiedTime",	// JET_coltypLongLong 		(15)
								L"CreationTime",	// JET_coltypLongLong 		(15)
								L"AccessedTime",	// JET_coltypLongLong 		(15)
								L"ExpiryTime",		// JET_coltypLongLong 		(15)
								L"ResponseHeaders",	// JET_coltypLongBinary 	(11)
								L"SecureDirectory",	// JET_coltypUnsignedLong 	(14)
								L"AccessCount"		// JET_coltypUnsignedLong 	(14)
							};

							JET_COLUMNDEF cache_column_info[NUM_CACHE_COLUMNS];
							for(size_t i = 0; i < NUM_CACHE_COLUMNS; ++i)
							{
								error_code = JetGetTableColumnInfoW(session_id, cache_table_id, CACHE_COLUMN_NAMES[i],
																	&cache_column_info[i], sizeof(cache_column_info[i]), JET_ColInfo);
							}

							bool found_cache_record = (JetMove(session_id, cache_table_id, JET_MoveFirst, 0) == JET_errSuccess);
							while(found_cache_record)
							{
								JET_RETRIEVECOLUMN cache_columns[NUM_CACHE_COLUMNS];

								for(size_t i = 0; i < NUM_CACHE_COLUMNS; ++i)
								{
									cache_columns[i].columnid = cache_column_info[i].columnid;
									cache_columns[i].pvData = NULL;
									cache_columns[i].cbData = 0;
									cache_columns[i].grbit = JET_bitRetrieveIgnoreDefault;
									cache_columns[i].ibLongValue = 0;
									cache_columns[i].itagSequence = 1;
								}
								error_code = JetRetrieveColumns(session_id, cache_table_id, cache_columns, NUM_CACHE_COLUMNS);

								unsigned long filename_size = cache_columns[IDX_FILENAME].cbActual;
								wchar_t* filename = push_arena(arena, filename_size, wchar_t);
								cache_columns[IDX_FILENAME].pvData = filename;
								cache_columns[IDX_FILENAME].cbData = filename_size;

								unsigned long url_size = cache_columns[IDX_URL].cbActual;
								wchar_t* url = push_arena(arena, url_size, wchar_t);
								cache_columns[IDX_URL].pvData = url;
								cache_columns[IDX_URL].cbData = url_size;

								s64 file_size;
								cache_columns[IDX_FILE_SIZE].pvData = &file_size;
								cache_columns[IDX_FILE_SIZE].cbData = sizeof(file_size);

								FILETIME last_modified_time_value;
								cache_columns[IDX_LAST_MODIFIED_TIME].pvData = &last_modified_time_value;
								cache_columns[IDX_LAST_MODIFIED_TIME].cbData = sizeof(last_modified_time_value);

								FILETIME creation_time_value;
								cache_columns[IDX_CREATION_TIME].pvData = &creation_time_value;
								cache_columns[IDX_CREATION_TIME].cbData = sizeof(creation_time_value);
								
								FILETIME last_access_time_value;
								cache_columns[IDX_LAST_ACCESS_TIME].pvData = &last_access_time_value;
								cache_columns[IDX_LAST_ACCESS_TIME].cbData = sizeof(last_access_time_value);

								FILETIME expiry_time_value;
								cache_columns[IDX_EXPIRY_TIME].pvData = &expiry_time_value;
								cache_columns[IDX_EXPIRY_TIME].cbData = sizeof(expiry_time_value);

								unsigned long headers_size = cache_columns[IDX_HEADERS].cbActual;
								char* headers = push_arena(arena, headers_size, char);
								cache_columns[IDX_HEADERS].pvData = headers;
								cache_columns[IDX_HEADERS].cbData = headers_size;

								u32 secure_directory_index;
								cache_columns[IDX_SECURE_DIRECTORY].pvData = &secure_directory_index;
								cache_columns[IDX_SECURE_DIRECTORY].cbData = sizeof(secure_directory_index);

								u32 access_count;
								cache_columns[IDX_ACCESS_COUNT].pvData = &access_count;
								cache_columns[IDX_ACCESS_COUNT].cbData = sizeof(access_count);

								error_code = JetRetrieveColumns(session_id, cache_table_id, cache_columns, NUM_CACHE_COLUMNS);
								for(size_t i = 0; i < NUM_CACHE_COLUMNS; ++i)
								{
									if(cache_columns[i].err < 0)
									{
										cache_columns[i].pvData = NULL;

										JET_RECPOS record_position = {};
										error_code = JetGetRecordPosition(session_id, cache_table_id, &record_position, sizeof(record_position));
										log_print(LOG_WARNING, "Internet Explorer 10 to 11: Error %ld while trying to retrieve column %Iu for Cache record %lu in the Cache table '%ls'.", cache_columns[i].err, i, record_position.centriesLT, cache_table_name);
									}
								}

								{
									wchar_t* decorated_filename = push_and_copy_to_arena(arena, filename_size, wchar_t, filename, filename_size);
									decorated_filename;
									undecorate_path(filename);
									wchar_t* file_extension = skip_to_file_extension(filename);

									decode_url(url);

									wchar_t cached_file_size[MAX_INT64_CHARS];
									convert_s64_to_string(file_size, cached_file_size);

									wchar_t last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS];
									format_filetime_date_time(last_modified_time_value, last_modified_time);

									wchar_t creation_time[MAX_FORMATTED_DATE_TIME_CHARS];
									format_filetime_date_time(creation_time_value, creation_time);

									wchar_t last_access_time[MAX_FORMATTED_DATE_TIME_CHARS];
									format_filetime_date_time(last_access_time_value, last_access_time);

									wchar_t expiry_time[MAX_FORMATTED_DATE_TIME_CHARS];
									format_filetime_date_time(expiry_time_value, expiry_time);

									wchar_t* server_response = NULL;
									wchar_t* cache_control = NULL;
									wchar_t* pragma = NULL;
									wchar_t* content_type = NULL;
									wchar_t* content_length = NULL;
									wchar_t* content_encoding = NULL;
									parse_cache_headers(arena, headers, headers_size,
														&server_response, &cache_control, &pragma,
														&content_type, &content_length, &content_encoding);

									wchar_t num_hits[MAX_INT32_CHARS];
									convert_u32_to_string(access_count, num_hits);

									// @Format: The cache directory indexes stored in the database are one based.
									secure_directory_index -= 1;
									_ASSERT(secure_directory_index < num_cache_directories);
									wchar_t* cache_directory = cache_directory_names[secure_directory_index];

									wchar_t short_file_path[MAX_PATH_CHARS];
									StringCchCopyW(short_file_path, MAX_PATH_CHARS, cache_directory);
									PathAppendW(short_file_path, decorated_filename);

									// @Format: 
									wchar_t full_file_path[MAX_PATH_CHARS] = L"";

									if(exporter->is_exporting_from_default_locations)
									{
										StringCchCopyW(full_file_path, MAX_PATH_CHARS, directory);
									}
									else
									{
										if(!set_original_database_path)
										{
											StringCchCopyW(original_database_path, MAX_PATH_CHARS, directory);
											PathAppendW(original_database_path, L"..\\..\\WebCache");
											set_original_database_path = !is_string_empty(original_database_path);
										}

										wchar_t path_from_database_to_cache[MAX_PATH_CHARS] = L"";
										PathRelativePathToW(path_from_database_to_cache,
															original_database_path, FILE_ATTRIBUTE_DIRECTORY, // From this directory...
															directory, FILE_ATTRIBUTE_DIRECTORY); // To this directory.

										StringCchCopyW(full_file_path, MAX_PATH_CHARS, index_directory_path);
										PathAppendW(full_file_path, path_from_database_to_cache);
									}

									PathAppendW(full_file_path, short_file_path);
									
									bool file_exists = does_file_exist(full_file_path);
									wchar_t* is_file_missing = (file_exists) ? (L"No") : (L"Yes");

									Csv_Entry csv_row[CSV_NUM_COLUMNS] =
									{
										{filename}, {url}, {file_extension}, {cached_file_size},
										{last_modified_time}, {creation_time}, {last_access_time}, {expiry_time},
										{server_response}, {cache_control}, {pragma}, {content_type}, {content_length}, {content_encoding},
										{num_hits}, {short_file_path}, {is_file_missing}
									};

									export_cache_entry(	exporter,
														CSV_COLUMN_TYPES, csv_row, CSV_NUM_COLUMNS,
														full_file_path, url, filename);
								}
								
								found_cache_record = (JetMove(session_id, cache_table_id, JET_MoveNext, 0) == JET_errSuccess);
							}

							// >>>>
							// >>>> FINISH EXPORTING
							// >>>>
							error_code = JetCloseTable(session_id, cache_table_id);
							cache_table_id = JET_tableidNil;
							if(error_code < 0)
							{
								log_print(LOG_WARNING, "Internet Explorer 10 to 11: Error %ld while trying to close the cache table '%ls'.", error_code, cache_table_name);
							}
						}
						else
						{
							log_print(LOG_ERROR, "Internet Explorer 10 to 11: Error %ld while trying to open the cache table '%ls'. The contents of this table will be ignored.", error_code, cache_table_name);
						}
					}
					else
					{
						log_print(LOG_ERROR, "Internet Explorer 10 to 11: Failed to format the cache container table's name for container ID %I64d.", container_id);
					}

				}
			}

			found_container_record = (JetMove(session_id, containers_table_id, JET_MoveNext, 0) == JET_errSuccess);
		}

		windows_nt_ese_clean_up(temporary_directory_path, &instance, &session_id, &database_id, &containers_table_id);
	}

#endif
