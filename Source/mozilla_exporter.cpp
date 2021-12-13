#include "web_cache_exporter.h"
#include "mozilla_exporter.h"

/*
	This file defines how the exporter processes the cache format used by Mozilla-based browsers, like Firefox and SeaMonkey.

	@SupportedFormats:
	- Mozilla 0.9.5 to Firefox 31 (version 1 - "Cache\_CACHE_MAP_").
	- Firefox 32 and later (version 2 - "cache2\entries\*").

	@DefaultCacheLocations:
	- 95, 98, ME 			C:\WINDOWS\Application Data\<Vendor and Browser>\Profiles\<Profile Name>\<Cache Subdirectory>
	- 2000, XP 				C:\Documents and Settings\<Username>\Local Settings\Application Data\<Vendor and Browser>\Profiles\<Profile Name>\<Cache Subdirectory>
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\Local\<Vendor and Browser>\Profiles\<Profile Name>\<Cache Subdirectory>

	Where the <Cache Subdirectory> may be "Cache" or "cache2" depending on the cache version.
	
	And where <Vendor and Browser> depends on the browser:
	- Mozilla Firefox 				"Mozilla\Firefox"
	- SeaMonkey 					"Mozilla\SeaMonkey"
	- Pale Moon 					"Moonchild Productions\Pale Moon"
	- Basilisk						"Moonchild Productions\Basilisk"
	- Waterfox						"Waterfox"
	- K-Meleon						"K-Meleon"
	- Netscape Navigator			"Netscape\Navigator" (for 9.x)
									"Netscape\NSB" (for 8.x)
	
	For older versions like Netscape Navigator 6.1 to 7.x, Phoenix, Mozilla Firebird, and the Mozilla Suite this location is slighty different:
	- "<AppData>\<Vendor and Browser>\Profiles\<Profile Name>\<8 Characters>.slt\<Cache Subdirectory>".
	1. It's located in <AppData> instead of <Local Appdata>.
	2. There's an extra subdirectory between the <Profile Name> and the <Cache Subdirectory>.
	3. For Netscape Navigator 6.1, the subdirectory "NewCache" may appear instead of "Cache" for the <Cache Subdirectory>.

	And once again, <Vendor and Browser> depends on the browser:
	- Phoenix / Mozilla Firebird						"Phoenix"
	- Netscape Navigator (6.1 to 7.x) / Mozilla Suite 	"Mozilla"
	
	Older Netscape Navigator versions (6.0 or earlier) use a different cache format than the ones listed above. For Netscape Navigator 6.0, the
	subdirectory "Users50" is used instead of "Profiles" before the <Profile Name>. We'll consider both of these profile directory names because
	the Mozilla cache format may appear in "Users50" if a user upgrades from Netscape Navigator 6.0 to 6.1.

	See also:
	- https://www-archive.mozilla.org/releases/history
	- https://www-archive.mozilla.org/releases/
	- https://www-archive.mozilla.org/releases/old-releases
	- https://releases.mozilla.org/pub/firefox/releases/
	- https://www-archive.mozilla.org/projects/seamonkey/release-notes/
	- https://www-archive.mozilla.org/start/1.4/faq/profile
	- https://web.archive.org/web/20011124190804/http://home.netscape.com/eng/mozilla/ns62/relnotes/62.html
	- https://bugzilla.mozilla.org/show_bug.cgi?id=74085
	- https://en.wikipedia.org/wiki/Firefox_version_history#Rapid_releases
	- https://en.wikipedia.org/wiki/Firefox_early_version_history
	- https://en.wikipedia.org/wiki/Netscape_(web_browser)#Release_history

	@SupportsCustomCacheLocations:
	- Same Machine: Yes, we check the prefs.js file for each profile before looking in the default locations above.
	- External Locations: Yes, see above.

	See: http://kb.mozillazine.org/Browser.cache.disk.parent_directory
	
	@Resources: Documents that specify how the Mozilla cache formats should be processed. This includes Mozilla Firefox's source code, which was
	mostly used to learn how to process version 2 of the cache format.

	[FCF] "firefox-cache-forensics - FfFormat.wiki"
	--> https://code.google.com/archive/p/firefox-cache-forensics/wikis/FfFormat.wiki

	[NC] "Necko/Cache"
	--> https://wiki.mozilla.org/Necko/Cache
	
	[JM] "Firefox cache file format"
	--> https://github.com/libyal/dtformats/blob/main/documentation/Firefox%20cache%20file%20format.asciidoc
	
	[JH] "Firefox Cache2 Storage Breakdown"
	--> https://web.archive.org/web/20150717095331/http://encase-forensic-blog.guidancesoftware.com/2015/02/firefox-cache2-storage-breakdown.html

	[HG-1] "netwerk/cache"
	--> https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache

	[HG-2] "netwerk/cache2"
	--> https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2

	@Tools: Existing software that also reads the Mozilla cache format.
	
	[NS-T1] "MZCacheView v2.01 - View the cache files of Firefox Web browsers"
	--> https://www.nirsoft.net/utils/mozilla_cache_viewer.html
	--> Used to validate the output of this application.
*/

static const TCHAR* OUTPUT_NAME = T("MZ");

static Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_URL, CSV_REQUEST_ORIGIN, CSV_FILE_EXTENSION, CSV_FILE_SIZE, 
	CSV_LAST_MODIFIED_TIME, CSV_LAST_ACCESS_TIME, CSV_EXPIRY_TIME, CSV_ACCESS_COUNT,
	CSV_RESPONSE, CSV_SERVER, CSV_CACHE_CONTROL, CSV_PRAGMA, CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_RANGE, CSV_CONTENT_ENCODING, 
	CSV_LOCATION_ON_CACHE, CSV_CACHE_ORIGIN, CSV_CACHE_VERSION,
	CSV_MISSING_FILE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR, CSV_EXPORTER_WARNING,
	CSV_CUSTOM_FILE_GROUP, CSV_CUSTOM_URL_GROUP, CSV_SHA_256
};

static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

static void export_mozilla_cache_version_1(Exporter* exporter);
static void export_mozilla_cache_version_2(Exporter* exporter);

// Finds any custom cache locations from a Mozilla browser's preferences file (prefs.js).
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the Mozilla cache should be exported.
// 2. prefs_file_path - The path to the prejs.js file.
// 3. result_cache_path - The resulting cache location. Only the first one is returned. Note that this location might be either the cache directory
// itself or its parent directory.
//
// @Returns: True if a user-defined cache location was found. Otherwise, false.
static bool find_cache_parent_directory_in_mozilla_prefs(Exporter* exporter, const TCHAR* prefs_file_path, TCHAR* result_cache_path)
{
	*result_cache_path = T('\0');
	bool success = false;

	Arena* arena = &(exporter->temporary_arena);
	lock_arena(arena);

	u64 prefs_file_size = 0;
	char* prefs_file = (char*) read_entire_file(arena, prefs_file_path, &prefs_file_size, true);

	if(prefs_file != NULL)
	{
		String_Array<char>* split_prefs = split_string(arena, prefs_file, "\r\n");

		for(int i = 0; i < split_prefs->num_strings; ++i)
		{
			char* line = split_prefs->strings[i];
			if(string_starts_with(line, "user_pref"))
			{
				// E.g. "user_pref("example.pref", "abc");".
				// This works because we only care about prefs with string values.
				String_Array<char>* split_line = split_string(arena, line, "\"");
				if(split_line->num_strings == 5)
				{
					char* key = split_line->strings[1];
					char* value = split_line->strings[3];

					bool is_cache_key = strings_are_equal(key, "browser.cache.disk.parent_directory")
										|| strings_are_equal(key, "browser.cache.disk.directory")
										|| strings_are_equal(key, "browser.newcache.directory")
										|| strings_are_equal(key, "browser.cache.directory");

					if(is_cache_key)
					{
						log_print(LOG_INFO, "Find Cache Parent Directory In Mozilla Prefs: Found the key '%hs' with the cache path '%hs'.", key, value);

						TCHAR* cache_directory_path = convert_utf_8_string_to_tchar(arena, value);
						string_unescape(cache_directory_path);

						if(exporter->should_load_external_locations)
						{
							success = resolve_exporter_external_locations_path(exporter, cache_directory_path, result_cache_path);
						}
						else
						{
							success = SUCCEEDED(StringCchCopy(result_cache_path, MAX_PATH_CHARS, cache_directory_path));
						}

						break;
					}
				}
			}
		}
	}
	else
	{
		log_print(LOG_ERROR, "Find Cache Parent Directory In Mozilla Prefs: Failed to read the prefs file in '%s'.", prefs_file_path);
	}

	clear_arena(arena);
	unlock_arena(arena);

	return success;
}

