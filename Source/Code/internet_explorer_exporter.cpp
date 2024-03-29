#include "web_cache_exporter.h"
#include "internet_explorer_exporter.h"

#ifndef WCE_9X
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

	// @Future: We could maybe parse a subset of the format ourselves and remove the dependency above by investigating the source code:
	// https://github.com/microsoft/Extensible-Storage-Engine
#endif

/*
	This file defines how the exporter processes Internet Explorer (IE)'s cache. Although we use the term "Internet Explorer",
	this actually represents the WinINet (Windows Internet)'s cache database, which will contain more files than the ones cached
	by the IE browser. This database also holds the cache for other web browsers (like Microsoft Edge, before being Chromium based)
	and web plugins (like the 3DVIA Player).

	This cache container is one of the most important ones when it comes to recovering lost web media for a couple of reasons:
	
	1. Internet Explorer had a large market share in late 1990s and early 2000s, meaning it's more likely that an older web game was
	played in this browser. See: https://en.wikipedia.org/wiki/Usage_share_of_web_browsers#Summary_tables
	2. It has been noted that IE 6 and older had a bug where cached files could persist on disk even after being removed from the
	index file. This improves the chances of finding lost web media in this cache location.

	@SupportedFormats:
	- Internet Explorer 4 (index.dat)
	- Internet Explorer 5 to 9 (Content.IE5\index.dat)
	- Internet Explorer 10 and 11 (WebCacheV01.dat and WebCacheV24.dat - JET Blue / ESE databases)

	@DefaultCacheLocations:
	- 95, 98, ME 	C:\WINDOWS\Temporary Internet Files
	- 2000, XP 		C:\Documents and Settings\<Username>\Local Settings\Temporary Internet Files
	- Vista, 7	 	C:\Users\<Username>\AppData\Local\Microsoft\Windows\Temporary Internet Files
	- 8.1, 10	 	C:\Users\<Username>\AppData\Local\Microsoft\Windows\INetCache

	In addition to these locations, assume that "<Cache Location>\Low" also exists and contains cached files like these previous
	locations. For example: "C:\Users\<Username>\AppData\Local\Microsoft\Windows\INetCache\Low".
	See:
	- https://helgeklein.com/blog/2009/01/internet-explorer-in-protected-mode-how-the-low-integrity-environment-gets-created/
	- https://kb.digital-detective.net/display/BF/Understanding+and+Working+in+Protected+Mode+Internet+Explorer

	For IE 4:
	- Cached Files: <Cache Location>\<8 Character Directory>
	- Index File: <Cache Location>\index.dat

	For IE 5 to 9:
	- Cached Files: <Cache Location>\Content.IE5\<8 Character Directory>
	- Index File: <Cache Location>\Content.IE5\index.dat

	For IE 10 and 11:
	- Cached Files: <Cache Location>\IE\<8 Character Directory>
	- Database File: <Cache Location>\..\WebCache\WebCacheV01.dat or WebCacheV24.dat

	@SupportsCustomCacheLocations:
	- Same Machine: Yes, since it's determined by querying the Windows API.
	- External Locations: Yes, since there's a dedicated field (called INTERNET_CACHE) that is used for the Windows cache.

	@Resources: Previous reverse engineering efforts that specify how the index.dat file format (IE 4 to 9) should be processed.
	Note that we don't handle the entirety of these formats (index.dat or ESE databases). We only process the subset of the file
	formats that is useful for this application. Any used members in the data structures that represent the various parts of the
	index.dat file are marked with @Used.
	
	[GC] "The INDEX.DAT File Format"
	--> http://www.geoffchappell.com/studies/windows/ie/wininet/api/urlcache/indexdat.htm
	
	[JM] "MSIE Cache File (index.dat) format specification"
	--> https://github.com/libyal/libmsiecf/blob/master/documentation/MSIE%20Cache%20File%20(index.dat)%20format.asciidoc
	
	[NS-B1] "A few words about the cache / history on Internet Explorer 10"
	--> https://blog.nirsoft.net/2012/12/08/a-few-words-about-the-cache-history-on-internet-explorer-10/
	
	[NS-B2] "Improved solution for reading the history of Internet Explorer 10"
	--> https://blog.nirsoft.net/2013/05/02/improved-solution-for-reading-the-history-of-internet-explorer-10/

	See also: https://kb.digital-detective.net/display/BF/Internet+Explorer

	@Tools: Existing software that also reads IE's cache.
	
	[NS-T1] "IECacheView v1.58 - Internet Explorer Cache Viewer"
	--> https://www.nirsoft.net/utils/ie_cache_viewer.html
	--> Used to validate the output of this application for IE 5 to 11.
	
	[NS-T2] "ESEDatabaseView v1.65"
	--> https://www.nirsoft.net/utils/ese_database_view.html
	--> Used to explore an existing JET Blue / ESE database in order to figure out how to process the cache for IE 10 and 11.
*/

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_URL, CSV_FILE_EXTENSION, CSV_FILE_SIZE,
	CSV_LAST_MODIFIED_TIME, CSV_CREATION_TIME, CSV_LAST_WRITE_TIME, CSV_LAST_ACCESS_TIME, CSV_EXPIRY_TIME, CSV_ACCESS_COUNT,
	CSV_RESPONSE, CSV_SERVER, CSV_CACHE_CONTROL, CSV_PRAGMA, CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_RANGE, CSV_CONTENT_ENCODING, 
	CSV_DECOMPRESSED_FILE_SIZE, CSV_LOCATION_ON_CACHE, CSV_CACHE_VERSION,
	CSV_MISSING_FILE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR, CSV_EXPORTER_WARNING,
	CSV_CUSTOM_FILE_GROUP, CSV_CUSTOM_URL_GROUP, CSV_SHA_256
};
static const int CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// ----------------------------------------------------------------------------------------------------

// @FormatVersion: Internet Explorer 4 to 9 (index.dat).
// @ByteOrder: Little Endian.
// @CharacterEncoding: ASCII. There is some data in the index.dat file that uses UTF-16 LE (according to [JM]), but we don't handle those parts.
// @DateTimeFormat: FILETIME and DOS date time.

// @Format: Various constants for the index.dat file.
static const size_t IE_4_5_NUM_SIGNATURE_CHARS = 28;
static const size_t IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS = 8;
static const size_t IE_4_5_ESE_MAX_NUM_CACHE_DIRECTORIES = 32;
static const size_t IE_4_5_HEADER_DATA_LENGTH = 32;
static const size_t IE_4_5_ALLOCATION_BITMAP_SIZE = 0x3DB0;
static const size_t IE_4_5_BLOCK_SIZE = 128;

// @Format: Deallocated blocks in index.dat are filled with this value.
static const u32 IE_4_5_DEALLOCATED_VALUE = 0x0BADF00D;

// @Format: The signature that identifies each entry in index.dat.
// We must be aware of all of them to properly traverse the allocated blocks.
enum Ie_Index_Entry_Signature
{
	ENTRY_URL = 0x204C5255, // "URL "
	ENTRY_REDIRECT = 0x52444552, // "REDR"
	ENTRY_LEAK = 0x4B41454C, // "LEAK"
	ENTRY_HASH = 0x48534148, // "HASH"

	// Mentioned in [GC].
	ENTRY_DELETED = 0x204C4544, // "DEL "
	ENTRY_UPDATED = 0x20445055, // "UPD "

	ENTRY_NEWLY_ALLOCATED = 0xDEADBEEF
	// The value in IE_4_5_DEALLOCATED_VALUE can also appear in an entry's signature member.
};

// We'll tightly pack the structures that represent different parts of the index.dat file and then access each member directly after
// mapping it into memory. Due to the way the file is designed, there shouldn't be any memory alignment problems when accessing this
// structure's values. 
#pragma pack(push, 1)

// @Format: The header for the index.dat file.
struct Ie_4_5_Index_Header
{
	u8 signature[IE_4_5_NUM_SIGNATURE_CHARS]; // @Used. Includes the null terminator.
	u32 file_size; // @Used.
	u32 file_offset_to_first_hash_table_page;

	u32 num_blocks; // @Used.
	u32 num_allocated_blocks;
	u32 _reserved_1;
	
	u32 max_size;
	u32 _reserved_2;
	u32 cache_size;
	u32 _reserved_3;
	u32 sticky_cache_size;
	u32 _reserved_4;
	
	u32 num_directories; // @Used.
	struct
	{
		u32 num_files;
		u8 name[IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS]; // Does *not* include the null terminator.
	} cache_directories[IE_4_5_ESE_MAX_NUM_CACHE_DIRECTORIES]; // @Used.

	u32 header_data[IE_4_5_HEADER_DATA_LENGTH];
	
	u32 _reserved_5;
};

// @Format: The beginning of each entry in the index.dat file.
struct Ie_4_5_Index_File_Map_Entry
{
	u32 signature; // @Used.
	u32 num_allocated_blocks; // @Used.
};

// @Format: The body of a URL entry in the index.dat file (IE 4, format version 4.7).
struct Ie_4_Index_Url_Entry
{
	u64 last_modified_time; // @Used.
	u64 last_access_time; // @Used.
	u64 expiry_time; // @Used.

	u32 cached_file_size; // @Used.
	u32 _reserved_1; // @TODO: High part of a 64-bit file size?
	u32 _reserved_2;
	u32 _reserved_3;

	u32 _reserved_4;
	u32 _reserved_5;
	u32 entry_offset_to_url; // @Used.

	u8 cache_directory_index; // @Used.
	u8 _reserved_6;
	u8 _reserved_7;
	u8 _reserved_8;

	u32 entry_offset_to_filename; // @Used.
	u32 cache_flags;
	u32 entry_offset_to_headers; // @Used.
	u32 headers_size; // @Used.
	
	u32 _reserved_9;
	u32 last_sync_time;
	u32 num_entry_locks; // @Used. Represents the number of hits (in practice at least).
	u32 _reserved_10;
	
	u32 creation_time; // @Used.
	u32 _reserved_11;
};

// @Format: The body of a URL entry in the index.dat file (IE 5 to 9, format version 5.2).
struct Ie_5_Index_Url_Entry
{
	u64 last_modified_time; // @Used.
	u64 last_access_time; // @Used.
	u32 expiry_time; // @Used.
	u32 _reserved_1;

	u32 low_cached_file_size; // @Used.
	u32 high_cached_file_size; // @Used

	u32 file_offset_to_group_or_group_list;
	
