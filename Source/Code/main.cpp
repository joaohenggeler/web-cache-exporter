#include "cache.h"
#include "common.h"

/*
	@TODO:
	- exporter: -auto-batch
*/

static bool arguments_parse_1(Array_View<TCHAR*> args);
static bool arguments_parse_2(Array_View<TCHAR*> args, Exporter* exporter);

int _tmain(int argc, TCHAR** argv)
{
	// The first argument is the executable's name.
	Array_View<TCHAR*> args = {argc - 1, argv + 1};

	if(args.count == 0)
	{
		printf("@TODO: Help message");
		return 0;
	}

	context_initialize_1();

	if(arguments_parse_1(args)) return 0;

	bool success = true;

	{
		if(!context_initialize_2())
		{
			success = false;
			goto cleanup;
		}

		Exporter exporter = {};
		if(!arguments_parse_2(args, &exporter))
		{
			success = false;
			goto cleanup;
		}

		context_initialize_3(&exporter);

		if(exporter.run_tests)
		{
			console_info("Running tests");
			log_info("Running tests");
		}

		if(context.large_tests)
		{
			console_info("Enabled large tests");
			log_info("Enabled large tests");
		}

		if(context.tiny_file_buffers)
		{
			console_info("Enabled tiny file buffers");
			log_info("Enabled tiny file buffers");
		}

		if(exporter.run_tests)
		{
			// @TODO: remove when ready
			arena_clear(context.current_arena);

			core_tests();
			context_tests();
			arena_tests();
			string_tests();
			array_tests();
			map_tests();
			time_tests();
			net_tests();
			path_tests();
			io_tests();
			hash_tests();
			decompress_tests();
			exporter_tests();
			batch_tests();
			label_tests();
			csv_tests();
			mozilla_tests();
			shockwave_tests();

			int passed_count = context.total_test_count - context.failed_test_count;
			console_info("Passed %d of %d tests", passed_count, context.total_test_count);
			log_info("Passed %d of %d tests", passed_count, context.total_test_count);

			success = (context.failed_test_count == 0);
			// @TODO: uncomment when ready
			//goto cleanup;
		}

		arena_clear(context.current_arena);
		exporter_main(&exporter);
	}

	cleanup:;
	{
		context_terminate();
	}

	return (success) ? (0) : (1);
}

#define IS_OPTION(arg, short, long) string_is_equal(arg, T(short)) || string_is_equal(arg, T(long))

static bool arguments_parse_1(Array_View<TCHAR*> args)
{
	// @NoArena
	// @NoLog

	bool terminate = false;

	for(int i = 0; i < args.count; i += 1)
	{
		TCHAR* arg = args.data[i];

		if(IS_OPTION(arg, "-v", "-version"))
		{
			printf(WCE_VERSION);
			terminate = true;
			break;
		}
		else if(IS_OPTION(arg, "-q", "-quiet"))
		{
			context.console_enabled = false;
		}
		else if(IS_OPTION(arg, "-nl", "-no-log"))
		{
			context.log_enabled = false;
		}
		else if(IS_OPTION(arg, "-lt", "-large-tests"))
		{
			context.large_tests = true;
		}
		else if(IS_OPTION(arg, "-tfb", "-tiny-file-buffers"))
		{
			context.tiny_file_buffers = true;
		}
		else if(IS_OPTION(arg, "-e", "-export")
			 || IS_OPTION(arg, "-i", "-input")
			 || IS_OPTION(arg, "-b", "-batch")
			 || IS_OPTION(arg, "-o", "-output")
			 || IS_OPTION(arg, "-td", "-temporary-directory")
			 || IS_OPTION(arg, "-pf", "-positive-filter")
			 || IS_OPTION(arg, "-nf", "-negative-filter")
			 || IS_OPTION(arg, "-if", "-filter-except"))
		{
			i += 1;
		}
	}

	return terminate;
}

