#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "custom_groups.h"
#include "internet_explorer.h"
#include "shockwave_plugin.h"

/*void resolve_cache_version_output_paths(Exporter* exporter, u32 cache_version, TCHAR* cache_version_to_string[])
{
	get_full_path_name(exporter->output_path);

	exporter->cache_version = cache_version;

	StringCchCopy(exporter->output_copy_path, MAX_PATH_CHARS, exporter->output_path);
	PathAppend(exporter->output_copy_path, cache_version_to_string[cache_version]);

	StringCchCopy(exporter->output_csv_path, MAX_PATH_CHARS, exporter->output_copy_path);
	StringCchCat(exporter->output_csv_path, MAX_PATH_CHARS, TEXT(".csv"));
}*/

void resolve_exporter_output_paths_and_create_csv_file(Exporter* exporter, const TCHAR* cache_identifier, const Csv_Type column_types[], size_t num_columns)
{
	Arena* arena = &(exporter->temporary_arena);

	get_full_path_name(exporter->output_path);

	StringCchCopy(exporter->output_copy_path, MAX_PATH_CHARS, exporter->output_path);
	PathAppend(exporter->output_copy_path, cache_identifier);

	StringCchCopy(exporter->output_csv_path, MAX_PATH_CHARS, exporter->output_copy_path);
	StringCchCat(exporter->output_csv_path, MAX_PATH_CHARS, TEXT(".csv"));

	if(exporter->should_create_csv)
	{
		create_csv_file(exporter->output_csv_path, &(exporter->csv_file_handle));
		csv_print_header(arena, exporter->csv_file_handle, column_types, num_columns);
		clear_arena(arena);
	}
}

void export_cache_entry(Exporter* exporter,
						const Csv_Type column_types[], Csv_Entry column_values[], size_t num_columns,
						const TCHAR* full_entry_path, const TCHAR* entry_url, const TCHAR* entry_filename)
{
	Arena* arena = &(exporter->temporary_arena);

	// @TODO: File and URL groups - check column_types for CSV_CUSTOM_FILE_GROUP and CSV_CUSTOM_URL_GROUP
	// and get the right group from the previously loaded files.

	if(exporter->should_create_csv)
	{
		csv_print_row(arena, exporter->csv_file_handle, column_types, column_values, num_columns);
	}

	if(exporter->should_copy_files)
	{
		copy_file_using_url_directory_structure(arena, full_entry_path, exporter->output_copy_path, entry_url, entry_filename);
	}

	clear_arena(arena);
}

void close_exporter_csv_file(Exporter* exporter)
{
	safe_close_handle(&(exporter->csv_file_handle));
}

static TCHAR* skip_to_suboption(TCHAR* str)
{
	TCHAR* suboption = NULL;

	if(str != NULL)
	{
		if(*str == TEXT('-'))
		{
			++str;
		}

		while(*str != TEXT('\0') && *str != TEXT('-'))
		{
			++str;
		}

		suboption = (*str != TEXT('\0')) ? (str) : (NULL);
	}

	return suboption;
}

const TCHAR* DEFAULT_EXPORT_PATH = TEXT("Exported-Cache");

