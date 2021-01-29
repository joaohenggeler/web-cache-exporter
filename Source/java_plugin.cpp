#include "web_cache_exporter.h"
#include "java_plugin.h"

/*
	This file defines how the exporter processes the Java Plugin's cache, which stores resources that are requested by Java applets.
	In the beginning, the Java Plugin was only able to store these files in the browser's own cache (likely in Internet Explorer or
	in Netscape Navigator's cache). Java 1.3 (2000) added a mechanism where applets could optionally store some of their archives
	in a separate location on disk. This was done by using a specific applet tag parameter. Java 1.4 (2002) introduced a new cache
	location where you didn't need to opt-in to use it. This location was divided into two subdirectories: another archive cache
	(like in Java 1.3), and a file cache that only allowed commonly used file types (.class files, .gif and .jpg images, and .au
	and .wav sounds). Each cached file had an index file (.idx) associated with it, which contained the requested resource's metadata.
	Java 6 (2006) joined both of these cache locations, and allowed any type of file to be cached. The index files remained though
	their format was changed slightly in a few subversions. Java 9 (2017) deprecated Java applets and Java Web Start applications,
	and these were finally removed from the language in Java 11 (2018).

	@SupportedFormats:
	- The AppletStore JAR cache introduced in Java 1.3.
	- Index files (.IDX) version 1 and 6 (6.02, 6.03, 6.04, 6.05) introduced in Java 1.4 and Java 6.

	@DefaultCacheLocations:
	
	The AppletStore cache:
	- 98, ME 				C:\WINDOWS\java_plugin_AppletStore
	- 2000, XP				C:\Documents and Settings\<Username>\java_plugin_AppletStore
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\java_plugin_AppletStore (theoretically)

	This location is defined as "<User Home>\java_plugin_AppletStore", where <User Home> is Java's "user.home" system property.
	
	Let's call this location the "AppletStore cache".
	Any archives that are specified in the "cache_archive" applet tag parameter are stored here.

	The IDX file cache:
	- 98, ME 				C:\WINDOWS\Application Data\Sun\Java\Deployment\cache
	- 2000, XP 				C:\Documents and Settings\<Username>\Application Data\Sun\Java\Deployment\cache
	- Vista, 7, 8.1, 10	 	C:\Users\<Username>\AppData\LocalLow\Sun\Java\Deployment\cache

	Note that we currently only look at these default locations. It's possible for Java's deployment properties to change the user and
	system level cache locations using the "deployment.user.cachedir" and "deployment.system.cachedir" properties in the
	deployment.properties file. These default to "<User Home>\cache" and <None>, respectively.

	We can consider two sublocations in the IDX file cache:
	1. <Cache Location>\javapi\v1.0
	2. <Cache Location>\6.0

	Where the first only stores archives (.JAR) in the "jar" subdirectory and specific files (.CLASS, .JPG, .GIF, .AU, .WAV) in
	the "file" subdirectory. Note that this "jar" location always stores files with the .ZIP file extension. The second sublocation
	can contain any type of file, stored in any of the 64 subdirectories, named "0" through "63".

	Note that for Java 1.4, the first sublocation is different:
	- For archives: <User Home>\.jpi_cache\jar\1.0
	- For files: 	<User Home>\.jpi_cache\file\1.0
	Notice the use of <User Home> instead of <Cache Location>.

	Note also that different Java vendors might have slightly different directory names. In IBM Java, for example, the second location
	is: "<...>\IBM\Java\Deployment\cache"

	Let's call 1. the "version 1 cache" and 2. the "version 6 cache".
	Any archives that are specified in the "cache_archive" applet tag parameter are stored in the "jar" subdirectory in version 1,
	and in any subdirectory in version 6.

	@Resources: The index file format was investigated by looking at the decompiled code of the following Java archives and release:
	- "jre\lib\jaws.jar" in JDK 1.3.1 update 28.
	- "jre\lib\plugin.jar" in JDK 1.4.2 update 19.
	- "jre\lib\plugin.jar" and "jre\lib\deploy.jar" in JDK 1.5.0 update 22.
	- "jre\lib\deploy.jar" in JDK 6 update 1
	- "jre\lib\deploy.jar" in JDK 8 update 181.

	Only after adding support for reading index files did I look for other resources (partly because I wanted to try to document it on
	my own as a learning exercise). Any previously unknown fields were added in this step.

	[BB] "Java_IDX_Parser" - a script that reads the index file format.
	--> https://github.com/Rurik/Java_IDX_Parser/
	
	[MW] "javaidx" - a console application that also contains documentation on the index file format.
	--> https://github.com/woanware/javaidx

	[JDK-SRC] JDK source code:
	--> https://code.google.com/archive/p/jdk-source-code/

	[JDK-DOCS] The Java API Specification
	--> https://docs.oracle.com/javase/8/docs/api/index.html

	@Tools: Used to investigate the index file format.

	[JD] "JD-GUI 1.4.0"
	--> http://java-decompiler.github.io/
	--> Used to decompiled some Java classes to get a better understanding of how the cache works.
*/

