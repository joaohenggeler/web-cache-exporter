#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "internet_explorer.h"
#include "shockwave_plugin.h"

// wce.exe -export [Cache Type] [Cache Path] [Export Folder Destination Path]
// wce.exe -csv [Cache Type] [Cache Path] [CSV File Destination Path]
// wce.exe [extra stuff] -export-ie

static char* skip_to_suboption(char* str)
{
	char* suboption = NULL;

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

static bool parse_exporter_arguments(int num_arguments, char* arguments[], Exporter* exporter)
{
	bool success = true;

	exporter->should_copy_files = true;
	exporter->should_create_csv = true;
	exporter->should_add_csv_header = true;
	exporter->cache_type = CACHE_UNKNOWN;
	exporter->cache_path[0] = '\0';
	exporter->output_path[0] = '\0';

	for(int i = 1; i < num_arguments; ++i)
	{
		char* option = arguments[i];

		if(lstrcmpiA(option, "-no-copy-files") == 0)
		{
			exporter->should_copy_files = false;
		}
		else if(lstrcmpiA(option, "-no-create-csv") == 0)
		{
			exporter->should_create_csv = false;
		}
		else if(lstrcmpiA(option, "-no-csv-header") == 0)
		{
			exporter->should_add_csv_header = false;
		}
		else if(_strnicmp(option, "-export", 7) == 0)
		{
			char* cache_type = skip_to_suboption(option);
			//char* cache_version = skip_to_suboption(cache_type);
			if(cache_type == NULL)
			{
				log_print(LOG_ERROR, "Missing web cache type in command line option '%s'", option);
				exporter->cache_type = CACHE_UNKNOWN;
				success = false;
			}
			else if(lstrcmpiA(cache_type, "-ie") == 0)
			{
				exporter->cache_type = CACHE_INTERNET_EXPLORER;
			}
			else if(lstrcmpiA(cache_type, "-shockwave") == 0)
			{
				exporter->cache_type = CACHE_SHOCKWAVE_PLUGIN;
			}
			else if(lstrcmpiA(cache_type, "-java") == 0)
			{
				exporter->cache_type = CACHE_JAVA_PLUGIN;
			}
			else
			{
				log_print(LOG_ERROR, "Unknown web cache type '%s' in command line option '%s'", cache_type, option);
				exporter->cache_type = CACHE_UNKNOWN;
				success = false;
			}

			if(i+1 < num_arguments && strlen(arguments[i+1]) > 0)
			{
				StringCchCopyA(exporter->cache_path, MAX_PATH_CHARS, arguments[i+1]);
			}

			if(i+2 < num_arguments && strlen(arguments[i+2]) > 0)
			{
				StringCchCopyA(exporter->output_path, MAX_PATH_CHARS, arguments[i+2]);
			}
			PathAppendA(exporter->output_path, "ExportedCache");
			
			break;
		}
		else
		{
			log_print(LOG_ERROR, "Unknown command line option '%s'", option);
			success = false;
			break;
		}
	}

	if(!exporter->should_copy_files && !exporter->should_create_csv)
	{
		log_print(LOG_ERROR, "You can't use both -no-copy-files and -no-create-csv at the same time.");
		success = false;
	}

	return success;
}

int main(int argc, char* argv[])
{
	create_log_file("Web-Cache-Exporter.log");
	log_print(LOG_INFO, "Web Cache Exporter version %s", BUILD_VERSION);

	if(argc <= 1)
	{
		log_print(LOG_ERROR, "No command line arguments supplied. The program will print a help message and exit.");
		console_print("-no-copy-files\n");
		console_print("-no-create-csv\n");
		console_print("-export-[cache type], where:\n");
		console_print("\t-ie\n");
		console_print("\t-shockwave\n");
		console_print("\t-java\n");
		return 1;
	}

	Exporter exporter;
	if(!parse_exporter_arguments(argc, argv, &exporter))
	{
		log_print(LOG_ERROR, "An error occured while parsing the command line arguments. The program will not run.");
		return 1;
	}

	if(!create_arena(&exporter.arena, megabytes_to_bytes(1)))
	{
		log_print(LOG_ERROR, "Could not allocate enough memory to run the program.");
		return 1;
	}

	char executable_path[MAX_PATH_CHARS] = "";
	GetModuleFileNameA(NULL, executable_path, MAX_PATH_CHARS);
	debug_log_print("Executable Directory: %s", executable_path);
	
	char working_path[MAX_PATH_CHARS] = "";
	GetCurrentDirectoryA(MAX_PATH_CHARS, working_path);
	debug_log_print("Working Directory: %s", working_path);

	debug_log_print("Parsed Exporter Arguments:");
	debug_log_print("- Cache Type: %s", CACHE_TYPE_TO_STRING[exporter.cache_type]);
	debug_log_print("- Should Copy Files: %s", (exporter.should_copy_files) ? ("true") : ("false"));
	debug_log_print("- Should Create CSV: %s", (exporter.should_create_csv) ? ("true") : ("false"));
	debug_log_print("- Should Add CSV Header: %s", (exporter.should_add_csv_header) ? ("true") : ("false"));
	debug_log_print("- Cache Path: '%s'", (exporter.cache_path != NULL) ? (exporter.cache_path) : ("-"));
	debug_log_print("- Output Path: '%s'", (exporter.output_path != NULL) ? (exporter.output_path) : ("-"));

	if(is_string_empty(exporter.cache_path))
	{
		log_print(LOG_INFO, "No cache path specified. Exporting the cache from any existing default directories.");
	}

	log_print(LOG_NONE, "");

	switch(exporter.cache_type)
	{
		case(CACHE_INTERNET_EXPLORER):
		{
			//export_internet_explorer_cache(&arena, exporter.cache_path);
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

	destroy_arena(&exporter.arena);
	close_log_file();

	return 0;
}
