#include "web_cache_exporter.h"

#include "internet_explorer.h"
#include "flash_plugin.h"
#include "shockwave_plugin.h"
#include "java_plugin.h"

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
	so it may not do certain things in the most optimal way.

	- The exporters are located in a .cpp file called "<Browser Name>.cpp" for web browsers and "<Plugin Name>_plugin.cpp" for web
	plugins. For example, "internet_explorer.cpp" and "shockwave_plugin.cpp". Each one is free to implement how they read their
	respective cache formats in the way that best suits the data, exposing one function called export_specific_or_default_<Name>_cache
	that takes the Exporter as a parameter.

	- The "memory_and_file_io.cpp" file defines functions for memory management, file I/O, date time formatting, string, path, and
	URL manipulation, etc. The "custom_groups.cpp" file defines the functions used to load .group files, and match each cache entry
	to a file or URL group. These are simple text files that allow you to label each cache entry based on their MIME types, file
	extensions, file signatures, and URLs. These are useful to identify files that belong to web plugins like Flash or Shockwave,
	or that came from certain websites like gaming portals.

	- When working with intermediary strings, this application will use narrow ANSI strings on Windows 98 and ME, and wide UTF-16
	strings on Windows 2000 to 10. Any files that are stored on disk use UTF-8 as the character encoding. This includes source
	files, READMEs, group files, CSV files, the log file, etc.

	- All Windows paths that are used by this application are limited to MAX_PATH (260) characters. Some exporter functions used
	to extended this limit on the Windows 2000 to 10 builds by using the "\\?\" prefix. However, not all functions in the Win32
	API support it (e.g. the Shell API), and it can also be difficult to delete any files or directories that exceed this limit
	on the supported Windows versions. Since the purpose of this application is to recover lost web media by asking the average
	user to check their cache, this behavior is not desirable.

	@Author: João Henggeler
*/

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> EXPORTER SETUP
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

static const TCHAR* LOG_FILE_NAME = TEXT("WCE.log");
static const TCHAR* DEFAULT_EXPORT_DIRECTORY_NAME = TEXT("ExportedCache");
static const char* COMMAND_LINE_HELP_MESSAGE = 	"Usage: WCE.exe [Optional Arguments] <Export Argument>\n"
												"\n"
												"Below are some commonly used arguments. To see the full list of arguments, check the readme.txt file.\n"
												"\n"
												"########## [1] EXPORT ARGUMENTS: <Export Option> [Optional Cache Path] [Optional Output Path]\n"
												"\n"
												"If you specify an empty path, then a default location is used.\n"
												"\n"
												"-export-ie    exports the WinINet cache, including Internet Explorer 4 to 11.\n"
												"\n"
												"-export-flash    exports the Flash Player cache.\n"
												"\n"
												"-export-shockwave    exports the Shockwave Player cache.\n"
												"\n"
												"-export-java    exports the Java Plugin cache.\n"
												"\n"
												"########## [1] EXAMPLES:\n"
												"\n"
												"WCE.exe -export-ie\n"
												"WCE.exe -export-ie \"C:\\PathToTheCache\"\n"
												"WCE.exe -export-ie \"C:\\PathToTheCache\" \"My Cache\"\n"
												"WCE.exe -export-ie \"\" \"My Cache\"    (choose the output path but use the default cache path)\n"
												"WCE.exe -export-ie \"C:\\PathToTheCache\" \"\"    (choose the cache path but use the default output path)\n"
												"\n"
												"\n"
												"########## [2] OPTIONAL ARGUMENTS: Put them before the export option.\n"
												"\n"
												"-no-copy-files    stops the exporter from copying files.\n"
												"\n"
												"-no-create-csv    stops the exporter from creating CSV files.\n"
												"\n"
												"-overwrite    deletes the previous output folder before running.\n"
												"\n"
												"-filter-by-groups    only exports files that match any loaded groups.\n"
												"\n"
												"########## [2] EXAMPLES:\n"
												"\n"
												"WCE.exe -no-copy-files -export-flash\n"
												"WCE.exe -overwrite -no-create-csv -export-shockwave\n"
												"WCE.exe -filter-by-groups -export-java"
												;

// Skips to the second dash in a command line argument. For example, "-export-ie" -> "-ie".
//
// @Parameters:
// 1. str - The command line argument string.
//
// @Returns: The beginning of the second option in the command line argument. This function returns NULL if this suboption doesn't exist.
static TCHAR* skip_to_suboption(TCHAR* str)
{
	TCHAR* suboption = NULL;

	if(*str == TEXT('-'))
	{
		++str;
	}

	while(*str != TEXT('\0') && *str != TEXT('-'))
	{
		++str;
	}

	suboption = (*str != TEXT('\0')) ? (str) : (NULL);

	return suboption;
}