static const TCHAR* OUTPUT_NAME = TEXT("JV");

static const Csv_Type CSV_COLUMN_TYPES[] =
{
	CSV_FILENAME, CSV_URL, CSV_FILE_EXTENSION,
	CSV_LAST_MODIFIED_TIME, CSV_EXPIRY_TIME,
	CSV_RESPONSE, CSV_SERVER, CSV_CACHE_CONTROL, CSV_PRAGMA, CSV_CONTENT_TYPE, CSV_CONTENT_LENGTH, CSV_CONTENT_ENCODING,
	CSV_CODEBASE_IP, CSV_VERSION,
	CSV_LOCATION_ON_CACHE, CSV_CACHE_VERSION, CSV_MISSING_FILE, CSV_LOCATION_IN_OUTPUT, CSV_COPY_ERROR,
	CSV_CUSTOM_FILE_GROUP, CSV_CUSTOM_URL_GROUP, CSV_SHA_256
};

static const size_t CSV_NUM_COLUMNS = _countof(CSV_COLUMN_TYPES);

// Entry point for the Java Plugin's cache exporter. This function will determine where to look for the cache before
// processing its contents.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how the cache should be exported.
// If the path to this location isn't defined, this function will look in the current AppData directory.
//
// @Returns: Nothing.
static TRAVERSE_DIRECTORY_CALLBACK(find_java_applet_store_files_callback);
static TRAVERSE_DIRECTORY_CALLBACK(find_java_index_files_callback);
void export_specific_or_default_java_plugin_cache(Exporter* exporter)
{
	console_print("Exporting the Java Plugin's cache...");

	initialize_cache_exporter(exporter, OUTPUT_NAME, CSV_COLUMN_TYPES, CSV_NUM_COLUMNS);
	{
		TCHAR* appdata_path = NULL;
		if(exporter->is_exporting_from_default_locations)
		{
			appdata_path = exporter->local_low_appdata_path;
			if(string_is_empty(appdata_path)) appdata_path = exporter->appdata_path;

			PathCombine(exporter->cache_path, appdata_path, TEXT("Sun\\Java\\Deployment\\cache"));
		}

		log_print(LOG_INFO, "Java Plugin: Exporting the cache from '%s'.", exporter->cache_path);
		
		traverse_directory_objects(exporter->cache_path, TEXT("*.idx"), TRAVERSE_FILES, true, find_java_index_files_callback, exporter);
		
		if(exporter->is_exporting_from_default_locations)
		{
			TCHAR* user_home_path = exporter->user_profile_path;
			if(string_is_empty(user_home_path)) user_home_path = exporter->windows_path;
		
			PathCombine(exporter->cache_path, user_home_path, TEXT(".jpi_cache"));
			log_print(LOG_INFO, "Java Plugin: Exporting the .jpi_cache from '%s'.", exporter->cache_path);
			traverse_directory_objects(exporter->cache_path, TEXT("*.idx"), TRAVERSE_FILES, true, find_java_index_files_callback, exporter);
		
			PathCombine(exporter->cache_path, appdata_path, TEXT("IBM\\Java\\Deployment\\cache"));
			log_print(LOG_INFO, "Java Plugin: Exporting the IBM Java cache from '%s'.", exporter->cache_path);
			traverse_directory_objects(exporter->cache_path, TEXT("*.idx"), TRAVERSE_FILES, true, find_java_index_files_callback, exporter);

			PathCombine(exporter->cache_path, user_home_path, TEXT("java_plugin_AppletStore"));
			log_print(LOG_INFO, "Java Plugin: Exporting the AppletStore cache from '%s'.", exporter->cache_path);
			set_exporter_output_copy_subdirectory(exporter, TEXT("AppletStore"));
			traverse_directory_objects(exporter->cache_path, TEXT("*"), TRAVERSE_FILES, true, find_java_applet_store_files_callback, exporter);
		}

		log_print(LOG_INFO, "Java Plugin: Finished exporting the cache.");
	}
	terminate_cache_exporter(exporter);
}