	union
	{
		u32 sticky_time_delta; // For a URL entry.
		u32 file_offset_to_next_leak_entry; // For a LEAK entry.
	};

	u32 _reserved_3;
	u32 entry_offset_to_url; // @Used.

	u8 cache_directory_index; // // @Used.
	u8 sync_count;
	u8 format_version;
	u8 format_version_copy;

	u32 entry_offset_to_filename; // @Used.
	u32 cache_flags;
	u32 entry_offset_to_headers; // @Used.
	u32 headers_size; // @Used.
	
	u32 entry_offset_to_file_extension;
	u32 last_sync_time;
	u32 num_entry_locks; // @Used. Represents the number of hits (in practice at least).
	u32 level_of_entry_lock_nesting;
	
	u32 creation_time; // @Used.
	u32 _reserved_4;
	u32 _reserved_5;
};

#pragma pack(pop)

_STATIC_ASSERT(sizeof(Ie_4_5_Index_Header) == 0x0250);
_STATIC_ASSERT(sizeof(Ie_4_5_Index_File_Map_Entry) == 0x08);
_STATIC_ASSERT(sizeof(Ie_4_Index_Url_Entry) == 0x60);
_STATIC_ASSERT(sizeof(Ie_5_Index_Url_Entry) == 0x60);

// ----------------------------------------------------------------------------------------------------

// Finds the current Internet Explorer version by querying the registry. This method is recommended in the following Windows
// documentation page, @Docs: https://docs.microsoft.com/en-us/troubleshoot/browsers/information-about-ie-version
//
// @GetLastError
//
// @Parameters:
// 1. ie_version - The buffer that receives the string with Internet Explorer's version in the form of:
// <major version>.<minor version>.<build number>.<subbuild number>
// 2. ie_version_size - The size of the buffer in bytes.
//
// @Returns: True if it's able to find Internet Explorer's version in the registry. Otherwise, false.
bool find_internet_explorer_version(TCHAR* ie_version, u32 ie_version_size)
{
	// We'll try "svcVersion" first since that one contains the correct value for the newer IE versions. In older versions this would
	// fails and we would resort to the "Version" key.
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
	#if defined(WCE_DEBUG) && !defined(WCE_9X)
		wchar_t actual_undecorated_path[MAX_PATH_CHARS] = L"";
		StringCchCopyW(actual_undecorated_path, MAX_PATH_CHARS, path);
		PathUndecorateW(actual_undecorated_path);
	#endif

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
	while(*filename != T('\0'))
	{
		if(*filename == T('[') && !is_first_char && filename < file_extension)
		{
			decoration_begin = filename;
			++filename;

			while(_istdigit(*filename))
			{
				++filename;
			}

			if(*filename == T(']'))
			{
				decoration_end = filename;
			}
		}

		// Check if it's different than NUL for the case where the decoration isn't closed (e.g. "C:\path\file[1"),
		// meaning 'filename' would already point to the end of the string.
		if(*filename != T('\0')) ++filename;
		is_first_char = false;
	}

	if(decoration_begin != NULL && decoration_end != NULL)
	{
		_ASSERT(decoration_begin < decoration_end);
		TCHAR* remaining_path = decoration_end + 1;
		MoveMemory(decoration_begin, remaining_path, string_size(remaining_path));
	}

	#if defined(WCE_DEBUG) && !defined(WCE_9X)
		// @Assert: Guarantee that our result matches PathUndecorate()'s.
		_ASSERT(strings_are_equal(path, actual_undecorated_path));
	#endif
}

// Entry point for Internet Explorer's cache exporter. This function will determine where to look for the cache before
// processing its contents.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how Internet Explorer's cache should be exported.
// If the path to this location isn't defined, this function will try to find it using the CSIDL value for the Temporary
// Internet Files directory.
//
// @Returns: Nothing.

static void export_internet_explorer_4_to_9_cache(Exporter* exporter);
static void export_internet_explorer_10_to_11_cache(Exporter* exporter, const wchar_t* ese_files_prefix);
static TRAVERSE_DIRECTORY_CALLBACK(find_internet_explorer_4_to_9_cache_files_callback);

void export_default_or_specific_internet_explorer_cache(Exporter* exporter)
{
	console_print("Exporting the Internet Explorer cache...");
	
	bool ie_4_to_9_cache_exists = false;

	initialize_cache_exporter(exporter, CACHE_INTERNET_EXPLORER, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		set_exporter_output_copy_subdirectory(exporter, T("Cache"));

		if(exporter->is_exporting_from_default_locations)
		{
			StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, exporter->wininet_cache_path);
		}

		log_info("Internet Explorer 4 to 9: Exporting the cache from '%s'.", exporter->cache_path);

		#define EXPORT_USING_INDEX_FILE(short_index_path)\
		do\
		{\
			log_newline();\
			log_info("Internet Explorer 4 to 9: Checking the index file '%hs'.", short_index_path);\
			PathCombine(exporter->index_path, exporter->cache_path, T(short_index_path));\
			export_internet_explorer_4_to_9_cache(exporter);\
			ie_4_to_9_cache_exists = ie_4_to_9_cache_exists || does_file_exist(exporter->index_path);\
		} while(false, false)

		EXPORT_USING_INDEX_FILE("index.dat");
		EXPORT_USING_INDEX_FILE("Low\\index.dat");
		EXPORT_USING_INDEX_FILE("Content.IE5\\index.dat");
		EXPORT_USING_INDEX_FILE("Low\\Content.IE5\\index.dat");

		#undef EXPORT_USING_INDEX_FILE

		log_newline();
		log_info("Internet Explorer 4 to 9: Finished exporting the cache.");

		#ifndef WCE_9X

			if(exporter->is_exporting_from_default_locations)
			{
				PathCombineW(exporter->cache_path, exporter->local_appdata_path, L"Microsoft\\Windows\\WebCache");
			}

			log_newline();
			log_info("Internet Explorer 10 to 11: Exporting the cache from '%s'.", exporter->cache_path);

			#define EXPORT_USING_DATABASE_FILE(ese_files_prefix)\
			do\
			{\
				const wchar_t* database_filename = T("WebCache") T(ese_files_prefix) T(".dat");\
				log_newline();\
				log_info("Internet Explorer 10 to 11: Checking the database file '%ls'.", database_filename);\
				PathCombineW(exporter->index_path, exporter->cache_path, database_filename);\
				export_internet_explorer_10_to_11_cache(exporter, T(ese_files_prefix));\
			} while(false, false)
			
			EXPORT_USING_DATABASE_FILE("V01");
			EXPORT_USING_DATABASE_FILE("V24");

			#undef EXPORT_USING_DATABASE_FILE

			log_newline();
			log_info("Internet Explorer 10 to 11: Finished exporting the cache.");

		#endif

		if(ie_4_to_9_cache_exists)
		{
			if(exporter->is_exporting_from_default_locations)
			{
				StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, exporter->wininet_cache_path);
			}

			log_newline();
			log_info("Raw Internet Explorer 4 to 9: Exporting the raw cache from '%s'.", exporter->cache_path);

			set_exporter_output_copy_subdirectory(exporter, T("Raw"));
			traverse_directory_objects(	exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, true,
										find_internet_explorer_4_to_9_cache_files_callback, exporter);		
		}
	}
	terminate_cache_exporter(exporter);
}