// Parses the application's command line arguments and sets the resulting Exporter structure's members accordingly.
//
// @Parameters:
// 1. num_arguments - The number of command line arguments.
// 2. arguments - The command line arguments. This must be the unmodified string array that is passed to main().
// 3. exporter - The resulting Exporter structure. This must be cleared to zero before calling this function.
//
// @Returns: True if every parsed argument was correct. Otherwise, it returns false and the application should terminate.
static bool parse_exporter_arguments(int num_arguments, TCHAR* arguments[], Exporter* exporter)
{
	bool success = true;
	bool seen_export_option = false;

	Arena* arena = &(exporter->temporary_arena);

	// Set any options that shouldn't be zero, false, or empty strings by default.
	exporter->should_copy_files = true;
	exporter->should_create_csv = true;
	exporter->csv_file_handle = INVALID_HANDLE_VALUE;

	// Skip the first argument which contains the executable's name.
	for(int i = 1; i < num_arguments; ++i)
	{
		TCHAR* option = arguments[i];

		if(strings_are_equal(option, TEXT("-no-copy-files")))
		{
			exporter->should_copy_files = false;
		}
		else if(strings_are_equal(option, TEXT("-no-create-csv")))
		{
			exporter->should_create_csv = false;
		}
		else if(strings_are_equal(option, TEXT("-overwrite")))
		{
			exporter->should_overwrite_previous_output = true;
		}
		else if(strings_are_equal(option, TEXT("-show-full-paths")))
		{
			exporter->should_show_full_paths = true;
		}
		else if(strings_are_equal(option, TEXT("-filter-by-groups")))
		{
			exporter->should_filter_by_groups = true;
		}
		else if(strings_are_equal(option, TEXT("-load-group-files")))
		{
			exporter->should_load_specific_groups_files = true;

			if(i+1 < num_arguments)
			{
				TCHAR* group_filenames = push_string_to_arena(arena, arguments[i+1]);
				i += 1;

				const TCHAR* FILENAME_DELIMITER = TEXT(" ");
				TCHAR* remaining_filenames = NULL;
				TCHAR* filename = _tcstok_s(group_filenames, FILENAME_DELIMITER, &remaining_filenames);

				u32 num_filenames = 0;
				TCHAR* first_filename = push_arena(arena, 0, TCHAR);
				while(filename != NULL)
				{
					++num_filenames;
					size_t size = string_size(filename);
					push_and_copy_to_arena(arena, size, u8, filename, size);

					filename = _tcstok_s(NULL, FILENAME_DELIMITER, &remaining_filenames);					
				}

				exporter->num_group_filenames_to_load = num_filenames;
				exporter->group_filenames_to_load = build_array_from_contiguous_strings(arena, first_filename, num_filenames);
			}			
		}
		else if(strings_are_equal(option, TEXT("-hint-ie")))
		{
			exporter->should_use_ie_hint = true;
			if(i+1 < num_arguments)
			{
				StringCchCopy(exporter->ie_hint_path, MAX_PATH_CHARS, arguments[i+1]);
				i += 1;
			}
		}
		else if(strings_are_equal(option, TEXT("-explore-files")))
		{
			exporter->cache_type = CACHE_EXPLORE;

			if(i+1 < num_arguments && !string_is_empty(arguments[i+1]))
			{
				StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, arguments[i+1]);
			}
			else
			{
				success = false;
				console_print("The -explore-files option requires a non-empty path.");
				log_print(LOG_ERROR, "Argument Parsing: The -explore-files option was given a non-empty path.");
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
		else if(strings_are_equal(option, TEXT("-find-and-export-all")))
		{
			exporter->cache_type = CACHE_ALL;

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
				StringCchCopy(exporter->external_locations_path, MAX_PATH_CHARS, arguments[i+2]);
				exporter->should_load_external_locations = true;
			}
	
			exporter->is_exporting_from_default_locations = true;
			seen_export_option = true;
			break;
		}
		else if(string_starts_with(option, TEXT("-export")))
		{
			TCHAR* cache_type = skip_to_suboption(option);

			if(cache_type == NULL)
			{
				console_print("Missing web cache type in command line option '%s'.", option);
				log_print(LOG_ERROR, "Argument Parsing: Missing web cache type in command line option '%s'", option);
				exporter->cache_type = CACHE_UNKNOWN;
				success = false;
			}
			else if(strings_are_equal(cache_type, TEXT("-ie")))
			{
				exporter->cache_type = CACHE_INTERNET_EXPLORER;
			}
			else if(strings_are_equal(cache_type, TEXT("-flash")))
			{
				exporter->cache_type = CACHE_FLASH_PLUGIN;
			}
			else if(strings_are_equal(cache_type, TEXT("-shockwave")))
			{
				exporter->cache_type = CACHE_SHOCKWAVE_PLUGIN;
			}
			else if(strings_are_equal(cache_type, TEXT("-java")))
			{
				exporter->cache_type = CACHE_JAVA_PLUGIN;
			}
			else
			{
				console_print("Unknown web cache type '%s' in command line option '%s'.", cache_type, option);
				log_print(LOG_ERROR, "Argument Parsing: Unknown web cache type '%s' in command line option '%s'", cache_type, option);
				exporter->cache_type = CACHE_UNKNOWN;
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
		#ifdef DEBUG
			else if(strings_are_equal(option, TEXT("-debug-assert")))
			{
				debug_log_print("Argument Parsing: Forcing failed assertion.");
				_ASSERT(false);
				success = false;
				seen_export_option = true;
				break;
			}
		#endif
		else
		{
			console_print("Unknown command line option '%s'.", option);
			log_print(LOG_ERROR, "Argument Parsing: Unknown command line option '%s'", option);
			success = false;
			break;
		}
	}

	if(!seen_export_option)
	{
		console_print("Missing the an export option.");
		log_print(LOG_ERROR, "Argument Parsing: The main -export option was not found.");
		success = false;
	}

	if(!exporter->should_copy_files && !exporter->should_create_csv)
	{
		console_print("The options -no-copy-files and no-create-csv can't be used at the same time.");
		log_print(LOG_ERROR, "Argument Parsing: The options '-no-copy-files' and '-no-create-csv' were used at the same time.");
		success = false;
	}

	if(exporter->should_load_specific_groups_files && exporter->num_group_filenames_to_load == 0)
	{
		console_print("The -load-group-files option requires one or more group filenames as its argument.");
		log_print(LOG_ERROR, "Argument Parsing: The -load-group-files option was used but the supplied value does not contain filenames.");
		success = false;
	}

	if(exporter->should_load_external_locations)
	{
		if(string_is_empty(exporter->external_locations_path))
		{
			console_print("The second argument in the -find-and-export-all option requires a non-empty path.");
			log_print(LOG_ERROR, "Argument Parsing: The -find-and-export-all option was used with the external locations argument but the supplied path was empty.");
			success = false;
		}
		else if(!does_file_exist(exporter->external_locations_path))
		{
			console_print("The external locations file in the -find-and-export-all option doesn't exist.");
			log_print(LOG_ERROR, "Argument Parsing: The -find-and-export-all option supplied an external locations file path that doesn't exist: '%s'.", exporter->external_locations_path);
			success = false;
		}
	}

	if(exporter->should_use_ie_hint && string_is_empty(exporter->ie_hint_path))
	{
		console_print("The -hint-ie option requires a non-empty path as its argument.");
		log_print(LOG_ERROR, "Argument Parsing: The -hint-ie option was used but the supplied path was empty.");
		success = false;
	}

	return success;
}

// Retrieves the size of the temporary memory in bytes, based on the current Windows version. This size is twice as large for the
// Windows 2000 through 10 builds in order to store wide UTF-16 strings.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current Windows version. The 'os_version' member must be set before calling
// this function.
//
// @Returns: The number of bytes to allocate for the temporary memory.
static size_t get_temporary_memory_size_for_os_version(Exporter* exporter)
{
	OSVERSIONINFO os_version = exporter->os_version;
	size_t size_for_os_version = 0;

	// Windows 98 (4.10)
	if(os_version.dwMajorVersion <= 4 && os_version.dwMinorVersion <= 10)
	{
		size_for_os_version = kilobytes_to_bytes(512); // x1 for char
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
	// Windows Vista (6.0), 7 (6.1), 8.1 (6.3), and 10 (10.0).
	else if(os_version.dwMajorVersion >= 6)
	{
		size_for_os_version = megabytes_to_bytes(4); // x2 for wchar_t
	}
	else
	{
		size_for_os_version = megabytes_to_bytes(3); // x1 or x2 for TCHAR
		log_print(LOG_WARNING, "Get Startup Memory Size: Using %Iu bytes for the unhandled Windows version %lu.%lu.",
								size_for_os_version, os_version.dwMajorVersion, os_version.dwMinorVersion);
	}

	return size_for_os_version * sizeof(TCHAR);
}

// Performs any clean up operations before this application terminates. This includes deleting the exporter's temporary directory,
// freeing any loaded library modules, deallocating the permanent and temporary memory, and closing the log file.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the necessary information to perform these clean up operations.
//
// @Returns: Nothing.
static void clean_up(Exporter* exporter)
{
	if(exporter->was_temporary_exporter_directory_created)
	{
		if(!delete_directory_and_contents(exporter->exporter_temporary_path))
		{
			console_print("Warning: Failed to delete the temporary exporter directory located in '%s'.\nYou may want to delete this directory yourself.", exporter->exporter_temporary_path);
			log_print(LOG_ERROR, "Clean Up: Failed to delete the temporary exporter directory in '%s'.", exporter->exporter_temporary_path);
		}
	}

	#ifndef BUILD_9X
		if( (exporter->cache_type == CACHE_INTERNET_EXPLORER) || (exporter->cache_type == CACHE_ALL) )
		{
			windows_nt_free_esent_functions();
			windows_nt_free_ntdll_functions();
			windows_nt_free_kernel32_functions();
		}
	#endif

	destroy_arena( &(exporter->secondary_temporary_arena) );
	destroy_arena( &(exporter->temporary_arena) );
	destroy_arena( &(exporter->permanent_arena) );

	close_log_file();
}

/*
	The Web Cache Exporter's entry point. Order of operations:
	
	>>>> 1. Create the log file.
	>>>> 2. Find the current Windows version, Internet Explorer version, and ANSI code page.
	>>>> 3. Create the temporary memory arena based on the current Windows version. On error, terminate.
	>>>> 4. Check if any command line options were passed. If not, terminate.
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

static size_t get_total_external_locations_size(Exporter* exporter, u32* result_num_profiles);
static void load_external_locations(Exporter* exporter, u32 num_profiles);
static void export_all_default_or_specific_cache_locations(Exporter* exporter);

int _tmain(int argc, TCHAR* argv[])
{
	console_print("Web Cache Exporter v%hs", EXPORTER_BUILD_VERSION);

	Exporter exporter = {};	

	if(!create_log_file(LOG_FILE_NAME))
	{
		console_print("Error: Failed to create the log file.");
	}

	log_print(LOG_INFO, "Startup: Running the Web Cache Exporter %hs version %hs in %hs mode.",
							EXPORTER_BUILD_TARGET, EXPORTER_BUILD_VERSION, EXPORTER_BUILD_MODE);

	exporter.os_version.dwOSVersionInfoSize = sizeof(exporter.os_version);

	// Disable the deprecation warnings for GetVersionExW() when building with Visual Studio 2015 or later.
	// This project is always built using Visual Studio 2005, but we'll do this anyways in case someone wants
	// to compile this application with a more modern version of Visual Studio.
	#pragma warning(push)
	#pragma warning(disable : 4996)
	if(GetVersionEx(&exporter.os_version))
	#pragma warning(pop)
	{
		log_print(LOG_INFO, "Startup: Running Windows version %lu.%lu '%s' build %lu in platform %lu.",
								exporter.os_version.dwMajorVersion, exporter.os_version.dwMinorVersion,
								exporter.os_version.szCSDVersion, exporter.os_version.dwBuildNumber,
								exporter.os_version.dwPlatformId);
	}
	else
	{
		log_print(LOG_ERROR, "Startup: Failed to get the current Windows version with the error code %lu.", GetLastError());
		exporter.os_version.dwMajorVersion = ULONG_MAX;
		exporter.os_version.dwMinorVersion = ULONG_MAX;
	}

	{
		const size_t NUM_IE_VERSION_CHARS = 32;
		TCHAR ie_version[NUM_IE_VERSION_CHARS] = TEXT("");
		if(find_internet_explorer_version(ie_version, sizeof(ie_version)))
		{
			log_print(LOG_INFO, "Startup: Running Internet Explorer version %s.", ie_version);
		}
		else
		{
			log_print(LOG_ERROR, "Startup: Failed to get Internet Explorer's version with the error code %lu.", GetLastError());
		}
		
		log_print(LOG_INFO, "Startup: The current Windows ANSI code page identifier is %u.", GetACP());
	}

	if(argc <= 1)
	{
		console_print("%hs", COMMAND_LINE_HELP_MESSAGE);

		log_print(LOG_ERROR, "No command line arguments supplied. The program will print a help message and terminate.");
		clean_up(&exporter);
		return 1;
	}

	{
		size_t temporary_memory_size = get_temporary_memory_size_for_os_version(&exporter);
		log_print(LOG_INFO, "Startup: Allocating %Iu bytes for the temporary memory arena.", temporary_memory_size);

		if(!create_arena(&exporter.temporary_arena, temporary_memory_size))
		{
			console_print("Could not allocate enough temporary memory to run the program.");
			log_print(LOG_ERROR, "Startup: Could not allocate %Iu bytes to run the program.", temporary_memory_size);
			clean_up(&exporter);
			return 1;
		}

		#ifdef BUILD_9X
			// Create a smaller, secondary memory arena for Windows 98 and ME. This will be used when loading group files.
			temporary_memory_size /= 10;
			log_print(LOG_INFO, "Startup: Allocating %Iu bytes for the secondary temporary memory arena.", temporary_memory_size);

			if(!create_arena(&exporter.secondary_temporary_arena, temporary_memory_size))
			{
				console_print("Could not allocate enough temporary memory to run the program.");
				log_print(LOG_ERROR, "Startup: Could not allocate %Iu bytes to run the program.", temporary_memory_size);
				clean_up(&exporter);
				return 1;
			}
		#endif
	}

	if(!parse_exporter_arguments(argc, argv, &exporter))
	{
		log_print(LOG_ERROR, "Startup: An error occured while parsing the command line arguments. The program will terminate.");
		clean_up(&exporter);
		return 1;
	}

	{
		if(GetModuleFileName(NULL, exporter.executable_path, MAX_PATH_CHARS) != 0)
		{
			// Remove the executable's name from the path.
			PathAppend(exporter.executable_path, TEXT(".."));
		}
		else
		{
			log_print(LOG_ERROR, "Startup: Failed to get the executable directory path with error code %lu.", GetLastError());
		}

		u32 num_groups = 0;
		u32 num_profiles = 0;
		
		size_t permanent_memory_size = 	get_total_group_files_size(&exporter, &num_groups);
		if(exporter.should_load_external_locations)
		{
			permanent_memory_size += get_total_external_locations_size(&exporter, &num_profiles);
		}

		log_print(LOG_INFO, "Startup: Allocating %Iu bytes for the permanent memory arena.", permanent_memory_size);

		if(!create_arena(&exporter.permanent_arena, permanent_memory_size))
		{
			console_print("Could not allocate enough permanent memory to run the program.");
			log_print(LOG_ERROR, "Startup: Could not allocate %Iu bytes to run the program.", permanent_memory_size);
			clean_up(&exporter);
			return 1;
		}

		log_print(LOG_INFO, "Startup: Loading %I32u groups.", num_groups);
		load_all_group_files(&exporter, num_groups);

		if(exporter.should_load_external_locations)
		{
			log_print(LOG_INFO, "Startup: Loading %I32u profiles from the external locations file '%s'.", num_profiles, exporter.external_locations_path);
			load_external_locations(&exporter, num_profiles);			
		}

		log_print(LOG_INFO, "Startup: The permanent memory arena is at %.2f%% used capacity.", get_used_arena_capacity(&exporter.permanent_arena));
	}

	#ifndef BUILD_9X
		if( (exporter.cache_type == CACHE_INTERNET_EXPLORER) || (exporter.cache_type == CACHE_ALL) )
		{
			log_print(LOG_INFO, "Startup: Dynamically loading any necessary functions.");
			windows_nt_load_kernel32_functions();
			windows_nt_load_ntdll_functions();
			windows_nt_load_esent_functions();
		}
	#endif

	if(GetWindowsDirectory(exporter.windows_path, MAX_PATH_CHARS) == 0)
	{
		log_print(LOG_ERROR, "Startup: Failed to get the Windows directory path with error code %lu.", GetLastError());
	}

	if(GetTempPath(MAX_PATH_CHARS, exporter.windows_temporary_path) != 0)
	{
		log_print(LOG_INFO, "Startup: Deleting any previous temporary exporter directories with the prefix '%s'.", TEMPORARY_NAME_PREFIX);
		delete_all_temporary_directories(exporter.windows_temporary_path);

		if(create_temporary_directory(exporter.windows_temporary_path, exporter.exporter_temporary_path))
		{
			exporter.was_temporary_exporter_directory_created = true;
			log_print(LOG_INFO, "Startup: Created the temporary exporter directory in '%s'.", exporter.exporter_temporary_path);
		}
		else
		{
			log_print(LOG_ERROR, "Startup: Failed to create the temporary exporter directory with error code %lu.", GetLastError());
		}
	}
	else
	{
		log_print(LOG_ERROR, "Startup: Failed to get the Temporary Files directory path with error code %lu.", GetLastError());
	}

	if(!get_special_folder_path(CSIDL_PROFILE, exporter.user_profile_path))
	{
		log_print(LOG_ERROR, "Startup: Failed to get the user profile directory path with error code %lu.", GetLastError());
	}

	if(!get_special_folder_path(CSIDL_APPDATA, exporter.appdata_path))
	{
		log_print(LOG_ERROR, "Startup: Failed to get the roaming application data directory path with error code %lu.", GetLastError());
	}

	if(get_special_folder_path(CSIDL_LOCAL_APPDATA, exporter.local_appdata_path))
	{
		StringCchCopy(exporter.local_low_appdata_path, MAX_PATH_CHARS, exporter.local_appdata_path);
		PathAppend(exporter.local_low_appdata_path, TEXT("..\\LocalLow"));
	}
	else
	{
		log_print(LOG_ERROR, "Startup: Failed to get the local application data directory path with error code %lu.", GetLastError());
	}

	if(!get_special_folder_path(CSIDL_INTERNET_CACHE, exporter.wininet_cache_path))
	{
		log_print(LOG_ERROR, "Startup: Failed to get the Temporary Internet Files cache directory path with the error code %lu.", GetLastError());
	}

	if(exporter.is_exporting_from_default_locations && (exporter.cache_type != CACHE_ALL))
	{
		log_print(LOG_INFO, "Startup: No cache path specified. Exporting the cache from any existing default directories.");
	}

	if(exporter.should_overwrite_previous_output)
	{
		TCHAR* directory_name = PathFindFileName(exporter.output_path);
		console_print("Deleting the previous output directory '%s' before starting...", directory_name);

		if(delete_directory_and_contents(exporter.output_path))
		{
			console_print("Deleted the previous output directory successfully.");
			log_print(LOG_INFO, "Startup: Deleted the previous output directory successfully.");
		}
		else
		{	
			console_print("Warning: Could not delete the previous output directory.");
			log_print(LOG_ERROR, "Startup: Failed to delete the previous output directory '%s'.", directory_name);
		}
	}

	// The temporary arena should be cleared before any cache exporter runs. Any data that needs to stick around
	// should be stored in the permanent arena.
	log_print(LOG_INFO, "Startup: The temporary memory arena is at %.2f%% used capacity before exporting files.", get_used_arena_capacity(&exporter.temporary_arena));
	clear_arena(&(exporter.temporary_arena));

	log_print_newline();

	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_INFO, "Exporter Options:");
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Cache Type: %s", CACHE_TYPE_TO_STRING[exporter.cache_type]);
	log_print(LOG_NONE, "- Should Copy Files: %hs", (exporter.should_copy_files) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Create CSV: %hs", (exporter.should_create_csv) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Overwrite Previous Output: %hs", (exporter.should_overwrite_previous_output) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Show Full Paths: %hs", (exporter.should_show_full_paths) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Filter By Groups: %hs", (exporter.should_filter_by_groups) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Load Specific Groups: %hs", (exporter.should_load_specific_groups_files) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Number Of Groups To Load: %Iu", exporter.num_group_filenames_to_load);
	log_print(LOG_NONE, "- Should Use Internet Explorer's Hint: %hs", (exporter.should_use_ie_hint) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Internet Explorer Hint Path: '%s'", exporter.ie_hint_path);
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Should Load External Locations: %hs", (exporter.should_load_external_locations) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- External Locations Path: '%s'", exporter.external_locations_path);
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Cache Path: '%s'", exporter.cache_path);
	log_print(LOG_NONE, "- Output Path: '%s'", exporter.output_path);
	log_print(LOG_NONE, "- Is Exporting From Default Locations: %hs", (exporter.is_exporting_from_default_locations) ? ("Yes") : ("No"));
	
	log_print_newline();

	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_INFO, "Current Locations:");
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Executable Path: '%s'", exporter.executable_path);
	log_print(LOG_NONE, "- Exporter Temporary Path: '%s'", exporter.exporter_temporary_path);
	log_print(LOG_NONE, "- Was Temporary Directory Created: %hs", (exporter.was_temporary_exporter_directory_created) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "------------------------------------------------------------");
	log_print(LOG_NONE, "- Windows Directory Path: '%s'", exporter.windows_path);
	log_print(LOG_NONE, "- Windows Temporary Path: '%s'", exporter.windows_temporary_path);
	log_print(LOG_NONE, "- User Profile Path: '%s'", exporter.user_profile_path);
	log_print(LOG_NONE, "- Roaming AppData Path: '%s'", exporter.appdata_path);
	log_print(LOG_NONE, "- Local AppData Path: '%s'", exporter.local_appdata_path);
	log_print(LOG_NONE, "- LocalLow AppData Path: '%s'", exporter.local_low_appdata_path);
	log_print(LOG_NONE, "- WinINet Cache Path: '%s'", exporter.wininet_cache_path);

	log_print_newline();

	switch(exporter.cache_type)
	{
		case(CACHE_INTERNET_EXPLORER):
		{
			export_specific_or_default_internet_explorer_cache(&exporter);
		} break;

		case(CACHE_FLASH_PLUGIN):
		{
			export_specific_or_default_flash_plugin_cache(&exporter);
		} break;

		case(CACHE_SHOCKWAVE_PLUGIN):
		{
			export_specific_or_default_shockwave_plugin_cache(&exporter);
		} break;

		case(CACHE_JAVA_PLUGIN):
		{
			export_specific_or_default_java_plugin_cache(&exporter);
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
			log_print(LOG_ERROR, "Startup: Attempted to export the cache from '%s' using the unhandled cache type %d.", exporter.cache_path, exporter.cache_type);
		} break;
	}

	console_print("Finished running:\n- Created %Iu CSV files.\n- Processed %Iu cached files.\n- Copied %Iu cached files.\n- Assigned names to %Iu files.", exporter.num_csv_files_created, exporter.num_processed_files, exporter.num_copied_files, exporter.num_nameless_files);
	log_print_newline();
	log_print(LOG_INFO, "Finished Running: Created %Iu CSV files. Processed %Iu cache entries. Copied %Iu cached files. Assigned names to %Iu files.", exporter.num_csv_files_created, exporter.num_processed_files, exporter.num_copied_files, exporter.num_nameless_files);
	
	clean_up(&exporter);

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
// - determining the fully qualified version of the cache path.
// - resolving the exporter's output paths for copying cache entries and creating CSV files.
// - creating a CSV file with a given header.
//
// This function should be called by each exporter before processing any cached files, and may be called multiple times by the same
// exporter. After finishing exporting, the terminate_cache_exporter() function should be called.
//
// @Parameters:
// 1. exporter - The Exporter structure where the resolved paths and CSV file's handle will be stored.
// 2. cache_identifier - The name of the output directory (for copying files) and the CSV file.
// 3. column_types - The array of column types used to determine the column names for the CSV file.
// 4. num_columns - The number of elements in this array.
// 
// @Returns: Nothing.
void initialize_cache_exporter(	Exporter* exporter, const TCHAR* cache_identifier,
								const Csv_Type* column_types, size_t num_columns)
{
	exporter->cache_identifier = cache_identifier;
	exporter->csv_column_types = column_types;
	exporter->num_csv_columns = num_columns;

	Arena* temporary_arena = &(exporter->temporary_arena);

	get_full_path_name(exporter->cache_path);
	get_full_path_name(exporter->output_path);
	
	set_exporter_output_copy_subdirectory(exporter, NULL);
	
	// Don't use PathCombine() since we're just adding a file extension to the previous path.
	StringCchCopy(exporter->output_csv_path, MAX_PATH_CHARS, exporter->output_copy_path);
	StringCchCat(exporter->output_csv_path, MAX_PATH_CHARS, TEXT(".csv"));

	if(exporter->should_create_csv)
	{
		_ASSERT(exporter->csv_file_handle == INVALID_HANDLE_VALUE);

		bool create_csv_success = false;
		u32 num_retry_attempts = 0;
		const u32 MAX_RETRY_ATTEMPTS = 10;

		do
		{
			create_csv_success = create_csv_file(exporter->output_csv_path, &(exporter->csv_file_handle));

			if(create_csv_success)
			{
				++(exporter->num_csv_files_created);
				csv_print_header(temporary_arena, exporter->csv_file_handle, column_types, num_columns);
				clear_arena(temporary_arena);
			}
			else
			{
				const u32 SLEEP_TIME_IN_SECONDS = 3;
				const u32 SLEEP_TIME_IN_MILLISECONDS = SLEEP_TIME_IN_SECONDS * 1000;

				++num_retry_attempts;
				log_print(LOG_ERROR, "Initialize Cache Exporter: Failed to create the CSV file '%s' with the error code %lu. Waiting %I32u seconds and retrying this operation (attempt %I32u of %I32u).", exporter->output_csv_path, GetLastError(), SLEEP_TIME_IN_SECONDS, num_retry_attempts, MAX_RETRY_ATTEMPTS);

				Sleep(SLEEP_TIME_IN_MILLISECONDS);
			}

		} while(!create_csv_success && num_retry_attempts < MAX_RETRY_ATTEMPTS);

		if(num_retry_attempts > 0)
		{
			if(create_csv_success)
			{
				log_print(LOG_WARNING, "Initialize Cache Exporter: Reached %I32u retry attempts before creating the CSV file '%s'.", num_retry_attempts, exporter->output_csv_path);
			}
			else
			{
				log_print(LOG_ERROR, "Initialize Cache Exporter: Failed to create the CSV file '%s' after %I32u retry attempts.", exporter->output_csv_path, num_retry_attempts);
			}
		}

	}
}

// Builds a cache exporter's output path for copying files and adds a given subdirectory's name to the end.
//
// This function is called by initialize_cache_exporter() to set the default output copy path for each cache exporter, and may be
// optionally called later to create more specific subdirectories. This function should be called after initialize_cache_exporter()
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
	
	if(exporter->should_load_external_locations)
	{
		PathAppend(exporter->output_copy_path, exporter->current_profile_name);
	}

	PathAppend(exporter->output_copy_path, exporter->cache_identifier);

	if(subdirectory_name != NULL)
	{
		PathAppend(exporter->output_copy_path, subdirectory_name);
	}
}

// Exports a cache entry by copying its file to the output location using the original website's directory structure, and by adding a
// new row to the CSV file. This function will also match the cache entry to any loaded group files.
//
// This function should be called after initialize_cache_exporter() and before terminate_cache_exporter().
//
// The following CSV columns are automatically handled by this function, and don't need to be set explicitly:
//
// - CSV_CUSTOM_FILE_GROUP - determined using the 'full_entry_path' parameter, and the CSV_CONTENT_TYPE and CSV_FILE_EXTENSION columns.
// - CSV_CUSTOM_URL_GROUP - determined using the 'entry_url' parameter.
//
// The following values and columns are also changed if the optional parameter 'optional_find_data' is used:
// - CSV_FILE_SIZE - determined using the 'nFileSizeHigh' and 'nFileSizeLow' members.
// - CSV_LAST_WRITE_TIME - determined using the 'ftLastWriteTime' member.
// - CSV_CREATION_TIME - determined using the 'ftCreationTime' member.
// - CSV_LAST_ACCESS_TIME - determined using the 'ftLastAccessTime' member.
//
// - CSV_FILENAME - determined using the 'entry_filename' parameter, or the 'cFileName' member if 'optional_find_data' is set.
// - CSV_FILE_EXTENSION - determined using the value above.
// - CSV_URL - determined using the 'entry_url' parameter.
// - CSV_LOCATION_ON_DISK - determined using the 'full_entry_path' parameter.
// - CSV_MISSING_FILE - determined using the 'full_entry_path' parameter.
//
// - CSV_LOCATION_ON_CACHE - replaced with 'full_entry_path' if the exporter option 'should_show_full_paths' is true.
//
// If these columns should be automatically handled, their corresponding array element must be set to {NULL}. For CSV columns that
// aren't related to group files, you can override this behavior by explicitly setting their value instead of using NULL.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the current cache exporter's parameters and the values of command line options
// that will influence how the entry is exported.
// 2. column_values - The array of values to write. Some values don't need to set explicitly if their respective column type is handled
// automatically.
// 3. full_entry_path - The absolute path to the cached file to copy. This file may or may not exist on disk. This parameter shouldn't
// be NULL.
// 4. entry_url - The cached file's original URL. This is used to build the copy destination's directory structure. This parameter may
// be NULL.
// 5. entry_filename - The cached file's original filename. This value is used to determine the copy destination's filename. This
// parameter shouldn't be NULL.
// 6. optional_find_data - An optional parameter that specifies a WIN32_FIND_DATA structure to use to fill some columns. This value
// defaults to NULL.
// 
// @Returns: Nothing.
void export_cache_entry(Exporter* exporter, Csv_Entry* column_values,
						TCHAR* full_entry_path, TCHAR* entry_url, TCHAR* entry_filename,
						WIN32_FIND_DATA* optional_find_data)
{
	_ASSERT(full_entry_path != NULL);

	if(optional_find_data != NULL) entry_filename = optional_find_data->cFileName;
	
	TCHAR unique_filename[MAX_PATH_CHARS] = TEXT("");
	if(entry_filename == NULL)
	{
		++(exporter->num_nameless_files);
		StringCchPrintf(unique_filename, MAX_PATH_CHARS, TEXT("__WCE-%Iu"), exporter->num_nameless_files);
		entry_filename = unique_filename;
	}

	Arena* temporary_arena = &(exporter->temporary_arena);

	bool file_exists = does_file_exist(full_entry_path);
	++(exporter->num_processed_files);

	Matchable_Cache_Entry entry_to_match = {};
	entry_to_match.full_file_path = full_entry_path;
	entry_to_match.url_to_match = entry_url;

	SSIZE_T file_group_index = -1;
	SSIZE_T url_group_index = -1;

	for(size_t i = 0; i < exporter->num_csv_columns; ++i)
	{
		TCHAR* value = column_values[i].value;

		switch(exporter->csv_column_types[i])
		{
			/*
				@CustomGroups: Used to fill the custom group columns.
				
				@FindData: Uses the values from the 'find_data' parameter if it exists, and if the column value in question
				is not NULL.
				
				@UseFunctionParameter: Uses one of the three parameters (full_entry_path, entry_url, entry_filename) if the
				column value in question is not NULL.
			*/

			// @CustomGroups
			case(CSV_CUSTOM_FILE_GROUP):
			{
				file_group_index = i;
			} break;

			// @CustomGroups
			case(CSV_CUSTOM_URL_GROUP):
			{
				url_group_index = i;
			} break;

			// @CustomGroups
			case(CSV_CONTENT_TYPE):
			{
				entry_to_match.mime_type_to_match = value;
			} break;

			// @CustomGroups @FindData
			case(CSV_FILE_EXTENSION):
			{
				if(value == NULL) column_values[i].value = skip_to_file_extension(entry_filename);
				// Note that the value may change here.
				entry_to_match.file_extension_to_match = column_values[i].value;
			} break;

			// @FindData @UseFunctionParameter
			case(CSV_FILENAME):
			{
				if(value == NULL) column_values[i].value = entry_filename;
			} break;

			// @UseFunctionParameter
			case(CSV_URL):
			{
				if(value == NULL) column_values[i].value = entry_url;
			} break;

			// @UseFunctionParameter
			case(CSV_LOCATION_ON_CACHE):
			{
				if(exporter->should_show_full_paths) column_values[i].value = full_entry_path;
			} break;

			// @UseFunctionParameter
			case(CSV_LOCATION_ON_DISK):
			{
				if(value == NULL) column_values[i].value = full_entry_path;
			} break;

			// @UseFunctionParameter
			case(CSV_MISSING_FILE):
			{
				if(value == NULL) column_values[i].value = (file_exists) ? (TEXT("No")) : (TEXT("Yes"));
			} break;

			// @FindData
			case(CSV_CONTENT_LENGTH):
			case(CSV_FILE_SIZE):
			{
				if(value == NULL && optional_find_data != NULL)
				{
					u64 file_size = combine_high_and_low_u32s_into_u64(optional_find_data->nFileSizeHigh, optional_find_data->nFileSizeLow);
					TCHAR file_size_string[MAX_INT64_CHARS] = TEXT("");
					convert_u64_to_string(file_size, file_size_string);

					column_values[i].value = file_size_string;
				}	
			} break;

			// @FindData
			case(CSV_LAST_WRITE_TIME):
			{
				if(value == NULL && optional_find_data != NULL)
				{
					TCHAR last_write_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
					format_filetime_date_time(optional_find_data->ftLastWriteTime, last_write_time);

					column_values[i].value = last_write_time;
				}
			} break;

			// @FindData
			case(CSV_CREATION_TIME):
			{
				if(value == NULL && optional_find_data != NULL)
				{
					TCHAR creation_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
					format_filetime_date_time(optional_find_data->ftCreationTime, creation_time);

					column_values[i].value = creation_time;
				}
			} break;

			// @FindData
			case(CSV_LAST_ACCESS_TIME):
			{
				if(value == NULL && optional_find_data != NULL)
				{
					TCHAR last_access_time[MAX_FORMATTED_DATE_TIME_CHARS] = TEXT("");
					format_filetime_date_time(optional_find_data->ftLastAccessTime, last_access_time);

					column_values[i].value = last_access_time;
				}
			} break;
		}
	}

	entry_to_match.should_match_file_group = (file_group_index != -1);
	entry_to_match.should_match_url_group = (url_group_index != -1);

	bool matched_group = match_cache_entry_to_groups(temporary_arena, exporter->custom_groups, &entry_to_match);
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

	bool match_allows_for_exporting_entry = (!exporter->should_filter_by_groups) || (exporter->should_filter_by_groups && matched_group);

	if(exporter->should_create_csv && match_allows_for_exporting_entry)
	{
		csv_print_row(temporary_arena, exporter->csv_file_handle, column_values, exporter->num_csv_columns);
	}

	if(file_exists && exporter->should_copy_files && match_allows_for_exporting_entry)
	{
		if(copy_file_using_url_directory_structure(temporary_arena, full_entry_path, exporter->output_copy_path, entry_url, entry_filename))
		{
			++(exporter->num_copied_files);
		}
	}

	clear_arena(temporary_arena);
}

