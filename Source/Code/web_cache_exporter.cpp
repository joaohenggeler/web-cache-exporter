#include "web_cache_exporter.h"

#include "internet_explorer_exporter.h"
#include "mozilla_exporter.h"
#include "flash_exporter.h"
#include "shockwave_exporter.h"
#include "java_exporter.h"
#include "unity_exporter.h"

#include "explore_files.h"

/*
	This file defines the exporter's startup operations (parsing command line options, allocating memory, etc) and any common
	functions that are used when processing each cache entry (resolving output paths, exporting each cached file, etc). It also
	defines this application's entry point.
	
	A few general notes:

	- This application is a command line utility that allows you to convert a web browser or plugin's cache from a hard to read
	format to a more easily viewable one. In this context, "exporting" means copying each cached file by recreating the original
	website's directory structure and creating a CSV file which contains information about each one. It was written to help recover
	lost web media like games, animations, virtual worlds, etc. The idea is that someone who has access to an old computer where
	they used to play web games can easily check their web cache for lost game files.

	- This application was written in C-style C++03 and built using Visual Studio 2005 Professional (version 8.0, _MSC_VER = 1400)
	to target both Windows 98 and later 64-bit Windows versions. Because of this, we'll often use the TCHAR and TEXT() macros to
	maintain compatibility with Windows 98 and ME. This was the first real C/C++ program I wrote outside of university assignments
	so it may not do certain things in the most optimal way. For brevity's sake, the shorthand macro T() is used instead of TEXT().

	- The exporters are located in a .cpp file called "<Name>_exporter.cpp" for both web browsers and web plugins. Each one is free
	to implement how they read their respective cache formats in the way that best suits the data, exposing one function called
	export_default_or_specific_<Name>_cache that takes the Exporter as a parameter.

	- The "common.cpp" file defines functions for memory management, file I/O, date time formatting, string, path, and URL
	manipulation, etc. The "custom_groups.cpp" file defines the functions used to load .group files, and match each cache entry
	to a file or URL group. These are simple text files that allow you to label each cache entry based on their MIME types, file
	extensions, file signatures, and URLs. These are useful to identify files that belong to web plugins like Flash or Shockwave,
	or that came from certain websites like gaming portals.

	- When working with intermediary strings, this application will use narrow ANSI strings on Windows 98 and ME, and wide UTF-16 LE
	strings on Windows 2000 to 10. Any files that are stored on disk use UTF-8 as the character encoding. This includes source
	files, READMEs, group files, CSV files, the log file, etc.

	- All Windows paths that are used by this application are limited to MAX_PATH (260) characters. Some exporter functions used
	to extend this limit on the Windows 2000 to 10 builds by using the "\\?\" prefix. However, not all functions in the Win32 API
	support it (e.g. the Shell API), and it can also be difficult to delete any exported files or directories that exceed this
	limit on the supported Windows versions. Since the purpose of this application is to recover lost web media by asking the
	average user to check their cache, this behavior is not desirable.

	@Author: João Henggeler
	
	@TODO:
	- Make raw IE go into an output subdirectory instead of its own thing. This will simplify the IE exporter and allow us to
	change CACHE_TYPE_TO_SHORT_NAME to two letter names. Then we'll use this new CACHE_TYPE_TO_SHORT_NAME when checking the
	-ignore-filter-for argument. The readme_body must also be updated.
	
	@Future:

	High Priority:
	- Investigate the Netscape Navigator (6.0 and older) cache format.
	- Investigate the Chromium (Google Chrome, Microsoft Edge, Brave, Opera, Vivaldi, etc) cache format.
	- Investigate the Opera (14 or older) cache format.
	
	Medium Priority:
	- Investigate the Java Web Start cache directory.
	- Investigate the custom Java cache locations specified in the Java deployment properties.
	- Parse the JET Blue / ESE database format ourselves instead of relying on the dynamically loaded functions from ESENT.dll.
	Future Windows API changes could change/break the Internet Explorer cache exporter. Moreover, this should make it easier to
	lower the complexity of the code while also removing a dependency. See: https://github.com/microsoft/Extensible-Storage-Engine
	- Make a simple local Python HTTP server to retrieve sample cache formats.
	- Export NPAPI plugin files from Mozilla-based browsers.
	- Generalize the concept of raw exports that only copy files that aren't listed in the metadata.

	Low Priority:
	- Investigate the Flash cache directory for the plugin that was bundled with Chromium before Flash's EOL.
	- Investigate the Safari (Windows) cache format.
	- Investigate the Internet Explorer 3 cache format.
	- Investigate the Internet Explorer Mac (WAF) cache format.
	- Investigate the NCSA Mosaic cache format.
	- Investigate the Black Sun / Blaxxun / BS Contact plugin cache format.
	- Investigate the Pulse Player plugin cache format.
	- Investigate the Atomic3D plugin cache format.
*/

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> EXPORTER SETUP
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

static const TCHAR* LOG_FILE_NAME = T("WCE.log");
static const TCHAR* DEFAULT_EXPORT_DIRECTORY_NAME = T("ExportedCache");
static const char* COMMAND_LINE_HELP_MESSAGE = 	"Usage: WCE.exe [Optional Arguments] <Export Argument>\n"
												"\n"
												"Below are some commonly used arguments. To see the full list of arguments, check the readme.txt file.\n"
												"\n"
												"########## [1] EXPORT ARGUMENTS: <Export Option> [Optional Cache Path] [Optional Output Path]\n"
												"\n"
												"If you specify an empty path, then a default location is used.\n"
												"\n"
												"-export-internet-explorer (-eie)    exports the WinINet cache, including Internet Explorer 4 to 11.\n"
												"\n"
												"-export-mozilla (-emz)    exports the Mozilla cache, including Mozilla Firefox and Netscape Navigator 6.1 to 9.\n"
												"\n"
												"-export-flash (-efl)    exports the Flash Player cache.\n"
												"\n"
												"-export-shockwave (-esw)    exports the Shockwave Player cache.\n"
												"\n"
												"-export-java (-ejv)    exports the Java Plugin cache.\n"
												"\n"
												"-export-unity (-eun)    exports the Unity Web Player cache.\n"
												"\n"
												"########## [1] EXAMPLES:\n"
												"\n"
												"WCE.exe -export-internet-explorer\n"
												"WCE.exe -eie \"C:\\PathToTheCache\"\n"
												"WCE.exe -eie \"C:\\PathToTheCache\" \"My Cache\"\n"
												"WCE.exe -eie \"\" \"My Cache\"    (choose the output path but use the default cache path)\n"
												"WCE.exe -eie \"C:\\PathToTheCache\" \"\"    (choose the cache path but use the default output path)\n"
												"\n"
												"\n"
												"########## [2] OPTIONAL ARGUMENTS: Put them before the export option.\n"
												"\n"
												"-csvs-only (-co)    only creates CSV files (don't export cached files).\n"
												"\n"
												"-files-only (-fo)    only exports cached files (don't create CSV files).\n"
												"\n"
												"-overwrite (-o)    deletes the previous output folder before running.\n"
												"\n"
												"########## [2] EXAMPLES:\n"
												"\n"
												"WCE.exe -csvs-only -export-flash\n"
												"WCE.exe -overwrite -files-only -export-shockwave"
												;

// Maps a cache exporter's short name to its cache type enum.
//
// @Parameters:
// 1. name - The short name.
//
// @Returns: The cache type enum if the name was mapped successfully. Otherwise, this function returns CACHE_UNKNOWN.
static Cache_Type get_cache_type_from_short_name(const TCHAR* name)
{
	Cache_Type result = CACHE_UNKNOWN;

	for(int i = 0; i < NUM_CACHE_TYPES; ++i)
	{
		if(strings_are_equal(name, CACHE_TYPE_TO_SHORT_NAME[i], true))
		{
			result = (Cache_Type) i;
			break;
		}
	}

	return result;
}

