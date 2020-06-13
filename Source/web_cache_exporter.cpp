#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "internet_explorer.h"
#include "shockwave_plugin.h"

// wce.exe -export [Cache Type] [Cache Path] [Export Folder Destination Path]
// wce.exe -csv [Cache Type] [Cache Path] [CSV File Destination Path]
// wce.exe [extra stuff] -export-ie

static TCHAR* skip_to_suboption(TCHAR* str)
{
	TCHAR* suboption = NULL;

	if(str != NULL)
	{
		if(*str == '-')
		{
			++str;
		}

		while(*str != '\0' && *str != '-')
		{
			++str;
		}

		suboption = (*str != '\0') ? (str) : (NULL);
	}

	return suboption;
}

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
		else if(lstrcmpi(option, TEXT("-no-csv-header")) == 0)
		{
			exporter->should_add_csv_header = false;
		}
		else if(_tcsncicmp(option, TEXT("-export"), 7) == 0)
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
			PathAppend(exporter->output_path, TEXT("ExportedCache"));
			
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

	return success;
}

static void clean_up(Exporter* exporter)
{
	destroy_arena(&exporter->arena);
	close_log_file();
}

int _tmain(int argc, TCHAR* argv[])
{
	Exporter exporter;
	exporter.should_copy_files = true;
	exporter.should_create_csv = true;
	exporter.should_add_csv_header = true;
	exporter.cache_type = CACHE_UNKNOWN;
	exporter.cache_path[0] = TEXT('\0');
	exporter.output_path[0] = TEXT('\0');
	exporter.arena = NULL_ARENA;

	create_log_file(TEXT("Web-Cache-Exporter.log"));
	log_print(LOG_INFO, "Web Cache Exporter version %hs", BUILD_VERSION);

	/*if(argc <= 1)
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
	}*/

	if(!parse_exporter_arguments(argc, argv, &exporter))
	{
		log_print(LOG_ERROR, "An error occured while parsing the command line arguments. The program will not run.");
		clean_up(&exporter);
		return 1;
	}

	if(!create_arena(&exporter.arena, megabytes_to_bytes(4)))
	{
		log_print(LOG_ERROR, "Could not allocate enough memory to run the program.");
		clean_up(&exporter);
		return 1;
	}

	/*
	char executable_path[MAX_PATH_CHARS] = "";
	GetModuleFileNameA(NULL, executable_path, MAX_PATH_CHARS);
	PathAppendA(executable_path, "..");
	debug_log_print("Executable Directory: %s", executable_path);
	
	char working_path[MAX_PATH_CHARS] = "";
	GetCurrentDirectoryA(MAX_PATH_CHARS, working_path);
	debug_log_print("Working Directory: %s", working_path);
	*/

	debug_log_print("Exporter Options:");
	debug_log_print("- Cache Type: %s", CACHE_TYPE_TO_STRING[exporter.cache_type]);
	debug_log_print("- Should Copy Files: %hs", (exporter.should_copy_files) ? ("true") : ("false"));
	debug_log_print("- Should Create CSV: %hs", (exporter.should_create_csv) ? ("true") : ("false"));
	debug_log_print("- Should Add CSV Header: %hs", (exporter.should_add_csv_header) ? ("true") : ("false"));
	debug_log_print("- Cache Path: '%s'", (exporter.cache_path != NULL) ? (exporter.cache_path) : (TEXT("-")));
	debug_log_print("- Output Path: '%s'", (exporter.output_path != NULL) ? (exporter.output_path) : (TEXT("-")));

	if(is_string_empty(exporter.cache_path))
	{
		log_print(LOG_INFO, "No cache path specified. Exporting the cache from any existing default directories.");
	}

	log_print(LOG_NONE, "");

	switch(exporter.cache_type)
	{
		case(CACHE_INTERNET_EXPLORER):
		{
			//export_specific_or_default_internet_explorer_cache(&exporter);
		} break;

		case(CACHE_SHOCKWAVE_PLUGIN):
		{
			export_specific_or_default_shockwave_plugin_cache(&exporter);
		} break;

		case(CACHE_JAVA_PLUGIN):
		{

		} break;

		default:
		{
			_ASSERT(false);
		} break;
	}

	clean_up(&exporter);

	return 0;
}