// Terminates a cache exporter by performing the following:
// - closing the exporter's current CSV file.
//
// This function should be called by each exporter after processing any cached files, and may be called multiple times by the same
// exporter. Before starting the export process, the initialize_cache_exporter() function should be called first.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the CSV file's handle.
// 
// @Returns: Nothing.
void terminate_cache_exporter(Exporter* exporter)
{
	safe_close_handle(&(exporter->csv_file_handle));
}

/*
	The following defines the necessary functions used to load the external locations file. This file contains zero or more profiles
	which specify the absolute paths of key Windows locations, allowing you to export the cache from files that came from another
	computer.

	Here's an example of an external locations file which defines three profiles: Windows 98, Windows XP, and Windows 8.1. If a line
	starts with a ';' character, then it's considered a comment and is not processed. The external locations file must end in a newline.
	
	If a location specifies "<None>", then the path is assumed to be empty. This is used when the Windows version of the computer where
	the files originated didn't have that type of location. This application will create multiple subdirectories in the main output
	directory with each profile's name. Because of this, any reserved Windows directory name characters may not be used.

	; For Windows 98:
	BEGIN_PROFILE Default User

		WINDOWS				C:\My Old Drives\Windows 98\WINDOWS
		TEMPORARY			C:\My Old Drives\Windows 98\WINDOWS\TEMP
		USER_PROFILE		<None>

		APPDATA				C:\My Old Drives\Windows 98\WINDOWS\Application Data
		LOCAL_APPDATA		<None>
		LOCAL_LOW_APPDATA	<None>

		INTERNET_CACHE		C:\My Old Drives\Windows 98\WINDOWS\Temporary Internet Files
	
	END

	; For Windows XP:
	BEGIN_PROFILE <Username>

		WINDOWS				C:\My Old Drives\Windows XP\WINDOWS
		TEMPORARY			C:\My Old Drives\Windows XP\Documents and Settings\<Username>\Local Settings\Temp
		USER_PROFILE		C:\My Old Drives\Windows XP\Documents and Settings\<Username>

		APPDATA				C:\My Old Drives\Windows XP\Documents and Settings\<Username>\Application Data
		LOCAL_APPDATA		C:\My Old Drives\Windows XP\Documents and Settings\<Username>\Local Settings\Application Data
		LOCAL_LOW_APPDATA	<None>

		INTERNET_CACHE		C:\My Old Drives\Windows XP\Documents and Settings\<Username>\Local Settings\Temporary Internet Files
	
	END

	; For Windows 8.1:
	BEGIN_PROFILE <Username>

		WINDOWS				C:\My Old Drives\Windows 8.1\Windows
		TEMPORARY			C:\My Old Drives\Windows 8.1\Users\<Username>\AppData\Local\Temp
		USER_PROFILE		C:\My Old Drives\Windows 8.1\Users\<Username>

		APPDATA				C:\My Old Drives\Windows 8.1\Users\<Username>\AppData\Roaming
		LOCAL_APPDATA		C:\My Old Drives\Windows 8.1\Users\<Username>\AppData\Local
		LOCAL_LOW_APPDATA	C:\My Old Drives\Windows 8.1\Users\<Username>\AppData\LocalLow

		INTERNET_CACHE		C:\My Old Drives\Windows 8.1\Users\<Username>\AppData\Local\Microsoft\Windows\INetCache
	
	END

	External locations files use UTF-8 as their character encoding, meaning you can use any Unicode character in the various paths.
	In the Windows 98 and ME builds, you must only use ASCII characters. However, since this feature is meant to export the cache
	from an older computer's files in a modern machine, this situation is extremely unlikely to come up.
*/