// Parses the application's command line arguments and sets the resulting Exporter structure's members accordingly.
//
// @Parameters:
// 1. num_arguments - The number of command line arguments.
// 2. arguments - The command line arguments. This must be the unmodified string array that is passed to main().
// 3. exporter - The resulting Exporter structure. This must be cleared to zero before calling this function.
//
// @Returns: True if every parsed argument was correct. Otherwise, it returns false and the application should terminate.
static bool parse_exporter_arguments(int num_arguments, TCHAR** arguments, Exporter* exporter)
{
	bool success = true;
	bool seen_export_option = false;

	Arena* temporary_arena = &(exporter->temporary_arena);
	
	// Set any options that shouldn't be zero, false, or empty strings by default.
	exporter->copy_files = true;
	exporter->create_csvs = true;
	exporter->decompress_files = true;
	exporter->clear_temporary_windows_directory = true;

	#define IS_OPTION(long_option, short_option) (strings_are_equal(option, T(long_option)) || strings_are_equal(option, T(short_option)))

	// Skip the first argument which contains the executable's name.
	for(int i = 1; i < num_arguments; ++i)
	{
		TCHAR* option = arguments[i];

		if(IS_OPTION("-version", "-v") || IS_OPTION("-no-log", "-nl") || IS_OPTION("-quiet", "-q"))
		{
			// Already handled right when the application started.
		}
		else if(IS_OPTION("-csvs-only", "-co"))
		{
			exporter->copy_files = false;
		}
		else if(IS_OPTION("-files-only", "-fo"))
		{
			exporter->create_csvs = false;
		}
		else if(IS_OPTION("-overwrite", "-o"))
		{
			exporter->overwrite_previous_output = true;
		}
		else if(IS_OPTION("-show-full-paths", "-sfp"))
		{
			exporter->show_full_paths = true;
		}
		else if(IS_OPTION("-group-by-origin", "-gbo"))
		{
			exporter->group_by_request_origin = true;
		}
		else if(IS_OPTION("-no-decompress", "-nd"))
		{
			exporter->decompress_files = false;
		}
		else if(IS_OPTION("-no-clear-default-temporary", "-ncdt"))
		{
			exporter->clear_temporary_windows_directory = false;
		}
		else if(IS_OPTION("-filter-by-groups", "-fbg"))
		{
			exporter->filter_by_groups = true;
			if(i+1 < num_arguments)
			{
				const TCHAR* group_file_list = arguments[i+1];
				exporter->group_files_for_filtering = copy_and_split_string(temporary_arena, group_file_list, T("/"));
				i += 1;
			}
		}
		else if(IS_OPTION("-ignore-filter-for", "-iff"))
		{
			if(i+1 < num_arguments)
			{
				const TCHAR* name_list = arguments[i+1];
				String_Array<TCHAR>* split_names = copy_and_split_string(temporary_arena, name_list, T("/"));

				for(int j = 0; j < split_names->num_strings; ++j)
				{
					TCHAR* name = split_names->strings[j];
					Cache_Type type = get_cache_type_from_short_name(name);

					if(type == CACHE_UNKNOWN || type == CACHE_ALL || type == CACHE_EXPLORE)
					{
						if(strings_are_equal(name, T("plugins")))
						{
							log_info("Argument Parsing: Ignoring filter for any plugin cache types.");
							for(int k = 0; k < NUM_CACHE_TYPES; ++k)
							{
								if(IS_CACHE_TYPE_PLUGIN[k]) exporter->ignore_filter_for_cache_type[k] = true;
							}
						}
						else if(strings_are_equal(name, T("browsers")))
						{
							log_info("Argument Parsing: Ignoring filter for any browser cache types.");
							for(int k = 0; k < NUM_CACHE_TYPES; ++k)
							{
								if(!IS_CACHE_TYPE_PLUGIN[k]) exporter->ignore_filter_for_cache_type[k] = true;
							}
						}
						else
						{
							success = false;
							console_print("Unknown cache type '%s' in the -ignore-filter-for option.", name);
							log_error("Argument Parsing: Unknown cache type '%s' in the -ignore-filter-for option.", name);
						}
					}
					else
					{
						log_info("Argument Parsing: Ignoring filter for the cache type '%s'.", name);
						exporter->ignore_filter_for_cache_type[type] = true;
					}
				}

				i += 1;
			}
		}
		else if(IS_OPTION("-temporary-directory", "-td"))
		{
			exporter->use_custom_temporary_directory = true;
			if(i+1 < num_arguments)
			{
				StringCchCopy(exporter->exporter_temporary_path, MAX_PATH_CHARS, arguments[i+1]);
				get_full_path_name(exporter->exporter_temporary_path);
				i += 1;
			}
		}
		else if(IS_OPTION("-hint-ie", "-hie"))
		{
			exporter->use_ie_hint = true;
			if(i+1 < num_arguments)
			{
				StringCchCopy(exporter->ie_hint_path, MAX_PATH_CHARS, arguments[i+1]);
				i += 1;
			}
		}
		else if(IS_OPTION("-explore-files", "-ef"))
		{
			exporter->command_line_cache_type = CACHE_EXPLORE;

			if(i+1 < num_arguments && !string_is_empty(arguments[i+1]))
			{
				StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, arguments[i+1]);
			}
			else
			{
				success = false;
				console_print("The -explore-files option requires a non-empty path.");
				log_error("Argument Parsing: The -explore-files option was given a non-empty path.");
			}

			if(i+2 < num_arguments && !string_is_empty(arguments[i+2]))
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, arguments[i+2]);
			}
			else
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, DEFAULT_EXPORT_DIRECTORY_NAME);
			}

			exporter->is_exporting_from_default_locations = false;
			seen_export_option = true;
			break;
		}
		else if(IS_OPTION("-find-and-export-all", "-faea"))
		{
			exporter->command_line_cache_type = CACHE_ALL;

			if(i+1 < num_arguments && !string_is_empty(arguments[i+1]))
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, arguments[i+1]);
			}
			else
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, DEFAULT_EXPORT_DIRECTORY_NAME);
			}

			if(i+2 < num_arguments)
			{
				StringCchCopy(exporter->external_locations_file_path, MAX_PATH_CHARS, arguments[i+2]);
				exporter->load_external_locations = true;
			}
	
			exporter->is_exporting_from_default_locations = true;
			seen_export_option = true;
			break;
		}
		else if(string_begins_with(option, T("-e")))
		{
			if(IS_OPTION("-export-internet-explorer", "-eie"))
			{
				exporter->command_line_cache_type = CACHE_INTERNET_EXPLORER;
			}
			else if(IS_OPTION("-export-mozilla", "-emz"))
			{
				exporter->command_line_cache_type = CACHE_MOZILLA;
			}
			else if(IS_OPTION("-export-flash", "-efl"))
			{
				exporter->command_line_cache_type = CACHE_FLASH;
			}
			else if(IS_OPTION("-export-shockwave", "-esw"))
			{
				exporter->command_line_cache_type = CACHE_SHOCKWAVE;
			}
			else if(IS_OPTION("-export-java", "-ejv"))
			{
				exporter->command_line_cache_type = CACHE_JAVA;
			}
			else if(IS_OPTION("-export-unity", "-eun"))
			{
				exporter->command_line_cache_type = CACHE_UNITY;
			}
			else
			{
				console_print("Unknown export option '%s'.", option);
				log_error("Argument Parsing: Unknown export option '%s'.", option);
				exporter->command_line_cache_type = CACHE_UNKNOWN;
				success = false;
			}

			bool was_given_cache_path = false;
			if(i+1 < num_arguments && !string_is_empty(arguments[i+1]))
			{
				StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, arguments[i+1]);
				was_given_cache_path = true;
			}

			if(i+2 < num_arguments && !string_is_empty(arguments[i+2]))
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, arguments[i+2]);
			}
			else
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, DEFAULT_EXPORT_DIRECTORY_NAME);
			}

			exporter->is_exporting_from_default_locations = !was_given_cache_path;
			seen_export_option = true;
			break;
		}
		else
		{
			console_print("Unknown command line option '%s'.", option);
			log_error("Argument Parsing: Unknown command line option '%s'", option);
			success = false;
			break;
		}
	}

	#undef IS_OPTION

	if(!seen_export_option)
	{
		console_print("Missing the export option.");
		log_error("Argument Parsing: The main -export option was not found.");
		success = false;
	}

	if(!exporter->copy_files && !exporter->create_csvs)
	{
		console_print("The options -csvs-only and -files-only can't be used at the same time.");
		log_error("Argument Parsing: The options '-csvs-only' and '-files-only' were used at the same time.");
		success = false;
	}

	// These empty string checks may seem overkill but remember that an empty path is used to specify the default
	// locations for the export command line options. This means that we want to warn the user about this behavior.

	_ASSERT( 	(!exporter->load_external_locations)
			|| 	(exporter->load_external_locations && exporter->command_line_cache_type == CACHE_ALL && exporter->is_exporting_from_default_locations));

	if(exporter->load_external_locations)
	{
		if(string_is_empty(exporter->external_locations_file_path))
		{
			console_print("The second argument in the -find-and-export-all option requires a non-empty path.");
			log_error("Argument Parsing: The -find-and-export-all option was used with the external locations argument but the supplied path was empty.");
			success = false;
		}
		else if(!does_file_exist(exporter->external_locations_file_path))
		{
			console_print("The external locations file in the -find-and-export-all option doesn't exist.");
			log_error("Argument Parsing: The -find-and-export-all option supplied an external locations file path that doesn't exist: '%s'.", exporter->external_locations_file_path);
			success = false;
		}
	}

	if(exporter->use_custom_temporary_directory && string_is_empty(exporter->exporter_temporary_path))
	{
		console_print("The -temporary-directory option requires a non-empty path as its argument.");
		log_error("Argument Parsing: The -temporary-directory option was used but the supplied path was empty.");
		success = false;
	}

	if(exporter->use_ie_hint)
	{
		if(exporter->command_line_cache_type != CACHE_INTERNET_EXPLORER)
		{
			console_print("The -hint-ie option can only be used when exporting the Internet Explorer cache.");
			log_error("Argument Parsing: The -hint-ie option was used with the unsupported cache type %d.", exporter->command_line_cache_type);
			success = false;
		}
		else if(string_is_empty(exporter->ie_hint_path))
		{
			console_print("The -hint-ie option requires a non-empty path as its argument.");
			log_error("Argument Parsing: The -hint-ie option was used but the supplied path was empty.");
			success = false;
		}
	}

	return success;
}

// Retrieves the size of the temporary memory in bytes, based on the current Windows version. This size is twice as large for the
// Windows 2000 through 10 builds in order to store UTF-16 strings.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current Windows version. The 'os_version' member must be set before calling
// this function.
//
// @Returns: The number of bytes to allocate for the temporary memory.
static size_t get_temporary_exporter_memory_size_for_os_version(Exporter* exporter)
{
	OSVERSIONINFO os_version = exporter->os_version;
	size_t size_for_os_version = 0;

	// Windows 98 (4.10)
	if(os_version.dwMajorVersion <= 4 && os_version.dwMinorVersion <= 10)
	{
		size_for_os_version = kilobytes_to_bytes(512 + 256); // x1 for char
		
	}
	// Windows 2000 (5.0) and ME (4.90)
	else if( 	(os_version.dwMajorVersion <= 5 && os_version.dwMinorVersion <= 0)
			|| 	(os_version.dwMajorVersion <= 4 && os_version.dwMinorVersion <= 90))
	{
		size_for_os_version = megabytes_to_bytes(1); // x1 for char (ME) and x2 for wchar_t (2000)
	}
	// Windows XP (5.1)
	else if(os_version.dwMajorVersion <= 5 && os_version.dwMinorVersion <= 1)
	{
		size_for_os_version = megabytes_to_bytes(2); // x2 for wchar_t
	}
	// Windows Vista (6.0) and 7 (6.1).
	else if(os_version.dwMajorVersion >= 6 && os_version.dwMinorVersion <= 1)
	{
		size_for_os_version = megabytes_to_bytes(5); // x2 for wchar_t
	}
	// Windows 8.1 (6.3) and 10 (10.0).
	else if(os_version.dwMajorVersion >= 6)
	{
		size_for_os_version = megabytes_to_bytes(8); // x2 for wchar_t
	}
	else
	{
		size_for_os_version = megabytes_to_bytes(4); // x1 or x2 for TCHAR
		log_warning("Get Temporary Exporter Memory Size For Os Version: Using %Iu bytes for the unhandled Windows version %lu.%lu.",
								size_for_os_version, os_version.dwMajorVersion, os_version.dwMinorVersion);
	}

	return size_for_os_version * sizeof(TCHAR);;
}

// Performs any clean up operations before this application terminates. This includes deleting the exporter's temporary directory,
// freeing any loaded library modules, deallocating the permanent and temporary memory, and closing the log file.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the necessary information to perform these clean up operations.
//
// @Returns: Nothing.
static void clean_up_exporter(Exporter* exporter)
{
	if(exporter->was_temporary_exporter_directory_created)
	{
		if(!delete_directory_and_contents(exporter->exporter_temporary_path))
		{
			console_print("Warning: Failed to delete the temporary exporter directory located in '%s'.\nYou may want to delete this directory yourself.", exporter->exporter_temporary_path);
			log_error("Clean Up: Failed to delete the temporary exporter directory in '%s'.", exporter->exporter_temporary_path);
		}
	}

	#ifndef WCE_9X
		if( (exporter->command_line_cache_type == CACHE_INTERNET_EXPLORER) || (exporter->command_line_cache_type == CACHE_ALL) )
		{
			free_esent_functions();
			free_ntdll_functions();
			free_kernel32_functions();
		}
	#endif

	destroy_arena( &(exporter->permanent_arena) );
	destroy_arena( &(exporter->secondary_temporary_arena) );
	destroy_arena( &(exporter->temporary_arena) );
	
	close_log_file();
}

/*
	The Web Cache Exporter's entry point. Order of operations:
	
	>>>> 1. Check if any command line options were passed. If not, terminate.
	>>>> 2. Create the log file.
	>>>> 3. Find the current Windows version, Internet Explorer version, and ANSI code page.
	>>>> 4. Create the temporary memory arena based on the current Windows version. On error, terminate.
	>>>> 5. Parse the command line options. If an option is incorrect, terminate.
	>>>> 6. Find the current executable's directory path.
	>>>> 7. Find how much memory is roughly required to store the information in the group and external
	locations files.
	>>>> 8. Create the permanent memory arena based on this previous information. On error, terminate.
	>>>> 9. Dynamically load any necessary functions.
	>>>> 10. Find the paths to relevant locations like the Application Data and Temporary Files directories.
	>>>> 11. Delete any previous temporary exporter directories in this last location, then create a new
	one for the current execution.
	>>>> 12. Delete the previous output directory if requested by the command line options.
	>>>> 13. Start exporting the cache based on the command line options.
	>>>> 14. Perform any clean up operations after finishing exporting. These are also done when any of the
	previous errors occur.
*/

static void clear_temporary_windows_directory(Exporter* exporter);
static size_t get_total_external_locations_size(Exporter* exporter, int* result_num_profiles);
static void load_external_locations(Exporter* exporter, int num_profiles);
static void export_all_default_or_specific_cache_locations(Exporter* exporter);

// Used to control if the logging and console print functions should be called when using the log_print()
// and console_print() macros.
bool GLOBAL_LOG_ENABLED = true;
bool GLOBAL_CONSOLE_ENABLED = true;