// Called every time a file is found in the AppletStore cache. Used to export every cache entry in this location.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(find_java_applet_store_files_callback)
{
	TCHAR* filename = callback_find_data->cFileName;

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, callback_directory_path, filename);

	TCHAR* short_file_path = skip_to_last_path_components(full_file_path, 3);

	TCHAR* cache_version = TEXT("AppletStore");

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* URL */}, {/* File Extension */},
		{/* Last Modified Time */}, {/* Expiry Time */},
		{/* Response */}, {/* Server */}, {/* Cache Control */}, {/* Pragma */}, {/* Content Type */}, {/* Content Length */}, {/* Content Encoding */},
		{/* Codebase IP */}, {/* Version */},
		{short_file_path}, {cache_version}, {/* Missing File */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* Custom URL Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);

	Exporter* exporter = (Exporter*) callback_user_data;
	export_cache_entry(exporter, csv_row, full_file_path, NULL, filename, callback_find_data);

	return true;
}

// The type of cache location where the index files are kept.
// In version 1, there were separate directories for files (images, sounds, and classes) and for archives (ZIPs and JARs).
// In version 6, all types of files were allowed.
enum Location_Type
{
	LOCATION_ALL = 0, // For any type of file. Used by version 6.

	LOCATION_FILES = 1, // For .class, .gif, .jpg, .au, .wav files only. Used by version 1.
	LOCATION_ARCHIVES = 2 // For .zip and .jar files only. Used by version 1.
};

// The version of the supported index file formats.
enum Cache_Version
{
	// Version 1. @Java: Taken from "sun.plugin.cache.Cache" (JDK 1.4).
	VERSION_1 = 16,
	
	// Version 6. @Java: Taken from "com.sun.deploy.cache.CacheEntry" (JDK 6).
	VERSION_602 = 602,
	VERSION_603 = 603,
	VERSION_604 = 604,
	VERSION_605 = 605
};

// The type of file that is allowed to be stored in either the "jar" and "file" subdirectories in version 1.
enum Java_File_Type
{
	JAVA_FILE_UNKNOWN = 0x00,

	JAVA_FILE_JAR = 0x01,
	JAVA_FILE_JARJAR = 0x02,
	JAVA_FILE_NONJAR = 0x03,

	JAVA_FILE_CLASS = 0x11,
	JAVA_FILE_GIF_IMAGE = 0x21,
	JAVA_FILE_JPEG_IMAGE = 0x22,
	JAVA_FILE_AU_SOUND = 0x41,
	JAVA_FILE_WAV_SOUND = 0x42
};

// The size of the header (section 1) of an index file in bytes. This only applies to version 6.
static const u32 VERSION_6_HEADER_SIZE = 128;