// Finds and exports the Mozilla cache from a given browser's default location.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the Mozilla cache should be exported.
// 2. vendor_and_browser_subdirectories - A string specifying one or more directories that identify the browser (e.g. "Mozilla\Firefox").
// 3. output_subdirectory_name - The name of the exporter's output subdirectory. This can be used to identify the browser (e.g. "FF" for Firefox).
// 4. optional_use_old_profiles_directory - An optional parameter that specifies if the old profiles directory name should be used (true) or not (false).
// This value defaults to false.
//
// @Returns: Nothing.
static void export_default_mozilla_cache(	Exporter* exporter, const TCHAR* vendor_and_browser_subdirectories,
											const TCHAR* output_subdirectory_name, bool optional_use_old_profiles_directory = false)
{
	set_exporter_output_copy_subdirectory(exporter, output_subdirectory_name);

	// We need to check both paths since older versions used to store the cache in AppData.
	const TCHAR* cache_appdata_path_array[] = {exporter->local_appdata_path, exporter->appdata_path};
	const TCHAR* CACHE_PROFILES_DIRECTORY_NAME = (optional_use_old_profiles_directory) ? (T("Users50")) : (T("Profiles"));

	for(int i = 0; i < _countof(cache_appdata_path_array); ++i)
	{
		const TCHAR* cache_appdata_path = cache_appdata_path_array[i];
		// Local AppData is skipped for Windows 98 and ME.
		if(string_is_empty(cache_appdata_path)) continue;

		TCHAR cache_profile_path[MAX_PATH_CHARS] = T("");
		PathCombine(cache_profile_path, cache_appdata_path, vendor_and_browser_subdirectories);
		PathAppend(cache_profile_path, CACHE_PROFILES_DIRECTORY_NAME);

		Arena* arena = &(exporter->temporary_arena);
		Traversal_Result* profiles = find_objects_in_directory(arena, cache_profile_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_DIRECTORIES, false);
		lock_arena(arena);

		// Look for browser profiles.
		for(int j = 0; j < profiles->num_objects; ++j)
		{
			Traversal_Object_Info profile_info = profiles->object_info[j];

			// We only check for custom locations in the prefs.js file when iterating over AppData (which is defined for all Windows versions and
			// is where this preferences file is located).
			bool should_check_prefs = (cache_appdata_path == exporter->appdata_path);
			TCHAR prefs_file_path[MAX_PATH_CHARS] = T("");
			PathCombine(prefs_file_path, cache_profile_path, profile_info.object_name);
			PathAppend(prefs_file_path, T("prefs.js"));

			TCHAR prefs_cache_path[MAX_PATH_CHARS] = T("");
			if(should_check_prefs && find_cache_parent_directory_in_mozilla_prefs(exporter, prefs_file_path, prefs_cache_path))
			{
				log_print(LOG_INFO, "Default Mozilla Cache Exporter: Checking the cache directory '%s' found in the prefs file '%s'.", prefs_cache_path, prefs_file_path);
			
				PathCombine(exporter->cache_path, prefs_cache_path, T("."));
				export_mozilla_cache_version_1(exporter);
				export_mozilla_cache_version_2(exporter);

				PathCombine(exporter->cache_path, prefs_cache_path, T("Cache"));
				export_mozilla_cache_version_1(exporter);

				PathCombine(exporter->cache_path, prefs_cache_path, T("cache2"));
				export_mozilla_cache_version_2(exporter);
			}

			TCHAR* parent_cache_path = profile_info.object_path;

			// If it exists, the custom location in the prefs.js file may be defined as the parent directory and not the cache directory itself.
			// By default, we try to append the possible cache subdirectory names to the current profile path we're iterating over, so we need
			// to be careful and avoid exporting the cache twice from the same location. If this prefs location doesn't exist, we always export
			// normally below.
			if(!do_paths_refer_to_the_same_directory(prefs_cache_path, parent_cache_path))
			{
				PathCombine(exporter->cache_path, parent_cache_path, T("Cache"));
				export_mozilla_cache_version_1(exporter);

				PathCombine(exporter->cache_path, parent_cache_path, T("cache2"));
				export_mozilla_cache_version_2(exporter);				
			}
			else
			{
				log_print(LOG_WARNING, "Default Mozilla Cache Exporter: Skipping the cache path '%s' since it's the same directory as the one found in the prefs: '%s'.", parent_cache_path, prefs_cache_path);
			}

			Traversal_Result* salt_directories = find_objects_in_directory(arena, profile_info.object_path, T("*.slt"), TRAVERSE_DIRECTORIES, false);
			lock_arena(arena);

			// Look for salt directories inside each browser profiles (for the old structure).
			for(int k = 0; k < salt_directories->num_objects; ++k)
			{
				Traversal_Object_Info salt_directory_info = salt_directories->object_info[k];

				PathCombine(prefs_file_path, cache_profile_path, profile_info.object_name);
				PathAppend(prefs_file_path, salt_directory_info.object_name);
				PathAppend(prefs_file_path, T("prefs.js"));

				if(should_check_prefs && find_cache_parent_directory_in_mozilla_prefs(exporter, prefs_file_path, prefs_cache_path))
				{
					log_print(LOG_INFO, "Default Mozilla Cache Exporter: Checking the cache directory '%s' found in the prefs file '%s'.", prefs_cache_path, prefs_file_path);
				
					PathCombine(exporter->cache_path, prefs_cache_path, T("."));
					export_mozilla_cache_version_1(exporter);

					PathCombine(exporter->cache_path, prefs_cache_path, T("Cache"));
					export_mozilla_cache_version_1(exporter);

					PathCombine(exporter->cache_path, prefs_cache_path, T("NewCache"));
					export_mozilla_cache_version_1(exporter);
				}

				parent_cache_path = salt_directory_info.object_path;

				if(!do_paths_refer_to_the_same_directory(prefs_cache_path, parent_cache_path))
				{
					PathCombine(exporter->cache_path, parent_cache_path, T("Cache"));
					export_mozilla_cache_version_1(exporter);

					PathCombine(exporter->cache_path, parent_cache_path, T("NewCache"));
					export_mozilla_cache_version_1(exporter);					
				}
				else
				{
					log_print(LOG_WARNING, "Default Mozilla Cache Exporter: Skipping the cache path '%s' since it's the same directory as the one found in the prefs: '%s'.", parent_cache_path, prefs_cache_path);
				}
			}

			unlock_arena(arena);
		}

		unlock_arena(arena);
	}
}

