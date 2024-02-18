#include "cache.h"
#include "common.h"

bool batch_load(Exporter* exporter)
{
	ASSERT(exporter->batch_path != NULL, "Missing batch path");

	bool success = true;
	String* content = NULL;

	TO_TEMPORARY_ARENA()
	{
		File file = {};
		success = file_read_all(exporter->batch_path, &file);
		content = string_from_utf_8((char*) file.data);
	}

	String_View filename = path_name(exporter->batch_path);

	if(success)
	{
		bool is_profile = false;
		Key_Paths key_paths = {};

		Split_State line_state = {};
		line_state.str = content;
		line_state.delimiters = LINE_DELIMITERS;

		String_View line = {};
		while(string_split(&line_state, &line))
		{
			line = string_trim(line);
			if(string_begins_with(line, T("#"))) continue;

			if(string_is_equal(line, T("END")))
			{
				if(is_profile)
				{
					is_profile = false;

					#define KEY_PATH_CHECK(member, directive) \
						do \
						{ \
							if(key_paths.member == NULL) \
							{ \
								console_error("Missing the directive '%s' in profile '%s' in '%.*s'", T(directive), key_paths.name->data, filename.code_count, filename.data); \
								log_error("Missing the directive '%s' in profile '%s' in '%s'", T(directive), key_paths.name->data, exporter->batch_path->data); \
								success = false; \
							} \
						} while(false)

					KEY_PATH_CHECK(drive, "DRIVE");
					KEY_PATH_CHECK(windows, "WINDOWS");
					KEY_PATH_CHECK(temporary, "TEMPORARY");
					KEY_PATH_CHECK(user, "USER");
					KEY_PATH_CHECK(appdata, "APPDATA");
					KEY_PATH_CHECK(local_appdata, "LOCAL_APPDATA");
					KEY_PATH_CHECK(local_low_appdata, "LOCAL_LOW_APPDATA");
					KEY_PATH_CHECK(wininet, "INTERNET_CACHE");

					#undef KEY_PATH_CHECK

					if(success)
					{
						array_add(&exporter->key_paths, key_paths);
						ZeroMemory(&key_paths, sizeof(key_paths));
						continue;
					}
					else
					{
						break;
					}
				}
				else
				{
					console_error("Unexpected END directive in '%.*s'", filename.code_count, filename.data);
					log_error("Unexpected END directive in '%s'", exporter->batch_path->data);
					success = false;
					break;
				}
			}

			Split_State directive_state = {};
			directive_state.view = line;
			directive_state.delimiters = SPACE_DELIMITERS;

			String_View directive = {};
			String_View value = {};

			if(!string_partition(&directive_state, &directive, &value))
			{
				console_error("Missing the value for directive '%.*s' in '%.*s'", directive.code_count, directive.data, filename.code_count, filename.data);
				log_error("Missing the value for directive '%.*s' in '%s'", directive.code_count, directive.data, exporter->batch_path->data);
				success = false;
				break;
			}

			value = string_trim(value);

			if(is_profile)
			{
				if(string_is_equal(directive, T("DRIVE")))
				{
					key_paths.drive = string_from_view(value);
				}
				else if(string_is_equal(directive, T("WINDOWS")))
				{
					key_paths.windows = string_from_view(value);
				}
				else if(string_is_equal(directive, T("TEMPORARY")))
				{
					key_paths.temporary = string_from_view(value);
				}
				else if(string_is_equal(directive, T("USER")))
				{
					key_paths.user = string_from_view(value);
				}
				else if(string_is_equal(directive, T("APPDATA")))
				{
					key_paths.appdata = string_from_view(value);
				}
				else if(string_is_equal(directive, T("LOCAL_APPDATA")))
				{
					key_paths.local_appdata = string_from_view(value);
				}
				else if(string_is_equal(directive, T("LOCAL_LOW_APPDATA")))
				{
					key_paths.local_low_appdata = string_from_view(value);
				}
				else if(string_is_equal(directive, T("INTERNET_CACHE")))
				{
					key_paths.wininet = string_from_view(value);
				}
				else
				{
					console_error("Unknown directive '%.*s' in profile '%s' in '%.*s'", directive.code_count, directive.data, key_paths.name->data, filename.code_count, filename.data);
					log_error("Unknown directive '%.*s' in profile '%s' in '%s'", directive.code_count, directive.data, key_paths.name->data, exporter->batch_path->data);
					success = false;
					break;
				}
			}
			else
			{
				if(string_is_equal(directive, T("BEGIN_PROFILE")))
				{
					is_profile = true;
					key_paths.name = string_from_view(value);
				}
				else if(string_is_equal(directive, T("WALK")))
				{
					Single_Path single = {CACHE_WALK, string_from_view(value)};
					array_add(&exporter->single_paths, single);
				}
				else if(string_is_equal(directive, T("WININET")))
				{
					Single_Path single = {CACHE_WININET, string_from_view(value)};
					array_add(&exporter->single_paths, single);
				}
				else if(string_is_equal(directive, T("MOZILLA")))
				{
					Single_Path single = {CACHE_MOZILLA, string_from_view(value)};
					array_add(&exporter->single_paths, single);
				}
				else if(string_is_equal(directive, T("FLASH")))
				{
					Single_Path single = {CACHE_FLASH, string_from_view(value)};
					array_add(&exporter->single_paths, single);
				}
				else if(string_is_equal(directive, T("SHOCKWAVE")))
				{
					Single_Path single = {CACHE_SHOCKWAVE, string_from_view(value)};
					array_add(&exporter->single_paths, single);
				}
				else if(string_is_equal(directive, T("JAVA")))
				{
					Single_Path single = {CACHE_JAVA, string_from_view(value)};
					array_add(&exporter->single_paths, single);
				}
				else if(string_is_equal(directive, T("UNITY")))
				{
					Single_Path single = {CACHE_UNITY, string_from_view(value)};
					array_add(&exporter->single_paths, single);
				}
				else
				{
					console_error("Unknown directive '%.*s' in '%.*s'", directive.code_count, directive.data, filename.code_count, filename.data);
					log_error("Unknown directive '%.*s' in '%s'", directive.code_count, directive.data, exporter->batch_path->data);
					success = false;
					break;
				}
			}
		}

		if(success && is_profile)
		{
			console_error("Unterminated profile in '%.*s'", filename.code_count, filename.data);
			log_error("Unterminated profile in '%s'", exporter->batch_path->data);
			success = false;
		}

		bool empty = (exporter->single_paths->count == 0) && (exporter->key_paths->count == 0);
		if(success && empty)
		{
			console_error("No paths found in '%.*s'", filename.code_count, filename.data);
			log_error("No paths found in '%s'", exporter->batch_path->data);
			success = false;
		}
	}
	else
	{
		console_error("Failed to read '%.*s'", filename.code_count, filename.data);
		log_error("Failed to read '%s'", exporter->batch_path->data);
		success = false;
	}

	return success;
}

