#include "web_cache_exporter.h"

#include "internet_explorer.h"
#include "flash_plugin.h"
#include "shockwave_plugin.h"

#include "explore_files.h"

/*
	This file defines the exporter's startup operations (parsing command line options, allocating memory, etc) and any common
	functions that are used when processing each cache entry (resolving output paths, exporting each cached file, etc). It also
	defines this application's entry point.
	
	A few general notes:

	- This application is a digital forensics utility that allows you to convert a web browser or plugin's cache from a hard to
	read format to a more easily viewable one. In this context, "exporting" means copying each cached file by recreating the
	original website's directory structure and creating a CSV file which contains information on each one. It was written to help
	recover lost web media like games, animations, virtual worlds, etc. The idea is that someone who has access to an old computer
	where they used to play web games can easily check their web cache for lost game files.

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

	@TODO:

	- Add support for the Java Plugin.
	- Handle export_cache_entry() with missing filenames (for the Java Plugin cache).
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
												"########## [1] AVAILABLE EXPORT ARGUMENTS: <Export Option> [Optional Cache Path] [Optional Output Path]\n"
												"\n"
												"-export-ie    to export the WinINet cache, including Internet Explorer 4 to 11 and Microsoft Edge.\n"
												"\n"
												"-export-flash    to export the Flash Player cache.\n"
												"\n"
												"-export-shockwave    to export the Shockwave Player cache.\n"
												"\n"
												"-find-and-export-all    to export all of the above at once. This option does not have an optional cache path argument.\n"
												"\n"
												"-explore-files    to export the files in a directory and its subdirectories. This option must have the cache path argument.\n"
												"\n"
												"########## [1] EXAMPLES:\n"
												"\n"
												"WCE.exe -export-ie\n"
												"WCE.exe -export-ie \"C:\\PathToTheCache\"\n"
												"WCE.exe -export-ie \"C:\\PathToTheCache\" \"My Cache\"\n"
												"WCE.exe -export-ie \"\" \"My Cache\" (choose the output path but use the default cache path)\n"
												"WCE.exe -export-ie \"C:\\PathToTheCache\" \"\" (choose the cache path but use the default output path)\n"
												"WCE.exe -find-and-export-all\n"
												"WCE.exe -find-and-export-all \"My Cache\"\n"
												"WCE.exe -explore-files \"C:\\PathToExplore\"\n"
												"WCE.exe -explore-files \"C:\\PathToExplore\" \"My Exploration\"\n"
												"\n"
												"\n"
												"########## [2] AVAILABLE OPTIONAL ARGUMENTS: Put them before the export option.\n"
												"\n"
												"-no-copy-files    to stop the exporter from copying files.\n"
												"\n"
												"-no-create-csv    to stop the exporter from creating CSV files.\n"
												"\n"
												"-overwrite    to delete the previous output folder before running.\n"
												"\n"
												"-filter-by-groups    to only export files that match any loaded groups.\n"
												"\n"
												"-load-group-files \"<Group Files>\"    to only load specific group files, separated by spaces and without the .group extension. By default, this application will load all group files.\n"
												"\n"
												"-hint-ie <Local AppData Path>    Only for Internet Explorer 10 to 11 and Microsoft Edge (i.e. if -export-ie or -find-and-export-all are used on a modern Windows version), and if your not exporting from a default location (i.e. if the cache was copied from another computer).\n"
												"    This is used to specify the absolute path to the Local AppData folder of the computer where the cache originated.\n"
												"    If this is option is not used, the exporter will try to guess this location.\n"
												"    You should rerun this application with this option if you notice that some cached files were not exported.\n"
												"\n"
												"########## [2] EXAMPLES:\n"
												"\n"
												"WCE.exe -no-copy-files -export-flash\n"
												"WCE.exe -overwrite -export-shockwave\n"
												"WCE.exe -filter-by-groups -explore-files \"C:\\PathToExplore\"\n"
												"WCE.exe -load-group-files \"FileA FileB\" -find-and-export-all\n"
												"WCE.exe -hint-ie \"C:\\Users\\My Old PC\\AppData\\Local\" -export-ie \"C:\\PathToTheCache\"\n"
												"\n"
												"WCE.exe -overwrite -no-create-csv -filter-by-groups -load-group-files \"FileA FileB\" -hint-ie \"C:\\Users\\My Old PC\\AppData\\Local\" -find-and-export-all \"My Cache\""
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
	
			exporter->is_exporting_from_default_locations = true;
			seen_export_option = true;
			break;
		}
		else if(string_starts_with(option, TEXT("-export")))
		{
			TCHAR* cache_type = skip_to_suboption(option);

			if(cache_type == NULL)
			{
				log_print(LOG_ERROR, "Missing web cache type in command line option '%s'", option);
				console_print("Missing web cache type in command line option '%s'\n", option);
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
				log_print(LOG_ERROR, "Unknown web cache type '%s' in command line option '%s'", cache_type, option);
				console_print("Unknown web cache type '%s' in command line option '%s'\n", cache_type, option);
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
		else
		{
			log_print(LOG_ERROR, "Unknown command line option '%s'", option);
			console_print("Unknown command line option '%s'\n", option);

			success = false;
			break;
		}
	}

	if(!seen_export_option)
	{
		console_print("Missing the -export option.");
		log_print(LOG_ERROR, "Argument Parsing: The main -export option was not found.");
		success = false;
	}

	if(!exporter->should_copy_files && !exporter->should_create_csv)
	{
		console_print("The options '-no-copy-files' and '-no-create-csv' can't be used at the same time.");
		log_print(LOG_ERROR, "Argument Parsing: The options '-no-copy-files' and '-no-create-csv' were used at the same time.");
		success = false;
	}

	if(exporter->should_load_specific_groups_files && exporter->num_group_filenames_to_load == 0)
	{
		console_print("The -load-group-files option requires one or more group filenames as its argument.");
		log_print(LOG_ERROR, "Argument Parsing: The -load-group-files option was used without passing its value.");
		success = false;
	}

	if(exporter->should_use_ie_hint && string_is_empty(exporter->ie_hint_path))
	{
		console_print("The -hint-ie option requires a path as its argument.");
		log_print(LOG_ERROR, "Argument Parsing: The -hint-ie option was used without passing its value.");
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
		_ASSERT(false);
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
	>>>> 3. Check if any command line options were passed. If not, terminate.
	>>>> 4. Create the temporary memory arena based on the current Windows version. On error, terminate.
	>>>> 5. Parse the command line options. If an option is incorrect, terminate.
	>>>> 6. Find the current executable's directory path.
	>>>> 7. Find the number of groups defined in the group files located on the executable's directory path,
	and how much memory is roughly required.
	>>>> 8. Create the permanent memory arena based on the number of group files. On error, terminate.
	>>>> 9. Dynamically load any necessary functions.
	>>>> 10. Find the location of the Temporary Files and Application Data directories.
	>>>> 11. Delete the previous output directory if requested in the command line options.
	>>>> 12. Start exporting the cache based on the command line options.
	>>>> 13. Perform any clean up operations after finishing exporting. These are also done when any of the
	previous errors occur.
*/
int _tmain(int argc, TCHAR* argv[])
{
	Exporter exporter = {};	

	if(!create_log_file(LOG_FILE_NAME))
	{
		console_print("Warning: Failed to create the log file.");
	}

	log_print(LOG_INFO, "Startup: Running the Web Cache Exporter %hs version %hs in %hs mode.",
							EXPORTER_BUILD_TARGET, EXPORTER_BUILD_VERSION, EXPORTER_BUILD_MODE);

	exporter.os_version.dwOSVersionInfoSize = sizeof(exporter.os_version);
	if(GetVersionEx(&exporter.os_version))
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
		find_internet_explorer_version(ie_version, sizeof(ie_version));
		log_print(LOG_INFO, "Startup: Running Internet Explorer version %s.", ie_version);
		log_print(LOG_INFO, "Startup: The current Windows ANSI code page identifier is %u.", GetACP());
	}

	if(argc <= 1)
	{
		console_print("No command line options supplied.");
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
		size_t permanent_memory_size = get_total_group_files_size(&exporter, &num_groups);
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
		log_print(LOG_INFO, "Startup: The permanent memory arena is at %.2f%% used capacity after loading the group files.", get_used_arena_capacity(&exporter.permanent_arena));
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

	if(GetTempPath(MAX_PATH_CHARS, exporter.windows_temporary_path) != 0
		&& create_temporary_directory(exporter.windows_temporary_path, exporter.exporter_temporary_path))
	{
		exporter.was_temporary_exporter_directory_created = true;
		log_print(LOG_INFO, "Startup: Created the temporary exporter directory in '%s'.", exporter.exporter_temporary_path);
	}
	else
	{
		log_print(LOG_ERROR, "Startup: Failed to create the temporary exporter directory with error code %lu.", GetLastError());
	}
	
	if(!get_special_folder_path(CSIDL_APPDATA, exporter.roaming_appdata_path))
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

	if(exporter.is_exporting_from_default_locations && (exporter.cache_type != CACHE_ALL))
	{
		log_print(LOG_INFO, "Startup: No cache path specified. Exporting the cache from any existing default directories.");
	}

	if(exporter.should_overwrite_previous_output)
	{
		TCHAR* directory_name = PathFindFileName(exporter.output_path);
		if(delete_directory_and_contents(exporter.output_path))
		{
			console_print("Deleted the previous output directory '%s' before starting.", directory_name);
			log_print(LOG_INFO, "Startup: Deleted the previous output directory successfully.");
		}
		else
		{	
			console_print("Warning: Could not delete the previous output directory '%s'.", directory_name);
			log_print(LOG_ERROR, "Startup: Failed to delete the previous output directory '%s'.", directory_name);
		}
	}

	// The temporary arena should be cleared before any cache exporter runs. Any data that needs to stick around
	// should be stored in the permanent arena.
	log_print(LOG_INFO, "Startup: The temporary memory arena is at %.2f%% used capacity before exporting files.", get_used_arena_capacity(&exporter.temporary_arena));
	clear_arena(&(exporter.temporary_arena));

	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_INFO, "Exporter Options:");
	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_NONE, "- Cache Type: %s", CACHE_TYPE_TO_STRING[exporter.cache_type]);
	log_print(LOG_NONE, "- Should Copy Files: %hs", (exporter.should_copy_files) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Create CSV: %hs", (exporter.should_create_csv) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Overwrite Previous Output: %hs", (exporter.should_overwrite_previous_output) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Filter By Groups: %hs", (exporter.should_filter_by_groups) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Load Specific Groups: %hs", (exporter.should_load_specific_groups_files) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Number Of Groups To Load: %Iu", exporter.num_group_filenames_to_load);
	log_print(LOG_NONE, "- Should Use Internet Explorer's Hint: %hs", (exporter.should_use_ie_hint) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Internet Explorer Hint Path: '%s'", exporter.ie_hint_path);
	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_NONE, "- Cache Path: '%s'", exporter.cache_path);
	log_print(LOG_NONE, "- Output Path: '%s'", exporter.output_path);
	log_print(LOG_NONE, "- Is Exporting From Default Locations: %hs", (exporter.is_exporting_from_default_locations) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_NONE, "- Executable Path: '%s'", exporter.executable_path);
	log_print(LOG_NONE, "- Exporter Temporary Path: '%s'", exporter.exporter_temporary_path);
	log_print(LOG_NONE, "- Was Temporary Directory Created: %hs", (exporter.was_temporary_exporter_directory_created) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_NONE, "- Windows Temporary Path: '%s'", exporter.windows_temporary_path);
	log_print(LOG_NONE, "- Roaming AppData Path: '%s'", exporter.roaming_appdata_path);
	log_print(LOG_NONE, "- Local AppData Path: '%s'", exporter.local_appdata_path);
	log_print(LOG_NONE, "- LocalLow AppData Path: '%s'", exporter.local_low_appdata_path);

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
			_ASSERT(false);
		} break;

		case(CACHE_ALL):
		{
			_ASSERT(exporter.is_exporting_from_default_locations);
			_ASSERT(string_is_empty(exporter.cache_path));

			export_specific_or_default_internet_explorer_cache(&exporter);
			log_print_newline();

			export_specific_or_default_flash_plugin_cache(&exporter);
			log_print_newline();

			export_specific_or_default_shockwave_plugin_cache(&exporter);
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
			_ASSERT(false);
		} break;
	}

	console_print("Finished running:\n- Created %Iu CSV files.\n- Processed %Iu cached files.\n- Copied %Iu cached files.", exporter.num_csv_files_created, exporter.num_processed_files, exporter.num_copied_files);
	log_print_newline();
	log_print(LOG_INFO, "Finished Running: Created %Iu CSV files. Processed %Iu cache entries. Copied %Iu cached files.", exporter.num_csv_files_created, exporter.num_processed_files, exporter.num_copied_files);
	
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
								const Csv_Type column_types[], size_t num_columns)
{
	Arena* temporary_arena = &(exporter->temporary_arena);

	get_full_path_name(exporter->cache_path);
	get_full_path_name(exporter->output_path);
	
	PathCombine(exporter->output_copy_path, exporter->output_path, cache_identifier);
	
	// Don't use PathCombine() since we're just adding a file extension to the previous path.
	StringCchCopy(exporter->output_csv_path, MAX_PATH_CHARS, exporter->output_copy_path);
	StringCchCat(exporter->output_csv_path, MAX_PATH_CHARS, TEXT(".csv"));

	if(exporter->should_create_csv && create_csv_file(exporter->output_csv_path, &(exporter->csv_file_handle)))
	{
		++(exporter->num_csv_files_created);
		csv_print_header(temporary_arena, exporter->csv_file_handle, column_types, num_columns);
		clear_arena(temporary_arena);
	}

	exporter->num_csv_columns = num_columns;
	exporter->csv_column_types = column_types;
}

// Exports a cache entry by copying its file to the output location using the original website's directory structure, and by adding a
// new row to the CSV file. This function will also match the cache entry to any loaded group files.
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
void export_cache_entry(Exporter* exporter, Csv_Entry column_values[],
						TCHAR* full_entry_path, TCHAR* entry_url, TCHAR* entry_filename,
						WIN32_FIND_DATA* optional_find_data)
{
	if(optional_find_data != NULL) entry_filename = optional_find_data->cFileName;

	_ASSERT(full_entry_path != NULL);
	_ASSERT(entry_filename != NULL);

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