// Entry point for the Mozilla cache exporter. This function will determine where to look for the cache before processing its contents.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the Mozilla cache should be exported.
// If the path to this location isn't defined, this function will try to find it in multiple paths used by different browsers.
//
// @Returns: Nothing.
void export_default_or_specific_mozilla_cache(Exporter* exporter)
{
	console_print("Exporting the Mozilla cache...");
	log_print(LOG_INFO, "Mozilla Cache Exporter: Started exporting the cache.");
	log_print_newline();

	DEBUG_BEGIN_MEASURE_TIME("Export Mozilla's Cache");

	initialize_cache_exporter(exporter, CACHE_MOZILLA, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		if(exporter->is_exporting_from_default_locations)
		{
			export_default_mozilla_cache(exporter, T("Mozilla\\Firefox"), 					T("FF"));
			export_default_mozilla_cache(exporter, T("Mozilla\\SeaMonkey"), 					T("SM"));
			export_default_mozilla_cache(exporter, T("Moonchild Productions\\Pale Moon"), 	T("PM"));
			export_default_mozilla_cache(exporter, T("Moonchild Productions\\Basilisk"), 	T("BS"));
			export_default_mozilla_cache(exporter, T("Waterfox"), 							T("WF"));
			export_default_mozilla_cache(exporter, T("K-Meleon"), 							T("KM"));
			export_default_mozilla_cache(exporter, T("Netscape\\Navigator"), 				T("NS"));
			export_default_mozilla_cache(exporter, T("Netscape\\NSB"), 						T("NS"));

			export_default_mozilla_cache(exporter, T("Phoenix"), 							T("PH-FB"));
			export_default_mozilla_cache(exporter, T("Mozilla"), 							T("MS-NS"), false);
			export_default_mozilla_cache(exporter, T("Mozilla"), 							T("MS-NS"), true);
		}
		else
		{
			export_mozilla_cache_version_1(exporter);
			export_mozilla_cache_version_2(exporter);
		}
	}
	terminate_cache_exporter(exporter);

	DEBUG_END_MEASURE_TIME();

	log_print_newline();
	log_print(LOG_INFO, "Mozilla Cache Exporter: Finished exporting the cache.");
}

// @FormatVersion: Mozilla 0.9.5 to Firefox 31 (Cache\_CACHE_MAP_).
// @ByteOrder: Big Endian.
// @CharacterEncoding: ASCII.
// @DateTimeFormat: Unix time.

static const size_t MZ1_NUM_BUCKETS = 32;

#pragma pack(push, 1)

// @Format: See nsDiskCacheRecord in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheMap.h
struct Mozilla_1_Map_Record
{
	u32 hash_number;
	u32 eviction_rank;
	u32 data_location;
	u32 metadata_location;
};

// Mozilla Version 	| Header Version
// Mozilla 0.9.5	| 00 01 00 03 = 1.3
// Mozilla 1.2 		| 00 01 00 05 = 1.5
// Mozilla 1.7.13 	| 00 01 00 05 = 1.5 (last Mozilla Suite version)
// Firefox 1.5 		| 00 01 00 06 = 1.6 (map header format change)
// Firefox 2.0 		| 00 01 00 08 = 1.8
// Firefox 3.0		| 00 01 00 0B = 1.11
// Firefox 4.0 		| 00 01 00 13 = 1.19
// Firefox 31 		| 00 01 00 13 = 1.19

// @Format: See nsDiskCacheHeader in nsDiskCacheMap.h (https://www-archive.mozilla.org/releases/old-releases-0.9.2-1.0rc3).
// The version is defined in nsDiskCache.h.
struct Mozilla_1_Map_Header_Version_3_To_5
{
	u16 major_version;
	u16 minor_version;
	s32 data_size; // @Format: Signed integer.
	s32 num_entries; // @Format: Signed integer.
	u32 dirty_flag;

	u32 eviction_ranks[MZ1_NUM_BUCKETS];
};

// @Format: Padded to the block size: sizeof(nsDiskCacheBucket) - sizeof(Previous Members Of nsDiskCacheHeader).
// Where sizeof(nsDiskCacheBucket) = kRecordsPerBucket * sizeof(nsDiskCacheRecord).
static const size_t MZ1_MAP_HEADER_VERSION_3_TO_5_PADDING_SIZE = 256 * sizeof(Mozilla_1_Map_Record) - sizeof(Mozilla_1_Map_Header_Version_3_To_5);

// @Format: See nsDiskCacheHeader in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheMap.h
// The version is defined in https://hg.mozilla.org/mozilla-central/log/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCache.h?patch=&linerange=20:22
struct Mozilla_1_Map_Header_Version_6_To_19
{
	u16 major_version;
	u16 minor_version;
	u32 data_size; 
	s32 num_entries; // @Format: Signed integer.
	u32 dirty_flag;

	s32 num_records; // @Format: Signed integer.
	u32 eviction_ranks[MZ1_NUM_BUCKETS];
	u32 bucket_usage[MZ1_NUM_BUCKETS];
};

// @Format: See the enum in nsDiskCacheRecord in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheMap.h
enum Mozilla_1_Data_Mask_Or_Offset
{
	MZ1_LOCATION_INITIALIZED_MASK = 0x80000000,

	MZ1_LOCATION_SELECTOR_MASK = 0x30000000,
	MZ1_LOCATION_SELECTOR_OFFSET = 28,

	MZ1_EXTRA_BLOCKS_MASK = 0x03000000,
	MZ1_EXTRA_BLOCKS_OFFSET = 24,

	MZ1_RESERVED_MASK = 0x4C000000,

	MZ1_BLOCK_NUMBER_MASK = 0x00FFFFFF,

	MZ1_FILE_SIZE_MASK = 0x00FFFF00,
	MZ1_FILE_SIZE_OFFSET = 8,
	MZ1_FILE_GENERATION_MASK = 0x000000FF,
	MZ1_FILE_RESERVED_MASK = 0x4F000000,	
};

// @Format: See nsDiskCacheEntry in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsDiskCacheEntry.h
// The version should be the same as Mozilla_1_Map_Header_*.
struct Mozilla_1_Metadata_Entry
{
	u16 header_major_version;
	u16 header_minor_version;
	u32 meta_location;
	s32 access_count; // @Format: Signed integer.
	u32 last_access_time;

	u32 last_modified_time;
	u32 expiry_time;
	u32 data_size;
	u32 key_size; // @Format: Includes the null terminator.

	u32 elements_size; // @Format: Includes the null terminator.
};

#pragma pack(pop)

_STATIC_ASSERT(sizeof(Mozilla_1_Map_Header_Version_3_To_5) == 144);
_STATIC_ASSERT(sizeof(Mozilla_1_Map_Header_Version_6_To_19) == 276);
_STATIC_ASSERT(sizeof(Mozilla_1_Map_Record) == 16);
_STATIC_ASSERT(sizeof(Mozilla_1_Metadata_Entry) == 36);

// Retrieves any HTTP headers and request origin information from the elements structure used by the Mozilla cache file format.
// This structure maps keys to values, both of which are null terminated ASCII strings that are stored contiguously.
//
// @Parameters:
// 1. arena - The Arena structure that receives the resulting values.
// 2. elements - The beginning of the elements structure present in the Mozilla cache file format.
// 3. elements_size - The total size of this structure in bytes.
// 4. result_headers - The resulting HTTP headers structure.
// 5. result_request_origin - The resulting request origin string.
//
// @Returns: Nothing.
static void parse_mozilla_cache_elements(	Arena* arena, void* elements, u32 elements_size,
											Http_Headers* result_headers, TCHAR** result_request_origin)
{
	char* element_key = (char*) elements;
	void* end_of_metadata = advance_bytes(elements, elements_size);
	
	while(element_key < end_of_metadata)
	{
		char* element_value = skip_to_next_string(element_key);

		if(strings_are_equal(element_key, "response-head", true))
		{
			parse_http_headers(arena, element_value, string_size(element_value), result_headers);
		}
		else if(*result_request_origin == NULL && strings_are_equal(element_key, "request-origin", true))
		{
			*result_request_origin = convert_ansi_string_to_tchar(arena, element_value);
		}

		element_key = skip_to_next_string(element_value);
	}
}