static bool parse_exporter_arguments(int num_arguments, TCHAR* arguments[], Exporter* exporter)
{
	bool success = true;
	bool seen_export_option = false;

	exporter->should_copy_files = true;
	exporter->should_create_csv = true;
	exporter->csv_file_handle = INVALID_HANDLE_VALUE;

	// Skip the first argument which contains the executable's name.
	for(int i = 1; i < num_arguments; ++i)
	{
		TCHAR* option = arguments[i];

		if(lstrcmpi(option, TEXT("-no-copy-files")) == 0)
		{
			exporter->should_copy_files = false;
		}
		else if(lstrcmpi(option, TEXT("-no-create-csv")) == 0)
		{
			exporter->should_create_csv = false;
		}
		else if(lstrcmpi(option, TEXT("-merge-copied-files")) == 0)
		{
			exporter->should_merge_copied_files = true;
		}
		else if(lstrcmpi(option, TEXT("-hint-ie")) == 0)
		{
			exporter->should_use_ie_hint = true;
			if(i+1 < num_arguments && !is_string_empty(arguments[i+1]))
			{
				StringCchCopy(exporter->ie_hint_path, MAX_PATH_CHARS, arguments[i+1]);
			}

			i += 1;
		}
		else if(lstrcmpi(option, TEXT("-find-and-export-all")) == 0)
		{
			exporter->cache_type = CACHE_ALL;

			if(i+1 < num_arguments && !is_string_empty(arguments[i+1]))
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, arguments[i+1]);
			}
			PathAppend(exporter->output_path, DEFAULT_EXPORT_PATH);

			exporter->is_exporting_from_default_locations = true;

			seen_export_option = true;
			break;
		}
		else if(string_starts_with_insensitive(option, TEXT("-export")))
		{
			TCHAR* cache_type = skip_to_suboption(option);
			//char* cache_version = skip_to_suboption(cache_type);
			if(cache_type == NULL)
			{
				log_print(LOG_ERROR, "Missing web cache type in command line option '%s'", option);
				console_print("Missing web cache type in command line option '%s'\n", option);
				exporter->cache_type = CACHE_UNKNOWN;
				success = false;
			}
			else if(lstrcmpi(cache_type, TEXT("-ie")) == 0)
			{
				exporter->cache_type = CACHE_INTERNET_EXPLORER;
			}
			else if(lstrcmpi(cache_type, TEXT("-shockwave")) == 0)
			{
				exporter->cache_type = CACHE_SHOCKWAVE_PLUGIN;
			}
			else if(lstrcmpi(cache_type, TEXT("-java")) == 0)
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

			if(i+1 < num_arguments && !is_string_empty(arguments[i+1]))
			{
				StringCchCopy(exporter->cache_path, MAX_PATH_CHARS, arguments[i+1]);
			}

			if(i+2 < num_arguments && !is_string_empty(arguments[i+2]))
			{
				StringCchCopy(exporter->output_path, MAX_PATH_CHARS, arguments[i+2]);
			}
			PathAppend(exporter->output_path, DEFAULT_EXPORT_PATH);

			exporter->is_exporting_from_default_locations = is_string_empty(exporter->cache_path);
			
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
		log_print(LOG_ERROR, "Argument Parsing: The main -export option was not found.");
		console_print("Missing the -export option.\n");
		success = false;
	}

	if(!exporter->should_copy_files && !exporter->should_create_csv)
	{
		log_print(LOG_ERROR, "Argument Parsing: -no-copy-files and -no-create-csv were used at the same time.");
		console_print("The options -no-copy-files and -no-create-csv can't be used at the same time.\n");
		success = false;
	}

	if(exporter->should_use_ie_hint && is_string_empty(exporter->ie_hint_path))
	{
		log_print(LOG_ERROR, "Argument Parsing: The -hint-ie option was used without passing its value.");
		console_print("The -hint-ie option requires a path as its argument.");
		success = false;
	}

	return success;
}

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
		size_for_os_version = megabytes_to_bytes(2); // x1 or x2 for TCHAR
		log_print(LOG_WARNING, "Get Startup Memory Size: Using %Iu bytes for unhandled Windows version %lu.%lu.",
								size_for_os_version, os_version.dwMajorVersion, os_version.dwMinorVersion);
		_ASSERT(false);
	}

	return size_for_os_version * sizeof(TCHAR);
}

static void clean_up(Exporter* exporter)
{
	if(exporter->was_temporary_directory_created)
	{
		exporter->was_temporary_directory_created = !delete_directory_and_contents(exporter->temporary_path);
	}

	#ifndef BUILD_9X
		if( (exporter->cache_type == CACHE_INTERNET_EXPLORER) || (exporter->cache_type == CACHE_ALL) )
		{
			windows_nt_free_esent_functions();
			windows_nt_free_ntdll_functions();
			windows_nt_free_kernel32_functions();
		}
	#endif

	destroy_arena( &(exporter->temporary_arena) );
	destroy_arena( &(exporter->permanent_arena) );

	close_log_file();
}

