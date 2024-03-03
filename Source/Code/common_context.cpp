#include "cache.h"
#include "common.h"

Context context = {};

static Arena temporary_arena;
static Arena permanent_arena;

void context_initialize_1(void)
{
	// @NoArena
	// @NoLog

	// Since Windows 8.1: always returns 6.2 (Windows 8) for non-manifested
	// applications. This results in deprecation warnings in newer Visual
	// Studio versions.
	#pragma warning(push)
	#pragma warning(disable : 4996)
	OSVERSIONINFO os_version = {};
	os_version.dwOSVersionInfoSize = sizeof(os_version);
	if(GetVersionEx(&os_version) != FALSE)
	#pragma warning(pop)
	{
		context.major_os_version = os_version.dwMajorVersion;
		context.minor_os_version = os_version.dwMinorVersion;
	}
	else
	{
		context.major_os_version = 6;
		context.minor_os_version = 2;
	}

	SYSTEM_INFO info = {};
	GetSystemInfo(&info);
	context.page_size = info.dwPageSize;

	DWORD max_component_count = 0;
	if(GetVolumeInformationA(NULL, NULL, 0, NULL, &max_component_count, NULL, NULL, 0) != FALSE)
	{
		context.max_component_count = max_component_count;
	}
	else
	{
		log_warning("Could not get the maximum component count with the error: %s", last_error_message());
		context.max_component_count = 255;
	}

	// Since Windows XP: always succeeds.
	LARGE_INTEGER frequency = {};
	bool query_success = QueryPerformanceFrequency(&frequency) != 0;
	context.performance_counter_frequency = (query_success) ? (frequency.QuadPart) : (1);

	context.console_enabled = true;
	context.log_enabled = true;
	context.log_handle = INVALID_HANDLE_VALUE;
}

bool context_initialize_2(void)
{
	if(context.log_enabled) log_create();

	console_info("Web Cache Exporter %hs (%hs)", WCE_VERSION, WCE_DATE);
	log_info("Web Cache Exporter %hs (%hs) compiled with Visual Studio %d in %hs mode for %hs %hs running on Windows %I32u.%I32u",
			  WCE_VERSION, WCE_DATE, _MSC_VER, WCE_MODE, WCE_FAMILY, WCE_ARCH, context.major_os_version, context.minor_os_version);

	dll_initialize();

	size_t temporary_size = from_megabytes(1) * sizeof(TCHAR);
	size_t permanent_size = from_megabytes(1) * sizeof(TCHAR);

	bool success = arena_create(&temporary_arena, temporary_size)
				&& arena_create(&permanent_arena, permanent_size);

	if(success)
	{
		context.current_arena = &temporary_arena;

		TO_PERMANENT_ARENA()
		{
			NO_PATH = CSTR("<None>");

			String_Builder* builder = builder_create(MAX_PATH_COUNT);
			if(GetModuleFileName(NULL, builder->data, builder->capacity) != 0)
			{
				String* path = builder_terminate(&builder);
				context.executable_path = string_from_view(path_parent(path));
			}
			else
			{
				log_error("Failed to get the executable path with error: %s", last_error_message());
				context.executable_path = CSTR(".");
			}
		}
	}

	return success;
}

void context_initialize_3(Exporter* exporter)
{
	TO_PERMANENT_ARENA()
	{
		context.temporary_path = path_absolute(exporter->temporary_directory);
		if(path_is_directory(context.temporary_path)) directory_delete(context.temporary_path);
		context.has_temporary = directory_create(context.temporary_path);
		if(!context.has_temporary) log_error("Failed to create the temporary directory");
	}
}

void context_terminate(void)
{
	if(context.has_temporary)
	{
		if(!directory_delete(context.temporary_path))
		{
			String_View directory = path_name(context.temporary_path);
			console_error("Failed to delete the temporary directory '%.*s'", directory.code_count, directory.data);
			log_error("Failed to delete the temporary directory '%s'", context.temporary_path->data);
		}
	}

	#if defined(WCE_DEBUG) || defined(WCE_9X)
		arena_destroy(&temporary_arena);
		arena_destroy(&permanent_arena);
	#endif

	dll_terminate();

	log_close();

	ASSERT(context.debug_walk_balance == 0, "Unbalanced walk begin and end");
	ASSERT(context.debug_file_read_balance == 0, "Unbalanced file read begin and end");
	ASSERT(context.debug_file_write_balance == 0, "Unbalanced file write begin and end");
	ASSERT(context.debug_file_temporary_balance == 0, "Unbalanced file temporary begin and end");
	ASSERT(context.debug_file_map_balance == 0, "Unbalanced file map begin and end");
	ASSERT(context.debug_timer_balance == 0, "Unbalanced timer begin and end");
	ASSERT(context.debug_exporter_balance == 0, "Unbalanced exporter begin and end");
	ASSERT(context.debug_report_balance == 0, "Unbalanced report begin and end");
}

bool windows_is_9x(void)
{
	return context.major_os_version == 4;
}

Arena* context_temporary_arena(void)
{
	return &temporary_arena;
}

Arena* context_permanent_arena(void)
{
	return &permanent_arena;
}

void context_tests(void)
{
	console_info("Running context tests");
	log_info("Running context tests");

	{
		TEST(context.current_arena, &temporary_arena);

		TO_PERMANENT_ARENA()
		{
			TEST(context.current_arena, &permanent_arena);

			TO_TEMPORARY_ARENA()
			{
				TEST(context.current_arena, &temporary_arena);

				TO_PERMANENT_ARENA()
				{
					TEST(context.current_arena, &permanent_arena);
				}

				TEST(context.current_arena, &temporary_arena);
			}

			TEST(context.current_arena, &permanent_arena);
		}

		TEST(context.current_arena, &temporary_arena);

		TO_PERMANENT_ARENA()
		{
			TEST(context.current_arena, &permanent_arena);

			TO_PERMANENT_ARENA()
			{
				TEST(context.current_arena, &permanent_arena);

				TO_TEMPORARY_ARENA()
				{
					TEST(context.current_arena, &temporary_arena);

					TO_TEMPORARY_ARENA()
					{
						TEST(context.current_arena, &temporary_arena);
					}

					TEST(context.current_arena, &temporary_arena);
				}

				TEST(context.current_arena, &permanent_arena);
			}

			TEST(context.current_arena, &permanent_arena);
		}

		TEST(context.current_arena, &temporary_arena);
	}
}