// Called every time a file is found in Internet Explorer 4 to 9's cache directory. Used to perform
// a raw export where the files are copied without relying on the metadata in the index.dat file.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_internet_explorer_4_to_9_cache_files_callback)
{
	// Skip the index.dat file itself. We only want the cached files.
	TCHAR* filename = callback_info->object_name;
	if(filenames_are_equal(filename, T("index.dat")) || filenames_are_equal(filename, T("desktop.ini"))) return true;

	TCHAR* full_location_on_cache = callback_info->object_path;
	TCHAR* short_location_on_cache = skip_to_last_path_components(full_location_on_cache, 2);
	TCHAR* cache_version = T("Raw");

	// We can remove the filename's decoration to obtain the original name.
	undecorate_path(filename);

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* URL */}, {/* File Extension */}, {/* File Size */},
		{/* Last Modified Time */}, {/* Creation Time */}, {/* Last Write Time */}, {/* Last Access Time */}, {/* Expiry Time */}, {/* Access Count */},
		{/* Response */}, {/* Server */}, {/* Cache Control */}, {/* Pragma */},
		{/* Content Type */}, {/* Content Length */}, {/* Content Range */}, {/* Content Encoding */},
		{/* Decompressed File Size */}, {/* Location On Cache */}, {cache_version},
		{/* Missing File */}, {/* Location In Output */}, {/* Copy Error */}, {/* Exporter Warning */},
		{/* Custom File Group */}, {/* Custom URL Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter* exporter = (Exporter*) callback_info->user_data;

	Exporter_Params params = {};
	params.copy_source_path = full_location_on_cache;
	params.filename = filename; // We want the undecorated filename, and not the real one.
	params.short_location_on_cache = short_location_on_cache;
	params.file_info = callback_info;

	export_cache_entry(exporter, csv_row, &params);

	return true;
}

// Exports Internet Explorer 4 through 9's cache from a given location.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how Internet Explorer's cache should be exported.
//
// @Returns: Nothing.
static void export_internet_explorer_4_to_9_cache(Exporter* exporter)
{
	if(!does_file_exist(exporter->index_path))
	{
		log_info("Internet Explorer 4 to 9: Skipping the missing index file '%s'.", exporter->index_path);
		return;
	}

	Arena* arena = &(exporter->temporary_arena);

	HANDLE index_handle = create_handle(exporter->index_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
	if(index_handle == INVALID_HANDLE_VALUE)
	{
		DWORD error_code = GetLastError();
		if(error_code == ERROR_SHARING_VIOLATION)
		{
			log_warning("Internet Explorer 4 to 9: Failed to get the index file handle since its being used by another process. Attempting to create a temporary copy.");
		
			TCHAR temporary_index_path[MAX_PATH_CHARS] = T("");
			bool copy_success = create_placeholder_exporter_file(exporter, temporary_index_path)
								&& copy_open_file(arena, exporter->index_path, temporary_index_path);

			if(copy_success)
			{
				log_info("Internet Explorer 4 to 9: Copied the index file to the temporary file in '%s'.", temporary_index_path);
				index_handle = create_handle(temporary_index_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
			}
			else
			{
				log_error("Internet Explorer 4 to 9: Failed to create a temporary copy of the index file.");
			}
		}
		else
		{
			log_error("Internet Explorer 4 to 9: Failed to get the index file handle with the error code %lu.", error_code);
		}
	}

	if(index_handle == INVALID_HANDLE_VALUE)
	{
		log_error("Internet Explorer 4 to 9: Failed to open the index file.");
		return;
	}

	u64 total_bytes_read = 0;

	Ie_4_5_Index_Header* header = (Ie_4_5_Index_Header*) aligned_push_arena(arena, sizeof(Ie_4_5_Index_Header) + IE_4_5_ALLOCATION_BITMAP_SIZE, MAX_SCALAR_ALIGNMENT_SIZE);
	if(!read_file_chunk(index_handle, header, sizeof(Ie_4_5_Index_Header) + IE_4_5_ALLOCATION_BITMAP_SIZE, total_bytes_read))
	{
		log_error("Internet Explorer 4 to 9: Failed to read the index file header and bitmap.");
		safe_close_handle(&index_handle);
		return;
	}

	total_bytes_read += sizeof(Ie_4_5_Index_Header) + IE_4_5_ALLOCATION_BITMAP_SIZE;
	
	{
		LITTLE_ENDIAN_TO_HOST(header->file_size);
		LITTLE_ENDIAN_TO_HOST(header->file_offset_to_first_hash_table_page);
		
		LITTLE_ENDIAN_TO_HOST(header->num_blocks);
		LITTLE_ENDIAN_TO_HOST(header->num_allocated_blocks);
		LITTLE_ENDIAN_TO_HOST(header->_reserved_1);
		
		LITTLE_ENDIAN_TO_HOST(header->max_size);
		LITTLE_ENDIAN_TO_HOST(header->_reserved_2);
		LITTLE_ENDIAN_TO_HOST(header->cache_size);
		LITTLE_ENDIAN_TO_HOST(header->_reserved_3);
		LITTLE_ENDIAN_TO_HOST(header->sticky_cache_size);
		LITTLE_ENDIAN_TO_HOST(header->_reserved_4);

		LITTLE_ENDIAN_TO_HOST(header->num_directories);
		for(int i = 0; i < _countof(header->cache_directories); ++i)
		{
			LITTLE_ENDIAN_TO_HOST(header->cache_directories[i].num_files);
			// No need to swap the byte order of the byte array: header->cache_directories[i].name
		}

		for(int i = 0; i < _countof(header->header_data); ++i) LITTLE_ENDIAN_TO_HOST(header->header_data[i]);

		LITTLE_ENDIAN_TO_HOST(header->_reserved_5);
	}

	u64 index_file_size = 0;
	if(!get_file_size(index_handle, &index_file_size))
	{
		log_error("Internet Explorer 4 to 9: Failed to get the index file's size.");
		safe_close_handle(&index_handle);
		return;
	}

	if(index_file_size != header->file_size)
	{
		log_warning("Internet Explorer 4 to 9: The size of the index file (%I64u) is different than the size specified in the header (%I32u).", index_file_size, header->file_size);
	}

	if(!string_begins_with((char*) header->signature, "Client UrlCache MMF Ver "))
	{
		char signature_string[IE_4_5_NUM_SIGNATURE_CHARS + 1] = "";
		CopyMemory(signature_string, header->signature, IE_4_5_NUM_SIGNATURE_CHARS);
		signature_string[IE_4_5_NUM_SIGNATURE_CHARS] = '\0';

		log_error("Internet Explorer 4 to 9: The index file has an invalid signature: '%hs'.", signature_string);
		safe_close_handle(&index_handle);
		return;
	}

	// We only handle two versions of the index file format: 4.7 and 5.2.
	char major_version = header->signature[24];
	char minor_version = header->signature[26];
	const size_t MAX_CACHE_VERSION_CHARS = 4;
	TCHAR cache_version[MAX_CACHE_VERSION_CHARS] = T("");
	StringCchPrintf(cache_version, MAX_CACHE_VERSION_CHARS, T("%hc.%hc"), major_version, minor_version);

	if( (major_version == '4' && minor_version == '7') || (major_version == '5' && minor_version == '2') )
	{
		log_info("Internet Explorer 4 to 9: The index file version %s was opened successfully.", cache_version);
	}
	else
	{
		log_error("Internet Explorer 4 to 9: The index file was opened successfully but its version (%s) is not supported.", cache_version);
		safe_close_handle(&index_handle);
		return;
	}

	// Go through each bit to check if a particular block was allocated. If so, we'll skip to that block and handle
	// that specific entry type. If not, we'll ignore it and move to the next one.
	u8* allocation_bitmap = (u8*) advance_bytes(header, sizeof(Ie_4_5_Index_Header));
	u32 total_num_blocks = ((u32) (index_file_size - total_bytes_read)) / IE_4_5_BLOCK_SIZE;
	
	if(total_num_blocks != header->num_blocks)
	{
		log_warning("Internet Explorer 4 to 9: The number of blocks in the index file (%I32u) is different than the value specified in the header (%I32u).", total_num_blocks, header->num_blocks);
	}

	const size_t MAX_NUM_BLOCKS = IE_4_5_ALLOCATION_BITMAP_SIZE * CHAR_BIT;
	if(total_num_blocks > MAX_NUM_BLOCKS)
	{
		log_warning("Internet Explorer 4 to 9: The number of blocks in the index file (%I32u) is larger than the maximum possible value (%Iu).", total_num_blocks, MAX_NUM_BLOCKS);
		total_num_blocks = MAX_NUM_BLOCKS;
	}

	const u32 NUM_BLOCKS_PER_READ = 1000; 
	_STATIC_ASSERT(NUM_BLOCKS_PER_READ >= 100);
	u32 block_buffer_size = get_arena_chunk_buffer_size(arena, NUM_BLOCKS_PER_READ * IE_4_5_BLOCK_SIZE);
	void* block_buffer = aligned_push_arena(arena, block_buffer_size, MAX_SCALAR_ALIGNMENT_SIZE);

	int num_url_entries = 0;
	int num_leak_entries = 0;

	int num_redirect_entries = 0;
	int num_hash_entries = 0;
	int num_updated_entries = 0;
	int num_deleted_entries = 0;
	int num_newly_allocated_entries = 0;

	int num_deallocated_entries = 0;
	int num_unknown_entries = 0;

	lock_arena(arena);

	bool must_read_chunk = true;
	for(u32 block_count = 0, block_index = 0; block_count < total_num_blocks; ++block_count, ++block_index)
	{
		u32 byte_index = block_count / CHAR_BIT;
		u32 block_index_in_byte = block_count % CHAR_BIT;

		bool is_block_allocated = ( allocation_bitmap[byte_index] & (1 << block_index_in_byte) ) != 0;
		if(!is_block_allocated) continue;

		u64 block_offset_in_index = sizeof(Ie_4_5_Index_Header) + IE_4_5_ALLOCATION_BITMAP_SIZE + block_count * IE_4_5_BLOCK_SIZE;
		if(must_read_chunk || block_offset_in_index >= total_bytes_read)
		{
			must_read_chunk = false;
			block_index = 0;

			u32 num_bytes_read = 0;
			if(read_file_chunk(index_handle, block_buffer, block_buffer_size, block_offset_in_index, true, &num_bytes_read))
			{
				if(num_bytes_read == 0)
				{
					log_error("Internet Explorer 4 to 9: Reached the end of the index file unexpectedly after processing %I32u blocks.", block_count);
					break;
				}
				
				total_bytes_read += num_bytes_read;
			}
			else
			{
				log_error("Internet Explorer 4 to 9: Failed to read the next block chunk from the index file after processing %I32u blocks.", block_count);
				break;
			}
		}

		_ASSERT(block_index * IE_4_5_BLOCK_SIZE < block_buffer_size);
		Ie_4_5_Index_File_Map_Entry* entry = (Ie_4_5_Index_File_Map_Entry*) advance_bytes(block_buffer, block_index * IE_4_5_BLOCK_SIZE);
		_ASSERT(IS_POINTER_ALIGNED_TO_TYPE(entry, u32));

		LITTLE_ENDIAN_TO_HOST(entry->signature);
		LITTLE_ENDIAN_TO_HOST(entry->num_allocated_blocks);

		// This should not happen if the index file was written correctly.
		if(entry->num_allocated_blocks == 0)
		{
			log_error("Internet Explorer 4 to 9: The entry starting in block %I32u has zero allocated blocks.", block_count);
			_ASSERT(false);
			break;
		}

		if(entry->signature == ENTRY_URL || entry->signature == ENTRY_LEAK || entry->signature == ENTRY_HASH
		|| entry->signature == ENTRY_UPDATED || entry->signature == ENTRY_DELETED || entry->signature == ENTRY_NEWLY_ALLOCATED)
		{
			if(entry->num_allocated_blocks > NUM_BLOCKS_PER_READ)
			{
				log_error("Internet Explorer 4 to 9: The entry starting in block %I32u has %I32u allocated blocks which exceeds the maximum value of %I32u.", block_count, entry->num_allocated_blocks, NUM_BLOCKS_PER_READ);
				_ASSERT(false);
				break;
			}

			if((block_offset_in_index + entry->num_allocated_blocks * IE_4_5_BLOCK_SIZE) > total_bytes_read)
			{
				--block_count;
				--block_index;
				total_bytes_read = block_offset_in_index;
				must_read_chunk = true;
				continue;
			}
		}

		bool skip_remaining_blocks = true;

		switch(entry->signature)
		{
			case(ENTRY_URL):
			case(ENTRY_LEAK):
			{
				// Some entries may contain garbage fields whose value is IE_4_5_DEALLOCATED_VALUE (which is used to fill deallocated
				// blocks). We'll use this macro to check if the low 32 bits of each member match this value. If so, we'll
				// clear them to zero. Empty strings or NULL values will show up as missing values in the CSV files.
				// This won't work for the few u8 members, though we only use 'cache_directory_index' whose value is always
				// strictly checked to see if it's within the correct bounds. Note that the low part of the cached file
				// size may still exist even if the high part is garbage. For example:
				// - low_cached_file_size = 1234
				// - high_cached_file_size = IE_4_5_DEALLOCATED_VALUE
				// Since these values are checked individually, we'll still keep the useful value and set the high part
				// to zero.
				bool found_deallocated_member = false;
				#define READ_MEMBER(member)\
				do\
				{\
					LITTLE_ENDIAN_TO_HOST(url_entry->member);\
					if( (major_version <= '4') && (((u32) url_entry->member & 0xFFFFFFFF) == IE_4_5_DEALLOCATED_VALUE) )\
					{\
						url_entry->member = 0;\
						found_deallocated_member = true;\
					}\
				} while(false, false)

				u32 entry_offset_to_filename = 0;
				u32 entry_offset_to_url = 0;
				u32 entry_offset_to_headers = 0;
				u32 headers_size = 0;
	
				TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
				TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
				TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
				TCHAR creation_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");

				u8 cache_directory_index = 0;

				TCHAR cached_file_size[MAX_INT_64_CHARS] = T("");
				TCHAR access_count[MAX_INT_32_CHARS] = T("");

				#define READ_COMMON()\
				do\
				{\
					entry_offset_to_filename = url_entry->entry_offset_to_filename;\
					entry_offset_to_url = url_entry->entry_offset_to_url;\
					entry_offset_to_headers = url_entry->entry_offset_to_headers;\
					headers_size = url_entry->headers_size;\
					\
					format_filetime_date_time(url_entry->last_modified_time, last_modified_time);\
					format_filetime_date_time(url_entry->last_access_time, last_access_time);\
					format_dos_date_time(url_entry->creation_time, creation_time);\
					\
					cache_directory_index = url_entry->cache_directory_index;\
					\
					convert_u32_to_string(url_entry->num_entry_locks, access_count);\
				} while(false, false)

				if(major_version == '4')
				{
					Ie_4_Index_Url_Entry* url_entry = (Ie_4_Index_Url_Entry*) advance_bytes(entry, sizeof(Ie_4_5_Index_File_Map_Entry));
					_ASSERT(IS_POINTER_ALIGNED_TO_TYPE(url_entry, u64));

					READ_MEMBER(last_modified_time);
					READ_MEMBER(last_access_time);
					READ_MEMBER(expiry_time);

					READ_MEMBER(cached_file_size);
					READ_MEMBER(_reserved_1);
					READ_MEMBER(_reserved_2);
					READ_MEMBER(_reserved_3);

					READ_MEMBER(_reserved_4);
					READ_MEMBER(_reserved_5);
					READ_MEMBER(entry_offset_to_url);

					READ_MEMBER(cache_directory_index);
					READ_MEMBER(_reserved_6);
					READ_MEMBER(_reserved_7);
					READ_MEMBER(_reserved_8);

					READ_MEMBER(entry_offset_to_filename);
					READ_MEMBER(cache_flags);
					READ_MEMBER(entry_offset_to_headers);
					READ_MEMBER(headers_size);
					
					READ_MEMBER(_reserved_9);
					READ_MEMBER(last_sync_time);
					READ_MEMBER(num_entry_locks);
					READ_MEMBER(_reserved_10);
					
					READ_MEMBER(creation_time);
					READ_MEMBER(_reserved_11);

					READ_COMMON();

					format_filetime_date_time(url_entry->expiry_time, expiry_time);
					convert_u32_to_string(url_entry->cached_file_size, cached_file_size);
				}
				else if(major_version == '5')
				{
					Ie_5_Index_Url_Entry* url_entry = (Ie_5_Index_Url_Entry*) advance_bytes(entry, sizeof(Ie_4_5_Index_File_Map_Entry));
					_ASSERT(IS_POINTER_ALIGNED_TO_TYPE(url_entry, u64));

					READ_MEMBER(last_modified_time);
					READ_MEMBER(last_access_time);
					READ_MEMBER(expiry_time);
					READ_MEMBER(_reserved_1);

					READ_MEMBER(low_cached_file_size);
					READ_MEMBER(high_cached_file_size);

					READ_MEMBER(file_offset_to_group_or_group_list);
					
					if(entry->signature == ENTRY_URL) READ_MEMBER(sticky_time_delta);
					else READ_MEMBER(file_offset_to_next_leak_entry);

					READ_MEMBER(_reserved_3);
					READ_MEMBER(entry_offset_to_url);

					READ_MEMBER(cache_directory_index);
					READ_MEMBER(sync_count);
					READ_MEMBER(format_version);
					READ_MEMBER(format_version_copy);

					READ_MEMBER(entry_offset_to_filename);
					READ_MEMBER(cache_flags);
					READ_MEMBER(entry_offset_to_headers);
					READ_MEMBER(headers_size);
					
					READ_MEMBER(entry_offset_to_file_extension);
					READ_MEMBER(last_sync_time);
					READ_MEMBER(num_entry_locks);
					READ_MEMBER(level_of_entry_lock_nesting);
					
					READ_MEMBER(creation_time);
					READ_MEMBER(_reserved_4);
					READ_MEMBER(_reserved_5);

					READ_COMMON();

					format_dos_date_time(url_entry->expiry_time, expiry_time);					
					u64 cached_file_size_value = combine_high_and_low_u32s_into_u64(url_entry->high_cached_file_size, url_entry->low_cached_file_size);
					convert_u64_to_string(cached_file_size_value, cached_file_size);
				}
				else
				{
					_ASSERT(false);
				}

				#undef READ_MEMBER
				#undef READ_COMMON

				TCHAR* decorated_filename = T("");
				if(entry_offset_to_filename > 0)
				{
					char* filename_in_entry = (char*) advance_bytes(entry, entry_offset_to_filename);
					decorated_filename = convert_ansi_string_to_tchar(arena, filename_in_entry);
				}

				TCHAR* url = T("");
				if(entry_offset_to_url > 0)
				{
					char* url_in_entry = (char*) advance_bytes(entry, entry_offset_to_url);
					url = convert_ansi_string_to_tchar(arena, url_in_entry);
					url = decode_url(arena, url);
				}

				Http_Headers headers = {};
				if(entry_offset_to_headers > 0 && headers_size > 0)
				{
					char* headers_in_entry = (char*) advance_bytes(entry, entry_offset_to_headers);
					parse_http_headers(arena, headers_in_entry, headers_size, &headers);
				}

				TCHAR* short_location_pointer = NULL;
				TCHAR short_location_on_cache[MAX_PATH_CHARS] = T("");
				TCHAR full_location_on_cache[MAX_PATH_CHARS] = T("");

				const u8 CHANNEL_DEFINITION_FORMAT_INDEX = 0xFF;
				if(cache_directory_index < IE_4_5_ESE_MAX_NUM_CACHE_DIRECTORIES)
				{
					short_location_pointer = short_location_on_cache;

					// Build the short file path by using the cached file's directory and its decorated filename.
					// E.g. "ABCDEFGH\image[1].gif".
					// @Format: The cache directory's name doesn't include the null terminator.
					char* cache_directory_name_in_entry = (char*) header->cache_directories[cache_directory_index].name;
					char cache_directory_ansi_name[IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS + 1] = "";
					CopyMemory(cache_directory_ansi_name, cache_directory_name_in_entry, IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS * sizeof(char));
					cache_directory_ansi_name[IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS] = '\0';

					TCHAR* cache_directory_name = convert_ansi_string_to_tchar(arena, cache_directory_ansi_name);
					PathCombine(short_location_on_cache, cache_directory_name, decorated_filename);

					// Build the absolute file path to the cache file. The cache directories are next to the index file
					// in this version of Internet Explorer. Here, exporter->index_path is already a full path.
					PathCombine(full_location_on_cache, exporter->index_path, T(".."));
					PathAppend(full_location_on_cache, short_location_on_cache);
				}
				else if(cache_directory_index == CHANNEL_DEFINITION_FORMAT_INDEX)
				{
					// CDF files are marked with this special string since they're not stored on disk.
					short_location_pointer = T("<CDF>");
				}
				else
				{
					// Any other unknown indexes.
					log_warning("Internet Explorer 4 to 9: Found unknown cache directory index 0x%02X for the file '%s' with the following URL: '%s'.", cache_directory_index, decorated_filename, url);
					short_location_pointer = T("<?>");
				}

				// @Alias: 'short_location_pointer' may alias 'short_location_on_cache'.
				TCHAR* format_version_prefix = (major_version == '5') ? (T("Content.IE5")) : (T(""));
				PathCombine(short_location_on_cache, format_version_prefix, short_location_pointer);

				if(found_deallocated_member)
				{
					add_exporter_warning_message(exporter, "Cleared one or more deallocated fields with the value 0x%08X (%I32u) to zero.", IE_4_5_DEALLOCATED_VALUE, IE_4_5_DEALLOCATED_VALUE);
					log_warning("Internet Explorer 4 to 9: The entry starting in block %I32u with %I32u blocks allocated and the signature 0x%08X contained one or more garbage values (0x%08X). The filename is '%s' and the URL is '%s'.", block_count, entry->num_allocated_blocks, entry->signature, IE_4_5_DEALLOCATED_VALUE, decorated_filename, url);
				}

				Csv_Entry csv_row[] =
				{
					{/* Filename */}, {/* URL */}, {/* File Extension */}, {cached_file_size},
					{last_modified_time}, {creation_time}, {/* Last Write Time */}, {last_access_time}, {expiry_time}, {access_count},
					{/* Response */}, {/* Server */}, {/* Cache Control */}, {/* Pragma */},
					{/* Content Type */}, {/* Content Length */}, {/* Content Range */}, {/* Content Encoding */},
					{/* Decompressed File Size */}, {/* Location On Cache */}, {cache_version},
					{/* Missing File */}, {/* Location In Output */}, {/* Copy Error */}, {/* Exporter Warning */},
					{/* Custom File Group */}, {/* Custom URL Group */}, {/* SHA-256 */}
				};
				_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

				Exporter_Params params = {};
				params.copy_source_path = full_location_on_cache;
				params.url = url;
				params.filename = NULL; // Comes from the URL.
				params.headers = headers;
				params.short_location_on_cache = short_location_on_cache;

				export_cache_entry(exporter, csv_row, &params);

				if(entry->signature == ENTRY_URL)
				{
					++num_url_entries;
				}
				else if(entry->signature == ENTRY_LEAK)
				{
					++num_leak_entries;
				}
				else
				{
					_ASSERT(false);
				}
			} break;

			// We won't handle the following entry types, so we'll always skip them.

			case(ENTRY_REDIRECT):
			{
				++num_redirect_entries;
			} break;

			case(ENTRY_HASH):
			{
				++num_hash_entries;
			} break;

			case(ENTRY_UPDATED):
			{
				++num_updated_entries;
			} break;

			case(ENTRY_DELETED):
			{
				++num_deleted_entries;
			} break;

			case(ENTRY_NEWLY_ALLOCATED):
			{
				++num_newly_allocated_entries;
			} break;

			// Deallocated entries whose signatures are set to IE_4_5_DEALLOCATED_VALUE may appear, but they shouldn't be handled
			// like the above since their 'num_allocated_blocks' members will contain a garbage value.
			case(IE_4_5_DEALLOCATED_VALUE):
			{
				skip_remaining_blocks = false;
				++num_deallocated_entries;
				// Do nothing and move to the next block on the next iteration.
			} break;

			// Check if we found an unhandled entry type. We'll want to know if these exist because otherwise
			// we could start treating their allocated blocks as the beginning of other entry types.
			default:
			{
				const size_t NUM_ENTRY_SIGNATURE_CHARS = 4;
				char signature_string[NUM_ENTRY_SIGNATURE_CHARS + 1] = "";
				CopyMemory(signature_string, &(entry->signature), NUM_ENTRY_SIGNATURE_CHARS);
				signature_string[NUM_ENTRY_SIGNATURE_CHARS] = '\0';
				log_warning("Internet Explorer 4 to 9: Found unknown entry signature (0x%08X, '%hs') starting in block %I32u with %I32u blocks allocated.", entry->signature, signature_string, block_count, entry->num_allocated_blocks);
			
				skip_remaining_blocks = false;
				++num_unknown_entries;
				// Move to the next block on the next iteration.
			} break;
		}

		if(skip_remaining_blocks)
		{
			// Skip to the last allocated block so we move to a new entry on the next iteration.
			block_count += entry->num_allocated_blocks - 1;
			block_index += entry->num_allocated_blocks - 1;
		}
	}

	unlock_arena(arena);

	if(total_bytes_read < index_file_size)
	{
		log_info("Internet Explorer 4 to 9: Stopped processing the index file with %I64u of %I64u bytes remaining.", index_file_size - total_bytes_read, index_file_size);
	}

	log_info("Internet Explorer 4 to 9: Found the following entries: Url = %d, Leak = %d, Redirect = %d, Hash = %d, Updated = %d, Deleted = %d, Newly Allocated = %d, Deallocated = %d, Unknown = %d.",
						num_url_entries, num_leak_entries, num_redirect_entries, num_hash_entries, num_updated_entries, num_deleted_entries, num_newly_allocated_entries, num_deallocated_entries, num_unknown_entries);

	safe_close_handle(&index_handle);

	reset_temporary_exporter_members(exporter);
}

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