// Various keywords and delimiters for the external locations file syntax.
static const char COMMENT = ';';
static const char* LINE_DELIMITERS = "\r\n";
static const char* TOKEN_DELIMITERS = " \t";
static const char* BEGIN_PROFILE = "BEGIN_PROFILE";
static const char* END_PROFILE = "END";
static const char* NO_LOCATION = "<None>";
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
static size_t get_total_external_locations_size(Exporter* exporter, u32* result_num_profiles)
{
	HANDLE file_handle = INVALID_HANDLE_VALUE;
	u64 file_size = 0;
	char* file = (char*) memory_map_entire_file(exporter->external_locations_path, &file_handle, &file_size, false);

	u32 num_profiles = 0;
	size_t total_locations_size = 0;

	if(file != NULL)
	{
		// Replace the last character (which should be a newline according to the external locations file guidelines)
		// with a null terminator. Otherwise, we'd read past this memory when using strtok_s(). This probably isn't
		// the most common way to read a text file line by line, but it works in our case.
		char* end_of_file = (char*) advance_bytes(file, file_size - 1);
		*end_of_file = '\0';

		char* remaining_lines = NULL;
		char* line = strtok_s(file, LINE_DELIMITERS, &remaining_lines);

		while(line != NULL)
		{
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

				char* name = NULL;
				char* type = strtok_s(line, TOKEN_DELIMITERS, &name);
				if(type != NULL)
				{
					name = skip_leading_whitespace(name);

					if(strings_are_equal(type, BEGIN_PROFILE) && !string_is_empty(name))
					{
						++num_profiles;
					}
				}
				else
				{
					_ASSERT(false);
				}
			}

			line = strtok_s(NULL, LINE_DELIMITERS, &remaining_lines);
		}
	}
	else
	{
		log_print(LOG_ERROR, "Get Total External Locations Size: Failed to load the external locations file '%s'.", exporter->external_locations_path);
	}

	safe_unmap_view_of_file((void**) &file);
	safe_close_handle(&file_handle);

	*result_num_profiles = num_profiles;
	// Total Size = Size for the Profile array + Size for the string data.
	return 	sizeof(External_Locations) + MAX(num_profiles - 1, 0) * sizeof(Profile)
			+ total_locations_size * sizeof(TCHAR);
}

