#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "internet_explorer.h"
#include "shockwave_plugin.h"

/*
	char* 					= %hs
	wchar_t*  				= %ls
	TCHAR* 					= %s
	void* / HANDLE 			= %p
	char 					= %hc / %hhd
	unsigned char 			= %hhu
	wchar_t 				= %lc
	TCHAR 					= %c

	short / s16				= %hd
	unsigned short / u16 	= %hu
	long / LONG				= %ld
	unsigned long / ULONG	= %lu
	long long 				= %lld
	u32						= %I32u
	s32						= %I32d
	u64						= %I64u
	s64						= %I64d
	WORD 					= %hu
	DWORD 					= %lu
	QWORD 					= %I64u
	size_t 					= %Iu
	ptrdiff_t				= %Id

	uint in hexadecimal		= 0x%08X
*/

void resolve_cache_version_output_paths(Exporter* exporter, u32 cache_version, TCHAR* cache_version_to_string[])
{
	get_full_path_name(exporter->output_path);

	exporter->cache_version = cache_version;

	StringCchCopy(exporter->output_copy_path, MAX_PATH_CHARS, exporter->output_path);
	PathAppend(exporter->output_copy_path, cache_version_to_string[cache_version]);

	StringCchCopy(exporter->output_csv_path, MAX_PATH_CHARS, exporter->output_copy_path);
	StringCchCat(exporter->output_csv_path, MAX_PATH_CHARS, TEXT(".csv"));
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

static void clean_up(Exporter* exporter)
{
	if(exporter->was_temporary_directory_created)
	{
		exporter->was_temporary_directory_created = !delete_directory_and_contents(exporter->temporary_path);
	}

	#ifndef BUILD_9X
		if( (exporter->cache_type == CACHE_INTERNET_EXPLORER) || (exporter->cache_type == CACHE_ALL) )
		{
			windows_nt_free_ntdll_functions();
			windows_nt_free_kernel32_functions();
		}
	#endif

	destroy_arena( &(exporter->arena) );
	close_log_file();
}

int _tmain(int argc, TCHAR* argv[])
{
	Exporter exporter;
	exporter.should_copy_files = true;
	exporter.should_create_csv = true;
	exporter.should_merge_copied_files = false;
	exporter.cache_type = CACHE_UNKNOWN;
	exporter.cache_path[0] = TEXT('\0');
	exporter.output_path[0] = TEXT('\0');
	exporter.is_exporting_from_default_locations = false;

	exporter.arena = NULL_ARENA;

	exporter.was_temporary_directory_created = false;
	exporter.temporary_path[0] = TEXT('\0');
	exporter.executable_path[0] = TEXT('\0');

	exporter.roaming_appdata_path[0] = TEXT('\0');
	exporter.local_appdata_path[0] = TEXT('\0');
	exporter.local_low_appdata_path[0] = TEXT('\0');

	exporter.should_use_ie_hint = false;
	exporter.ie_hint_path[0] = TEXT('\0');
	
	exporter.cache_version = 0;
	exporter.output_copy_path[0] = TEXT('\0');
	exporter.output_csv_path[0] = TEXT('\0');
	exporter.index_path[0] = TEXT('\0');

	create_log_file(TEXT("Web-Cache-Exporter.log"));
	log_print(LOG_INFO, "Startup: Running the Web Cache Exporter %hs version %hs in %hs mode.", BUILD_TARGET, BUILD_VERSION, BUILD_MODE);
	{
		const size_t ie_version_size = 32;
		TCHAR ie_version[ie_version_size] = TEXT("");
		find_internet_explorer_version(ie_version, ie_version_size);
		log_print(LOG_INFO, "Startup: Running Internet Explorer version '%s'.", ie_version);		
	}

	log_print(LOG_INFO, "foo is %d", foo);

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

	size_t memory_to_use = megabytes_to_bytes(4) * sizeof(TCHAR);
	debug_log_print("Startup: Allocating %Iu bytes for the temporary memory arena.", memory_to_use);

	if(!create_arena(&exporter.arena, memory_to_use))
	{
		log_print(LOG_ERROR, "Startup: Could not allocate enough memory to run the program.");
		clean_up(&exporter);
		return 1;
	}

	#ifndef BUILD_9X
		if( (exporter.cache_type == CACHE_INTERNET_EXPLORER) || (exporter.cache_type == CACHE_ALL) )
		{
			windows_nt_load_ntdll_functions();
			windows_nt_load_kernel32_functions();
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

	if(GetModuleFileName(NULL, exporter.executable_path, MAX_PATH_CHARS) != 0)
	{
		PathAppend(exporter.executable_path, TEXT(".."));
	}
	else
	{
		log_print(LOG_ERROR, "Startup: Failed to get the executable directory path with error code %lu.", GetLastError());
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
		log_print(LOG_ERROR, "Startup: Failed to get the roaming application data directory path with error code %lu.", GetLastError());
	}

	/*
	char working_path[MAX_PATH_CHARS] = "";
	GetCurrentDirectoryA(MAX_PATH_CHARS, working_path);
	debug_log_print("Working Directory: %s", working_path);
	*/

	debug_log_print("Exporter Options:");
	debug_log_print("- Cache Type: %s", CACHE_TYPE_TO_STRING[exporter.cache_type]);
	debug_log_print("- Should Copy Files: %hs", (exporter.should_copy_files) ? ("Yes") : ("No"));
	debug_log_print("- Should Create CSV: %hs", (exporter.should_create_csv) ? ("Yes") : ("No"));
	debug_log_print("- Should Merge Copied Files: %hs", (exporter.should_merge_copied_files) ? ("Yes") : ("No"));
	debug_log_print("- Cache Path: '%s'", exporter.cache_path);
	debug_log_print("- Output Path: '%s'", exporter.output_path);
	debug_log_print("- Is Exporting From Default Locations: %hs", (exporter.is_exporting_from_default_locations) ? ("Yes") : ("No"));
	debug_log_print("----------------------------------------");
	debug_log_print("- Temporary Path: '%s'", exporter.temporary_path);
	debug_log_print("- Executable Path: '%s'", exporter.executable_path);
	debug_log_print("- Roaming AppData Path: '%s'", exporter.roaming_appdata_path);
	debug_log_print("- Local AppData Path: '%s'", exporter.local_appdata_path);
	debug_log_print("- LocalLow AppData Path: '%s'", exporter.local_low_appdata_path);
	debug_log_print("----------------------------------------");
	debug_log_print("- Cache Version: %I32u", exporter.cache_version);
	debug_log_print("- Output Copy Path: '%s'", exporter.output_copy_path);
	debug_log_print("- Output CSV Path: '%s'", exporter.output_csv_path);
	debug_log_print("- Index Path: '%s'", exporter.index_path);

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
			_ASSERT(false);
		} break;
	}

	clean_up(&exporter);

	return 0;
}