// Define the export process for Internet Explorer 10 and 11. Only available on the Windows 2000 through 10 builds.
#ifndef WCE_9X

	// Define the stub versions of the functions we want to dynamically load, and the variables that will either contain the pointer to
	// these loaded functions or to the stub versions (if we can't load the real ones).
	// This is useful for two reasons:
	// 1. We want to use a few functions that were only introduced in Windows Vista. On Windows 2000 and XP, the stub versions will be
	// called instead and will return an error so the exporter can fail gracefully.
	// 2. The user doesn't need to have ESE Runtime DLL on their machine. These functions are only required in Windows 7 through 10
	// for the WinINet cache. It doesn't make sense to stop the whole application from running because of this specific type of cache.

	#define JET_GET_DATABASE_FILE_INFO_W(function_name) JET_ERR JET_API function_name(JET_PCWSTR szDatabaseName, void* pvResult, unsigned long cbMax, unsigned long InfoLevel)
	static JET_GET_DATABASE_FILE_INFO_W(stub_jet_get_database_file_info_w)
	{
		log_warning("JetGetDatabaseFileInfoW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_GET_DATABASE_FILE_INFO_W(Jet_Get_Database_File_Info_W);
	static Jet_Get_Database_File_Info_W* dll_jet_get_database_file_info_w = stub_jet_get_database_file_info_w;
	#define JetGetDatabaseFileInfoW dll_jet_get_database_file_info_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_GET_SYSTEM_PARAMETER_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance, JET_SESID sesid, unsigned long paramid, JET_API_PTR* plParam, JET_PWSTR szParam, unsigned long cbMax)
	static JET_GET_SYSTEM_PARAMETER_W(stub_jet_get_system_parameter_w)
	{
		log_warning("JetGetSystemParameterW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_GET_SYSTEM_PARAMETER_W(Jet_Get_System_Parameter_W);
	static Jet_Get_System_Parameter_W* dll_jet_get_system_parameter_w = stub_jet_get_system_parameter_w;
	#define JetGetSystemParameterW dll_jet_get_system_parameter_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_SET_SYSTEM_PARAMETER_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance, JET_SESID sesid, unsigned long paramid, JET_API_PTR lParam, JET_PCWSTR szParam)
	static JET_SET_SYSTEM_PARAMETER_W(stub_jet_set_system_parameter_w)
	{
		log_warning("JetSetSystemParameterW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_SET_SYSTEM_PARAMETER_W(Jet_Set_System_Parameter_W);
	static Jet_Set_System_Parameter_W* dll_jet_set_system_parameter_w = stub_jet_set_system_parameter_w;
	#define JetSetSystemParameterW dll_jet_set_system_parameter_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_CREATE_INSTANCE_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance, JET_PCWSTR szInstanceName)
	static JET_CREATE_INSTANCE_W(stub_jet_create_instance_w)
	{
		log_warning("JetCreateInstanceW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_CREATE_INSTANCE_W(Jet_Create_Instance_W);
	static Jet_Create_Instance_W* dll_jet_create_instance_w = stub_jet_create_instance_w;
	#define JetCreateInstanceW dll_jet_create_instance_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_INIT(function_name) JET_ERR JET_API function_name(JET_INSTANCE* pinstance)
	static JET_INIT(stub_jet_init)
	{
		log_warning("JetInit: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_INIT(Jet_Init);
	static Jet_Init* dll_jet_init = stub_jet_init;
	#define JetInit dll_jet_init

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_TERM(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance)
	static JET_TERM(stub_jet_term)
	{
		log_warning("JetTerm: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_TERM(Jet_Term);
	static Jet_Term* dll_jet_term = stub_jet_term;
	#define JetTerm dll_jet_term

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_BEGIN_SESSION_W(function_name) JET_ERR JET_API function_name(JET_INSTANCE instance, JET_SESID* psesid, JET_PCWSTR szUserName, JET_PCWSTR szPassword)
	static JET_BEGIN_SESSION_W(stub_jet_begin_session_w)
	{
		log_warning("JetBeginSessionW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_BEGIN_SESSION_W(Jet_Begin_Session_W);
	static Jet_Begin_Session_W* dll_jet_begin_session_w = stub_jet_begin_session_w;
	#define JetBeginSessionW dll_jet_begin_session_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_END_SESSION(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_GRBIT grbit)
	static JET_END_SESSION(stub_jet_end_session)
	{
		log_warning("JetEndSession: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_END_SESSION(Jet_End_Session);
	static Jet_End_Session* dll_jet_end_session = stub_jet_end_session;
	#define JetEndSession dll_jet_end_session

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_ATTACH_DATABASE_2_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename, const unsigned long cpgDatabaseSizeMax, JET_GRBIT grbit)
	static JET_ATTACH_DATABASE_2_W(stub_jet_attach_database_2_w)
	{
		log_warning("JetAttachDatabase2W: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_ATTACH_DATABASE_2_W(Jet_Attach_Database_2_W);
	static Jet_Attach_Database_2_W* dll_jet_attach_database_2_w = stub_jet_attach_database_2_w;
	#define JetAttachDatabase2W dll_jet_attach_database_2_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_DETACH_DATABASE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename)
	static JET_DETACH_DATABASE_W(stub_jet_detach_database_w)
	{
		log_warning("JetDetachDatabaseW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_DETACH_DATABASE_W(Jet_Detach_Database_W);
	static Jet_Detach_Database_W* dll_jet_detach_database_w = stub_jet_detach_database_w;
	#define JetDetachDatabaseW dll_jet_detach_database_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_OPEN_DATABASE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_PCWSTR szFilename, JET_PCWSTR szConnect, JET_DBID* pdbid, JET_GRBIT grbit)
	static JET_OPEN_DATABASE_W(stub_jet_open_database_w)
	{
		log_warning("JetOpenDatabaseW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_OPEN_DATABASE_W(Jet_Open_Database_W);
	static Jet_Open_Database_W* dll_jet_open_database_w = stub_jet_open_database_w;
	#define JetOpenDatabaseW dll_jet_open_database_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_CLOSE_DATABASE(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_DBID dbid, JET_GRBIT grbit)
	static JET_CLOSE_DATABASE(stub_jet_close_database)
	{
		log_warning("JetCloseDatabase: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_CLOSE_DATABASE(Jet_Close_Database);
	static Jet_Close_Database* dll_jet_close_database = stub_jet_close_database;
	#define JetCloseDatabase dll_jet_close_database

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_OPEN_TABLE_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_DBID dbid, JET_PCWSTR szTableName, const void* pvParameters, unsigned long cbParameters, JET_GRBIT grbit, JET_TABLEID* ptableid)
	static JET_OPEN_TABLE_W(stub_jet_open_table_w)
	{
		log_warning("JetOpenTableW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_OPEN_TABLE_W(Jet_Open_Table_W);
	static Jet_Open_Table_W* dll_jet_open_table_w = stub_jet_open_table_w;
	#define JetOpenTableW dll_jet_open_table_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_CLOSE_TABLE(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid)
	static JET_CLOSE_TABLE(stub_jet_close_table)
	{
		log_warning("JetCloseTable: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_CLOSE_TABLE(Jet_Close_Table);
	static Jet_Close_Table* dll_jet_close_table = stub_jet_close_table;
	#define JetCloseTable dll_jet_close_table

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_GET_TABLE_COLUMN_INFO_W(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_PCWSTR szColumnName, void* pvResult, unsigned long cbMax, unsigned long InfoLevel)
	static JET_GET_TABLE_COLUMN_INFO_W(stub_jet_get_table_column_info_w)
	{
		log_warning("JetGetTableColumnInfoW: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_GET_TABLE_COLUMN_INFO_W(Jet_Get_Table_Column_Info_W);
	static Jet_Get_Table_Column_Info_W* dll_jet_get_table_column_info_w = stub_jet_get_table_column_info_w;
	#define JetGetTableColumnInfoW dll_jet_get_table_column_info_w

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_RETRIEVE_COLUMN(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID columnid, void* pvData, unsigned long cbData, unsigned long* pcbActual, JET_GRBIT grbit, JET_RETINFO* pretinfo)
	static JET_RETRIEVE_COLUMN(stub_jet_retrieve_column)
	{
		log_warning("JetRetrieveColumn: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_RETRIEVE_COLUMN(Jet_Retrieve_Column);
	static Jet_Retrieve_Column* dll_jet_retrieve_column = stub_jet_retrieve_column;
	#define JetRetrieveColumn dll_jet_retrieve_column

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_RETRIEVE_COLUMNS(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_RETRIEVECOLUMN* pretrievecolumn, unsigned long cretrievecolumn)
	static JET_RETRIEVE_COLUMNS(stub_jet_retrieve_columns)
	{
		log_warning("JetRetrieveColumns: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_RETRIEVE_COLUMNS(Jet_Retrieve_Columns);
	static Jet_Retrieve_Columns* dll_jet_retrieve_columns = stub_jet_retrieve_columns;
	#define JetRetrieveColumns dll_jet_retrieve_columns

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_GET_RECORD_POSITION(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, JET_RECPOS* precpos, unsigned long cbRecpos)
	static JET_GET_RECORD_POSITION(stub_jet_get_record_position)
	{
		log_warning("JetGetRecordPosition: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_GET_RECORD_POSITION(Jet_Get_Record_Position);
	static Jet_Get_Record_Position* dll_jet_get_record_position = stub_jet_get_record_position;
	#define JetGetRecordPosition dll_jet_get_record_position

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	#define JET_MOVE(function_name) JET_ERR JET_API function_name(JET_SESID sesid, JET_TABLEID tableid, long cRow, JET_GRBIT grbit)
	static JET_MOVE(stub_jet_move)
	{
		log_warning("JetMove: Calling the stub version of this function.");
		return JET_wrnNyi;
	}
	typedef JET_MOVE(Jet_Move);
	static Jet_Move* dll_jet_move = stub_jet_move;
	#define JetMove dll_jet_move

	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------
	// ----------------------------------------------------------------------

	static HMODULE esent_library = NULL;

	// Dynamically load any necessary functions from ESENT.dll. After being called, the following functions may used:
	//
	// - JetGetDatabaseFileInfoW()
	// - JetGetSystemParameterW()
	// - JetSetSystemParameterW()
	// - JetCreateInstanceW()
	// - JetInit()
	// - JetTerm()
	// - JetBeginSessionW()
	// - JetEndSession()
	// - JetAttachDatabase2W()
	// - JetDetachDatabaseW()
	// - JetOpenDatabaseW()
	// - JetCloseDatabase()
	// - JetOpenTableW()
	// - JetCloseTable()
	// - JetGetTableColumnInfoW()
	// - JetRetrieveColumn()
	// - JetRetrieveColumns()
	// - JetGetRecordPosition()
	// - JetMove()
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	void load_esent_functions(void)
	{
		if(esent_library != NULL)
		{
			log_warning("Load Esent Functions: The library was already loaded.");
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
			log_error("Load Esent Functions: Failed to load the library with error code %lu.", GetLastError());
		}
	}

	// Free any functions that were previously dynamically loaded from ESENT.dll. After being called, these functions should
	// no longer be called.
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	void free_esent_functions(void)
	{
		if(esent_library == NULL)
		{
			log_warning("Free Esent: The library has not been loaded yet.");
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
			log_error("Free Esent: Failed to free the library with the error code %lu.", GetLastError());
		}
	}

	// Performs all clean up operations on the ESE database.
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters:
	// 1. exporter - The Exporter structure that is used to clear the temporary exporter directory of any copied ESE files.
	// 2. instance - The address of the ESE instance to terminate. This value will be set to JET_instanceNil.
	// 3. session_id - The address of the ESE session ID to end. This value will be set to JET_sesidNil.
	// 4. database_id - The address of the database ID to close and detach. This value will be set to JET_dbidNil.
	// 5. containers_table_id - The address of the Containers table ID to close. This value will be set to JET_tableidNil.
	//
	// @Returns: Nothing.
	static void ese_clean_up(Exporter* exporter, JET_INSTANCE* instance, JET_SESID* session_id, JET_DBID* database_id, JET_TABLEID* containers_table_id)
	{
		JET_ERR error_code = JET_errSuccess;

		if(*containers_table_id != JET_tableidNil)
		{
			error_code = JetCloseTable(*session_id, *containers_table_id);
			if(error_code != JET_errSuccess) log_warning("Ese Clean Up: Failed to close the Containers table with the error code %ld.", error_code);
			*containers_table_id = JET_tableidNil;
		}

		if(*database_id != JET_dbidNil)
		{
			error_code = JetCloseDatabase(*session_id, *database_id, 0);
			if(error_code != JET_errSuccess) log_warning("Ese Clean Up: Failed to close the database with the error code %ld.", error_code);
			error_code = JetDetachDatabaseW(*session_id, NULL);
			if(error_code != JET_errSuccess) log_warning("Ese Clean Up: Failed to detach the database with the error code %ld.", error_code);
			*database_id = JET_dbidNil;
		}

		if(*session_id != JET_sesidNil)
		{
			error_code = JetEndSession(*session_id, 0);
			if(error_code != JET_errSuccess) log_warning("Ese Clean Up: Failed to end the session with the error code %ld.", error_code);
			*session_id = JET_sesidNil;
		}

		if(*instance != JET_instanceNil)
		{
			error_code = JetTerm(*instance);
			if(error_code != JET_errSuccess) log_warning("Ese Clean Up: Failed to terminate the ESE instance with the error code %ld.", error_code);
			*instance = JET_instanceNil;
		}

		clear_temporary_exporter_directory(exporter);
		reset_temporary_exporter_members(exporter);
	}

	// Maps the value of the database state to a string.
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters:
	// 1. state - The value of the 'dbstate' member in the JET_DBINFOMISC database information structure.
	//
	// @Returns: The database state as a constant string.
	static wchar_t* get_database_state_string(unsigned long state)
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

	// Exports Internet Explorer 10 and 11's cache from a given location.
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters:
	// 1. exporter - The Exporter structure which contains information on how Internet Explorer's cache should be exported.
	// 2. ese_files_prefix - The three character prefix on the ESE files that are kept next to the ESE database. This parameter
	// is required to ensure that the data is recovered correctly. For example, for the database file "WebCacheV01.dat", we would
	// use the prefix "V01", as seen in the files next to this one (e.g. the transaction log file "V01.log").
	//
	// @Returns: Nothing.
	static void export_internet_explorer_10_to_11_cache(Exporter* exporter, const wchar_t* ese_files_prefix)
	{
		Arena* arena = &(exporter->temporary_arena);
		wchar_t* index_filename = PathFindFileNameW(exporter->index_path);

		if(!does_file_exist(exporter->index_path))
		{
			log_info("Internet Explorer 10 to 11: Skipping the missing ESE database file '%ls'.", index_filename);
			return;
		}

		if(!exporter->was_temporary_exporter_directory_created)
		{
			log_error("Internet Explorer 10 to 11: The temporary exporter directory used to recover the ESE database's contents was not previously created.");
			return;
		}

		// How the ESE database will be read:
		// 1. Copy every ESE file in the database's directory to a temporary location. This may require forcibly
		// copying files that are being used by another process.
		// 2. Set the required ESE system parameters so a database recovery is attempted if necessary. We'll need
		// to point it to our temporary location which contains the copied transaction logs, and specify the three
		// character base name (e.g. "V01") that is used in their filenames.
		
		wchar_t index_directory_path[MAX_PATH_CHARS] = L"";
		PathCombineW(index_directory_path, exporter->index_path, L"..");

		// Find and copy every ESE file in the database's directory to our temporary one.
		Traversal_Result* database_files = find_objects_in_directory(arena, index_directory_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, false);
		int num_copy_failures = 0;

		wchar_t temporary_database_path[MAX_PATH_CHARS] = L"";

		for(int i = 0; i < database_files->num_objects; ++i)
		{
			Traversal_Object_Info file_info = database_files->object_info[i];

			wchar_t* copy_source_path = file_info.object_path;
			wchar_t* filename = file_info.object_name;
			wchar_t copy_destination_path[MAX_PATH_CHARS] = L"";
			
			log_info("Internet Explorer 10 to 11: Copying the ESE file '%ls' to the temporary exporter directory.", filename);
			bool copy_success = create_placeholder_exporter_file(exporter, copy_destination_path, filename)
								&& copy_open_file(arena, copy_source_path, copy_destination_path);

			if(!copy_success)
			{
				++num_copy_failures;
				log_error("Internet Explorer 10 to 11: Failed to copy the ESE file '%ls' to the temporary exporter directory.", filename);
			}
			else if(filenames_are_equal(index_filename, filename))
			{
				StringCchCopyW(temporary_database_path, MAX_PATH_CHARS, copy_destination_path);
			}
		}

		if(num_copy_failures > 0)
		{
			log_warning("Internet Explorer 10 to 11: Could not copy %d ESE files to the temporary exporter directory.", num_copy_failures);
		}

		if(string_is_empty(temporary_database_path))
		{
			log_error("Internet Explorer 10 to 11: Failed to find the ESE database file after copying the files in '%ls'.", index_directory_path);
			return;
		}

		log_info("Internet Explorer 10 to 11: Reading the information contained in the temporary ESE database file '%ls'.", temporary_database_path);

		// @FormatVersion: Internet Explorer 10 to 11 (ESE database).
		// @ByteOrder: Little Endian. We won't have to deal with the database file directly since we're using the ESE API.
		// @CharacterEncoding: UTF-16 LE. Although it can also be ASCII according to the ESE API reference, we will always assume it's UTF-16 LE.
		// @DateTimeFormat: FILETIME.

		// Read the ESE database that was copied to our temporary directory.
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
			// Default to this value (taken from sample WebCache*.dat files) if we can't get it out of the database for some reason.
			page_size = 32768;
			log_warning("Internet Explorer 10 to 11: Failed to get the ESE database's page size with the error code %ld. This value will default to %lu.", error_code, page_size);
		}
		error_code = JetSetSystemParameterW(&instance, session_id, JET_paramDatabasePageSize, page_size, NULL);

		JET_DBINFOMISC database_info = {};
		error_code = JetGetDatabaseFileInfoW(temporary_database_path, &database_info, sizeof(database_info), JET_DbInfoMisc);
		const size_t MAX_CACHE_VERSION_CHARS = 64;
		wchar_t cache_version[MAX_CACHE_VERSION_CHARS] = T("");
		if(error_code == JET_errSuccess)
		{
			StringCchPrintfW(cache_version, MAX_CACHE_VERSION_CHARS, T("ESE.v%X.u%X"), database_info.ulVersion, database_info.ulUpdate);
			log_info("Internet Explorer 10 to 11: The ESE database's version is '%ls' and the state is '%ls'.", cache_version, get_database_state_string(database_info.dbstate));
		}

		error_code = JetCreateInstanceW(&instance, L"WebCacheExporter");
		if(error_code < 0)
		{
			log_error("Internet Explorer 10 to 11: Failed to create the ESE instance with the error code %ld.", error_code);
			ese_clean_up(exporter, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}
		
		// Set the required system parameters so the recovery process is attempted.
		
		// @Docs: The system parameters that specify paths must be at most 246 characters in length (260 - 14);
		const size_t MAX_TEMPORARY_PATH_CHARS = MAX_PATH_CHARS - 14;
		wchar_t temporary_directory_path[MAX_TEMPORARY_PATH_CHARS] = L"";
		
		// @Docs: The system parameters that use this path must end in a backslash.
		StringCchCopyW(temporary_directory_path, MAX_TEMPORARY_PATH_CHARS, temporary_database_path);
		PathAppendW(temporary_directory_path, L"..");
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
			log_error("Internet Explorer 10 to 11: Failed to initialize the ESE instance with the error code %ld.", error_code);
			ese_clean_up(exporter, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}
		
		error_code = JetBeginSessionW(instance, &session_id, NULL, NULL);
		if(error_code < 0)
		{
			log_error("Internet Explorer 10 to 11: Failed to begin the session with the error code %ld.", error_code);
			ese_clean_up(exporter, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}

		// @PageSize: Passing zero to the page size makes it so no maximum is enforced by the database engine.
		error_code = JetAttachDatabase2W(session_id, temporary_database_path, 0, JET_bitDbReadOnly);
		if(error_code < 0)
		{
			log_error("Internet Explorer 10 to 11: Failed to attach the database '%ls' with the error code %ld.", temporary_database_path, error_code);
			ese_clean_up(exporter, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}
	
		error_code = JetOpenDatabaseW(session_id, temporary_database_path, NULL, &database_id, JET_bitDbReadOnly);
		if(error_code < 0)
		{
			log_error("Internet Explorer 10 to 11: Failed to open the database '%ls' with the error code %ld.", temporary_database_path, error_code);
			ese_clean_up(exporter, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}

		error_code = JetOpenTableW(session_id, database_id, L"Containers", NULL, 0, JET_bitTableReadOnly | JET_bitTableSequential, &containers_table_id);
		if(error_code < 0)
		{
			log_error("Internet Explorer 10 to 11: Failed to open the Containers table with the error code %ld.", error_code);
			ese_clean_up(exporter, &instance, &session_id, &database_id, &containers_table_id);
			return;
		}

		// @Hint:
		// When exporting the cache, we need to resolve the paths of the cached files that are stored on disk. This is a problem
		// if the database file came from a different computer since the base cache directory (the IDX_DIRECTORY column below)
		// contains an absolute path. This path won't exist on the current computer. However, we already know the path to the database
		// file on the current computer (index_directory_path), so if we can figure out what this same path was on the original computer,
		// we can take the relative path from one to the other and apply that to the base cache directory column. This will then take
		// us to the absolute path of the cached files in the current computer, even though they came from another machine.
		//
		// We'll solve this one of two ways:
		// 1. Assume that the first directory in the Containers table is "<Local Appdata>\Microsoft\Windows\INetCache\IE", meaning
		// we can go back two directories ("..\\..\\WebCache") and retrieve "<Local AppData>\Microsoft\Windows\WebCache".
		// 2. Allow the user to pass a command line option that specifies the path on the current computer to where the "<Local AppData>"
		// directory was located in the other machine. We can then add "Microsoft\\Windows\\WebCache" and arrive at the same directory
		// as in 1.
		//
		// This original path will either stay empty (if we're exporting from default locations on the current machine) or will be
		// set to "<Local AppData>\Microsoft\Windows\WebCache" (using either of the previously mentioned methods).
		bool is_original_database_path_set = false;
		wchar_t original_database_path[MAX_PATH_CHARS] = L"";
		if(!exporter->is_exporting_from_default_locations && exporter->use_ie_hint)
		{
			is_original_database_path_set = true;
			PathCombineW(original_database_path, exporter->ie_hint_path, L"Microsoft\\Windows\\WebCache");
		}

		enum Container_Column_Index
		{
			IDX_NAME = 0,
			IDX_CONTAINER_ID = 1,
			IDX_DIRECTORY = 2,
			IDX_SECURE_DIRECTORIES = 3,
			NUM_CONTAINER_COLUMNS = 4
		};

		const wchar_t* const CONTAINER_COLUMN_NAMES[] =
		{
			L"Name", 				// JET_coltypText		(10)
			L"ContainerId",			// JET_coltypLongLong 	(15)
			L"Directory", 			// JET_coltypLongText 	(12)
			L"SecureDirectories" 	// JET_coltypLongText 	(12)
		};
		_STATIC_ASSERT(_countof(CONTAINER_COLUMN_NAMES) == NUM_CONTAINER_COLUMNS);

		// Get the necessary column IDs for the Containers table.
		JET_COLUMNDEF container_column_info[NUM_CONTAINER_COLUMNS] = {};
		for(int i = 0; i < NUM_CONTAINER_COLUMNS; ++i)
		{
			error_code = JetGetTableColumnInfoW(session_id, containers_table_id, CONTAINER_COLUMN_NAMES[i],
												&container_column_info[i], sizeof(container_column_info[i]), JET_ColInfo);
		}

		// Move through the Containers table. This will tell us where each cache directory is located.
		bool found_container_record = (JetMove(session_id, containers_table_id, JET_MoveFirst, 0) == JET_errSuccess);
		while(found_container_record)
		{
			// @Docs: "JET_coltypText: A fixed or variable length text column that can be up to 255 ASCII characters in length
			// or 127 Unicode characters in length." - JET_COLTYP, Extensible Storage Engine Reference.
			const size_t MAX_COLUMN_TYPE_TEXT_CHARS = 256;
			wchar_t container_name[MAX_COLUMN_TYPE_TEXT_CHARS] = L"";
			unsigned long actual_container_name_size = 0;
			error_code = JetRetrieveColumn(	session_id, containers_table_id, container_column_info[IDX_NAME].columnid,
											container_name, sizeof(container_name), &actual_container_name_size, 0, NULL);
			size_t num_container_name_chars = actual_container_name_size / sizeof(wchar_t);

			// Check if the container record belongs to the cache.
			if(strings_are_at_most_equal(container_name, L"Content", num_container_name_chars))
			{
				// Retrieve the "ContainerId", "Directory", and "SecureDirectories" columns.
				JET_RETRIEVECOLUMN container_columns[NUM_CONTAINER_COLUMNS] = {};
				for(int i = 0; i < NUM_CONTAINER_COLUMNS; ++i)
				{
					container_columns[i].columnid = container_column_info[i].columnid;
					container_columns[i].pvData = NULL;
					container_columns[i].cbData = 0;
					// Don't handle multi-valued columns (JET_bitRetrieveIgnoreDefault + sequence tag 1).
					container_columns[i].grbit = JET_bitRetrieveIgnoreDefault;
					container_columns[i].ibLongValue = 0;
					container_columns[i].itagSequence = 1;
				}

				s64 container_id = -1;
				container_columns[IDX_CONTAINER_ID].pvData = &container_id;
				container_columns[IDX_CONTAINER_ID].cbData = sizeof(container_id);

				wchar_t directory[MAX_PATH_CHARS] = L"";
				container_columns[IDX_DIRECTORY].pvData = directory;
				container_columns[IDX_DIRECTORY].cbData = sizeof(directory);

				wchar_t secure_directories[IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS * IE_4_5_ESE_MAX_NUM_CACHE_DIRECTORIES + 1] = L"";
				container_columns[IDX_SECURE_DIRECTORIES].pvData = secure_directories;
				container_columns[IDX_SECURE_DIRECTORIES].cbData = sizeof(secure_directories);

				// Skip retrieving the "Name" column (we already got it above) and only get "ContainerId" onwards.
				error_code = JetRetrieveColumns(session_id, containers_table_id, &container_columns[IDX_CONTAINER_ID], NUM_CONTAINER_COLUMNS - 1);
				
				// Check if we were able to retrieve every column.
				bool retrieval_success = true;
				for(int i = IDX_CONTAINER_ID; i < NUM_CONTAINER_COLUMNS; ++i)
				{
					if(container_columns[i].err != JET_errSuccess)
					{
						retrieval_success = false;

						JET_RECPOS record_position = {};
						error_code = JetGetRecordPosition(session_id, containers_table_id, &record_position, sizeof(record_position));
						log_error("Internet Explorer 10 to 11: Failed to retrieve the '%ls' column (%Iu) for Content record %lu in the Containers table with the error code %ld.", CONTAINER_COLUMN_NAMES[i], i, record_position.centriesLT, container_columns[i].err);
					}
				}

				// We'll only handle cache locations (records) whose column values were read correctly. Otherwise, we wouldn't have
				// enough information to properly export them.
				if(retrieval_success)
				{
					log_info("Internet Explorer 10 to 11: Found cache location '%s' (%I64d).", directory, container_id);

					// @Assert: The name of a cache directory should have exactly this many characters.
					_ASSERT( (string_length(secure_directories) % IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS) == 0 );

					// Create an array of cache directory names (including the null terminator) to make future accesses easier.
					size_t num_cache_directories = string_length(secure_directories) / IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS;
					wchar_t cache_directory_names[IE_4_5_ESE_MAX_NUM_CACHE_DIRECTORIES][IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS + 1] = {L""};
					size_t cache_directory_name_size = IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS * sizeof(wchar_t);
					for(size_t i = 0; i < num_cache_directories; ++i)
					{
						wchar_t* name = (wchar_t*) advance_bytes(secure_directories, i * cache_directory_name_size);
						CopyMemory(cache_directory_names[i], name, cache_directory_name_size);
						cache_directory_names[i][IE_4_5_ESE_NUM_CACHE_DIRECTORY_NAME_CHARS] = L'\0';
					}

					// Open each Cache table by building its name ("Container_<s64 id>") using the previously retrieved ID.
					const size_t NUM_CACHE_TABLE_NAME_CHARS = 10 + MAX_INT_64_CHARS;
					wchar_t cache_table_name[NUM_CACHE_TABLE_NAME_CHARS] = L"";
					if(SUCCEEDED(StringCchPrintfW(cache_table_name, NUM_CACHE_TABLE_NAME_CHARS, L"Container_%I64d", container_id)))
					{
						JET_TABLEID cache_table_id = JET_tableidNil;
						error_code = JetOpenTableW(session_id, database_id, cache_table_name, NULL, 0, JET_bitTableReadOnly | JET_bitTableSequential, &cache_table_id);
						if(error_code >= 0)
						{							
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

							const wchar_t* const CACHE_COLUMN_NAMES[] =
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
							_STATIC_ASSERT(_countof(CACHE_COLUMN_NAMES) == NUM_CACHE_COLUMNS);

							// Get the necessary column IDs for each Cache table.
							JET_COLUMNDEF cache_column_info[NUM_CACHE_COLUMNS] = {};
							for(int i = 0; i < NUM_CACHE_COLUMNS; ++i)
							{
								error_code = JetGetTableColumnInfoW(session_id, cache_table_id, CACHE_COLUMN_NAMES[i],
																	&cache_column_info[i], sizeof(cache_column_info[i]), JET_ColInfo);
							}

							// Move through each Cache table. This will give us all the information needed to export the cache.
							bool found_cache_record = (JetMove(session_id, cache_table_id, JET_MoveFirst, 0) == JET_errSuccess);
							while(found_cache_record)
							{
								JET_RETRIEVECOLUMN cache_columns[NUM_CACHE_COLUMNS] = {};

								for(int i = 0; i < NUM_CACHE_COLUMNS; ++i)
								{
									cache_columns[i].columnid = cache_column_info[i].columnid;
									cache_columns[i].pvData = NULL;
									cache_columns[i].cbData = 0;
									// Don't handle multi-valued columns (JET_bitRetrieveIgnoreDefault + sequence tag 1).
									cache_columns[i].grbit = JET_bitRetrieveIgnoreDefault;
									cache_columns[i].ibLongValue = 0;
									cache_columns[i].itagSequence = 1;
								}
								// Retrieve the actual sizes for "Filename", "Url", and "ResponseHeaders" columns.
								error_code = JetRetrieveColumns(session_id, cache_table_id, cache_columns, NUM_CACHE_COLUMNS);

								unsigned long filename_size = cache_columns[IDX_FILENAME].cbActual;
								wchar_t* filename = push_arena(arena, filename_size, wchar_t);
								cache_columns[IDX_FILENAME].pvData = filename;
								cache_columns[IDX_FILENAME].cbData = filename_size;

								unsigned long url_size = cache_columns[IDX_URL].cbActual;
								wchar_t* url = push_arena(arena, url_size, wchar_t);
								cache_columns[IDX_URL].pvData = url;
								cache_columns[IDX_URL].cbData = url_size;

								s64 file_size = -1;
								cache_columns[IDX_FILE_SIZE].pvData = &file_size;
								cache_columns[IDX_FILE_SIZE].cbData = sizeof(file_size);

								FILETIME last_modified_time_value = {};
								cache_columns[IDX_LAST_MODIFIED_TIME].pvData = &last_modified_time_value;
								cache_columns[IDX_LAST_MODIFIED_TIME].cbData = sizeof(last_modified_time_value);

								FILETIME creation_time_value = {};
								cache_columns[IDX_CREATION_TIME].pvData = &creation_time_value;
								cache_columns[IDX_CREATION_TIME].cbData = sizeof(creation_time_value);
								
								FILETIME last_access_time_value = {};
								cache_columns[IDX_LAST_ACCESS_TIME].pvData = &last_access_time_value;
								cache_columns[IDX_LAST_ACCESS_TIME].cbData = sizeof(last_access_time_value);

								FILETIME expiry_time_value = {};
								cache_columns[IDX_EXPIRY_TIME].pvData = &expiry_time_value;
								cache_columns[IDX_EXPIRY_TIME].cbData = sizeof(expiry_time_value);

								unsigned long headers_size = cache_columns[IDX_HEADERS].cbActual;
								char* headers = push_arena(arena, headers_size, char);
								cache_columns[IDX_HEADERS].pvData = headers;
								cache_columns[IDX_HEADERS].cbData = headers_size;

								u32 secure_directory_index = 0;
								cache_columns[IDX_SECURE_DIRECTORY].pvData = &secure_directory_index;
								cache_columns[IDX_SECURE_DIRECTORY].cbData = sizeof(secure_directory_index);

								u32 access_count = 0;
								cache_columns[IDX_ACCESS_COUNT].pvData = &access_count;
								cache_columns[IDX_ACCESS_COUNT].cbData = sizeof(access_count);

								// Retrieve the values for every column.
								error_code = JetRetrieveColumns(session_id, cache_table_id, cache_columns, NUM_CACHE_COLUMNS);
								for(int i = 0; i < NUM_CACHE_COLUMNS; ++i)
								{
									if(cache_columns[i].err < 0)
									{
										cache_columns[i].pvData = NULL;

										JET_RECPOS record_position = {};
										error_code = JetGetRecordPosition(session_id, cache_table_id, &record_position, sizeof(record_position));
										log_warning("Internet Explorer 10 to 11: Failed to retrieve column %Iu for Cache record %lu in the Cache table '%ls' with the error code %ld.", i, record_position.centriesLT, cache_table_name, cache_columns[i].err);
									}
								}

								// Handle the retrieved values.
								{
									wchar_t* decorated_filename = push_string_to_arena(arena, filename);
									undecorate_path(filename);

									url = decode_url(arena, url);

									wchar_t cached_file_size[MAX_INT_64_CHARS] = L"";
									convert_s64_to_string(file_size, cached_file_size);

									wchar_t last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS] = L"";
									format_filetime_date_time(last_modified_time_value, last_modified_time);

									wchar_t creation_time[MAX_FORMATTED_DATE_TIME_CHARS] = L"";
									format_filetime_date_time(creation_time_value, creation_time);

									wchar_t last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = L"";
									format_filetime_date_time(last_access_time_value, last_access_time);

									wchar_t expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = L"";
									format_filetime_date_time(expiry_time_value, expiry_time);

									Http_Headers cache_headers = {};
									parse_http_headers(arena, headers, headers_size, &cache_headers);

									wchar_t access_count_string[MAX_INT_32_CHARS] = L"";
									convert_u32_to_string(access_count, access_count_string);

									// @Format: The cache directory indexes stored in the database are one based.
									secure_directory_index -= 1;
									_ASSERT(secure_directory_index < num_cache_directories);
									wchar_t* cache_directory = cache_directory_names[secure_directory_index];

									wchar_t short_location_on_cache[MAX_PATH_CHARS] = L"";
									PathCombineW(short_location_on_cache, cache_directory, decorated_filename);

									// @Hint: If we're exporting from a live machine, the absolute path stored in the database
									// can be used directly. Otherwise, we'll use one of the two methods described in @Hint to
									// determine the absolute path to the cached files.
									wchar_t full_location_on_cache[MAX_PATH_CHARS] = L"";

									if(exporter->is_exporting_from_default_locations)
									{
										StringCchCopyW(full_location_on_cache, MAX_PATH_CHARS, directory);
									}
									else
									{
										if(!is_original_database_path_set)
										{
											PathCombineW(original_database_path, directory, L"..\\..\\WebCache");
											is_original_database_path_set = !string_is_empty(original_database_path);
										}

										wchar_t path_from_database_to_cache[MAX_PATH_CHARS] = L"";
										PathRelativePathToW(path_from_database_to_cache,
															original_database_path, FILE_ATTRIBUTE_DIRECTORY, // From this directory...
															directory, FILE_ATTRIBUTE_DIRECTORY); // ...to this directory.

										PathCombineW(full_location_on_cache, index_directory_path, path_from_database_to_cache);
									}

									PathAppendW(full_location_on_cache, short_location_on_cache);

									wchar_t short_location_on_cache_with_prefix[MAX_PATH_CHARS] = L"";
									StringCchPrintfW(short_location_on_cache_with_prefix, MAX_PATH_CHARS, L"Content[%I64d]\\%ls", container_id, short_location_on_cache);

									Csv_Entry csv_row[] =
									{
										{/* Filename */}, {/* URL */}, {/* File Extension */}, {cached_file_size},
										{last_modified_time}, {creation_time}, {/* Last Write Time */}, {last_access_time}, {expiry_time}, {access_count_string},
										{/* Response */}, {/* Server */}, {/* Cache Control */}, {/* Pragma */},
										{/* Content Type */}, {/* Content Length */}, {/* Content Range */}, {/* Content Encoding */},
										{/* Decompressed File Size */}, {/* Location On Cache */}, {cache_version},
										{/* Missing File */}, {/* Location In Output */}, {/* Copy Error */}, {/* Exporter Warning */},
										{/* Custom File Group*/}, {/* Custom URL Group */}, {/* SHA-256 */}
									};
									_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

									Exporter_Params params = {};
									params.copy_source_path = full_location_on_cache;
									params.url = url;
									params.filename = NULL; // Comes from the URL.
									params.headers = cache_headers;
									params.short_location_on_cache = short_location_on_cache_with_prefix;

									export_cache_entry(exporter, csv_row, &params);
								}
								
								// Move to the next cache record.
								found_cache_record = (JetMove(session_id, cache_table_id, JET_MoveNext, 0) == JET_errSuccess);
							}

							error_code = JetCloseTable(session_id, cache_table_id);
							cache_table_id = JET_tableidNil;
							if(error_code < 0)
							{
								log_warning("Internet Explorer 10 to 11: Failed to close the cache table '%ls' with the error code %ld.", cache_table_name, error_code);
							}
						}
						else
						{
							log_error("Internet Explorer 10 to 11: Failed to open the cache table '%ls' with the error code %ld. The contents of this table will be ignored.", cache_table_name, error_code);
						}
					}
					else
					{
						log_error("Internet Explorer 10 to 11: Failed to format the cache container table's name for container ID %I64d.", container_id);
					}

				}
			}

			// Move to the next container record.
			found_container_record = (JetMove(session_id, containers_table_id, JET_MoveNext, 0) == JET_errSuccess);
		}

		ese_clean_up(exporter, &instance, &session_id, &database_id, &containers_table_id);
	}

#endif
