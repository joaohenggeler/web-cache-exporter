#include "web_cache_exporter.h"
#include "file_io.h"
#include "internet_explorer.h"

extern HANDLE LOG_FILE_HANDLE;

int main(int argc, char* argv[])
{
	Arena arena;
	create_arena(&arena, 1024*1024);

	LOG_FILE_HANDLE = CreateFileA("Web-Cache-Exporter.log",
					  FILE_APPEND_DATA,
					  FILE_SHARE_READ,
					  NULL,
					  CREATE_ALWAYS,
					  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
					  NULL);

	debug_log_print("Max Path: %d\n", MAX_PATH);
	debug_log_print("\n");
	debug_log_print("Args:\n");
	for(int i = 0; i < argc; ++i)
	{
		debug_log_print("%d: %s\n", i, argv[i]);
	}
	debug_log_print("\n");

	read_internet_explorer_cache(&arena, argv[1]);

	destroy_arena(&arena);

	return 0;
}