// Exports the Mozilla cache format (version 1) from a given location.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the Mozilla cache should be exported.
//
// @Returns: Nothing.
static void export_mozilla_cache_version_1(Exporter* exporter)
{
	log_print(LOG_INFO, "Mozilla Cache Version 1: Exporting the cache from '%s'.", exporter->cache_path);

	Arena* arena = &(exporter->temporary_arena);

	PathCombine(exporter->index_path, exporter->cache_path, T("_CACHE_MAP_"));
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

	const size_t MINIMUM_MAP_HEADER_SIZE = MIN(sizeof(Mozilla_1_Map_Header_Version_3_To_5), sizeof(Mozilla_1_Map_Header_Version_6_To_19));
	if(map_file_size < MINIMUM_MAP_HEADER_SIZE)
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 1: The size of the map file (%I64u) is smaller than the minimum header size (%Iu). No files will be exported from this cache.", map_file_size, MINIMUM_MAP_HEADER_SIZE);
		return;
	}

	TCHAR temporary_file_path[MAX_PATH_CHARS] = T("");
	HANDLE temporary_file_handle = INVALID_HANDLE_VALUE;
	
	if(!create_temporary_exporter_file(exporter, temporary_file_path, &temporary_file_handle))
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to create the intermediate file in the temporary exporter directory. No files will be exported from this cache.");
		return;
	}

	void* header_cursor = map_file;
	u64 remaining_header_size = map_file_size;
	bool reached_end_of_header = false;

	// Helper macro function used to read an integer of any size from the current file position.
	#define READ_INTEGER(variable)\
	do\
	{\
		if(remaining_header_size < sizeof(variable)) reached_end_of_header = true;\
		if(reached_end_of_header) break;\
		\
		CopyMemory(&variable, header_cursor, sizeof(variable));\
		BIG_ENDIAN_TO_HOST(variable);\
		\
		header_cursor = advance_bytes(header_cursor, sizeof(variable));\
		remaining_header_size -= sizeof(variable);\
	} while(false, false)

	// Helper macro function used to read an array of integers from the current file position.
	#define READ_ARRAY(variable)\
	do\
	{\
		if(remaining_header_size < sizeof(variable)) reached_end_of_header = true;\
		if(reached_end_of_header) break;\
		\
		for(int i = 0; i < _countof(variable); ++i)\
		{\
			READ_INTEGER(variable[i]);\
		}\
	} while(false, false)

	Mozilla_1_Map_Header_Version_6_To_19 header = {};

	READ_INTEGER(header.major_version);
	READ_INTEGER(header.minor_version);

	bool is_version_1_5_or_earlier = (header.major_version <= 1 && header.minor_version <= 5);
	bool is_version_1_19_or_later = (header.major_version >= 1 && header.minor_version >= 19);
	
	const u32 MAP_HEADER_SIZE = (is_version_1_5_or_earlier) ?
								(sizeof(Mozilla_1_Map_Header_Version_3_To_5) + MZ1_MAP_HEADER_VERSION_3_TO_5_PADDING_SIZE) :
								(sizeof(Mozilla_1_Map_Header_Version_6_To_19));

	if(is_version_1_5_or_earlier)
	{
		READ_INTEGER(header.data_size);
		READ_INTEGER(header.num_entries);
		READ_INTEGER(header.dirty_flag);

		READ_ARRAY(header.eviction_ranks);
	}
	else
	{
		READ_INTEGER(header.data_size);
		READ_INTEGER(header.num_entries);
		READ_INTEGER(header.dirty_flag);
		READ_INTEGER(header.num_records);

		READ_ARRAY(header.eviction_ranks);
		READ_ARRAY(header.bucket_usage);
	}

	#undef READ_INTEGER
	#undef READ_ARRAY

	const size_t MAX_CACHE_VERSION_CHARS = MAX_INT16_CHARS + 1 + MAX_INT16_CHARS;
	TCHAR cache_version[MAX_CACHE_VERSION_CHARS] = T("");
	StringCchPrintf(cache_version, MAX_CACHE_VERSION_CHARS, T("%hu.%hu"), header.major_version, header.minor_version);

	log_print(LOG_INFO, "Mozilla Cache Version 1: The map file (version %s) was opened successfully.", cache_version);

	if(header.dirty_flag != 0)
	{
		log_print(LOG_WARNING, "Mozilla Cache Version 1: The map file's dirty flag is set to 0x%08X.", header.dirty_flag);
	}

	u32 num_records = ((u32) map_file_size - MAP_HEADER_SIZE) / sizeof(Mozilla_1_Map_Record);
	if(!is_version_1_5_or_earlier)
	{
		if(header.num_records < 0)
		{
			log_print(LOG_WARNING, "Mozilla Cache Version 1: The map file header has a negative number of records (%I32d).", header.num_records);
		}
		else if(num_records != (u32) header.num_records)
		{
			log_print(LOG_WARNING, "Mozilla Cache Version 1: The map file header has %I32d records when %I32u were expected. Only this last number of records will be processed.", header.num_records, num_records);
		}
	}

	// Open any existing blocks files for reading and determine version-specific parameters.
	const size_t MAX_BLOCK_FILENAME_CHARS = 12;
	struct Block_File
	{
		TCHAR filename[MAX_BLOCK_FILENAME_CHARS];
		TCHAR file_path[MAX_PATH_CHARS];		
		HANDLE file_handle;

		u32 header_size; // Bitmap.
		u32 block_size;
		u32 max_entry_size;
	};

	const int MAX_NUM_BLOCKS_PER_RECORD = 4;
	const int MAX_NUM_BLOCK_FILES = 3;
	Block_File block_file_array[MAX_NUM_BLOCK_FILES + 1] = {};

	// Block file zero corresponds to an external file and is never accessed using this array.
	for(int i = 1; i <= MAX_NUM_BLOCK_FILES; ++i)
	{
		Block_File* block_file = &block_file_array[i];

		StringCchPrintf(block_file->filename, MAX_BLOCK_FILENAME_CHARS, T("_CACHE_00%d_"), i);
		PathCombine(block_file->file_path, exporter->cache_path, block_file->filename);

		block_file->file_handle = create_handle(block_file->file_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, 0);

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
		
		// @Format:
		//
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
		// Number of Blocks = (131072 >> (2 * (index - 1))), Number of Words = Number of Blocks / 32, Number of Bytes = Number of Word * 4
		// Block File Header Size = (131072 >> (2 * (index - 1))) / 32 * 4
		// Block File 1: 131072 >> 0 = 131072 / 32 * 4 = 16384
		// Block File 2: 131072 >> 2 = 32768 / 32 * 4 = 4096
		// Block File 3: 131072 >> 4 = 8192 / 32 * 4 = 1024

		if(i == 1)
		{
			block_file->header_size = (is_version_1_19_or_later) ? (16384) : (4096);
			block_file->block_size = 256;
		}
		else if(i == 2)
		{
			block_file->header_size = 4096;
			block_file->block_size = 1024;
		}
		else if(i == 3)
		{
			block_file->header_size = (is_version_1_19_or_later) ? (1024) : (4096);
			block_file->block_size = 4096;
		}
		else
		{
			_ASSERT(false);
		}

		block_file->max_entry_size = MAX_NUM_BLOCKS_PER_RECORD * block_file->block_size;
	}

	// E.g. "C:\Users\<Username>\AppData\Local\<Vendor and Browser>\Profiles\<Profile Name>\Cache".
	exporter->browser_name = find_path_component(arena, exporter->cache_path, -4);
	exporter->browser_profile = find_path_component(arena, exporter->cache_path, -2);

	// E.g. "C:\Documents and Settings\<Username>\Local Settings\Application Data\<Vendor and Browser>\Profiles\<Profile Name>\<8 Characters>.slt\Cache".
	bool using_old_directory_format = string_ends_with(exporter->browser_profile, T(".slt"), true);
	if(using_old_directory_format)
	{
		TCHAR* profile_name = find_path_component(arena, exporter->cache_path, -3);
		TCHAR* salt_name = exporter->browser_profile;

		TCHAR* profile_and_salt_name = push_arena(arena, MAX_PATH_SIZE, TCHAR);
		PathCombine(profile_and_salt_name, profile_name, salt_name);
		
		exporter->browser_name = find_path_component(arena, exporter->cache_path, -5);
		exporter->browser_profile = profile_and_salt_name;
	}

	lock_arena(arena);

	Mozilla_1_Map_Record* record_array = (Mozilla_1_Map_Record*) advance_bytes(map_file, MAP_HEADER_SIZE);
	_ASSERT((uintptr_t) record_array % sizeof(u32) == 0);

	log_print(LOG_INFO, "Mozilla Cache Version 1: Processing %I32u records in the map file.", num_records);

	for(u32 i = 0; i < num_records; ++i)
	{
		Mozilla_1_Map_Record record = record_array[i];

		if(record.hash_number == 0) continue;

		// @WaitWhat: For the first map file header version (1.3 to 1.5), the records appear to be stored in
		// little endian, even though the header and cache entries are in big endian. The data for version 1.6
		// onwards is always big endian. I don't understand why this only happens for the records in the first
		// version, maybe I'm missing something. This has been tested with versions 1.3, 1.5, 1.6, 1.11, and 1.19.
		if(is_version_1_5_or_earlier)
		{
			LITTLE_ENDIAN_TO_HOST(record.hash_number);
			LITTLE_ENDIAN_TO_HOST(record.eviction_rank);
			LITTLE_ENDIAN_TO_HOST(record.data_location);
			LITTLE_ENDIAN_TO_HOST(record.metadata_location);			
		}
		else
		{
			BIG_ENDIAN_TO_HOST(record.hash_number);
			BIG_ENDIAN_TO_HOST(record.eviction_rank);
			BIG_ENDIAN_TO_HOST(record.data_location);
			BIG_ENDIAN_TO_HOST(record.metadata_location);	
		}
	
		u32 file_initialized = (record.data_location & MZ1_LOCATION_INITIALIZED_MASK);
		u32 file_selector = (record.data_location & MZ1_LOCATION_SELECTOR_MASK) >> MZ1_LOCATION_SELECTOR_OFFSET;
		u8 file_generation = (u8) (record.data_location & MZ1_FILE_GENERATION_MASK);
		u32 file_first_block = (record.data_location & MZ1_BLOCK_NUMBER_MASK);
		u32 file_num_blocks = ((record.data_location & MZ1_EXTRA_BLOCKS_MASK) >> MZ1_EXTRA_BLOCKS_OFFSET) + 1;

		u32 metadata_initialized = (record.metadata_location & MZ1_LOCATION_INITIALIZED_MASK);
		u32 metadata_selector = (record.metadata_location & MZ1_LOCATION_SELECTOR_MASK) >> MZ1_LOCATION_SELECTOR_OFFSET;
		u8 metadata_generation = (u8) (record.metadata_location & MZ1_FILE_GENERATION_MASK);
		u32 metadata_first_block = (record.metadata_location & MZ1_BLOCK_NUMBER_MASK);
		u32 metadata_num_blocks = ((record.metadata_location & MZ1_EXTRA_BLOCKS_MASK) >> MZ1_EXTRA_BLOCKS_OFFSET) + 1;	

		bool is_file_initialized = (file_initialized != 0);
		bool is_metadata_initialized = (metadata_initialized != 0);

		if(!is_file_initialized && !is_metadata_initialized) continue;

		_ASSERT(1 <= file_num_blocks && file_num_blocks <= MAX_NUM_BLOCKS_PER_RECORD);
		_ASSERT(1 <= metadata_num_blocks && metadata_num_blocks <= MAX_NUM_BLOCKS_PER_RECORD);

		// Determines the filename or short path of an external file associated with the current record.
		//
		// @Parameters:
		// 1. is_metadata - Whether the external file contains cached data (false) or its metadata (true).
		// 2. result_path - The buffer which receives the filename or short path. This buffer must be able to hold MAX_PATH_CHARS characters.
		#define GET_EXTERNAL_DATA_FILE_PATH(is_metadata, result_path)\
		do\
		{\
			TCHAR hash[MAX_INT32_CHARS] = T("");\
			StringCchPrintf(hash, MAX_INT32_CHARS, T("%08X"), record.hash_number);\
			const TCHAR* identifier = (is_metadata) ? (T("m")) : (T("d"));\
			u8 generation = (is_metadata) ? (metadata_generation) : (file_generation);\
			/* For example: data file (hash 0E0A6E00 and generation 1) -> "0\E0\A6E00d01" */\
			if(is_version_1_19_or_later)\
			{\
				StringCchPrintf(result_path, MAX_PATH_CHARS, T("%.1s\\") T("%.2s\\") T("%s") T("%s") T("%02X"), hash, hash + 1, hash + 3, identifier, generation);\
			}\
			/* For example: data file (hash 0E0A6E00 and generation 1) -> "0E0A6E00d01" */\
			else\
			{\
				StringCchPrintf(result_path, MAX_PATH_CHARS, T("%s") T("%s") T("%02X"), hash, identifier, generation);\
			}\
		} while(false, false)

		Mozilla_1_Metadata_Entry* metadata = NULL;

		if(is_metadata_initialized)
		{
			if(metadata_selector <= MAX_NUM_BLOCK_FILES)
			{
				if(metadata_selector == 0)
				{				
					TCHAR full_metadata_path[MAX_PATH_CHARS] = T("");
					GET_EXTERNAL_DATA_FILE_PATH(true, full_metadata_path);
					PathCombine(full_metadata_path, exporter->cache_path, full_metadata_path);
					
					u64 metadata_file_size = 0;
					metadata = (Mozilla_1_Metadata_Entry*) read_entire_file(arena, full_metadata_path, &metadata_file_size);
					if(metadata != NULL)
					{
						if(metadata_file_size < sizeof(Mozilla_1_Metadata_Entry))
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
						_ASSERT(sizeof(Mozilla_1_Metadata_Entry) <= total_metadata_size);
						_ASSERT(total_metadata_size <= block_file.max_entry_size);

						metadata = push_arena(arena, total_metadata_size, Mozilla_1_Metadata_Entry);

						u32 read_metadata_size = 0;
						if(read_file_chunk(block_file.file_handle, metadata, total_metadata_size, offset_in_block_file, true, &read_metadata_size))
						{
							if(read_metadata_size < sizeof(Mozilla_1_Metadata_Entry))
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

		TCHAR cached_file_size_string[MAX_INT32_CHARS] = T("");
		TCHAR access_count[MAX_INT32_CHARS] = T("");

		TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
		TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
		TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
		
		TCHAR* url = NULL;
		TCHAR* request_origin = NULL;
		Http_Headers headers = {};

		if(metadata != NULL)
		{
			// Here, the metadata size is at least sizeof(Mozilla_1_Metadata_Entry).

			BIG_ENDIAN_TO_HOST(metadata->header_major_version);
			BIG_ENDIAN_TO_HOST(metadata->header_minor_version);
			BIG_ENDIAN_TO_HOST(metadata->meta_location);
			BIG_ENDIAN_TO_HOST(metadata->access_count);
			
			BIG_ENDIAN_TO_HOST(metadata->last_access_time);
			BIG_ENDIAN_TO_HOST(metadata->last_modified_time);
			BIG_ENDIAN_TO_HOST(metadata->expiry_time);
			BIG_ENDIAN_TO_HOST(metadata->data_size);
			
			BIG_ENDIAN_TO_HOST(metadata->key_size);
			BIG_ENDIAN_TO_HOST(metadata->elements_size);

			_ASSERT( (metadata->header_major_version == header.major_version) && (metadata->header_minor_version == header.minor_version) );

			convert_u32_to_string(metadata->data_size, cached_file_size_string);
			convert_s32_to_string(metadata->access_count, access_count);
		
			format_time64_t_date_time(metadata->last_access_time, last_access_time);
			format_time64_t_date_time(metadata->last_modified_time, last_modified_time);
			format_time64_t_date_time(metadata->expiry_time, expiry_time);

			Block_File block_file = block_file_array[metadata_selector];
			u32 remaining_metadata_size = (metadata_num_blocks * block_file.block_size) - sizeof(Mozilla_1_Metadata_Entry);
			_ASSERT( (metadata_num_blocks * block_file.block_size) >= sizeof(Mozilla_1_Metadata_Entry) );

			// @Format: The key and elements are null terminated.

			if(remaining_metadata_size >= metadata->key_size)
			{
				// @Format: Extract the URL from the metadata key. This key contains two values separated by the colon character,
				// where the URL is the second one. For example: "HTTP:http://www.example.com/index.html"
				//
				// See:
				// - ClientKeyFromCacheKey() in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsCache.cpp
				// - nsCacheService::CreateRequest() in https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache/nsCacheService.cpp

				char* key_in_metadata = (char*) advance_bytes(metadata, sizeof(Mozilla_1_Metadata_Entry));
				TCHAR* key = convert_ansi_string_to_tchar(arena, key_in_metadata);
				
				String_Array<TCHAR>* split_key = split_string(arena, key, T(":"), 1);

				if(split_key->num_strings == 2)
				{
					url = split_key->strings[1];
					url = decode_url(arena, url);
				}
				else
				{
					log_print(LOG_WARNING, "Mozilla Cache Version 1: The key '%s' in record %I32u does not contain the URL.", key, i);
				}

				remaining_metadata_size -= metadata->key_size;

				if(remaining_metadata_size >= metadata->elements_size)
				{
					void* elements = advance_bytes(key_in_metadata, metadata->key_size);
					parse_mozilla_cache_elements(arena, elements, metadata->elements_size, &headers, &request_origin);

					remaining_metadata_size -= metadata->elements_size;
				}
				else
				{
					log_print(LOG_WARNING, "Mozilla Cache Version 1: Skipping the elements metadata in record %I32u since the remaining size (%I32u) is too small to contain the elements (%I32u).", i, remaining_metadata_size, metadata->elements_size);
				}
			}
			else
			{
				log_print(LOG_WARNING, "Mozilla Cache Version 1: Skipping the key and elements metadata in record %I32u since the remaining size (%I32u) is too small to contain the key (%I32u).", i, remaining_metadata_size, metadata->key_size);
			}
		}

		// The file we'll copy will either be the cached file (if the data is stored in its own file),
		// or the temporary file (if we had to extract some chunks from a block file).
		TCHAR cached_file_path[MAX_PATH_CHARS] = T("");
		TCHAR* copy_source_path = NULL;

		TCHAR short_location_on_cache[MAX_PATH_CHARS] = T("");
		TCHAR full_location_on_cache[MAX_PATH_CHARS] = T("");

		if(is_file_initialized)
		{
			if(file_selector <= MAX_NUM_BLOCK_FILES)
			{
				if(file_selector == 0)
				{	
					TCHAR short_data_path[MAX_PATH_CHARS] = T("");		
					GET_EXTERNAL_DATA_FILE_PATH(false, short_data_path);
					
					PathCombine(cached_file_path, exporter->cache_path, short_data_path);
					PathCombine(short_location_on_cache, exporter->browser_profile, short_data_path);

					copy_source_path = cached_file_path;
				}
				else
				{
					// For external data, the file we'll copy will always be the intermediate temporary file that was previously created
					// (unless we fail to extract some chunks from the block file).
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
								// Avoid copying more bytes than expected if the size in the metadata is wrong.
								read_cached_file_size = MIN(read_cached_file_size, metadata->data_size);
							}
							else
							{
								// Try to guess the cached file's size if there's no metadata. This isn't guaranteed to work since
								// we might remove one too many null bytes and corrupt the real cached file.
								// @Format: The data in a block file is padded with null bytes, unless it's the last entry.
								u32 num_null_bytes = 0;
								u8* last_cached_file_byte = (u8*) advance_bytes(cached_file_in_block_file, read_cached_file_size - 1);

								while(*last_cached_file_byte == 0 && num_null_bytes < read_cached_file_size)
								{
									++num_null_bytes;
									--last_cached_file_byte;
								}

								_ASSERT(num_null_bytes <= read_cached_file_size);
								read_cached_file_size -= num_null_bytes;

								add_exporter_warning_message(exporter, "Removed %I32u bytes from the end of the file due to missing metadata. The file size was reduced from %I32u to %I32u.", num_null_bytes, read_cached_file_size + num_null_bytes, read_cached_file_size);
								log_print(LOG_WARNING, "Mozilla Cache Version 1: Attempted to find the cached file's size in record %I32u since the metadata was missing. Reduced the size to %I32u after finding %I32u null bytes. The exported file may be corrupted.", i, read_cached_file_size, num_null_bytes);
							}

							bool write_success = empty_file(temporary_file_handle) && write_to_file(temporary_file_handle, cached_file_in_block_file, read_cached_file_size);

							if(write_success)
							{
								copy_source_path = temporary_file_path;
							}
							else
							{
								log_print(LOG_ERROR, "Mozilla Cache Version 1: Failed to write the cached file (%I32u) in record %I32u from block file '%s' to the temporary exporter directory.", read_cached_file_size, i, block_file.filename);
							}

							// Create a pretty version of the location on cache which includes the address and size in the block file.
							const size_t MAX_LOCATION_IN_FILE_CHARS = MAX_INT32_CHARS * 2 + 2;
							TCHAR location_in_file[MAX_LOCATION_IN_FILE_CHARS] = T("");
							StringCchPrintf(location_in_file, MAX_LOCATION_IN_FILE_CHARS, T("@%08X") T("#%08X"), offset_in_block_file, read_cached_file_size);

							PathCombine(short_location_on_cache, exporter->browser_profile, block_file.filename);
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

		#undef GET_EXTERNAL_DATA_FILE_PATH

		Csv_Entry csv_row[] =
		{
			{/* Filename */}, {/* URL */}, {/* Request Origin */}, {/* File Extension */}, {cached_file_size_string},
			{last_modified_time}, {last_access_time}, {expiry_time}, {access_count},
			{/* Response */}, {/* Server */}, {/* Cache Control */}, {/* Pragma */},
			{/* Content Type */}, {/* Content Length */}, {/* Content Range */}, {/* Content Encoding */},
			{/* Location On Cache */}, {exporter->browser_name}, {cache_version},
			{/* Missing File */}, {/* Location In Output */}, {/* Copy Error */}, {/* Exporter Warning */},
			{/* Custom File Group */}, {/* Custom URL Group */}, {/* SHA-256 */}
		};
		_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

		Exporter_Params params = {};
		params.copy_source_path = copy_source_path;
		params.url = url;
		params.filename = NULL; // Comes from the URL.
		params.request_origin = request_origin;
		params.headers = headers;
		params.short_location_on_cache = short_location_on_cache;
		params.full_location_on_cache = full_location_on_cache;

		export_cache_entry(exporter, csv_row, &params);
	}

	unlock_arena(arena);
	clear_arena(arena);

	exporter->browser_name = NULL;
	exporter->browser_profile = NULL;

	for(int i = 1; i <= MAX_NUM_BLOCK_FILES; ++i)
	{
		Block_File* block_file = &block_file_array[i];
		safe_close_handle(&block_file->file_handle);
	}

	safe_close_handle(&temporary_file_handle);
}

// @FormatVersion: Mozilla Firefox 32 and later (cache2\entries\*).
// @ByteOrder: Big Endian.
// @CharacterEncoding: ASCII.
// @DateTimeFormat: Unix time.

#pragma pack(push, 1)

// @Format: See CacheIndexHeader in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheIndex.h
// The version is defined in https://hg.mozilla.org/mozilla-central/log/tip/netwerk/cache2/CacheIndex.cpp?patch=&linerange=29:29
struct Mozilla_2_Index_Header
{
	u32 version;
	u32 last_write_time;
	u32 dirty_flag;

	// This last member didn't exist in the oldest header version, and since we only use two of the members above
	// there's no need to add it to the total header struct size.
	#if 0
	u32 used_cache_size; // In kilobytes.
	#endif
};

// @Format: CacheFileMetadataHeader in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileMetadata.h
// The version is defined in https://hg.mozilla.org/mozilla-central/log/tip/netwerk/cache2/CacheFileMetadata.h?patch=&linerange=37:37
struct Mozilla_2_Metadata_Header_Version_1_And_2
{
	u32 version;
	u32 access_count;
	
	u32 last_access_time;
	u32 last_modified_time;
	
	u32 expiry_time;
	u32 key_length; // @Format: Called "mKeySize" but it's set to "mKey.Length()". Does not include the null terminator.
};

struct Mozilla_2_Metadata_Header_Version_3
{
	u32 version;
	u32 access_count;
	
	u32 last_access_time;
	u32 last_modified_time;
	
	u32 frecency;
	u32 expiry_time;
	
	u32 key_length; // @Format: See above.
	u32 flags;
};

#pragma pack(pop)

_STATIC_ASSERT(sizeof(Mozilla_2_Index_Header) == 12);
_STATIC_ASSERT(sizeof(Mozilla_2_Metadata_Header_Version_1_And_2) == 24);
_STATIC_ASSERT(sizeof(Mozilla_2_Metadata_Header_Version_3) == 32);

struct Find_Mozilla_2_Files_Params
{
	Exporter* exporter;
	u32 index_version;
	TCHAR temporary_file_path[MAX_PATH_CHARS];
	HANDLE temporary_file_handle;
};

// Called every time a file is found in the Mozilla cache directory (version 2). Used to export every cache entry.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_mozilla_cache_version_2_files_callback)
{
	Find_Mozilla_2_Files_Params* find_params = (Find_Mozilla_2_Files_Params*) callback_info->user_data;

	Exporter* exporter = find_params->exporter;
	Arena* arena = &(exporter->temporary_arena);

	TCHAR* cached_filename = callback_info->object_name;
	TCHAR* full_location_on_cache = callback_info->object_path;
	u64 total_file_size = callback_info->object_size;

	u32 metadata_offset = 0; // Also the cached file's size.

	if(total_file_size < sizeof(metadata_offset))
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: The size of file '%s' is too small to contain the metadata offset. This cached file will not be exported.", cached_filename);
		return true;
	}

	// @Format: CacheFileMetadata::ReadMetadata() in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileMetadata.cpp
	if(!read_file_chunk(full_location_on_cache, &metadata_offset, sizeof(metadata_offset), total_file_size - sizeof(metadata_offset)))
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: Failed to read the metadata offset in the file '%s' with the error code %lu. This cached file will not be exported.", cached_filename, GetLastError());
		return true;
	}

	BIG_ENDIAN_TO_HOST(metadata_offset);

	if(metadata_offset > total_file_size)
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: The metadata offset 0x%08X goes past the end of the file '%s'. This cached file will not be exported.", metadata_offset, cached_filename);
		return true;
	}

	// @Format: CacheFileMetadata::OnDataRead() in https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2/CacheFileMetadata.cpp
	const u32 HASH_CHUNK_SIZE = 256 * 1024;
	u32 num_hashes = (metadata_offset == 0) ? (0) : (((u32) metadata_offset - 1) / HASH_CHUNK_SIZE + 1);
	u32 hash_size = sizeof(u32) + num_hashes * sizeof(u16);
	
	u32 remaining_metadata_size = (u32) total_file_size - metadata_offset;
	u32 minimum_metadata_size = hash_size + sizeof(Mozilla_2_Metadata_Header_Version_1_And_2) + sizeof(metadata_offset);

	if(remaining_metadata_size < minimum_metadata_size)
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: The size of the metadata in file '%s' was %I32u when at least %I32u bytes were expected. This cached file will not be exported.", cached_filename, remaining_metadata_size, minimum_metadata_size);
		return true;
	}
	
	void* metadata = push_arena(arena, remaining_metadata_size, u8);

	if(!read_file_chunk(full_location_on_cache, metadata, remaining_metadata_size, metadata_offset))
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: Failed to read the metadata in the file '%s' with the error code %lu.", cached_filename, GetLastError());
		metadata = NULL;
	}

	TCHAR cached_file_size[MAX_INT32_CHARS] = T("");
	convert_u32_to_string(metadata_offset, cached_file_size);

	TCHAR access_count[MAX_INT32_CHARS] = T("");

	TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
	TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
	TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");

	const size_t MAX_CACHE_VERSION_CHARS = MAX_INT32_CHARS + 3 + MAX_INT32_CHARS;
	TCHAR cache_version[MAX_CACHE_VERSION_CHARS] = T("");

	TCHAR* url = NULL;
	TCHAR* request_origin = NULL;
	TCHAR* partition_key = NULL;
	Http_Headers headers = {};

	if(metadata != NULL)
	{
		metadata = advance_bytes(metadata, hash_size);
		remaining_metadata_size -= hash_size;
		remaining_metadata_size -= sizeof(metadata_offset);

		// From here on out, the remaining size only takes into account the header, key, and elements.
		// We are guaranteed the size of the first version of the metadata header because of the check above.

		bool reached_end_of_metadata = false;
		// Helper macro function used to read an integer of any size from the current file position.
		#define READ_INTEGER(variable)\
		do\
		{\
			if(remaining_metadata_size < sizeof(variable)) reached_end_of_metadata = true;\
			if(reached_end_of_metadata) break;\
			\
			CopyMemory(&variable, metadata, sizeof(variable));\
			BIG_ENDIAN_TO_HOST(variable);\
			\
			metadata = advance_bytes(metadata, sizeof(variable));\
			remaining_metadata_size -= sizeof(variable);\
		} while(false, false)

		// @Format: Version 3 includes every value from the previous versions.
		Mozilla_2_Metadata_Header_Version_3 metadata_header = {};
		READ_INTEGER(metadata_header.version);

		bool is_version_supported = true;
		if(metadata_header.version <= 2)
		{
			READ_INTEGER(metadata_header.access_count);

			READ_INTEGER(metadata_header.last_access_time);
			READ_INTEGER(metadata_header.last_modified_time);

			READ_INTEGER(metadata_header.expiry_time);
			READ_INTEGER(metadata_header.key_length);
		}
		else if(metadata_header.version == 3)
		{
			READ_INTEGER(metadata_header.access_count);

			READ_INTEGER(metadata_header.last_access_time);
			READ_INTEGER(metadata_header.last_modified_time);

			READ_INTEGER(metadata_header.frecency);
			READ_INTEGER(metadata_header.expiry_time);

			READ_INTEGER(metadata_header.key_length);
			READ_INTEGER(metadata_header.flags);
		}
		else
		{
			is_version_supported = false;
			log_print(LOG_WARNING, "Mozilla Cache Version 2: Skipping the unsupported metadata version %I32u in the file '%s'.", metadata_header.version, cached_filename);
		}

		#undef READ_INTEGER

		if(is_version_supported)
		{
			convert_u32_to_string(metadata_header.access_count, access_count);

			format_time64_t_date_time(metadata_header.last_access_time, last_access_time);
			format_time64_t_date_time(metadata_header.last_modified_time, last_modified_time);
			format_time64_t_date_time(metadata_header.expiry_time, expiry_time);

			StringCchPrintf(cache_version, MAX_CACHE_VERSION_CHARS, T("2-i%I32u-e%I32u"), find_params->index_version, metadata_header.version);

			u32 key_size = metadata_header.key_length + 1;
			if(remaining_metadata_size >= key_size)
			{
				// @Format:
				// Extract the URL and partition key (scheme + host) from the metadata key.
				// This key is a comma separated list of properties, where the first character specifies their type.
				// E.g. "O" = origin attributes, "a" = is anonymous, ":" = the URL (and the last value).
				// Any ":" character before this last value is replaced with the "+" character.
				// 
				// We only want the URL (which always appears at the end), and the partition key (which is part of the
				// origin attributes). These origin attributes start with a "^" character and are followed by a list
				// of URL parameters (e.g. "param1=value1&param2=value2"). The partition key is one of these key-value
				// pairs, and takes the form of "partitionKey=(scheme,host)" or "partitionKey=(scheme,host,port)".
				// These characters "()," are percent encoded.
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
				TCHAR* key = convert_ansi_string_to_tchar(arena, (char*) metadata);

				String_Array<TCHAR>* split_key = split_string(arena, key, T(":"), 1);

				if(split_key->num_strings == 2)
				{
					TCHAR* tags = split_key->strings[0];
					url = split_key->strings[1];
					url = decode_url(arena, url);

					String_Array<TCHAR>* split_tags = split_string(arena, tags, T(","));

					for(int i = 0; i < split_tags->num_strings; ++i)
					{
						TCHAR* tag = split_tags->strings[i];

						if(string_starts_with(tag, T("O^")))
						{
							String_Array<TCHAR>* tag_params = split_string(arena, tag + 2, T("&"));

							for(int j = 0; j < tag_params->num_strings; ++j)
							{
								TCHAR* pair = tag_params->strings[j];
								String_Array<TCHAR>* split_pair = split_string(arena, pair, T("="), 1);

								if(split_pair->num_strings == 2)
								{
									TCHAR* key = split_pair->strings[0];
									TCHAR* value = split_pair->strings[1];

									if(strings_are_equal(key, T("partitionKey")))
									{
										value = decode_url(arena, value);
										String_Array<TCHAR>* url_parts = split_string(arena, value, T("(),"), 2);

										// We split two times at most for the scheme, host, and port, but we only
										// care about the first two.
										if(url_parts->num_strings >= 2)
										{
											TCHAR* scheme = url_parts->strings[0];
											TCHAR* host = url_parts->strings[1];

											size_t num_partition_key_chars = string_length(scheme) + 3 + string_length(host) + 1;
											partition_key = push_arena(arena, num_partition_key_chars * sizeof(TCHAR), TCHAR);

											StringCchPrintf(partition_key, num_partition_key_chars, T("%s://%s"), scheme, host);
										}
										else
										{
											log_print(LOG_WARNING, "Mozilla Cache Version 2: The partition key '%s' in the file '%s' does not contain a scheme and host.", value, cached_filename);
										}

										// We don't care about the other parameters.
										break;
									}
								}
								else
								{
									log_print(LOG_WARNING, "Mozilla Cache Version 2: The key-value pair '%s' in the file '%s' does not contain a value.", pair, cached_filename);
								}
							}

							// We don't care about the other tags.
							break;
						}
					}
				}
				else
				{
					log_print(LOG_WARNING, "Mozilla Cache Version 2: The key '%s' in the file '%s' does not contain the URL.", key, cached_filename);
				}

				metadata = advance_bytes(metadata, key_size);
				remaining_metadata_size -= key_size;

				parse_mozilla_cache_elements(arena, metadata, remaining_metadata_size, &headers, &request_origin);
			}
			else
			{
				log_print(LOG_WARNING, "Mozilla Cache Version 2: Skipping the URL and partition key metadata in the file '%s' since the remaining size (%I32u) is too small to contain them (%I32u).", cached_filename, remaining_metadata_size, key_size);
			}
		}
	}

	// Use the request origin found in the metadata elements if there was one.
	// Otherwise, use the partition key we extracted from the metadata key.
	if(request_origin == NULL)
	{
		request_origin = partition_key;
	}

	// The file we'll copy will always be the intermediate temporary file that was previously created (unless we fail
	// to extract some chunks from the cached file).
	TCHAR* copy_source_path = NULL;
	TCHAR* temporary_file_path = find_params->temporary_file_path;
	HANDLE temporary_file_handle = find_params->temporary_file_handle;

	// Again, the metadata offset is the cached file's size.
	bool copy_success = empty_file(temporary_file_handle) && copy_file_chunks(arena, full_location_on_cache, metadata_offset, 0, temporary_file_handle);

	if(copy_success)
	{
		copy_source_path = temporary_file_path;
	}
	else
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: Failed to copy the cached file of size %I32u in the file '%s' to the temporary exporter directory.", metadata_offset, cached_filename);
	}

	TCHAR short_location_on_cache[MAX_PATH_CHARS] = T("");
	PathCombine(short_location_on_cache, exporter->browser_profile, cached_filename);

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* URL */}, {/* Request Origin */}, {/* File Extension */}, {cached_file_size},
		{last_modified_time}, {last_access_time}, {expiry_time}, {access_count},
		{/* Response */}, {/* Server */}, {/* Cache Control */}, {/* Pragma */},
		{/* Content Type */}, {/* Content Length */}, {/* Content Range */}, {/* Content Encoding */},
		{/* Location On Cache */}, {exporter->browser_name}, {cache_version},
		{/* Missing File */}, {/* Location In Output */}, {/* Copy Error */}, {/* Exporter Warning */},
		{/* Custom File Group */}, {/* Custom URL Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter_Params exporter_params = {};
	exporter_params.copy_source_path = copy_source_path;
	exporter_params.url = url;
	exporter_params.filename = NULL; // Comes from the URL.
	exporter_params.request_origin = request_origin;
	exporter_params.headers = headers;
	exporter_params.short_location_on_cache = short_location_on_cache;
	exporter_params.full_location_on_cache = full_location_on_cache;
	exporter_params.file_info = NULL; // We don't want to use the file's real name on disk if we can't use the URL to determine the output filename.

	export_cache_entry(exporter, csv_row, &exporter_params);

	return true;
}

// Exports the Mozilla cache format (version 2) from a given location.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the Mozilla cache should be exported.
//
// @Returns: Nothing.
static void export_mozilla_cache_version_2(Exporter* exporter)
{
	log_print(LOG_INFO, "Mozilla Cache Version 2: Exporting the cache from '%s'.", exporter->cache_path);

	Arena* arena = &(exporter->temporary_arena);

	TCHAR* cache_directory_name = PathFindFileName(exporter->cache_path);
	const TCHAR* CACHE_ENTRY_DIRECTORY_NAME = T("entries");

	if(!filenames_are_equal(cache_directory_name, CACHE_ENTRY_DIRECTORY_NAME))
	{
		PathAppend(exporter->cache_path, CACHE_ENTRY_DIRECTORY_NAME);
	}

	if(!does_directory_exist(exporter->cache_path))
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: The cache entry directory '%s' does not exist. No files will be exported from this cache.", exporter->cache_path);
		return;
	}

	PathCombine(exporter->index_path, exporter->cache_path, T("..\\index"));

	Mozilla_2_Index_Header index_header = {};
	if(read_first_file_bytes(exporter->index_path, &index_header, sizeof(index_header)))
	{
		BIG_ENDIAN_TO_HOST(index_header.version);
		BIG_ENDIAN_TO_HOST(index_header.last_write_time);
		BIG_ENDIAN_TO_HOST(index_header.dirty_flag);

		if(index_header.dirty_flag != 0)
		{
			log_print(LOG_WARNING, "Mozilla Cache Version 2: The index file's dirty flag is set to 0x%08X.", index_header.dirty_flag);
		}
	}
	else
	{
		log_print(LOG_WARNING, "Mozilla Cache Version 2: Failed to open the index file with the error code %lu.", GetLastError());
	}

	Find_Mozilla_2_Files_Params params = {};
	params.exporter = exporter;
	params.index_version = index_header.version;
	params.temporary_file_path[0] = T('\0');
	params.temporary_file_handle = INVALID_HANDLE_VALUE;

	// E.g. "C:\Users\<Username>\AppData\Local\<Vendor and Browser>\Profiles\<Profile Name>\cache2\entries".
	exporter->browser_name = find_path_component(arena, exporter->cache_path, -5);
	exporter->browser_profile = find_path_component(arena, exporter->cache_path, -3);

	lock_arena(arena);

	if(create_temporary_exporter_file(exporter, params.temporary_file_path, &params.temporary_file_handle))
	{
		traverse_directory_objects(exporter->cache_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES, false, find_mozilla_cache_version_2_files_callback, &params);
	}
	else
	{
		log_print(LOG_ERROR, "Mozilla Cache Version 2: Failed to create the intermediate file in the temporary exporter directory. No files will be exported from this cache.");
	}

	unlock_arena(arena);
	clear_arena(arena);

	exporter->browser_name = NULL;
	exporter->browser_profile = NULL;

	safe_close_handle(&params.temporary_file_handle);
}