// A structure that represents the contents of an index file for all supported versions.
// Note that the following layout doesn't correspond byte for byte to the index file format.
// The members of this structure are filled by read_index_file().
struct Index
{
	// >>>> Version 1 only.
	s8 status;
	s32 file_type;

	// >>>> Version 6 combined with a few attributes from version 1.

	s8 busy;
	s8 incomplete;
	s32 cache_version;

	s8 force_update;
	s8 no_href;

	s8 is_shortcut_image;
	s32 content_length;
	s64 last_modified_time; // In milliseconds.
	s64 expiry_time; // In milliseconds.

	s64 validation_timestamp;
	s8 known_to_be_signed;

	s32 section_2_length;
	s32 section_3_length;
	s32 section_4_length;

	s64 blacklist_validation_time;
	s64 cert_expiration_date;
	s8 class_verification_status;

	s32 reduced_manifest_length;
	s32 section_4_pre_15_Length;
	
	s8 has_only_signed_entries;
	s8 has_single_code_source;
	
	s32 section_4_certs_length;
	s32 section_4_signers_length;

	s8 has_missing_signed_entries;
	s64 trusted_libraries_validation_time;

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
	// This value is used in the Content Length CSV column if it exists. Otherwise, the content length numeric value is used instead.
	TCHAR* content_length_string;
	TCHAR* content_encoding;
};

// Maps a cached resource's file type to its respective file extension. Note that JAR and JARJAR files map to ".zip".
//
// @Parameters:
// 1. type - The file type.
//
// @Returns: The file extension.
static TCHAR* get_cached_file_extension_from_java_file_type(s32 type)
{
	switch(type)
	{
		case(JAVA_FILE_JAR):
		case(JAVA_FILE_JARJAR): 	return TEXT(".zip"); // And not ".jar" or ".jarjar".
		case(JAVA_FILE_CLASS): 		return TEXT(".class");
		case(JAVA_FILE_GIF_IMAGE): 	return TEXT(".gif");
		case(JAVA_FILE_JPEG_IMAGE): return TEXT(".jpg");
		case(JAVA_FILE_AU_SOUND): 	return TEXT(".au");
		case(JAVA_FILE_WAV_SOUND): 	return TEXT(".wav");
		default:					return NULL;
	}
}

// Helper structure to pass some values to and from find_cached_filename_that_starts_with_callback().
struct Find_Filename_Result
{
	bool was_found;
	TCHAR* result_buffer;
};

// Called every time a cached file that begins with a given prefix is found.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True if the file was not found (to keep searching) or false if it was found (to stop searching).
static TRAVERSE_DIRECTORY_CALLBACK(find_cached_filename_that_starts_with_callback)
{
	Find_Filename_Result* result = (Find_Filename_Result*) callback_user_data;
	result->was_found = !string_ends_with(callback_find_data->cFileName, TEXT(".idx"))
					&& SUCCEEDED(StringCchCopy(result->result_buffer, MAX_PATH_CHARS, callback_find_data->cFileName));
	return !result->was_found;
}

// Finds the first file that begins with a given prefix in a given directory. This search does not include any subdirectories.
//
// @Parameters:
// 1. directory_path - The path to the directory to search.
// 2. filename_prefix - The filename prefix to search for.
// 3. result_filename - The buffer which receives the found filename. This buffer must be able to hold MAX_PATH_CHARS characters.
// 
// @Returns: True if the file was found. Otherwise, false.
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

