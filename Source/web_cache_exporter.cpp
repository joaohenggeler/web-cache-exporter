#include "web_cache_exporter.h"
#include "memory_and_file_io.h"
#include "internet_explorer.h"

int main(int argc, char* argv[])
{
	Arena arena;
	create_arena(&arena, 1024*1024);

	create_log_file("Web-Cache-Exporter.log");

	char current_directory[MAX_PATH_CHARS] = "";
	GetCurrentDirectoryA(MAX_PATH_CHARS, current_directory);
	debug_log_print("Working Directory: %s\n", current_directory);
	debug_log_print("\n");
	debug_log_print("Args:\n");
	for(int i = 0; i < argc; ++i)
	{
		debug_log_print("%d: %s\n", i, argv[i]);
	}
	debug_log_print("\n");

	export_internet_explorer_cache(&arena, argv[1]);

	close_log_file();
	destroy_arena(&arena);

	return 0;
}