void batch_tests(void)
{
	console_info("Running batch tests");
	log_info("Running batch tests");

	{
		bool success = false;
		Exporter exporter = {};

		#define BATCH_LOAD(path) \
			do \
			{ \
				exporter.batch_path = CSTR(path); \
				exporter.single_paths = array_create<Single_Path>(10); \
				exporter.key_paths = array_create<Key_Paths>(10); \
				success = batch_load(&exporter); \
			} while(false)

		BATCH_LOAD("Tests\\Batch\\correct.txt");
		TEST(success, true);

		u32 expected_flags[] = {CACHE_WALK, CACHE_WININET, CACHE_MOZILLA, CACHE_FLASH, CACHE_SHOCKWAVE, CACHE_JAVA, CACHE_UNITY};

		const TCHAR* expected_paths[] =
		{
			T("D:\\Documents and Settings\\<User>"),
			T("D:\\Documents and Settings\\<User>\\Local Settings\\Temporary Internet Files"),
			T("D:\\Documents and Settings\\<User>\\Local Settings\\Application Data\\Mozilla\\Firefox\\Profiles\\<Profile>\\Cache"),
			T("D:\\Documents and Settings\\<User>\\Application Data\\Adobe\\Flash Player"),
			T("D:\\Documents and Settings\\<User>\\Application Data\\Adobe\\Shockwave Player"),
			T("D:\\Documents and Settings\\<User>\\Application Data\\Sun\\Java\\Deployment\\cache"),
			T("D:\\Documents and Settings\\<User>\\Local Settings\\Application Data\\Unity\\WebPlayer\\Cache"),
		};

		TEST(exporter.single_paths->count, (int) _countof(expected_flags));
		TEST(exporter.single_paths->count, (int) _countof(expected_paths));

		for(int i = 0; i < exporter.single_paths->count; i += 1)
		{
			Single_Path single = exporter.single_paths->data[i];
			TEST(single.flags, expected_flags[i]);
			TEST(single.path, expected_paths[i]);
		}

		TEST(exporter.key_paths->count, 1);

		Key_Paths key_paths = exporter.key_paths->data[0];
		TEST(key_paths.name, T("Profile"));
		TEST(key_paths.drive, T("D:\\"));
		TEST(key_paths.windows, T("D:\\WINDOWS"));
		TEST(key_paths.temporary, T("D:\\Documents and Settings\\<User>\\Local Settings\\Temp"));
		TEST(key_paths.user, T("D:\\Documents and Settings\\<User>"));
		TEST(key_paths.appdata, T("D:\\Documents and Settings\\<User>\\Application Data"));
		TEST(key_paths.local_appdata, T("D:\\Documents and Settings\\<User>\\Local Settings\\Application Data"));
		TEST(key_paths.local_low_appdata, T("<None>"));
		TEST(key_paths.wininet, T("D:\\Documents and Settings\\<User>\\Local Settings\\Temporary Internet Files"));

		BATCH_LOAD("Tests\\Batch\\bad_directive.txt");
		TEST(success, false);

		BATCH_LOAD("Tests\\Batch\\bad_profile_directive.txt");
		TEST(success, false);

		BATCH_LOAD("Tests\\Batch\\empty.txt");
		TEST(success, false);

		BATCH_LOAD("Tests\\Batch\\missing_profile_directive.txt");
		TEST(success, false);

		BATCH_LOAD("Tests\\Batch\\missing_value.txt");
		TEST(success, false);

		BATCH_LOAD("Tests\\Batch\\unexpected_end.txt");
		TEST(success, false);

		BATCH_LOAD("Tests\\Batch\\unterminated_profile.txt");
		TEST(success, false);

		#undef BATCH_LOAD
	}
}