// Called every time an index file is found in the Java Plugin's cache. Used to export every cache entry.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index, Location_Type location_type);
static TRAVERSE_DIRECTORY_CALLBACK(find_java_index_files_callback)
{
	Exporter* exporter = (Exporter*) callback_user_data;
	Arena* arena = &(exporter->temporary_arena);

	// Find out what kind of cache location we're in by looking at the directory's name:
	// - "[...]\cache\javapi\v1.0\file\file.ext"
	// - "[...]\cache\javapi\v1.0\jar\archive.zip"
	// Otherwise, we assume that it's version 6, whose directory structure is "[...]\cache\6.0\<Number>\<Random Characters>".
	TCHAR* directory_name = PathFindFileName(callback_directory_path);
	
	// For the ".jpi_cache" directory (version 1), where the directory structure follows ".jpi_cache\file\1.0\file.ext" instead.
	TCHAR previous_directory_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(previous_directory_path, callback_directory_path, TEXT(".."));
	TCHAR* previous_directory_name = PathFindFileName(previous_directory_path);

	Location_Type location_type = LOCATION_ALL;
	if(strings_are_equal(directory_name, TEXT("file"), true) || strings_are_equal(previous_directory_name, TEXT("file"), true))
	{
		location_type = LOCATION_FILES;
	}
	else if(strings_are_equal(directory_name, TEXT("jar"), true) || strings_are_equal(previous_directory_name, TEXT("jar"), true))
	{
		location_type = LOCATION_ARCHIVES;
	}

	TCHAR* index_filename = callback_find_data->cFileName;
	PathCombine(exporter->index_path, callback_directory_path, index_filename);
	Index index = {};
	read_index_file(arena, exporter->index_path, &index, location_type);

	// @Docs: According to Java's URL class description: "The URL class does not itself encode or decode
	// any URL components according to the escaping mechanism defined in RFC2396." - java.net.URL - Java
	// API Specification. We'll decode it anyways though it's technically possible that the final URL's
	// representation isn't the intended one.
	TCHAR* url = decode_url(arena, index.url);
	Url_Parts url_parts = {};
	TCHAR* filename = NULL;
	if(partition_url(arena, url, &url_parts))
	{
		filename = PathFindFileName(url_parts.path);
	}
	
	// @Format: The time information is stored in milliseconds while time_t is measured in seconds.
	TCHAR last_modified_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	format_time64_t_date_time(index.last_modified_time / 1000, last_modified_time);

	TCHAR expiry_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
	format_time64_t_date_time(index.expiry_time / 1000, expiry_time);

	TCHAR* content_length = NULL;
	TCHAR content_length_buffer[MAX_INT32_CHARS] = TEXT("");
	if(index.content_length_string != NULL)
	{
		content_length = index.content_length_string;
	}
	else
	{
		convert_s32_to_string(index.content_length, content_length_buffer);
		content_length = content_length_buffer;
	}

	// How we find the cached filename depends on the cache version.
	//
	// In version 1, where there's separate directories for the type of file, the cached file has the same name as the index but
	// with its original file extension (e.g. ".class") instead of ".idx". The one exception are JAR files, which always use the
	// ".zip" extension.
	//
	// In version 6, where every type of file is allowed, the cached file has the same name as the index but without the ".idx"
	// file extension.
	//
	// Note that the version 1 cache directory may still exist in version 6. For example, if a user updated their Java version
	// and their cache version was upgraded from one format to the other (e.g. Java 5 to Java 6).
	TCHAR cached_filename[MAX_PATH_CHARS] = TEXT("");
	StringCchCopy(cached_filename, MAX_PATH_CHARS, index_filename);
	{
		// Remove the .idx file extension:
		// - Version 1: "file.ext-ABCDEFGH-12345678.idx" -> "file.ext-ABCDEFGH-12345678" (not the actual filename though).
		// - Version 6: "ABCDEFGH-12345678.idx" -> "ABCDEFGH-12345678".
		TCHAR* idx_file_extension = skip_to_file_extension(cached_filename, true);
		*idx_file_extension = TEXT('\0');

		// The above works for version 6, but for version 1 (the file or archive cache) we still need to determine
		// the actual filename by appending the file extension. Otherwise, we won't be able to copy the file.
		if(location_type != LOCATION_ALL)
		{
			if(filename == NULL)
			{
				// The filename shown in the first column may be NULL if the URL data wasn't stored in the index.
				// In version 1, since the cached filename is something like "file.ext-ABCDEFGH-12345678.ext",
				// we can truncate this string to find a good representation of the resource's name.
				// E.g. "file.ext-ABCDEFGH-12345678.ext" -> "file.ext".
				//
				// This applies to the version 1 cache directories that still exist in version 6.
				filename = push_string_to_arena(arena, cached_filename);
				TCHAR* last_dash = _tcsrchr(filename, TEXT('-'));
				if(last_dash != NULL)
				{
					*last_dash = TEXT('\0');
					last_dash = _tcsrchr(filename, TEXT('-'));
					if(last_dash != NULL) *last_dash = TEXT('\0');
				}
			}

			// We'll try to use the file's type to determine the file extension (note that ".jar" and ."jarjar"
			// are cached using ".zip").
			// If that fails, attempt to find the file extension using the filename (which is determined using
			// the URL or the cached filename).
			TCHAR* actual_file_extension = get_cached_file_extension_from_java_file_type(index.file_type);
			if(actual_file_extension == NULL)
			{
				actual_file_extension = skip_to_file_extension(filename, true);
				if(actual_file_extension != NULL && string_starts_with(actual_file_extension, TEXT(".jar"), true))
				{
					actual_file_extension = TEXT(".zip");
				}
			}

			if(actual_file_extension != NULL)
			{
				// If it worked, add the file extension to build the actual filename.
				// This applies to the version 1 cache directories that exist in their original Java version.
				StringCchCat(cached_filename, MAX_PATH_CHARS, actual_file_extension);
			}
			else
			{
				// If that fails, take the time to search on disk for the actual filename.
				// This applies to the version 1 cache directories that still exist in version 6.
				TCHAR actual_filename[MAX_PATH_CHARS] = TEXT("");
				if(find_cached_filename_that_starts_with(callback_directory_path, cached_filename, actual_filename))
				{
					StringCchCopy(cached_filename, MAX_PATH_CHARS, actual_filename);
				}
			}
		}
	}
	
	TCHAR* cache_version = NULL;
	TCHAR cache_version_buffer[MAX_INT32_CHARS] = TEXT("");
	if(location_type != LOCATION_ALL)
	{
		cache_version = TEXT("1");
	}
	else
	{
		convert_s32_to_string(index.cache_version, cache_version_buffer);
		cache_version = cache_version_buffer;
	}

	TCHAR full_file_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(full_file_path, callback_directory_path, cached_filename);

	TCHAR* short_file_path = skip_to_last_path_components(full_file_path, 3);

	Csv_Entry csv_row[] =
	{
		{/* Filename */}, {/* URL */}, {/* File Extension */},
		{last_modified_time}, {expiry_time},
		{index.response}, {index.server}, {index.cache_control}, {index.pragma}, {index.content_type}, {content_length}, {index.content_encoding},
		{index.codebase_ip}, {index.version},
		{short_file_path}, {cache_version}, {/* Missing File */}, {/* Location In Output */}, {/* Copy Error */},
		{/* Custom File Group */}, {/* Custom URL Group */}, {/* SHA-256 */}
	};
	_STATIC_ASSERT(_countof(csv_row) == CSV_NUM_COLUMNS);
	
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
					log_print(LOG_ERROR, "Convert Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. The second byte (0x%08X) does not match the pattern.", modified_utf_8_string, b);
					return NULL;
				}
			}
			else
			{
				log_print(LOG_ERROR, "Convert Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. Missing the second byte in the group.", modified_utf_8_string);
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
					log_print(LOG_ERROR, "Convert Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. The second (0x%08X) or third byte (0x%08X) does not match the pattern.", modified_utf_8_string, b, c);
					return NULL;
				}
			}
			else
			{
				log_print(LOG_ERROR, "Convert Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. Missing the second or third byte in the group.", modified_utf_8_string);
				return NULL;
			}
		}
		else
		{
			log_print(LOG_ERROR, "Convert Modified Utf-8 String To Tchar: Error while parsing the string '%hs'. The first byte (0x%08X) does not match any pattern.", modified_utf_8_string, a);
			return NULL;
		}

		++utf_16_index;
	}

	utf_16_string[utf_16_index] = L'\0';

	#ifdef BUILD_9X
		return convert_utf_16_string_to_tchar(arena, utf_16_string);
	#else
		return utf_16_string;
	#endif
}