int _tmain(int argc, TCHAR* argv[])
{
	Exporter exporter = {};
	//SecureZeroMemory(&exporter, sizeof(exporter));	

	create_log_file(TEXT("Web-Cache-Exporter.log"));
	log_print(LOG_INFO, "Startup: Running the Web Cache Exporter %hs version %hs in %hs mode.", BUILD_TARGET, BUILD_VERSION, BUILD_MODE);

	if(argc <= 1)
	{
		log_print(LOG_ERROR, "No command line arguments supplied. The program will print a help message and exit.");
		console_print("-no-copy-files\n");
		console_print("-no-create-csv\n");
		console_print("-export-[cache type], where:\n");
		console_print("\t-ie\n");
		console_print("\t-shockwave\n");
		console_print("\t-java\n");

		clean_up(&exporter);
		return 1;
	}

	if(!parse_exporter_arguments(argc, argv, &exporter))
	{
		log_print(LOG_ERROR, "Startup: An error occured while parsing the command line arguments. The program will not run.");
		clean_up(&exporter);
		return 1;
	}

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
	}

	{
		const size_t ie_version_size = 32;
		TCHAR ie_version[ie_version_size] = TEXT("");
		find_internet_explorer_version(ie_version, ie_version_size);
		log_print(LOG_INFO, "Startup: Running Internet Explorer version %s.", ie_version);
		log_print(LOG_INFO, "Startup: The current Windows ANSI code page identifier is %u.", GetACP());
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

		size_t permanent_memory_size = get_total_group_files_size(&exporter);
		log_print(LOG_INFO, "Startup: Allocating %Iu bytes for the permanent memory arena.", permanent_memory_size);

		if(!create_arena(&exporter.permanent_arena, permanent_memory_size))
		{
			console_print("Could not allocate enough permanent memory to run the program.");
			log_print(LOG_ERROR, "Startup: Could not allocate %Iu bytes to run the program.", permanent_memory_size);
			clean_up(&exporter);
			return 1;
		}

		// ----------------------------------------------------------------------------------------------------

		size_t temporary_memory_size = get_temporary_memory_size_for_os_version(&exporter);
		log_print(LOG_INFO, "Startup: Allocating %Iu bytes for the temporary memory arena.", temporary_memory_size);

		if(!create_arena(&exporter.temporary_arena, temporary_memory_size))
		{
			console_print("Could not allocate enough temporary memory to run the program.");
			log_print(LOG_ERROR, "Startup: Could not allocate %Iu bytes to run the program.", temporary_memory_size);
			clean_up(&exporter);
			return 1;
		}
	}

	{
		load_all_group_files(&exporter);
	}

	#ifndef BUILD_9X
		if( (exporter.cache_type == CACHE_INTERNET_EXPLORER) || (exporter.cache_type == CACHE_ALL) )
		{
			windows_nt_load_kernel32_functions();
			windows_nt_load_ntdll_functions();
			windows_nt_load_esent_functions();
		}
	#endif

	TCHAR temporary_files_directory_path[MAX_PATH_CHARS] = TEXT("");
	if(GetTempPath(MAX_PATH_CHARS, temporary_files_directory_path) != 0
		&& create_temporary_directory(temporary_files_directory_path, exporter.temporary_path))
	{
		exporter.was_temporary_directory_created = true;
		log_print(LOG_INFO, "Startup: Created the temporary exporter directory in '%s'.", exporter.temporary_path);
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

	log_print(LOG_INFO, "Exporter Options:");
	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_NONE, "- Cache Type: %s", CACHE_TYPE_TO_STRING[exporter.cache_type]);
	log_print(LOG_NONE, "- Should Copy Files: %hs", (exporter.should_copy_files) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Create CSV: %hs", (exporter.should_create_csv) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Should Merge Copied Files: %hs", (exporter.should_merge_copied_files) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "- Cache Path: '%s'", exporter.cache_path);
	log_print(LOG_NONE, "- Output Path: '%s'", exporter.output_path);
	log_print(LOG_NONE, "- Is Exporting From Default Locations: %hs", (exporter.is_exporting_from_default_locations) ? ("Yes") : ("No"));
	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_NONE, "- Temporary Path: '%s'", exporter.temporary_path);
	log_print(LOG_NONE, "- Executable Path: '%s'", exporter.executable_path);
	log_print(LOG_NONE, "- Roaming AppData Path: '%s'", exporter.roaming_appdata_path);
	log_print(LOG_NONE, "- Local AppData Path: '%s'", exporter.local_appdata_path);
	log_print(LOG_NONE, "- LocalLow AppData Path: '%s'", exporter.local_low_appdata_path);
	log_print(LOG_NONE, "----------------------------------------");
	log_print(LOG_NONE, "- Cache Version: %I32u", exporter.cache_version);
	log_print(LOG_NONE, "- Output Copy Path: '%s'", exporter.output_copy_path);
	log_print(LOG_NONE, "- Output CSV Path: '%s'", exporter.output_csv_path);
	log_print(LOG_NONE, "- Index Path: '%s'", exporter.index_path);
	log_print(LOG_NONE, "----------------------------------------");

	if(exporter.is_exporting_from_default_locations && (exporter.cache_type != CACHE_ALL))
	{
		log_print(LOG_INFO, "Startup: No cache path specified. Exporting the cache from any existing default directories.");
	}

	log_print_newline();

	switch(exporter.cache_type)
	{
		case(CACHE_INTERNET_EXPLORER):
		{
			export_specific_or_default_internet_explorer_cache(&exporter);
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
			_ASSERT(is_string_empty(exporter.cache_path));

			export_specific_or_default_internet_explorer_cache(&exporter);
			exporter.cache_path[0] = TEXT('\0');
			log_print_newline();

			export_specific_or_default_shockwave_plugin_cache(&exporter);
			exporter.cache_path[0] = TEXT('\0');
		} break;

		default:
		{
			log_print(LOG_ERROR, "Startup: Attempted to export the cache from '%s' using the unhandled cache type %d.", exporter.cache_path, exporter.cache_type);
			_ASSERT(false);
		} break;
	}

	clean_up(&exporter);

	return 0;
}