int _tmain(int num_arguments, TCHAR** arguments)
{
	Exporter exporter = {};

	if(num_arguments <= 1)
	{
		console_print("%hs", COMMAND_LINE_HELP_MESSAGE);
		return 1;
	}

	#define IS_OPTION(long_option, short_option) (strings_are_equal(option, T(long_option)) || strings_are_equal(option, T(short_option)))

	// Skip the first argument which contains the executable's name.
	for(int i = 1; i < num_arguments; ++i)
	{
		TCHAR* option = arguments[i];

		if(IS_OPTION("-version", "-v"))
		{
			GLOBAL_CONSOLE_ENABLED = true;
			console_print("Web Cache Exporter version %hs compiled with Visual Studio %d in %hs mode for %hs.", EXPORTER_BUILD_VERSION, _MSC_VER, EXPORTER_BUILD_MODE, EXPORTER_BUILD_TARGET);
			return 0;
		}
		else if(IS_OPTION("-no-log", "-nl"))
		{
			GLOBAL_LOG_ENABLED = false;
		}
		else if(IS_OPTION("-quiet", "-q"))
		{
			GLOBAL_CONSOLE_ENABLED = false;
		}
		// Don't check for these arguments past the main export options. This is a little silly since we're repeating
		// these options here and in parse_exporter_arguments(), but it works for now.
		else if(IS_OPTION("-explore-files", "-ef")
				|| IS_OPTION("-find-and-export-all", "-faea")
				|| string_begins_with(option, T("-export"))
				|| (string_begins_with(option, T("-e")) && string_length(option) <= 4))
		{
			break;
		}
	}

	#undef IS_OPTION

	console_print("Web Cache Exporter v%hs", EXPORTER_BUILD_VERSION);

	create_log_file(LOG_FILE_NAME);

	#if defined(WCE_DEBUG) && defined(WCE_EMPTY_EXPORT)
		console_print("Debug: Exporting empty files!");
		log_debug("Exporting empty files.");
	#endif

	#if defined(WCE_DEBUG) && defined(WCE_TINY_FILE_BUFFERS)
		console_print("Debug: Using tiny file buffers!");
		log_debug("Using tiny file buffers.");
	#endif

	log_info("Startup: Running the Web Cache Exporter version %hs compiled with Visual Studio %d in %hs mode for %hs.",
			 EXPORTER_BUILD_VERSION, _MSC_VER, EXPORTER_BUILD_MODE, EXPORTER_BUILD_TARGET);

	exporter.os_version.dwOSVersionInfoSize = sizeof(exporter.os_version);

	// Disable the deprecation warnings for GetVersionExW() when building with Visual Studio 2015 or later.
	// This project is always built using Visual Studio 2005, but we'll do this anyways in case someone wants
	// to compile this application with a more modern version of Visual Studio.
	#pragma warning(push)
	#pragma warning(disable : 4996)
	if(GetVersionEx(&exporter.os_version))
	#pragma warning(pop)
	{
		log_info("Startup: Using Windows version %lu.%lu '%s' build %lu in platform %lu.",
								exporter.os_version.dwMajorVersion, exporter.os_version.dwMinorVersion,
								exporter.os_version.szCSDVersion, exporter.os_version.dwBuildNumber,
								exporter.os_version.dwPlatformId);
	}
	else
	{
		log_error("Startup: Failed to get the current Windows version with the error code %lu.", GetLastError());
		exporter.os_version.dwMajorVersion = MAX_UINT_32;
		exporter.os_version.dwMinorVersion = MAX_UINT_32;
	}

	{
		const size_t NUM_IE_VERSION_CHARS = 32;
		TCHAR ie_version[NUM_IE_VERSION_CHARS] = T("");
		if(find_internet_explorer_version(ie_version, sizeof(ie_version)))
		{
			log_info("Startup: Using Internet Explorer version %s.", ie_version);
		}
		
		CPINFOEX code_page_info = {};
		if(GetCPInfoEx(CP_ACP, 0, &code_page_info) == TRUE)
		{
			log_info("Startup: The current Windows ANSI code page is '%s' (%u).", code_page_info.CodePageName, code_page_info.CodePage);
		}
		else
		{
			log_info("Startup: The current Windows ANSI code page identifier is %u.", GetACP());
		}
	}

	Arena* temporary_arena = &(exporter.temporary_arena);
	{
		size_t temporary_memory_size = get_temporary_exporter_memory_size_for_os_version(&exporter);
		log_info("Startup: Allocating %Iu bytes for the temporary memory arena.", temporary_memory_size);

		if(!create_arena(temporary_arena, temporary_memory_size))
		{
			console_print("Could not allocate enough temporary memory to run the program.");
			log_error("Startup: Could not allocate %Iu bytes to run the program.", temporary_memory_size);
			clean_up_exporter(&exporter);
			return 1;
		}

		#ifdef WCE_9X
			Arena* secondary_temporary_arena = &(exporter.secondary_temporary_arena);

			// Create a smaller, secondary memory arena for Windows 98 and ME. This will be used when loading group files.
			temporary_memory_size /= 10;
			log_info("Startup: Allocating %Iu bytes for the secondary temporary memory arena.", temporary_memory_size);

			if(!create_arena(secondary_temporary_arena, temporary_memory_size))
			{
				console_print("Could not allocate enough temporary memory to run the program.");
				log_error("Startup: Could not allocate %Iu bytes to run the program.", temporary_memory_size);
				clean_up_exporter(&exporter);
				return 1;
			}
		#endif
	}

	log_info("Startup: Parsing command line arguments.");
	if(!parse_exporter_arguments(num_arguments, arguments, &exporter))
	{
		log_error("Startup: An error occured while parsing the command line arguments. The program will terminate.");
		clean_up_exporter(&exporter);
		return 1;
	}

	Arena* permanent_arena = &(exporter.permanent_arena);
	{
		// Keep any variable-length values from the command line arguments around while we load any group and external locations files.
		lock_arena(temporary_arena);

		if(GetModuleFileName(NULL, exporter.executable_path, MAX_PATH_CHARS) != 0)
		{
			// Remove the executable's name from the path.
			PathAppend(exporter.executable_path, T(".."));
		}
		else
		{
			log_error("Startup: Failed to get the executable directory path with error code %lu.", GetLastError());
		}

		PathCombine(exporter.group_files_path, exporter.executable_path, T("Groups"));

		int num_groups = 0;
		int num_profiles = 0;
		
		size_t permanent_memory_size = get_total_group_files_size(&exporter, &num_groups);
		if(exporter.load_external_locations)
		{
			permanent_memory_size += get_total_external_locations_size(&exporter, &num_profiles);
		}

		log_info("Startup: Allocating %Iu bytes for the permanent memory arena.", permanent_memory_size);

		if(!create_arena(permanent_arena, permanent_memory_size))
		{
			console_print("Could not allocate enough permanent memory to run the program.");
			log_error("Startup: Could not allocate %Iu bytes to run the program.", permanent_memory_size);
			clean_up_exporter(&exporter);
			return 1;
		}

		log_info("Startup: Loading %d groups.", num_groups);
		load_all_group_files(&exporter, num_groups);

		if(exporter.load_external_locations)
		{
			log_info("Startup: Loading %d profiles from the external locations file '%s'.", num_profiles, exporter.external_locations_file_path);
			load_external_locations(&exporter, num_profiles);			
		}

		log_info("Startup: The permanent memory arena is at %.2f%% used capacity before being locked.", get_used_arena_capacity(permanent_arena));

		// This memory lasts throughout the program's lifetime.
		lock_arena(permanent_arena);

		unlock_arena(temporary_arena);
	}

	#ifndef WCE_9X
		if( (exporter.command_line_cache_type == CACHE_INTERNET_EXPLORER) || (exporter.command_line_cache_type == CACHE_ALL) )
		{
			log_info("Startup: Dynamically loading any necessary functions.");
			load_kernel32_functions();
			load_ntdll_functions();
			load_esent_functions();
		}
	#endif

	if(GetWindowsDirectory(exporter.windows_path, MAX_PATH_CHARS) != 0)
	{
		// The drive path used to be determined using GetVolumeInformation(), but that started returning names like "Windows-SSD"
		// in later Windows versions.
		StringCchPrintf(exporter.drive_path, MAX_PATH_CHARS, T("%c:\\"), exporter.windows_path[0]);
	}
	else
	{
		log_error("Startup: Failed to get the Windows directory path with error code %lu.", GetLastError());
		StringCchCopy(exporter.windows_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
		StringCchCopy(exporter.drive_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
	}

	if(GetTempPath(MAX_PATH_CHARS, exporter.windows_temporary_path) != 0)
	{
		if(exporter.clear_temporary_windows_directory) clear_temporary_windows_directory(&exporter);
	}
	else
	{
		log_error("Startup: Failed to get the Temporary Files directory path with error code %lu.", GetLastError());
		StringCchCopy(exporter.windows_temporary_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
	}

	if(exporter.use_custom_temporary_directory)
	{
		exporter.was_temporary_exporter_directory_created = create_directories(exporter.exporter_temporary_path);
	}
	else
	{
		exporter.was_temporary_exporter_directory_created = create_temporary_directory(exporter.windows_temporary_path, exporter.exporter_temporary_path);
	}

	if(exporter.was_temporary_exporter_directory_created)
	{
		log_info("Startup: Created the temporary exporter directory in '%s'.", exporter.exporter_temporary_path);
	}
	else
	{
		log_error("Startup: Failed to create the temporary exporter directory in '%s'.", exporter.exporter_temporary_path);
	}

	if(!get_special_folder_path(CSIDL_PROFILE, exporter.user_profile_path))
	{
		log_error("Startup: Failed to get the user profile directory path with error code %lu.", GetLastError());
		StringCchCopy(exporter.user_profile_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
	}

	if(!get_special_folder_path(CSIDL_APPDATA, exporter.appdata_path))
	{
		log_error("Startup: Failed to get the roaming application data directory path with error code %lu.", GetLastError());
		StringCchCopy(exporter.appdata_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
	}

	if(get_special_folder_path(CSIDL_LOCAL_APPDATA, exporter.local_appdata_path))
	{
		// @Future: Is there a better way to find LocalLow?
		StringCchCopy(exporter.local_low_appdata_path, MAX_PATH_CHARS, exporter.local_appdata_path);
		PathAppend(exporter.local_low_appdata_path, T("..\\LocalLow"));

		if(!does_directory_exist(exporter.local_low_appdata_path))
		{
			StringCchCopy(exporter.local_low_appdata_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
		}
	}
	else
	{
		log_error("Startup: Failed to get the local application data directory path with error code %lu.", GetLastError());
		StringCchCopy(exporter.local_appdata_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
		StringCchCopy(exporter.local_low_appdata_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
	}

	if(!get_special_folder_path(CSIDL_INTERNET_CACHE, exporter.wininet_cache_path))
	{
		log_error("Startup: Failed to get the Temporary Internet Files cache directory path with the error code %lu.", GetLastError());
		StringCchCopy(exporter.wininet_cache_path, MAX_PATH_CHARS, PATH_NOT_FOUND);
	}

	if(exporter.is_exporting_from_default_locations && (exporter.command_line_cache_type != CACHE_ALL))
	{
		log_info("Startup: No cache path specified. Exporting the cache from any existing default directories.");
	}

	if(exporter.overwrite_previous_output)
	{
		TCHAR* directory_name = PathFindFileName(exporter.output_path);
		console_print("Deleting the previous output directory '%s' before starting...", directory_name);

		if(delete_directory_and_contents(exporter.output_path))
		{
			log_info("Startup: Deleted the previous output directory successfully.");
		}
		// We'll do this little check since the error codes from SHFileOperation() (used by the deletion
		// function above) are pre-Win32 and may be subject to change.
		else if(does_directory_exist(exporter.output_path))
		{	
			console_print("Warning: Could not delete the previous output directory.");
			log_error("Startup: Failed to delete the previous output directory '%s'.", directory_name);
		}
	}

	log_info("Startup: The temporary memory arena is at %.2f%% used capacity before exporting files.", get_used_arena_capacity(temporary_arena));
	
	#define YN(member_name) ( (exporter.member_name) ? (T("Yes")) : (T("No")) )

	log_newline();

	log_print(LOG_NONE, "------------------------------------------------------------");
	log_info("Exporter Options:");
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Cache Type: %s", CACHE_TYPE_TO_FULL_NAME[exporter.command_line_cache_type]);
	log_print(LOG_NONE, "- Should Copy Files: %s", YN(copy_files));
	log_print(LOG_NONE, "- Should Create CSV: %s", YN(create_csvs));
	log_print(LOG_NONE, "- Should Overwrite Previous Output: %s", YN(overwrite_previous_output));
	log_print(LOG_NONE, "- Should Show Full Paths: %s", YN(show_full_paths));
	log_print(LOG_NONE, "- Should Group By Request Origin: %s", YN(group_by_request_origin));
	log_print(LOG_NONE, "- Should Decompress Files: %s", YN(decompress_files));
	log_print(LOG_NONE, "- Should Clear Temporary Windows Directory: %s", YN(clear_temporary_windows_directory));
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Should Filter By Groups: %s", YN(filter_by_groups));
	log_print(LOG_NONE, "- Number Of Group Files Enabled For Filtering: %d", (exporter.group_files_for_filtering != NULL) ? (exporter.group_files_for_filtering->num_strings) : (-1));
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Should Use Internet Explorer's Hint: %s", YN(use_ie_hint));
	log_print(LOG_NONE, "- Internet Explorer Hint Path: '%s'", exporter.ie_hint_path);
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Should Load External Locations: %s", YN(load_external_locations));
	log_print(LOG_NONE, "- External Locations Path: '%s'", exporter.external_locations_file_path);
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Cache Path: '%s'", exporter.cache_path);
	log_print(LOG_NONE, "- Output Path: '%s'", exporter.output_path);
	log_print(LOG_NONE, "- Is Exporting From Default Locations: %s", YN(is_exporting_from_default_locations));
	
	log_newline();

	log_print(LOG_NONE, "------------------------------------------------------------");
	log_info("Current Locations:");
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Executable Path: '%s'", exporter.executable_path);
	log_print(LOG_NONE, "- Exporter Temporary Path: '%s'", exporter.exporter_temporary_path);
	log_print(LOG_NONE, "- Was Temporary Directory Created: %s", YN(was_temporary_exporter_directory_created));
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Drive Path: '%s'", exporter.drive_path);
	log_print(LOG_NONE, "- Windows Directory Path: '%s'", exporter.windows_path);
	log_print(LOG_NONE, "- Windows Temporary Path: '%s'", exporter.windows_temporary_path);
	log_print(LOG_NONE, "- User Profile Path: '%s'", exporter.user_profile_path);
	log_print(LOG_NONE, "- Roaming AppData Path: '%s'", exporter.appdata_path);
	log_print(LOG_NONE, "- Local AppData Path: '%s'", exporter.local_appdata_path);
	log_print(LOG_NONE, "- LocalLow AppData Path: '%s'", exporter.local_low_appdata_path);
	log_print(LOG_NONE, "- WinINet Cache Path: '%s'", exporter.wininet_cache_path);

	log_newline();

	#undef YN

	// The temporary arena should be cleared before any cache exporter runs. Any data that needs to stick around should be stored in the permanent arena.
	_ASSERT(permanent_arena->num_locks == 1);
	_ASSERT(temporary_arena->num_locks == 0);

	// Get rid of any variable-length values that is no longer necessary.
	clear_arena(temporary_arena);
	exporter.group_files_for_filtering = NULL;

	switch(exporter.command_line_cache_type)
	{
		case(CACHE_INTERNET_EXPLORER):
		{
			export_default_or_specific_internet_explorer_cache(&exporter);
		} break;

		case(CACHE_MOZILLA):
		{
			export_default_or_specific_mozilla_cache(&exporter);
		} break;

		case(CACHE_FLASH):
		{
			export_default_or_specific_flash_cache(&exporter);
		} break;

		case(CACHE_SHOCKWAVE):
		{
			export_default_or_specific_shockwave_cache(&exporter);
		} break;

		case(CACHE_JAVA):
		{
			export_default_or_specific_java_cache(&exporter);
		} break;

		case(CACHE_UNITY):
		{
			export_default_or_specific_unity_cache(&exporter);
		} break;

		case(CACHE_ALL):
		{
			_ASSERT(exporter.is_exporting_from_default_locations);
			_ASSERT(string_is_empty(exporter.cache_path));

			export_all_default_or_specific_cache_locations(&exporter);

		} break;

		case(CACHE_EXPLORE):
		{
			_ASSERT(!exporter.is_exporting_from_default_locations);
			_ASSERT(!string_is_empty(exporter.cache_path));

			export_explored_files(&exporter);
		} break;

		default:
		{
			log_error("Startup: Attempted to export the cache from '%s' using the unhandled cache type %d.", exporter.cache_path, exporter.command_line_cache_type);
			_ASSERT(false);
		} break;
	}

	console_print("Finished running:\n- Created %d CSV files.\n- Processed %d cached files.\n- Copied %d cached files.\n- Assigned %d filenames.", exporter.total_csv_files_created, exporter.total_processed_files, exporter.total_copied_files, exporter.total_assigned_filenames);
	log_newline();
	log_info("Finished Running: Created %d CSV files. Processed %d cache entries. Copied %d cached files. Assigned %d filenames.", exporter.total_csv_files_created, exporter.total_processed_files, exporter.total_copied_files, exporter.total_assigned_filenames);
	
	clean_up_exporter(&exporter);

	return 0;
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> EXPORTER OPERATIONS
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

// Initializes a cache exporter by performing the following:
// - Determining the absolute version of the cache path.
// - Resolving the exporter's output paths for copying cache entries and creating CSV files.
// - Creating a CSV file with a given header.
//
// This function must be called by each exporter before processing any cached files, and may be called multiple times by the same
// exporter. After finishing exporting, the terminate_cache_exporter() function must be called.
//
// @Parameters:
// 1. exporter - The Exporter structure where the resolved paths and CSV file's handle will be stored.
// 2. column_types - The array of column types used to determine the column names for the CSV file.
// 3. num_columns - The number of elements in this array.
// 
// @Returns: Nothing.
void initialize_cache_exporter(Exporter* exporter, Cache_Type cache_type, Csv_Type* column_types, int num_columns)
{
	exporter->csv_file_handle = INVALID_HANDLE_VALUE;
	exporter->exported_at_least_one_file = false;
	exporter->index_path[0] = T('\0');
	exporter->browser_name = NULL;
	exporter->browser_profile = NULL;
	exporter->num_assigned_filenames = 0;

	_ASSERT(cache_type != CACHE_UNKNOWN && cache_type != CACHE_ALL);

	exporter->current_cache_type = cache_type;
	exporter->csv_column_types = column_types;
	exporter->num_csv_columns = num_columns;

	get_full_path_name(exporter->cache_path);
	get_full_path_name(exporter->output_path);
	
	set_exporter_output_copy_subdirectory(exporter, NULL);
	
	// Don't use PathCombine() since we're just adding a file extension to the previous path.
	StringCchCopy(exporter->output_csv_path, MAX_PATH_CHARS, exporter->output_copy_path);
	StringCchCat(exporter->output_csv_path, MAX_PATH_CHARS, T(".csv"));

	Arena* temporary_arena = &(exporter->temporary_arena);

	if(exporter->create_csvs)
	{
		_ASSERT(exporter->csv_file_handle == INVALID_HANDLE_VALUE);

		const int MAX_RETRY_ATTEMPTS = 10;
		for(int i = 0; i < MAX_RETRY_ATTEMPTS; ++i)
		{
			if(create_csv_file(exporter->output_csv_path, &(exporter->csv_file_handle)))
			{
				++(exporter->total_csv_files_created);
				csv_print_header(temporary_arena, exporter->csv_file_handle, column_types, num_columns);
				break;
			}
			else
			{
				const u32 SLEEP_TIME_IN_SECONDS = 3;
				const u32 SLEEP_TIME_IN_MILLISECONDS = SLEEP_TIME_IN_SECONDS * 1000;
				log_warning("Initialize Cache Exporter: Could not create the CSV file. Waiting %I32u seconds and retrying this operation (attempt %d of %d).", SLEEP_TIME_IN_SECONDS, i+1, MAX_RETRY_ATTEMPTS);
				Sleep(SLEEP_TIME_IN_MILLISECONDS);
			}
		}

		if(exporter->csv_file_handle == INVALID_HANDLE_VALUE)
		{
			log_error("Initialize Cache Exporter: Failed to create the CSV file '%s' after %d retry attempts.", exporter->output_csv_path, MAX_RETRY_ATTEMPTS);
		}
	}

	clear_arena(temporary_arena);
}

// Builds a cache exporter's output path for copying files and adds a given subdirectory's name to the end.
//
// This function is called by initialize_cache_exporter() to set the default output copy path for each cache exporter, and may be
// optionally called later to create more specific subdirectories. This function must be called after initialize_cache_exporter()
// and before terminate_cache_exporter().
//
// @Parameters:
// 1. exporter - The Exporter structure where the resolved output path (for copying files) will be stored.
// 2. subdirectory_name - The name of the output subdirectory (for copying files). This parameter may be NULL if this name shouldn't
// be added to the path.
// 
// @Returns: Nothing.
void set_exporter_output_copy_subdirectory(Exporter* exporter, const TCHAR* subdirectory_name)
{
	StringCchCopy(exporter->output_copy_path, MAX_PATH_CHARS, exporter->output_path);
	
	if(exporter->load_external_locations)
	{
		PathAppend(exporter->output_copy_path, exporter->current_profile_name);
	}

	const TCHAR* cache_identifier = CACHE_TYPE_TO_SHORT_NAME[exporter->current_cache_type];
	PathAppend(exporter->output_copy_path, cache_identifier);

	if(subdirectory_name != NULL)
	{
		PathAppend(exporter->output_copy_path, subdirectory_name);
	}

	// E.g. "SW\Xtras" = 2 components.
	exporter->num_output_components = count_path_components(cache_identifier) + count_path_components(subdirectory_name);
	_ASSERT(exporter->num_output_components > 0);
}

// Assigns a short filename to a cache entry that doesn't have a name.
//
// @Parameters:
// 1. exporter - The Exporter structure that keeps track of the number of assigned filenames, in total and for each cache type.
// 2. result_filename - The buffer that receives the assigned filename. This buffer must be able to hold MAX_PATH_CHARS characters.
// 
// @Returns: Nothing.
static void assign_exporter_short_filename(Exporter* exporter, TCHAR* result_filename)
{
	++(exporter->num_assigned_filenames);
	++(exporter->total_assigned_filenames);
	StringCchPrintf(result_filename, MAX_PATH_CHARS, T("~WCE%04d"), exporter->num_assigned_filenames);
}

// Adds a formatted string to the current exporter's warning message. Successive messages are separated by spaces.
//
// Use the add_exporter_warning_message() macro to perform this operation without having to wrap the format string with T().
// This function and macro must be called after initialize_cache_exporter() and before terminate_cache_exporter().
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current warning message.
// 2. string_format - The format string.
// 3. ... - Zero or more arguments to be inserted in the format string.
// 
// @Returns: Nothing.
void tchar_add_exporter_warning_message(Exporter* exporter, const TCHAR* string_format, ...)
{
	TCHAR message_buffer[MAX_EXPORTER_WARNING_CHARS] = T("");

	va_list arguments;
	va_start(arguments, string_format);
	StringCchVPrintf(message_buffer, MAX_EXPORTER_WARNING_CHARS, string_format, arguments);
	va_end(arguments);

	if(!string_is_empty(exporter->warning_message))
	{
		StringCchCat(exporter->warning_message, MAX_EXPORTER_WARNING_CHARS, T(" "));
	}

	StringCchCat(exporter->warning_message, MAX_EXPORTER_WARNING_CHARS, message_buffer);
}

// Decompresses a cached file to the exporter's temporary directory according to the Content-Encoding HTTP header.
// Note that this header may contain a list of multiple encodings in the order that they were applied (e.g. "deflate, gzip").
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current cache exporter's parameters.
// 2. source_file_path - The path to the file to decompress.
// 3. content_encoding - The value of the Content-Encoding HTTP header that specifies how the file was compressed.
// 4. result_decompressed_file_path - The path to the newly created decompressed file.
// 5. result_decompressed_file_handle - The handle to the newly created decompressed file.
// 6. result_decompressed_file_size - The size of the newly created decompressed file.
// 
// @Returns: True if the file was decompressed successfully. Otherwise, false. Not being able to decompress a file
// isn't always an important error since some files are either: 1) empty; 2) not compressed despite having the
// Content-Encoding header set; 3) contain an unsupported or invalid value in the Content-Encoding header.
static bool decompress_exporter_file(Exporter* exporter, const TCHAR* source_file_path, const TCHAR* content_encoding,
									 TCHAR* result_decompressed_file_path, HANDLE* result_decompressed_file_handle,
									 u64* result_decompressed_file_size)
{
	u64 file_size = 0;
	if(get_file_size(source_file_path, &file_size) && file_size == 0) return false;

	TCHAR current_file_path[MAX_PATH_CHARS] = T("");
	HANDLE current_file_handle = INVALID_HANDLE_VALUE;
	
	if(!create_temporary_exporter_file(exporter, current_file_path, &current_file_handle))
	{
		log_error("Decompress Exporter File: Failed to create the temporary file to decompress the file '%s' with the content encoding '%s'.", source_file_path, content_encoding);
		return false;
	}

	bool success = true;

	Arena* temporary_arena = &(exporter->temporary_arena);
	String_Array<TCHAR>* split_encodings = copy_and_split_string(temporary_arena, content_encoding, T(", \t"));

	TCHAR previous_file_path[MAX_PATH_CHARS] = T("");
	StringCchCopy(previous_file_path, MAX_PATH_CHARS, source_file_path);
	HANDLE previous_file_handle = INVALID_HANDLE_VALUE;

	// The Content-Encoding HTTP header contains a list of comma-separated encodings in the order they were applied. As such, we'll iterate
	// over each one backwards and attempt to decompress them one by one. If we can't decompress a supported format or find an invalid one,
	// then the entire operation stops and the decompression fails.
	//
	// This tool only supports the main encodings listed here: https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Encoding
	// To avoid any confusion, here's a brief description of each one:
	//
	// 1. identity - no compression.
	// 2. gzip - Gzip file format (RFC 1952) that uses the DEFLATE compression method. Alias: x-gzip.
	//
	// 3. deflate - Zlib data format (RFC 1950) that uses the DEFLATE compression method. Despite the name, this is not supposed to be a raw
	// DEFLATE stream (RFC 1951). Although HTTP 1.1 defines this encoding as the Zlib format, some servers (e.g. Microsoft) transmitted raw
	// DEFLATE data. As such, we'll try to decompress the data using both approaches when we see the "deflate" encoding.
	// See: https://zlib.net/zlib_faq.html#faq39
	//
	// 4. br - Brotli data format (RFC 7932).
	// 5. compress - data compressed using the compress/ncompress Unix utility format. Although uncommon nowdays, this method should be handled
	// since we support cache formats from older browsers. Alias: x-compress.
	//
	// See also:
	// - "Hypertext Transfer Protocol (HTTP) Parameters": https://www.iana.org/assignments/http-parameters/http-parameters.xml#http-parameters-1
	// - "Hypertext Transfer Protocol -- HTTP/1.0": https://datatracker.ietf.org/doc/html/rfc1945#section-3.5
	// - "Hypertext Transfer Protocol -- HTTP/1.1": https://datatracker.ietf.org/doc/html/rfc2616#section-3.5

	for(int i = split_encodings->num_strings - 1; i >= 0; --i)
	{
		TCHAR* encoding = split_encodings->strings[i];

		if(strings_are_equal(encoding, T("identity"), true))
		{
			continue;
		}
		else if(strings_are_equal(encoding, T("gzip"), true) || strings_are_equal(encoding, T("deflate"), true) || strings_are_equal(encoding, T("x-gzip"), true))
		{
			int error_code = 0;
			success = decompress_gzip_zlib_deflate_file(temporary_arena, previous_file_path, current_file_handle, &error_code);
			if(!success) add_exporter_warning_message(exporter, "Failed to decompress the file using Gzip/Zlib/DEFLATE with the error code %d.", error_code);
			
			*previous_file_path = T('\0');
			safe_close_handle(&previous_file_handle);
		}
		else if(strings_are_equal(encoding, T("br"), true))
		{
			int error_code = 0;
			success = decompress_brotli_file(temporary_arena, previous_file_path, current_file_handle, &error_code);
			if(!success) add_exporter_warning_message(exporter, "Failed to decompress the file using Brotli with the error code %d.", error_code);
			
			*previous_file_path = T('\0');
			safe_close_handle(&previous_file_handle);
		}
		else if(strings_are_equal(encoding, T("compress"), true) || strings_are_equal(encoding, T("x-compress"), true))
		{
			int error_code = 0;
			success = decompress_compress_file(temporary_arena, previous_file_path, current_file_handle, &error_code);
			if(!success) add_exporter_warning_message(exporter, "Failed to decompress the file using Compress with the error code %d.", error_code);
			
			*previous_file_path = T('\0');
			safe_close_handle(&previous_file_handle);
		}
		else
		{
			add_exporter_warning_message(exporter, "Skipping decompression due to the unsupported content encoding in '%s'.", content_encoding);
			log_warning("Decompress Exporter File: Found unsupported encoding in '%s' while trying to decompress the file '%s'.", content_encoding, source_file_path);
			success = false;
		}

		if(success)
		{
			_ASSERT(string_is_empty(previous_file_path) && previous_file_handle == INVALID_HANDLE_VALUE);

			if(i > 0)
			{
				StringCchCopy(previous_file_path, MAX_PATH_CHARS, current_file_path);
				previous_file_handle = current_file_handle;

				if(!create_temporary_exporter_file(exporter, current_file_path, &current_file_handle))
				{
					TCHAR* next_encoding = split_encodings->strings[i-1];
					add_exporter_warning_message(exporter, "Could not create a temporary file when switching from the content encoding '%s' to '%s'.", encoding, next_encoding);
					log_error("Decompress Exporter File: Failed to create the temporary file to decompress the file '%s' when switching from the content encoding '%s' to '%s' in '%s'.", source_file_path, encoding, next_encoding, content_encoding);
					success = false;
					break;
				}		
			}
		}
		else
		{
			break;
		}
	}

	safe_close_handle(&previous_file_handle);

	if(success)
	{
		StringCchCopy(result_decompressed_file_path, MAX_PATH_CHARS, current_file_path);
		*result_decompressed_file_handle = current_file_handle;
		success = get_file_size(current_file_handle, result_decompressed_file_size);
	}
	else
	{
		safe_close_handle(&current_file_handle);
	}

	return success;
}

// Copies an existing file to a new location while taking into account a few quirks, such as copying a temporary file that's being
// used by the exporter process.
//
// @GetLastError
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current cache exporter's parameters.
// 2. source_file_path - The absolute path to the source file to copy.
// 3. destination_file_path - The absolute path to the destination file to create.
// 
// @Returns: True if the file was copied successfully. Otherwise, false and the error code can be retrieved using GetLastError().
static bool copy_exporter_file(Exporter* exporter, const TCHAR* source_file_path, const TCHAR* destination_file_path)
{
	// @Note: Any function used to copy the file here must set the last Windows error code properly so that we can perform the correct
	// checks using GetLastError() in copy_exporter_file_using_url_directory_structure(). This applies to CopyFile(), create_empty_file(),
	// copy_file_chunks(), and this function itself.
	bool copy_success = false;

	#if defined(WCE_DEBUG) && defined(WCE_EMPTY_EXPORT)
		copy_success = create_empty_file(destination_file_path, false);
	#else
		copy_success = CopyFile(source_file_path, destination_file_path, TRUE) != FALSE;

		// For older Windows versions when we're copying temporary files that are currently being used by the exporter's process.
		if(!copy_success && GetLastError() == ERROR_SHARING_VIOLATION)
		{
			log_warning("Copy Exporter File: Attempting to copy the file '%s' to '%s' chunk by chunk due to a sharing violation.", source_file_path, destination_file_path);

			u64 file_size = 0;
			if(get_file_size(source_file_path, &file_size))
			{
				Arena* temporary_arena = &(exporter->temporary_arena);
				copy_success = copy_file_chunks(temporary_arena, source_file_path, file_size, 0, destination_file_path, false);
			}
			else
			{
				// Propagate a generic error code for a failure case where we can't attempt to copy the whole file in chunks because we couldn't
				// determine its size. What matters here is that we specify an error code for every case that this function returns false.
				SetLastError(CUSTOM_ERROR_FAILED_TO_GET_FILE_SIZE);
			}
		}
	#endif

	return copy_success;
}

// Copies a file using a given URL's directory structure. If the generated file path already exists, this function will resolve any
// naming collisions by adding a number to the filename. This function is a core part of the cache exporters.
//
// The final path is built by joining the following paths:
// 1. The exporter's current base destination directory.
// 2. The host and path components of the URL (if a URL was passed to this function).
// 3. The filename.
//
// For example: "C:\Path" + "http://www.example.com:80/path/file.php?query#fragment" + "file.ext"
// results in: "C:\Path\www.example.com\path\file.ext"
//
// If this file already exists, a tilde followed by a number will be added before the file extension. This number will be incremented
// until there's no longer a naming collision. For example: "C:\Path\www.example.com\path\file~1.ext".
//
// Since the URL and filename can be invalid Windows paths, these may be modified accordingly (e.g. replacing invalid characters).
//
// Note that all of these paths are limited to MAX_PATH_CHARS characters. This limit used to be extended by using the "\\?\" prefix
// on the Windows 2000 through 10 builds. However, in practice that would result in paths that would be too long for the File Explorer
// to delete. This is a problem for this application since the whole point is to get the average user to check their cache for lost
// web media files.
//
// Instead of failing in the cases where the final path length exceeds this limit, this function will attempt to copy the file to the base
// destination directory. Using the example above, this would be "C:\Path\file.ext". This limit may be exceed by either the URL structure
// or the filename.
//
// @GetLastError
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current cache exporter's parameters and the values of command line options
// that will influence how the file is copied.
//
// 2. full_source_path - The absolute path to the source file to copy.
// 3. url - The URL whose host and path components are converted into a Windows path. If this string is NULL, only the base directory
// and filename will be used.
// 4. filename - The filename to add to the end of the path.
// 5. default_file_extension - The file extension to add to the end of the filename in case it doesn't have one already. This extension
// should be determined using the matched file groups and must not begin with a period.
//
// 6. result_destination_path - The final destination path after resolving any naming collisions. This string is only set if this function
// returns true. Otherwise, this string will be empty.
// 7. result_error_code - A string containing the Windows system error code generated after attempting to copy the file. This string can
// only be set if this function returns false. Otherwise, this string will be empty. Note that, if the function terminates before attempting
// to copy the file due to an error early on, this string will also be empty.
//
// @Returns: True if the file was copied successfully. Otherwise, false. This function fails if the source file path is empty.
static bool copy_exporter_file_using_url_directory_structure(	Exporter* exporter,
																const TCHAR* full_source_path, const TCHAR* url,
																const TCHAR* filename, const TCHAR* default_file_extension,
																TCHAR* result_destination_path, TCHAR* result_error_code)
{
	*result_destination_path = T('\0');
	*result_error_code = T('\0');

	if(full_source_path == NULL || string_is_empty(full_source_path))
	{
		convert_u32_to_string(CUSTOM_ERROR_EMPTY_OR_NULL_SOURCE_PATH, result_error_code);
		return false;
	}

	Arena* temporary_arena = &(exporter->temporary_arena);
	const TCHAR* full_base_directory_path = exporter->output_copy_path;

	// Copy Target = Base Destination Path
	TCHAR full_destination_path[MAX_PATH_CHARS] = T("");
	PathCanonicalize(full_destination_path, full_base_directory_path);

	int num_base_components = count_path_components(full_destination_path);

	// Copy Target = Base Destination Path + Url Converted To Path (if it exists)
	if(url != NULL)
	{
		TCHAR url_path[MAX_PATH_CHARS] = T("");
		bool build_target_success = convert_url_to_path(temporary_arena, url, url_path) && (PathAppend(full_destination_path, url_path) != FALSE);
		if(!build_target_success)
		{
			log_warning("Copy File Using Url Structure: The website directory structure for the file '%s' could not be created. This file will be copied to the base export directory instead.", filename);
			StringCchCopy(full_destination_path, MAX_PATH_CHARS, full_base_directory_path);
		}
	}

	_ASSERT(!string_is_empty(full_destination_path));

	// Create every directory in the copy target, while resolving any file naming collisions.
	// - If a directory with the same name already exists, nothing is done and the function continues.
	// - If a file with the same name already exists, then the name of the directory will be changed (e.g. "Dir" -> "Dir~1").
	TCHAR resolved_full_destination_path[MAX_PATH_CHARS] = T("");
	if(create_directories(full_destination_path, true, resolved_full_destination_path))
	{
		StringCchCopy(full_destination_path, MAX_PATH_CHARS, resolved_full_destination_path);
	}
	else
	{
		log_warning("Copy File Using Url Structure: Could not create the directory structure for the file '%s': '%s'. This file will be copied to the base export directory instead.", filename, full_destination_path);
		StringCchCopy(full_destination_path, MAX_PATH_CHARS, full_base_directory_path);
	}

	// Keep track of the file extension in case we have to modify the filename due to a
	// naming collision or because the destination path is too long. Given the information
	// available to us (original and default file extensions), we can use the file extension
	// in the following ways:
	//
	// 1. Filename too long. E.g. "http://www.example.com/<Long Name>.html" -> "~WCE0001.html".
	// 2. No filename. E.g. "http://www.example.com/" -> "~WCE0001.html".
	// 3. Filename missing file extension. E.g. "http://www.example.com/page" -> "page.html".
	// 
	// The third case is essencially the same as the second, just without assigning a shorter
	// filename. Note also that in these last cases we're adding information that wasn't there
	// before.
	// @Future: This isn't important for the second case, but for the third one we might want
	// to add an option to disable this behavior.

	TCHAR corrected_filename[MAX_PATH_CHARS] = T("");
	StringCchCopy(corrected_filename, MAX_PATH_CHARS, filename);
	
	TCHAR* corrected_file_extension = NULL;
	const TCHAR* file_extension = skip_to_file_extension((TCHAR*) filename, true);
	
	if(string_is_empty(file_extension) && default_file_extension != NULL)
	{
		_ASSERT(*default_file_extension != T('.'));
		size_t file_extension_size = sizeof(TCHAR) + string_size(default_file_extension);
		corrected_file_extension = push_arena(temporary_arena, file_extension_size, TCHAR);
		StringCbCopy(corrected_file_extension, file_extension_size, T("."));
		StringCbCat(corrected_file_extension, file_extension_size, default_file_extension);

		StringCchCat(corrected_filename, MAX_PATH_CHARS, corrected_file_extension);
	}
	else
	{
		corrected_file_extension = push_string_to_arena(temporary_arena, file_extension);
	}
	
	correct_url_path_characters(corrected_filename);
	truncate_path_components(corrected_filename);
	correct_reserved_path_components(corrected_filename);

	correct_url_path_characters(corrected_file_extension);
	truncate_path_components(corrected_file_extension);
	correct_reserved_path_components(corrected_file_extension);

	// Copy Target = Base Destination Path + Url Converted To Path (if it exists) + Filename
	bool build_target_success = PathAppend(full_destination_path, corrected_filename) != FALSE;
	if(!build_target_success)
	{
		log_warning("Copy File Using Url Structure: Could not add the filename '%s' to the website directory structure. This file will be copied to the base export directory instead.", filename);
		
		StringCchCopy(full_destination_path, MAX_PATH_CHARS, full_base_directory_path);
		if(PathAppend(full_destination_path, corrected_filename) == FALSE)
		{
			log_warning("Copy File Using Url Structure: Could not add the filename '%s' to the base export directory. This file will be copied using a shorter name generated by the exporter.", filename);
			
			StringCchCopy(full_destination_path, MAX_PATH_CHARS, full_base_directory_path);

			assign_exporter_short_filename(exporter, corrected_filename);
			StringCchCat(corrected_filename, MAX_PATH_CHARS, corrected_file_extension);

			if(PathAppend(full_destination_path, corrected_filename) == FALSE)
			{
				log_error("Copy File Using Url Structure: Failed to build any valid path for the file '%s'. This file will not be copied.", filename);
				convert_u32_to_string(CUSTOM_ERROR_FAILED_TO_BUILD_VALID_DESTINATION_PATH, result_error_code);
				return false;
			}
		}
	}

	_ASSERT(!string_is_empty(full_destination_path));

	u32 num_naming_collisions = 0;
	TCHAR unique_id[MAX_INT_32_CHARS + 1] = T("~");
	TCHAR full_unique_destination_path[MAX_PATH_CHARS] = T("");
	StringCchCopy(full_unique_destination_path, MAX_PATH_CHARS, full_destination_path);

	bool copy_success = copy_exporter_file(exporter, full_source_path, full_unique_destination_path);

	// Copy the file to the target directory, while resolving any file naming collisions.
	// - If a file with the same name already exists (ERROR_FILE_EXISTS), then the current name will be changed (e.g. "File.ext" -> "File~1.ext").
	// - If a directory with the same name already exists (ERROR_ACCESS_DENIED), then the operation above is also performed.
	#define NAMING_COLLISION() ( GetLastError() == ERROR_FILE_EXISTS || (GetLastError() == ERROR_ACCESS_DENIED && does_directory_exist(full_unique_destination_path)) )

	while(!copy_success && NAMING_COLLISION())
	{
		++num_naming_collisions;
		if(num_naming_collisions == 0)
		{
			log_error("Copy File Using Url Structure: Wrapped around the number of naming collisions for the file '%s'. This file will not be copied.", filename);
			SetLastError(CUSTOM_ERROR_TOO_MANY_NAMING_COLLISIONS);
			break;
		}

		bool naming_success = SUCCEEDED(StringCchCopy(full_unique_destination_path, MAX_PATH_CHARS, full_destination_path));
		if(naming_success)
		{
			TCHAR* file_extension_in_target = skip_to_file_extension(full_unique_destination_path, true);
			*file_extension_in_target = T('\0');
		}

		naming_success = naming_success && convert_u32_to_string(num_naming_collisions, unique_id + 1)
										&& SUCCEEDED(StringCchCat(full_unique_destination_path, MAX_PATH_CHARS, unique_id))
										&& SUCCEEDED(StringCchCat(full_unique_destination_path, MAX_PATH_CHARS, corrected_file_extension));

		if(!naming_success)
		{
			log_error("Copy File Using Url Structure: Failed to resolve the naming collision %I32u for the file '%s'. This file will not be copied.", num_naming_collisions, filename);
			SetLastError(CUSTOM_ERROR_UNRESOLVED_NAMING_COLLISION);
			break;
		}
		
		// Try again with a new name.
		copy_success = copy_exporter_file(exporter, full_source_path, full_unique_destination_path);
	}
	
	#undef NAMING_COLLISION

	// Set the output values depending on the copy operation's success.
	if(copy_success)
	{
		TCHAR* final_destination_path = (num_naming_collisions == 0) ? (full_destination_path) : (full_unique_destination_path);

		if(!exporter->show_full_paths)
		{
			int num_final_components = count_path_components(final_destination_path);
			int num_short_components = num_final_components - num_base_components + exporter->num_output_components;
			_ASSERT(num_short_components > 0);

			final_destination_path = skip_to_last_path_components(final_destination_path, num_short_components);
		}

		StringCchCopy(result_destination_path, MAX_PATH_CHARS, final_destination_path);
		_ASSERT(!string_is_empty(result_destination_path));
	}
	else
	{
		DWORD copy_error_code = GetLastError();
		log_error("Copy File Using Url Structure: Failed to copy '%s' to '%s' with the error code %lu.", filename, full_destination_path, copy_error_code);
		convert_u32_to_string(copy_error_code, result_error_code);
	}
	
	return copy_success;
}

// Exports a cache entry by copying its file to the output location using the original website's directory structure, and by adding a
// new row to the CSV file. This function will also match the cache entry to any loaded group files.
//
// This function must be called after initialize_cache_exporter() and before terminate_cache_exporter().
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current cache exporter's parameters and the values of command line options
// that will influence how the entry is exported.
//
// 2. column_values - The array of CSV column values to write. Some values don't need to set explicitly if their respective column type is handled
// automatically.
//
// 3. params - The basic exporter parameters used to copy the cached file and fill certain CSV columns for each cache exporter:
//
// - The 'copy_source_path' should be defined in most cases, but may be NULL if an exporter has to manipulate the cached data using temporary files.
// It's possible that this manipulation may fail (e.g. extracting the payload from a file), leading to a situation where we don't want to copy anything.
//
// - The 'url' may be NULL if the cached file has no URL information associated with it.
// - The 'filename' may be NULL if either 'url' or 'file_info' were set and contain a non-empty string. Otherwise, this function assigns the cached
// file a unique name.
//
// - The 'short_location_on_cache' is always required.
// - The 'full_location_on_cache' defaults to 'copy_source_path' if it's not set.
//
// - The 'file_info' specifies additional information about the file on disk that may be use to fill some CSV columns if their values are missing.
// These include the file size, creation time, last access time, and last write time. If an exporter isn't using any Traversal_Object_Info structs,
// then this should be set to NULL. If it is using them but you don't want to potentially replace the output filename with the file's real name on
// disk, then you may also set it to NULL. For example, if both 'filename' and 'file_info' are NULL, and a name can't be determined from the URL,
// then the exporter will just generate a unique one.
// 
// @Returns: Nothing.
void export_cache_entry(Exporter* exporter, Csv_Entry* column_values, Exporter_Params* params)
{
	#define IS_STRING_EMPTY(string) ( ((string) == NULL) || string_is_empty(string) )

	exporter->exported_at_least_one_file = true;

	Arena* temporary_arena = &(exporter->temporary_arena);

	TCHAR* entry_source_path = params->copy_source_path;
	TCHAR* entry_url = params->url;
	TCHAR* entry_filename = params->filename;

	TCHAR* entry_request_origin = params->request_origin;
	Http_Headers entry_headers = params->headers;
	Traversal_Object_Info* entry_file_info = params->file_info;

	TCHAR* location_on_cache = NULL;
	{
		TCHAR* short_location_on_cache = params->short_location_on_cache;
		TCHAR* full_location_on_cache = params->full_location_on_cache;
		if(IS_STRING_EMPTY(full_location_on_cache)) full_location_on_cache = entry_source_path;
		
		location_on_cache = (exporter->show_full_paths) ? (full_location_on_cache) : (short_location_on_cache);	
	}
 
	if(IS_STRING_EMPTY(entry_filename) && entry_url != NULL)
	{
		Url_Parts parts = {};
		if(partition_url(temporary_arena, entry_url, &parts))
		{
			entry_filename = parts.filename;
		}
	}
	
	if(IS_STRING_EMPTY(entry_filename) && entry_file_info != NULL)
	{
		entry_filename = entry_file_info->object_name;
	}
	
	bool assigned_short_filename = false;
	TCHAR short_filename[MAX_PATH_CHARS] = T("");
	if(IS_STRING_EMPTY(entry_filename))
	{
		assigned_short_filename = true;
		assign_exporter_short_filename(exporter, short_filename);
		entry_filename = short_filename;
	}

	_ASSERT(!IS_STRING_EMPTY(entry_source_path));
	_ASSERT(entry_url == NULL || !string_is_empty(entry_url));
	_ASSERT(!IS_STRING_EMPTY(entry_filename));
	_ASSERT(exporter->current_cache_type == CACHE_EXPLORE || !IS_STRING_EMPTY(location_on_cache));

	// ------------------------------------------------------------

	++(exporter->total_processed_files);

	// The 'original_file_path' is the path to the original cached file on disk, while 'entry_source_path' points to
	// whatever file we want to copy and use to determine the file groups. In most cases these are the same, though
	// when a file is compressed we want to export the decompressed version instead.
	bool file_exists = does_file_exist(entry_source_path);
	TCHAR* original_file_path = entry_source_path;
	
	// Decompress the file according to its Content-Encoding HTTP header (if it exists).
	TCHAR decompressed_file_path[MAX_PATH_CHARS] = T("");
	HANDLE decompressed_file_handle = INVALID_HANDLE_VALUE;
	TCHAR decompressed_file_size[MAX_INT_64_CHARS] = T("");

	if(exporter->decompress_files && file_exists && entry_headers.content_encoding != NULL)
	{
		u64 decompressed_file_size_value = 0;
		if(decompress_exporter_file(exporter, entry_source_path, entry_headers.content_encoding,
									decompressed_file_path, &decompressed_file_handle, &decompressed_file_size_value))
		{
			entry_source_path = decompressed_file_path;
			convert_u64_to_string(decompressed_file_size_value, decompressed_file_size);
		}
	}

	Matchable_Cache_Entry entry_to_match = {};
	entry_to_match.full_file_path = entry_source_path;

	int file_group_index = -1;
	int url_group_index = -1;

	TCHAR file_size[MAX_INT_64_CHARS] = T("");
	TCHAR creation_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
	TCHAR last_write_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
	TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = T("");
	
	for(int i = 0; i < exporter->num_csv_columns; ++i)
	{
		TCHAR* value = column_values[i].value;
		bool use_value_from_file_info = (value == NULL && entry_file_info != NULL);

		switch(exporter->csv_column_types[i])
		{
			/*
				@CustomGroups: Used to fill the custom group columns.
				@FileInfo: Uses the values from the 'file_info' parameter if it exists, and if the column value in question is not NULL.
				@ExporterParams: Uses the values from the 'params' parameter, except 'file_info'.
			*/

			// @FileInfo @ExporterParams
			case(CSV_FILENAME):
			{
				if(value == NULL) value = entry_filename;
			} break;

			// @ExporterParams
			case(CSV_URL):
			{
				if(value == NULL) value = entry_url;
			} break;

			case(CSV_REQUEST_ORIGIN):
			{
				if(value == NULL) value = entry_request_origin;
			} break;

			// @CustomGroups @FileInfo
			case(CSV_FILE_EXTENSION):
			{
				if(value == NULL) value = skip_to_file_extension(entry_filename);
				entry_to_match.file_extension_to_match = value;
			} break;

			// @FileInfo
			case(CSV_FILE_SIZE):
			{
				if(IS_STRING_EMPTY(value))
				{
					u64 file_size_value = 0;
					if(use_value_from_file_info)
					{
						file_size_value = entry_file_info->object_size;
					}
					else
					{
						get_file_size(original_file_path, &file_size_value);
					}

					convert_u64_to_string(file_size_value, file_size);
					value = file_size;
				}
			} break;

			// @FileInfo
			case(CSV_CREATION_TIME):
			{
				if(use_value_from_file_info)
				{
					format_filetime_date_time(entry_file_info->creation_time, creation_time);
					value = creation_time;
				}
			} break;

			// @FileInfo
			case(CSV_LAST_WRITE_TIME):
			{
				if(use_value_from_file_info)
				{
					format_filetime_date_time(entry_file_info->last_write_time, last_write_time);
					value = last_write_time;
				}
			} break;

			// @FileInfo
			case(CSV_LAST_ACCESS_TIME):
			{
				if(use_value_from_file_info)
				{
					format_filetime_date_time(entry_file_info->last_access_time, last_access_time);
					value = last_access_time;
				}
			} break;

			// @ExporterParams
			case(CSV_RESPONSE):
			{
				if(value == NULL) value = entry_headers.response;
			} break;

			// @ExporterParams
			case(CSV_SERVER):
			{
				if(value == NULL) value = entry_headers.server;
			} break;

			// @ExporterParams
			case(CSV_CACHE_CONTROL):
			{
				if(value == NULL) value = entry_headers.cache_control;
			} break;

			// @ExporterParams
			case(CSV_PRAGMA):
			{
				if(value == NULL) value = entry_headers.pragma;
			} break;

			// @CustomGroups @ExporterParams
			case(CSV_CONTENT_TYPE):
			{
				if(value == NULL) value = entry_headers.content_type;
				entry_to_match.mime_type_to_match = value;
			} break;

			// @ExporterParams
			case(CSV_CONTENT_LENGTH):
			{
				if(value == NULL) value = entry_headers.content_length;
			} break;

			// @ExporterParams
			case(CSV_CONTENT_RANGE):
			{
				if(value == NULL) value = entry_headers.content_range;
			} break;

			// @ExporterParams
			case(CSV_CONTENT_ENCODING):
			{
				if(value == NULL) value = entry_headers.content_encoding;
			} break;

			case(CSV_DECOMPRESSED_FILE_SIZE):
			{
				_ASSERT(value == NULL);
				value = decompressed_file_size;
			} break;

			// @ExporterParams
			case(CSV_LOCATION_ON_CACHE):
			{
				_ASSERT(value == NULL);
				value = location_on_cache;
			} break;

			// @ExporterParams
			case(CSV_LOCATION_ON_DISK):
			{
				_ASSERT(value == NULL);
				value = original_file_path;
			} break;

			// @ExporterParams
			case(CSV_MISSING_FILE):
			{
				_ASSERT(value == NULL);
				value = (file_exists) ? (T("No")) : (T("Yes"));
			} break;

			// @ExporterParams
			case(CSV_EXPORTER_WARNING):
			{
				_ASSERT(value == NULL);
				value = exporter->warning_message;
			} break;

			// @CustomGroups
			case(CSV_CUSTOM_FILE_GROUP):
			{
				_ASSERT(value == NULL);
				file_group_index = i;
			} break;

			// @CustomGroups
			case(CSV_CUSTOM_URL_GROUP):
			{
				_ASSERT(value == NULL);
				url_group_index = i;
			} break;

			// @ExporterParams
			case(CSV_SHA_256):
			{
				_ASSERT(value == NULL);
				if(file_exists)
				{
					value = generate_sha_256_from_file(temporary_arena, entry_source_path);
				}
			} break;
		}

		column_values[i].value = value;
	}

	entry_to_match.match_file_group = (file_group_index != -1);
	entry_to_match.match_url_group = (url_group_index != -1);

	// Add the request origin to the beginning of the URL if needed.
	if(exporter->group_by_request_origin && entry_url != NULL && entry_request_origin != NULL)
	{
		entry_url = skip_url_scheme(entry_url);

		const TCHAR* GENERIC_SCHEME = T("http://");
		size_t new_url_size = string_size(GENERIC_SCHEME) + string_size(entry_request_origin) + string_size(entry_url);
		TCHAR* new_url = push_arena(temporary_arena, new_url_size, TCHAR);

		// Add a generic scheme if the request origin doesn't have one.
		Url_Parts parts = {};
		partition_url(temporary_arena, entry_request_origin, &parts);

		if(parts.scheme == NULL)
		{
			StringCbCopy(new_url, new_url_size, GENERIC_SCHEME);
			StringCbCat(new_url, new_url_size, entry_request_origin);
			StringCbCat(new_url, new_url_size, T("/"));
			StringCbCat(new_url, new_url_size, entry_url);

		}
		else
		{
			StringCbCopy(new_url, new_url_size, entry_request_origin);
			StringCbCat(new_url, new_url_size, T("/"));
			StringCbCat(new_url, new_url_size, entry_url);
		}

		entry_url = new_url;
	}

	entry_to_match.url_to_match = entry_url;

	// Files can match groups even if they don't exist on disk.
	bool matched_group = match_cache_entry_to_groups(exporter, &entry_to_match);
	if(matched_group)
	{
		if(file_group_index != -1)
		{
			column_values[file_group_index].value = entry_to_match.matched_file_group_name;
		}

		if(url_group_index != -1)
		{
			column_values[url_group_index].value = entry_to_match.matched_url_group_name;
		}
	}

	bool match_allows_for_exporting_entry = true;
	if(exporter->filter_by_groups)
	{
		match_allows_for_exporting_entry = (matched_group && entry_to_match.match_is_enabled_for_filtering) || exporter->ignore_filter_for_cache_type[exporter->current_cache_type];
	}

	TCHAR copy_destination_path[MAX_PATH_CHARS] = T("");
	TCHAR copy_error_code[MAX_INT_32_CHARS] = T("");
	if(file_exists && exporter->copy_files && match_allows_for_exporting_entry)
	{
		if(copy_exporter_file_using_url_directory_structure(exporter,
															entry_source_path, entry_url,
															entry_filename, entry_to_match.matched_default_file_extension,
															copy_destination_path, copy_error_code))
		{
			++(exporter->total_copied_files);
		}
	}

	// For any values that can only be added to the CSV row after copying the file.
	for(int i = 0; i < exporter->num_csv_columns; ++i)
	{
		TCHAR* value = column_values[i].value;

		switch(exporter->csv_column_types[i])
		{
			case(CSV_LOCATION_IN_OUTPUT):
			{
				_ASSERT(value == NULL);
				value = copy_destination_path;
			} break;

			case(CSV_COPY_ERROR):
			{
				_ASSERT(value == NULL);
				value = copy_error_code;
			} break;
		}

		column_values[i].value = value;
	}

	if(exporter->create_csvs && match_allows_for_exporting_entry)
	{
		csv_print_row(temporary_arena, exporter->csv_file_handle, column_values, exporter->num_csv_columns);
	}

	safe_close_handle(&decompressed_file_handle);

	clear_arena(temporary_arena);
	exporter->warning_message[0] = T('\0');

	#undef IS_STRING_EMPTY
}

// Resets any exporter members that are used to hold temporary values that should not persist between multiple cache locations
// even if they belong to the same cache type. Usually only used for web browser cache formats.
//
// This function must be called after initialize_cache_exporter() and before terminate_cache_exporter().
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the temporary members to reset.
// 
// @Returns: Nothing.
void reset_temporary_exporter_members(Exporter* exporter)
{
	exporter->browser_name = NULL;
	exporter->browser_profile = NULL;
	clear_arena(&(exporter->temporary_arena));
}

// Terminates a cache exporter by performing the following:
// - Closing the exporter's current CSV file.
// - Clearing the temporary exporter directory.
// - Clearing the temporary memory arena.
//
// This function must be called by each exporter after processing any cached files, and may be called multiple times by the same
// exporter. Before starting the export process, the initialize_cache_exporter() function must be called.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the CSV file's handle.
// 
// @Returns: Nothing.
void terminate_cache_exporter(Exporter* exporter)
{
	safe_close_handle(&(exporter->csv_file_handle));
	if(!exporter->exported_at_least_one_file)
	{
		log_info("Terminate Cache Exporter: Deleting the CSV file '%s' since no files were exported for the %s cache.", exporter->output_csv_path, CACHE_TYPE_TO_FULL_NAME[exporter->current_cache_type]);
		DeleteFile(exporter->output_csv_path);
	}
	
	clear_temporary_exporter_directory(exporter);
	clear_arena(&(exporter->temporary_arena));
}

// Creates an empty file in the temporary exporter directory.
//
// This function is useful for two particular cases:
// - Creating a temporary file without having to worry about getting its file handle right away. For example, if it's going to be
// overwritten by a call to CreateFile() or CopyFile()).
// - Creating a temporary file with a specific name.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the temporary directory.
// 2. result_file_path - The buffer that receives the created file's path. This buffer must be able to hold MAX_PATH_CHARS characters.
// 3. optional_filename - An optional parameter that specifies the filename. If this value is not set, the function generates a
// unique name. Note that, in this case, the function will overwrite any file in the temporary directory with the same name.
// This value defaults to NULL.
// 
// @Returns: True if the file was created successfully. Otherwise, false.
bool create_placeholder_exporter_file(Exporter* exporter, TCHAR* result_file_path, const TCHAR* optional_filename)
{
	*result_file_path = T('\0');
	if(!exporter->was_temporary_exporter_directory_created) return false;
	bool create_success = false;

	if(optional_filename != NULL)
	{
		PathCombine(result_file_path, exporter->exporter_temporary_path, optional_filename);
		create_success = create_empty_file(result_file_path, true);
	}
	else
	{
		create_success = GetTempFileName(exporter->exporter_temporary_path, TEMPORARY_NAME_PREFIX, 0, result_file_path) != 0;
	}

	return create_success;
}

// Creates an empty file in the temporary exporter directory and opens it for reading and writing. This file is automatically
// deleted after closing it.
//
// This function is useful for two particular cases:
// - Avoiding writing temporary data back to mass storage and having the file be automatically deleted after closing it.
// - Creating a temporary file without caring about its filename.
//
// Note that any future calls to CreateFile() with the resulting file path *must* specify FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
// for the share mode, and GENERIC_READ for the desired access. The annotation @TemporaryFiles is used in any function defined in
// common.cpp that could interface with a temporary file using only its path (e.g. generating the hash of a temporary cached file).
//
// Note also that you should be careful and remember to close any additional file handles that are opened in the previously mentioned
// functions marked with @TemporaryFiles. Not doing so will prevent the temporary directory from being deleted (since the temporary file
// would still exist in there). The file itself would be deleted after the application terminates since Windows closes all opened file
// handles, but an empty temporary directory would be left behind, which is annoying.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the temporary directory.
// 2. result_file_path - The buffer that receives the created file's path. This buffer must be able to hold MAX_PATH_CHARS characters.
// 3. result_file_handle - The resulting file handle of the created file.
// 
// @Returns: True if the file was created successfully. Otherwise, false.
bool create_temporary_exporter_file(Exporter* exporter, TCHAR* result_file_path, HANDLE* result_file_handle)
{
	*result_file_path = T('\0');
	*result_file_handle = INVALID_HANDLE_VALUE;

	if(!exporter->was_temporary_exporter_directory_created) return false;

	// This is only used to get a unique filename. We will overwrite this file below.
	bool create_success = create_placeholder_exporter_file(exporter, result_file_path);
	bool get_handle_success = false;

	if(create_success)
	{
		*result_file_handle = create_handle(result_file_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
											CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE);

		get_handle_success = (*result_file_handle != INVALID_HANDLE_VALUE);

		// If we couldn't get the handle, the function will fail, so we'll make sure to delete the file.
		if(!get_handle_success) DeleteFile(result_file_path);
	}

	return create_success && get_handle_success;
}

// Deletes all files and subdirectories inside the temporary exporter directory.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the exporter's temporary directory.
// 
// @Returns: Nothing.
void clear_temporary_exporter_directory(Exporter* exporter)
{
	if(!exporter->was_temporary_exporter_directory_created) return;

	Arena* temporary_arena = &(exporter->temporary_arena);
	Traversal_Result* objects = find_objects_in_directory(temporary_arena, exporter->exporter_temporary_path, ALL_OBJECTS_SEARCH_QUERY, TRAVERSE_FILES | TRAVERSE_DIRECTORIES, false);

	for(int i = 0; i < objects->num_objects; ++i)
	{
		Traversal_Object_Info objects_info = objects->object_info[i];
		TCHAR* full_object_path = objects_info.object_path;

		if(objects_info.is_directory)
		{
			delete_directory_and_contents(full_object_path);
		}
		else
		{
			DeleteFile(full_object_path);
		}
	}
}

// Deletes every temporary exporter directory with a specific prefix inside the Windows Temporary Files directory.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the Windows Temporary Files directory.
// 
// @Returns: Nothing.
static void clear_temporary_windows_directory(Exporter* exporter)
{
	Arena* temporary_arena = &(exporter->temporary_arena);
	Traversal_Result* directories = find_objects_in_directory(temporary_arena, exporter->windows_temporary_path, TEMPORARY_NAME_SEARCH_QUERY, TRAVERSE_DIRECTORIES, false);

	log_info("Clear Temporary Windows Directory: Deleting %d temporary exporter directories with the prefix '%s' located in '%s'.", directories->num_objects, TEMPORARY_NAME_PREFIX, exporter->windows_temporary_path);

	for(int i = 0; i < directories->num_objects; ++i)
	{
		Traversal_Object_Info directory_info = directories->object_info[i];
		_ASSERT(directory_info.is_directory);

		TCHAR* full_directory_path = directory_info.object_path;
		log_info("Clear Temporary Windows Directory: Deleting the temporary directory in '%s'.", full_directory_path);
		delete_directory_and_contents(full_directory_path);
	}
}

/*
	The following defines the necessary functions used to load the external locations file. External locations files are text files
	that define zero or more profiles, each one specifying a list of absolute paths of key Windows locations. This allows you to export
	the cache from files that came from another computer.

	To learn how these files are supposed to work, refer to the "About External Locations.txt" help file in "Source\ExternalLocations".
*/

// Various keywords and delimiters for the external locations file syntax.
static const char COMMENT = ';';
static const char* LINE_DELIMITERS = "\r\n";
static const char* TOKEN_DELIMITERS = " \t";
static const char* BEGIN_PROFILE = "BEGIN_PROFILE";
static const char* END_PROFILE = "END";
static const char* LOCATION_DRIVE = "DRIVE";
static const char* LOCATION_WINDOWS = "WINDOWS";
static const char* LOCATION_TEMPORARY = "TEMPORARY";
static const char* LOCATION_USER_PROFILE = "USER_PROFILE";
static const char* LOCATION_APPDATA = "APPDATA";
static const char* LOCATION_LOCAL_APPDATA = "LOCAL_APPDATA";
static const char* LOCATION_LOCAL_LOW_APPDATA = "LOCAL_LOW_APPDATA";
static const char* LOCATION_INTERNET_CACHE = "INTERNET_CACHE";

// Retrieves the number of profiles and how many bytes are (roughly) required to store them from the external locations file.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the external locations file.
// 2. result_num_profiles - The number of profiles found.
//
// @Returns: The total size in bytes required to store the profiles found.
static size_t get_total_external_locations_size(Exporter* exporter, int* result_num_profiles)
{
	Arena* temporary_arena = &(exporter->temporary_arena);
	lock_arena(temporary_arena);

	u64 file_size = 0;
	char* file = (char*) read_entire_file(temporary_arena, exporter->external_locations_file_path, &file_size, true);

	size_t total_locations_size = 0;
	int num_profiles = 0;

	if(file != NULL)
	{
		String_Array<char>* split_lines = split_string(temporary_arena, file, LINE_DELIMITERS);
		
		for(int i = 0; i < split_lines->num_strings; ++i)
		{
			char* line = split_lines->strings[i];
			line = skip_leading_whitespace(line);

			if(*line == COMMENT || string_is_empty(line))
			{
				// Skip comments and empty lines.
			}
			else
			{
				// Keep track of the total group file string data. We're essentially getting a single byte character
				// string's length plus the null terminator here. At the end, we'll multiply this value by sizeof(TCHAR).
				// This should guarantee enough memory (in excess) for both types of build (char vs wchar_t).
				total_locations_size += string_size(line);

				String_Array<char>* split_tokens = split_string(temporary_arena, line, TOKEN_DELIMITERS, 1);
				
				if(split_tokens->num_strings == 2)
				{
					char* type = split_tokens->strings[0];
					char* name = split_tokens->strings[1];

					if(strings_are_equal(type, BEGIN_PROFILE) && !string_is_empty(name))
					{
						++num_profiles;
					}
				}
			}
		}
	}
	else
	{
		log_error("Get Total External Locations Size: Failed to load the external locations file '%s'.", exporter->external_locations_file_path);
	}

	clear_arena(temporary_arena);
	unlock_arena(temporary_arena);

	*result_num_profiles = num_profiles;

	// Total Size = Size for the Profile array + Size for the string data.
	return 	sizeof(External_Locations) + MAX(num_profiles - 1, 0) * sizeof(Profile)
			+ total_locations_size * sizeof(TCHAR);
}

// Loads the external locations file on disk. This function must be called after get_total_external_locations_size() and with a
// memory arena that is capable of holding the number of bytes it returned.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the external locations file, and the permanent memory arena where
// the profile data will be stored. After loading this data, this structure's 'external_locations' member will modified.
// 2. num_profiles - The number of profiles found by an earlier call to get_total_external_locations_size().
//
// @Returns: Nothing.
static void load_external_locations(Exporter* exporter, int num_profiles)
{
	if(num_profiles == 0)
	{
		log_warning("Load External Locations: Attempted to load zero profiles. No external locations will be loaded.");
		return;
	}

	// The relevant loaded group data will go to the permanent arena, and any intermediary data to the temporary one.
	Arena* permanent_arena = &(exporter->permanent_arena);
	Arena* temporary_arena = &(exporter->temporary_arena);

	lock_arena(temporary_arena);

	// The number of profiles is always greater than zero here.
	size_t external_locations_size = sizeof(External_Locations) + sizeof(Profile) * (num_profiles - 1);
	External_Locations* external_locations = push_arena(permanent_arena, external_locations_size, External_Locations);
	
	u64 file_size = 0;
	char* file = (char*) read_entire_file(temporary_arena, exporter->external_locations_file_path, &file_size, true);

	int num_processed_profiles = 0;

	if(file != NULL)
	{
		// Keep track of which profile we're loading data to.
		bool seen_begin_list = false;
		bool is_invalid = false;
		Profile* profile_array = external_locations->profiles;
		Profile* profile = NULL;

		String_Array<char>* split_lines = split_string(temporary_arena, file, LINE_DELIMITERS);
		
		for(int i = 0; i < split_lines->num_strings; ++i)
		{
			char* line = split_lines->strings[i];
			line = skip_leading_whitespace(line);

			if(*line == COMMENT || string_is_empty(line))
			{
				// Skip comments and empty lines.
			}
			// Begin a new profile or skip it if the keyword is incorrect.
			else if(!seen_begin_list)
			{
				seen_begin_list = true;
				is_invalid = true;

				String_Array<char>* split_tokens = split_string(temporary_arena, line, TOKEN_DELIMITERS, 1);

				if(split_tokens->num_strings == 2)
				{
					char* type = split_tokens->strings[0];
					char* name = split_tokens->strings[1];

					if(strings_are_equal(type, BEGIN_PROFILE) && !string_is_empty(name))
					{
						profile = &profile_array[num_processed_profiles];
						++num_processed_profiles;

						is_invalid = false;
						profile->name = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, name);
						log_info("Load External Locations: Loading the profile '%s'.", profile->name);
					}
					else
					{
						log_error("Load External Locations: Skipping invalid profile of type '%hs' and name '%hs'.", type, name);
					}
				}
				else
				{
					log_error("Load External Locations: Found %d tokens while looking for a new profile when two were expected.", split_tokens->num_strings);
				}
			}
			// While processing the current profile.
			else if(seen_begin_list)
			{
				// End the current profile (regardless if it was valid or not).
				if(strings_are_equal(line, END_PROFILE))
				{
					seen_begin_list = false;
					is_invalid = false;
				}
				// Skip invalid path lists (unknown list type or missing a name).
				else if(is_invalid)
				{
					// Do nothing until we reach the END keyword.
				}
				// Load the path list in the current profile.
				else
				{
					String_Array<char>* split_tokens = split_string(temporary_arena, line, TOKEN_DELIMITERS, 1);

					if(split_tokens->num_strings == 2)
					{
						char* location_type = split_tokens->strings[0];
						char* path = split_tokens->strings[1];

						#define SET_IF_TYPE(member_type, member_name)\
						if(strings_are_equal(location_type, member_type))\
						{\
							profile->member_name = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);\
						}

						SET_IF_TYPE(LOCATION_DRIVE, drive_path)
						else SET_IF_TYPE(LOCATION_WINDOWS, windows_path)
						else SET_IF_TYPE(LOCATION_TEMPORARY, windows_temporary_path)
						else SET_IF_TYPE(LOCATION_USER_PROFILE, user_profile_path)
						else SET_IF_TYPE(LOCATION_APPDATA, appdata_path)
						else SET_IF_TYPE(LOCATION_LOCAL_APPDATA, local_appdata_path)
						else SET_IF_TYPE(LOCATION_LOCAL_LOW_APPDATA, local_low_appdata_path)
						else SET_IF_TYPE(LOCATION_INTERNET_CACHE, wininet_cache_path)
						else
						{
							log_error("Load External Locations: Unknown location type '%hs'.", location_type);
						}

						#undef SET_IF_TYPE
					}
					else
					{
						log_error("Load External Locations: Found %d tokens while loading the path list when two were expected.", split_tokens->num_strings);
					}
				}
			}
			else
			{
				_ASSERT(false);
			}
		}

		if(seen_begin_list)
		{
			log_warning("Load External Locations: Found unterminated profile location list.");
		}
	}
	else
	{
		log_error("Load External Locations: Failed to load the external locations file '%s'.", exporter->external_locations_file_path);
	}

	clear_arena(temporary_arena);
	unlock_arena(temporary_arena);

	external_locations->num_profiles = num_processed_profiles;
	if(num_processed_profiles != num_profiles)
	{
		log_error("Load External Locations: Loaded %d profiles when %d were expected.", num_processed_profiles, num_profiles);
	}

	exporter->external_locations = external_locations;
}

// A helper function used by export_all_default_or_specific_cache_locations() that exports every supported cache type.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how every cache type should be exported.
//
// @Returns: Nothing.
static void export_all_cache_locations(Exporter* exporter)
{
	export_default_or_specific_internet_explorer_cache(exporter);
	log_newline();

	export_default_or_specific_mozilla_cache(exporter);
	log_newline();

	export_default_or_specific_flash_cache(exporter);
	log_newline();

	export_default_or_specific_shockwave_cache(exporter);
	log_newline();

	export_default_or_specific_java_cache(exporter);
	log_newline();

	export_default_or_specific_unity_cache(exporter);
}

// Entry point for a cache exporter that handles every supported cache type. This function exports from a given number of locations if
// the external locations file was previously loaded. Otherwise, it exports from each cache type's default location.
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information on how every cache type should be exported.
//
// @Returns: Nothing.
static void export_all_default_or_specific_cache_locations(Exporter* exporter)
{
	if(exporter->load_external_locations)
	{
		External_Locations* external_locations = exporter->external_locations;
		_ASSERT(external_locations != NULL);

		console_print("Exporting the cache from the default locations in %d profiles...", external_locations->num_profiles);
		log_info("All Locations: Exporting the cache from the default locations in %d profiles.", external_locations->num_profiles);
		log_newline();

		for(int i = 0; i < external_locations->num_profiles; ++i)
		{
			Profile profile = external_locations->profiles[i];
			exporter->current_profile_name = profile.name;
			console_print("- [%d of %d] Exporting from the profile '%s'...", i+1, external_locations->num_profiles, profile.name);
			
			#define STRING_OR_DEFAULT(str) (str != NULL) ? (str) : (T(""))

			log_print(LOG_NONE, "------------------------------------------------------------");
			log_info("Exporting from the profile '%s' (%I32u).", profile.name, i);
			log_print(LOG_NONE, "------------------------------------------------------------");
			log_print(LOG_NONE, "- Drive Path: '%s'", STRING_OR_DEFAULT(profile.drive_path));
			log_print(LOG_NONE, "- Windows Directory Path: '%s'", STRING_OR_DEFAULT(profile.windows_path));
			log_print(LOG_NONE, "- Windows Temporary Path: '%s'", STRING_OR_DEFAULT(profile.windows_temporary_path));
			log_print(LOG_NONE, "- User Profile Path: '%s'", STRING_OR_DEFAULT(profile.user_profile_path));
			log_print(LOG_NONE, "- Roaming AppData Path: '%s'", STRING_OR_DEFAULT(profile.appdata_path));
			log_print(LOG_NONE, "- Local AppData Path: '%s'", STRING_OR_DEFAULT(profile.local_appdata_path));
			log_print(LOG_NONE, "- LocalLow AppData Path: '%s'", STRING_OR_DEFAULT(profile.local_low_appdata_path));
			log_print(LOG_NONE, "- WinINet Cache Path: '%s'", STRING_OR_DEFAULT(profile.wininet_cache_path));
			log_print(LOG_NONE, "------------------------------------------------------------");
			log_newline();

			#undef STRING_OR_DEFAULT

			bool are_all_locations_valid = true;
			// Helper macro function used to check if all paths don't exceed MAX_PATH_CHARS characters and if all
			// types were specified. Empty paths are allowed (using an empty string or "<None>"), but every path
			// type keyword must always appear.
			#define CHECK_AND_COPY_LOCATION(member_name, location_name)\
			do\
			{\
				if(profile.member_name == NULL)\
				{\
					are_all_locations_valid = false;\
					console_print("This profile will be skipped since the %hs path was not found in the list.", location_name);\
					log_warning("All Locations: The profile '%s' (%I32u) will be skipped since the %hs path was not found in the list.", profile.name, i, location_name);\
				}\
				else if(FAILED(StringCchCopy(exporter->member_name, MAX_PATH_CHARS, profile.member_name)))\
				{\
					are_all_locations_valid = false;\
					console_print("This profile will be skipped since the %hs path is too long.", location_name);\
					log_warning("All Locations: The profile '%s' (%I32u) will be skipped since the %hs path is too long.", profile.name, i, location_name);\
				}\
			} while(false, false)

			CHECK_AND_COPY_LOCATION(drive_path, 				"Drive");
			CHECK_AND_COPY_LOCATION(windows_path, 				"Windows");
			CHECK_AND_COPY_LOCATION(windows_temporary_path, 	"Temporary");
			CHECK_AND_COPY_LOCATION(user_profile_path, 			"User Profile");
			CHECK_AND_COPY_LOCATION(appdata_path, 				"AppData");
			CHECK_AND_COPY_LOCATION(local_appdata_path, 		"Local AppData");
			CHECK_AND_COPY_LOCATION(local_low_appdata_path, 	"Local Low AppData");
			CHECK_AND_COPY_LOCATION(wininet_cache_path, 		"Internet Cache");

			#undef CHECK_AND_COPY_LOCATION

			if(are_all_locations_valid)
			{
				export_all_cache_locations(exporter);
			}

			log_newline();
		}
	}
	else
	{
		_ASSERT(exporter->external_locations == NULL);
		export_all_cache_locations(exporter);
	}
}

// Resolves an absolute path from a different computer using the information from the current profile in the external locations file.
// This function must only be called when the external locations file was passed to the exporter.
//
// For example, given the path "D:\Path\File.ext" with the drive path "C:\Old Drives\Computer A" specified in the external locations file,
// the resulting path is returned as "C:\Old Drives\Computer A\Path\File.ext".
//
// @Parameters:
// 1. exporter - The Exporter structure which contains information from the external locations file.
// 2. full_path - The absolute path to resolve.
// 3. result_path - The buffer which receives the resolved path. This buffer must be able to hold MAX_PATH_CHARS characters.
//
// @Returns: True if the path was resolved successfully. Otherwise, false. This function returns false if the given path is not absolute.
bool resolve_exporter_external_locations_path(Exporter* exporter, const TCHAR* full_path, TCHAR* result_path)
{
	_ASSERT(exporter->load_external_locations);

	if(PathIsRelative(full_path) != FALSE)
	{
		_ASSERT(false);
		log_error("Resolve Exporter External Locations Path: Attempted to resolve the relative path '%s' when an absolute one was expected.", full_path);
		return false;
	}

	size_t num_chars = string_length(full_path);
	const size_t NUM_DRIVE_CHARS = 3;
	return (num_chars >= NUM_DRIVE_CHARS) && (PathCombine(result_path, exporter->drive_path, full_path + NUM_DRIVE_CHARS) != FALSE);
}