// Reads any of the supported index file formats and fills an Index structure with any relevant information.
//
// @Parameters:
// 1. arena - The Arena structure where any read string values are stored.
// 2. index_path - The path to the index file to read.
// 3. index - The Index structure that receives the index file's contents. This structure must be cleared to zero before calling this
// function.
// 4. location_type - The type of location where the index file is stored on disk. For version 1, this must be LOCATION_ARCHIVES or
// LOCATION_FILES. For version 6, this may be any location type (LOCATION_ARCHIVES, LOCATION_FILES, or LOCATION_ALL).
// 
// @Returns: Nothing.
static void read_index_file(Arena* arena, const TCHAR* index_path, Index* index, Location_Type location_type)
{
	u64 index_file_size = 0;
	void* index_file = read_entire_file(arena, index_path, &index_file_size);
	
	if(index_file == NULL)
	{
		log_print(LOG_ERROR, "Read Index File: Failed to open the index file '%s'. No files will be exported using this index.", index_path);
		return;
	}

	/*
		@ByteOrder: Big Endian.

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
	*/

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

	// Helper macro function used to read multiple HTTP header values as modified UTF-8 strings.
	// This emulates the behavior of the readHeaders() type of functions from the Java code that
	// handles the cache. The variable passed to this macro specifies the key name used to retrieve
	// the value of the 'codebase_ip' member from the headers map. This parameter may be NULL if
	// the Codebase IP value is found elsewhere.
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
			/* Disable the constant expression and logical operation on string literal warnings. */\
			__pragma(warning(push))\
			__pragma(warning(disable : 4127 4130))\
			if(codebase_ip_key != NULL && strings_are_equal(key, codebase_ip_key, true))\
			__pragma(warning(pop))\
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
			else if(strings_are_equal(key, TEXT("content-length"), true))\
			{\
				index->content_length_string = value;\
			}\
			else if(strings_are_equal(key, TEXT("content-encoding"), true))\
			{\
				index->content_encoding = value;\
			}\
		}\
	} while(false, false)

	// Helper macro function used to read the entrie second section from index files versions 6.03,
	// 6.04, and 6.05. This emulates the behavior of the readSection2() function from the Java code
	// that handles the cache. This macro will skip to the end of the header and retrieve any remaining
	// strings and HTTP headers.
	#define READ_SECTION_2()\
	do\
	{\
		if(total_bytes_read < VERSION_6_HEADER_SIZE)\
		{\
			u32 header_padding_size = VERSION_6_HEADER_SIZE - total_bytes_read;\
			SKIP_BYTES(header_padding_size);\
		}\
		\
		if(index->section_2_length > 0)\
		{\
			READ_STRING(index->version);\
			READ_STRING(index->url);\
			READ_STRING(index->namespace_id);\
			READ_STRING(index->codebase_ip);\
			\
			READ_HEADERS(NULL);\
		}\
		\
		u32 expected_total_bytes_read = VERSION_6_HEADER_SIZE + index->section_2_length;\
		if(total_bytes_read < expected_total_bytes_read)\
		{\
			log_print(LOG_WARNING, "Read Index File: Expected to process a total of %I32u bytes after reading the header and section 2 but found only %I32u bytes in the index file '%s'.", expected_total_bytes_read, total_bytes_read, index_path);\
		}\
	} while(false, false)

	// Read the first bytes in the header.

	s8 first_byte = 0;
	READ_INTEGER(first_byte);

	// @Java: In package sun.plugin.cache.* (JDK 1.4).
	// See FileCache.verifyFile() -> readHeaderFields() and CachedFileLoader.createCacheFiles() -> writeHeaders().
	// See JarCache.verifyFile() and CachedJarLoader.authenticateFromCache() and authenticate().
	if(first_byte == VERSION_1)
	{
		// In version 1, the first byte also represents the status. It may also be incomplete (0), unusable (1), or in-use (2).
		index->status = first_byte;

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
			// @Assert: We should never get here in version 1.
			_ASSERT(false);
		}
	}
	// @Java: In package com.sun.deploy.cache.* (JDK 6).
	else
	{
		index->busy = first_byte;
		READ_INTEGER(index->incomplete);
		READ_INTEGER(index->cache_version);

		// Read the remaining bytes in the header, whose layout depends on the previously read cache version.
		switch(index->cache_version)
		{
			// @Java: See CacheEntry.readIndexFile() -> readSection1Remaining() -> readSection2() -> readHeaders().
			case(VERSION_605):
			{
				READ_INTEGER(index->is_shortcut_image);
				READ_INTEGER(index->content_length);
				READ_INTEGER(index->last_modified_time);
				READ_INTEGER(index->expiry_time);

				READ_INTEGER(index->validation_timestamp);
				READ_INTEGER(index->known_to_be_signed);

				READ_INTEGER(index->section_2_length);
				READ_INTEGER(index->section_3_length);
				READ_INTEGER(index->section_4_length);

				READ_INTEGER(index->blacklist_validation_time);
				READ_INTEGER(index->cert_expiration_date);
				READ_INTEGER(index->class_verification_status);

				READ_INTEGER(index->reduced_manifest_length);
				READ_INTEGER(index->section_4_pre_15_Length);
				
				READ_INTEGER(index->has_only_signed_entries);
				READ_INTEGER(index->has_single_code_source);
				
				READ_INTEGER(index->section_4_certs_length);
				READ_INTEGER(index->section_4_signers_length);

				READ_INTEGER(index->has_missing_signed_entries);
				READ_INTEGER(index->trusted_libraries_validation_time);

				READ_INTEGER(index->reduced_manifest_2_length);
				READ_INTEGER(index->is_proxied_host);

				READ_SECTION_2();

			} break;

			// @Java: See CacheEntry.readIndexFileOld() -> readSection1Remaining604() -> readSection2() -> readHeaders().
			case(VERSION_604):
			case(VERSION_603):
			{
				READ_INTEGER(index->force_update);
				READ_INTEGER(index->no_href);

				READ_INTEGER(index->is_shortcut_image);
				READ_INTEGER(index->content_length);
				READ_INTEGER(index->last_modified_time);
				READ_INTEGER(index->expiry_time);

				READ_INTEGER(index->validation_timestamp);
				READ_INTEGER(index->known_to_be_signed);

				READ_INTEGER(index->section_2_length);
				READ_INTEGER(index->section_3_length);
				READ_INTEGER(index->section_4_length);

				READ_INTEGER(index->blacklist_validation_time);
				READ_INTEGER(index->cert_expiration_date);
				READ_INTEGER(index->class_verification_status);

				READ_INTEGER(index->reduced_manifest_length);
				READ_INTEGER(index->section_4_pre_15_Length);

				READ_INTEGER(index->has_only_signed_entries);
				READ_INTEGER(index->has_single_code_source);

				READ_INTEGER(index->section_4_certs_length);
				READ_INTEGER(index->section_4_signers_length);

				READ_INTEGER(index->has_missing_signed_entries);
				READ_INTEGER(index->trusted_libraries_validation_time);

				READ_INTEGER(index->reduced_manifest_2_length);

				READ_SECTION_2();

			} break;

			// @Java: See CacheEntry.readIndexFileOld() -> readIndexFile602() -> readHeaders602().
			case(VERSION_602):
			{
				READ_INTEGER(index->force_update);
				READ_INTEGER(index->no_href);

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
}

/*
	@Java: "jre\lib\jaws.jar" in JDK 1.3.1 update 28.
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
*/