static bool arguments_parse_2(Array_View<TCHAR*> args, Exporter* exporter)
{
	bool success = true;

	TO_PERMANENT_ARENA()
	{
		exporter->copy_files = true;
		exporter->create_csvs = true;
		exporter->decompress = true;

		for(int i = 0; i < args.count; i += 1)
		{
			TCHAR* arg = args.data[i];

			if(IS_OPTION(arg, "-e", "-export"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* ids = args.data[i + 1];
					success = cache_flags_from_names(ids, &exporter->cache_flags);
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-i", "-input"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* path = args.data[i + 1];
					exporter->input_path = string_from_c(path);
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-b", "-batch"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* path = args.data[i + 1];
					exporter->batch_path = string_from_c(path);
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-o", "-output"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* path = args.data[i + 1];
					exporter->output_path = string_from_c(path);
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-td", "-temporary-directory"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* path = args.data[i + 1];
					exporter->temporary_directory = string_from_c(path);
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-pf", "-positive-filter"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* path = args.data[i + 1];

					Split_State state = {};
					state.view = view_from_c(path);
					state.delimiters = T(",");

					String_View name = {};
					Array<String*>* filter = array_create<String*>(0);

					while(string_split(&state, &name))
					{
						array_add(&filter, string_from_view(name));
					}

					exporter->positive_filter = filter;
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-nf", "-negative-filter"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* path = args.data[i + 1];

					Split_State state = {};
					state.view = view_from_c(path);
					state.delimiters = T(",");

					String_View name = {};
					Array<String*>* filter = array_create<String*>(0);

					while(string_split(&state, &name))
					{
						array_add(&filter, string_from_view(name));
					}

					exporter->negative_filter = filter;
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-if", "-ignore-filter"))
			{
				if(i + 1 < args.count)
				{
					TCHAR* ids = args.data[i + 1];
					success = cache_flags_from_names(ids, &exporter->ignore_filter);
					i += 1;
				}
				else
				{
					success = false;
				}
			}
			else if(IS_OPTION(arg, "-fo", "-files-only"))
			{
				exporter->create_csvs = false;
			}
			else if(IS_OPTION(arg, "-co", "-csvs-only"))
			{
				exporter->copy_files = false;
			}
			else if(IS_OPTION(arg, "-nd", "-no-decompress"))
			{
				exporter->decompress = false;
			}
			else if(IS_OPTION(arg, "-go", "-group-origin"))
			{
				exporter->group_origin = true;
			}
			else if(IS_OPTION(arg, "-y", "-yes"))
			{
				exporter->auto_confirm = true;
			}
			else if(IS_OPTION(arg, "-rt", "-run-tests"))
			{
				exporter->run_tests = true;
			}
			#ifdef WCE_DEBUG
			else if(IS_OPTION(arg, "-dec", "-debug-empty-copy"))
			{
				exporter->empty_copy = true;
			}
			#endif
			else if(IS_OPTION(arg, "-v", "-version")
				 || IS_OPTION(arg, "-q", "-quiet")
				 || IS_OPTION(arg, "-nl", "-no-log")
				 || IS_OPTION(arg, "-lt", "-large-tests")
				 || IS_OPTION(arg, "-tfb", "-tiny-file-buffers"))
			{
				// Already parsed.
			}
			else
			{
				// @TODO: remove assert when ready
				success = false;
				ASSERT(false, "Unhandled argument");
			}

			if(!success) goto error;
		}

		if(exporter->cache_flags == 0 && !exporter->run_tests)
		{
			console_error("Missing the -e option");
			log_error("Missing -e");
			success = false;
			goto error;
		}

		// @TODO: enable when ready
		#if 0
		if(exporter->cache_flags != 0 && exporter->run_tests)
		{
			console_error("The -e and -rt options cannot be used at the same time");
			log_error("Passed -e and -rt at the same time");
			success = false;
			goto error;
		}
		#endif

		if(context.large_tests && !exporter->run_tests)
		{
			console_error("The -lt option requires -rt");
			log_error("Passed -lt without -rt");
			success = false;
			goto error;
		}

		if(!exporter->copy_files && !exporter->create_csvs)
		{
			console_error("The -fo and -co options cannot be used at the same time");
			log_error("Passed -fo and -co at the same time");
			success = false;
			goto error;
		}

		if(exporter->input_path != NULL && !flag_has_one(exporter->cache_flags))
		{
			console_error("The -i option cannot be used when exporting more than one cache format");
			log_error("Passed -i while -e specifies more than one cache format");
			success = false;
			goto error;
		}

		if(exporter->input_path != NULL && exporter->batch_path != NULL)
		{
			console_error("The -i and -b options cannot be used at the same time");
			log_error("Passed -i and -b at the same time");
			success = false;
			goto error;
		}

		if(exporter->input_path != NULL && !path_is_directory(exporter->input_path))
		{
			console_error("Cannot find the input directory '%s'", exporter->input_path->data);
			log_error("Cannot find the input directory '%s'", exporter->input_path->data);
			success = false;
			goto error;
		}

		if(exporter->batch_path != NULL && !path_is_file(exporter->batch_path))
		{
			console_error("Cannot find the batch file '%s'", exporter->batch_path->data);
			log_error("Cannot find the batch file '%s'", exporter->batch_path->data);
			success = false;
			goto error;
		}

		exporter->single_paths = array_create<Single_Path>(10);
		exporter->key_paths = array_create<Key_Paths>(10);

		if(exporter->input_path == NULL && exporter->batch_path == NULL)
		{
			array_add(&exporter->key_paths, default_key_paths());
		}
		else if(exporter->input_path != NULL)
		{
			Single_Path single = {};
			single.flag = exporter->cache_flags;
			single.path = exporter->input_path;
			array_add(&exporter->single_paths, single);
		}
		else if(exporter->batch_path != NULL)
		{
			success = batch_load(exporter);
			if(success) success = batch_check(exporter);
			if(!success) goto error;
		}
		else
		{
			ASSERT(false, "Bad state");
		}

		if(exporter->output_path == NULL)
		{
			exporter->output_path = CSTR("ExportedCache");
		}

		if(path_refers_to_same_object(exporter->output_path, CSTR(".")))
		{
			String_View directory = path_name(exporter->output_path);
			console_error("The output directory '%.*s' cannot be the current working directory", directory.code_count, directory.data);
			log_error("The output directory '%s' is the working directory", exporter->output_path->data);
			success = false;
			goto error;
		}

		if(path_is_directory(exporter->output_path))
		{
			String_View directory = path_name(exporter->output_path);

			if(exporter->auto_confirm)
			{
				console_info("Deleting the previous output directory '%.*s'..", directory.code_count, directory.data);
				log_info("Deleting the previous output directory '%s'", exporter->output_path->data);
				if(!directory_delete(exporter->output_path))
				{
					console_error("Failed to delete the previous output directory '%.*s'", directory.code_count, directory.data);
					log_error("Failed to delete the previous output directory '%s'", exporter->output_path->data);
					success = false;
					goto error;
				}
			}
			else
			{
				int option = EOF;

				do
				{
					console_prompt("Delete previous output '%.*s'? [(y)es, (n)o]:", directory.code_count, directory.data);
					option = getchar();
					while(option != '\n' && getchar() != '\n');
				} while(option != 'y' && option != 'n');

				if(option == 'y')
				{
					console_info("Deleting the previous output directory '%.*s'..", directory.code_count, directory.data);
					log_info("Deleting the previous output directory '%s'", exporter->output_path->data);
					if(!directory_delete(exporter->output_path))
					{
						console_error("Failed to delete the previous output directory '%.*s'", directory.code_count, directory.data);
						log_error("Failed to delete the previous output directory '%s'", exporter->output_path->data);
						success = false;
						goto error;
					}
				}
				else
				{
					console_info("Terminating at the user's request");
					log_info("Terminating at the user's request");
					success = false;
					goto error;
				}
			}
		}
		else if(path_is_file(exporter->output_path))
		{
			String_View directory = path_name(exporter->output_path);
			console_error("The output directory '%.*s' is already a file", directory.code_count, directory.data);
			log_error("The output directory '%s' is a file", exporter->output_path->data);
			success = false;
			goto error;
		}

		if(exporter->temporary_directory == NULL)
		{
			exporter->temporary_directory = CSTR(".temp");
		}

		label_load_all(exporter);
		label_filter_check(exporter);

		exporter->builder = builder_create(MAX_PATH_COUNT);

		error:;
	}

	return success;
}