// Loads the external locations file on disk. This function should be called after get_total_external_locations_size() and with a
// memory arena that is capable of holding the number of bytes it returned.
//
// @Parameters:
// 1. exporter - The Exporter structure that contains the path to the external locations file, and the permanent memory arena where
// the profile data will be stored. After loading this data, this structure's 'external_locations' member will modified.
// 2. num_profiles - The number of profiles found by an earlier call to get_total_external_locations_size().
//
// @Returns: Nothing.
static void load_external_locations(Exporter* exporter, u32 num_profiles)
{
	if(num_profiles == 0)
	{
		log_print(LOG_WARNING, "Load External Locations: Attempted to load zero profiles. No external locations will be loaded.");
		return;
	}

	// The relevant loaded group data will go to the permanent arena, and any intermediary data to the temporary one.
	Arena* permanent_arena = &(exporter->permanent_arena);
	Arena* temporary_arena = &(exporter->temporary_arena);

	// The number of profiles is always greater than zero here.
	size_t external_locations_size = sizeof(External_Locations) + sizeof(Profile) * (num_profiles - 1);
	External_Locations* external_locations = push_arena(permanent_arena, external_locations_size, External_Locations);
	
	HANDLE file_handle = INVALID_HANDLE_VALUE;
	u64 file_size = 0;
	char* file = (char*) memory_map_entire_file(exporter->external_locations_path, &file_handle, &file_size, false);

	u32 num_processed_profiles = 0;

	if(file != NULL)
	{
		// Replace the last character (which should be a newline according to the external locations file guidelines)
		// with a null terminator. Otherwise, we'd read past this memory when using strtok_s(). This probably isn't
		// the most common way to read a text file line by line, but it works in our case.
		char* end_of_file = (char*) advance_bytes(file, file_size - 1);
		*end_of_file = '\0';

		char* remaining_lines = NULL;
		char* line = strtok_s(file, LINE_DELIMITERS, &remaining_lines);

		// Keep track of which profile we're loading data to.
		bool seen_begin_list = false;
		bool is_invalid = false;
		Profile* profile_array = external_locations->profiles;
		Profile* profile = NULL;

		while(line != NULL)
		{
			line = skip_leading_whitespace(line);

			if(*line == COMMENT || string_is_empty(line))
			{
				// Skip comments and empty lines.
			}
			// Begin a new profile or skip it if the keyword is incorrect.
			else if(!seen_begin_list)
			{
				seen_begin_list = true;

				char* name = NULL;
				char* type = strtok_s(line, TOKEN_DELIMITERS, &name);
				if(type != NULL)
				{
					if(strings_are_equal(type, BEGIN_PROFILE) && !string_is_empty(name))
					{
						profile = &profile_array[num_processed_profiles];
						++num_processed_profiles;

						name = skip_leading_whitespace(name);
						profile->name = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, name);
						log_print(LOG_INFO, "Load External Locations: Loading the profile '%s'.", profile->name);
					}
					else
					{
						is_invalid = true;
						log_print(LOG_ERROR, "Load External Locations: Skipping invalid profile of type '%hs' and name '%hs'.", type, name);
					}
				}
				else
				{
					_ASSERT(false);
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
					char* path = NULL;
					char* location_type = strtok_s(line, TOKEN_DELIMITERS, &path);
					if(location_type != NULL)
					{
						path = skip_leading_whitespace(path);

						if(strings_are_equal(path, NO_LOCATION))
						{
							path = "";
						}

						if(strings_are_equal(location_type, LOCATION_WINDOWS))
						{
							profile->windows_path = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);
						}
						else if(strings_are_equal(location_type, LOCATION_TEMPORARY))
						{
							profile->windows_temporary_path = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);
						}
						else if(strings_are_equal(location_type, LOCATION_USER_PROFILE))
						{
							profile->user_profile_path = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);
						}
						else if(strings_are_equal(location_type, LOCATION_APPDATA))
						{
							profile->appdata_path = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);
						}
						else if(strings_are_equal(location_type, LOCATION_LOCAL_APPDATA))
						{
							profile->local_appdata_path = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);
						}
						else if(strings_are_equal(location_type, LOCATION_LOCAL_LOW_APPDATA))
						{
							profile->local_low_appdata_path = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);
						}
						else if(strings_are_equal(location_type, LOCATION_INTERNET_CACHE))
						{
							profile->wininet_cache_path = convert_utf_8_string_to_tchar(permanent_arena, temporary_arena, path);
						}
						else
						{
							log_print(LOG_ERROR, "Load External Locations: Unknown location type '%hs'.", location_type);
						}
					}
					else
					{
						_ASSERT(false);
					}
				}
			}
			else
			{
				_ASSERT(false);
			}

			line = strtok_s(NULL, LINE_DELIMITERS, &remaining_lines);
		}

		if(seen_begin_list)
		{
			log_print(LOG_WARNING, "Load External Locations: Found unterminated profile location list.");
		}
	}
	else
	{
		log_print(LOG_ERROR, "Load External Locations: Failed to load the external locations file '%s'.", exporter->external_locations_path);
	}

	safe_unmap_view_of_file((void**) &file);
	safe_close_handle(&file_handle);

	external_locations->num_profiles = num_processed_profiles;
	if(num_processed_profiles != num_profiles)
	{
		log_print(LOG_ERROR, "Load External Locations: Loaded %I32u profiles when %I32u were expected.", num_processed_profiles, num_profiles);
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
	export_specific_or_default_internet_explorer_cache(exporter);
	log_print_newline();

	export_specific_or_default_flash_plugin_cache(exporter);
	log_print_newline();

	export_specific_or_default_shockwave_plugin_cache(exporter);
	log_print_newline();

	export_specific_or_default_java_plugin_cache(exporter);
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
	if(exporter->should_load_external_locations)
	{
		External_Locations* external_locations = exporter->external_locations;
		_ASSERT(external_locations != NULL);

		console_print("Exporting the cache from %I32u default external locations...", external_locations->num_profiles);
		log_print(LOG_INFO, "All Locations: Exporting the cache from %I32u default external locations.", external_locations->num_profiles);
		log_print_newline();

		for(u32 i = 0; i < external_locations->num_profiles; ++i)
		{
			Profile profile = external_locations->profiles[i];
			exporter->current_profile_name = profile.name;
			console_print("- [%I32u of %I32u] Exporting from the profile '%s'...", i+1, external_locations->num_profiles, profile.name);
			
			#define STRING_OR_DEFAULT(str) (str != NULL) ? (str) : (TEXT(""))

			log_print(LOG_NONE, "------------------------------------------------------------");
			log_print(LOG_INFO, "Exporting from the profile '%s' (%I32u).", profile.name, i);
			log_print(LOG_NONE, "------------------------------------------------------------");
			log_print(LOG_NONE, "- Windows Directory Path: '%s'", STRING_OR_DEFAULT(profile.windows_path));
			log_print(LOG_NONE, "- Windows Temporary Path: '%s'", STRING_OR_DEFAULT(profile.windows_temporary_path));
			log_print(LOG_NONE, "- User Profile Path: '%s'", STRING_OR_DEFAULT(profile.user_profile_path));
			log_print(LOG_NONE, "- Roaming AppData Path: '%s'", STRING_OR_DEFAULT(profile.appdata_path));
			log_print(LOG_NONE, "- Local AppData Path: '%s'", STRING_OR_DEFAULT(profile.local_appdata_path));
			log_print(LOG_NONE, "- LocalLow AppData Path: '%s'", STRING_OR_DEFAULT(profile.local_low_appdata_path));
			log_print(LOG_NONE, "- WinINet Cache Path: '%s'", STRING_OR_DEFAULT(profile.wininet_cache_path));
			log_print(LOG_NONE, "------------------------------------------------------------");
			log_print_newline();

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
					log_print(LOG_WARNING, "All Locations: The profile '%s' (%I32u) will be skipped since the %hs path was not found in the list.", profile.name, i, location_name);\
				}\
				else if(FAILED(StringCchCopy(exporter->member_name, MAX_PATH_CHARS, profile.member_name)))\
				{\
					are_all_locations_valid = false;\
					console_print("This profile will be skipped since the %hs path is too long.", location_name);\
					log_print(LOG_WARNING, "All Locations: The profile '%s' (%I32u) will be skipped since the %hs path is too long.", profile.name, i, location_name);\
				}\
			} while(false, false)

			CHECK_AND_COPY_LOCATION(windows_path, 			"Windows");
			CHECK_AND_COPY_LOCATION(windows_temporary_path, "Temporary");
			CHECK_AND_COPY_LOCATION(user_profile_path, 		"User Profile");
			CHECK_AND_COPY_LOCATION(appdata_path, 			"AppData");
			CHECK_AND_COPY_LOCATION(local_appdata_path, 	"Local AppData");
			CHECK_AND_COPY_LOCATION(local_low_appdata_path, "Local Low AppData");
			CHECK_AND_COPY_LOCATION(wininet_cache_path, 	"Internet Cache");

			if(are_all_locations_valid)
			{
				export_all_cache_locations(exporter);
			}

			log_print_newline();
		}
	}
	else
	{
		_ASSERT(exporter->external_locations == NULL);
		export_all_cache_locations(exporter);
	}
}
