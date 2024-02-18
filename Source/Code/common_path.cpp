#include "common.h"

// MAX_PATH = drive (3) + path (256) + null terminator (1)
const int MAX_PATH_COUNT = MAX_PATH - 1;
String* NO_PATH = NULL;

bool path_is_file(String* path)
{
	// @GetLastError: see COLLISION in exporter_copy.
	u32 error = GetLastError();
	u32 attributes = GetFileAttributes(path->data);
	SetLastError(error);
	return (attributes != INVALID_FILE_ATTRIBUTES) && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool path_is_directory(String* path)
{
	// @GetLastError: see COLLISION in exporter_copy.
	u32 error = GetLastError();
	u32 attributes = GetFileAttributes(path->data);
	SetLastError(error);
	return (attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

template<typename String_Type_1, typename String_Type_2>
static bool path_is_equal(String_Type_1 a, String_Type_2 b)
{
	return string_is_equal(a, b, IGNORE_CASE);
}

template<typename String_Type_1, typename String_Type_2>
static bool path_ends_with(String_Type_1 a, String_Type_2 b)
{
	return string_ends_with(a, b, IGNORE_CASE);
}

bool path_refers_to_same_object(String* a, String* b)
{
	bool result = false;

	if(path_is_directory(a) && path_is_directory(b))
	{
		if(windows_is_9x())
		{
			// We have to resort to this method on Windows 9x since we can't create directory handles.
			// We also can't use functions like stat since they don't return meaningful values on Windows.
			// See: https://www.boost.org/doc/libs/1_53_0/libs/filesystem/doc/reference.html#equivalent
			ARENA_SAVEPOINT()
			{
				String_Builder* a_builder = builder_create(MAX_PATH_COUNT);
				String_Builder* b_builder = builder_create(MAX_PATH_COUNT);

				result = (PathCanonicalize(a_builder->data, a->data) != FALSE)
					  && (PathCanonicalize(b_builder->data, b->data) != FALSE)
					  && (GetLongPathName(a_builder->data, a_builder->data, a_builder->capacity) != 0)
					  && (GetLongPathName(b_builder->data, b_builder->data, b_builder->capacity) != 0)
					  && path_is_equal(a_builder->data, b_builder->data);
			}
		}
		else
		{
			HANDLE a_handle = directory_metadata_handle_create(a);
			HANDLE b_handle = directory_metadata_handle_create(b);
			result = handle_refers_to_same_object(a_handle, b_handle);
			handle_close(&a_handle);
			handle_close(&b_handle);
		}
	}
	else
	{
		HANDLE a_handle = metadata_handle_create(a);
		HANDLE b_handle = metadata_handle_create(b);
		result = handle_refers_to_same_object(a_handle, b_handle);
		handle_close(&a_handle);
		handle_close(&b_handle);
	}

	return result;
}

Array<String*>* path_unique_directories(Array<String*>* paths)
{
	Array<String*>* unique_paths = array_create<String*>(0);

	for(int i = 0; i < paths->count; i += 1)
	{
		String* path = paths->data[i];
		if(!path_is_directory(path)) continue;

		bool unique = true;

		for(int j = 0; j < unique_paths->count; j += 1)
		{
			String* previous_path = unique_paths->data[j];
			if(path_refers_to_same_object(path, previous_path))
			{
				unique = false;
				break;
			}
		}

		if(unique) array_add(&unique_paths, path);
	}

	return unique_paths;
}

Path_Parts path_parse(String* path)
{
	Path_Parts parts = {};

	{
		Split_State state = {};
		state.str = path;
		state.delimiters = PATH_DELIMITERS;
		state.keep_empty = true;
		state.reverse = true;

		string_partition(&state, &parts.name, &parts.parent);
	}

	{
		Split_State state = {};
		state.view = parts.name;
		state.delimiters = T(".");
		state.keep_empty = true;
		state.reverse = true;

		bool split = string_partition(&state, &parts.extension, &parts.stem);

		if(!split)
		{
			parts.stem = parts.extension;
			parts.extension = EMPTY_VIEW;
		}
	}

	return parts;
}

String_View path_parent(String* path)
{
	return path_parse(path).parent;
}

String_View path_name(String* path)
{
	return path_parse(path).name;
}

String_View path_stem(String* path)
{
	return path_parse(path).stem;
}

String_View path_extension(String* path)
{
	return path_parse(path).extension;
}

bool path_has_extension(String* path, const TCHAR* extension)
{
	String_View ext = path_extension(path);
	return path_is_equal(ext, extension);
}

String* path_absolute(String* path)
{
	int count = GetFullPathName(path->data, 0, NULL, NULL);
	String_Builder* builder = builder_create(count);
	bool success = GetFullPathName(path->data, builder->capacity, builder->data, NULL) != 0;
	if(!success) log_error("Failed to get the absolute path of '%s' with the error: %s", path->data, last_error_message());
	return (success) ? (builder_terminate(&builder)) : (path);
}

static String* path_safe_chars(String* path)
{
	bool unsafe = false;

	TCHAR _reserved[] = {T('<'), T('>'), T('\"'), T('|'), T('?'), T('*')};
	Array_View<TCHAR> reserved = ARRAY_VIEW_FROM_C(_reserved);

	{
		String_View chr = {};

		for(int i = 0; string_next_char(path, &chr); i += 1)
		{
			if(chr.code_count > 1) continue;

			TCHAR code = chr.data[0];

			if(array_has(reserved, code))
			{
				unsafe = true;
				break;
			}
			else if(code == T(':') && i != 1)
			{
				unsafe = true;
				break;
			}
			else if(code == T('/'))
			{
				unsafe = true;
				break;
			}
			else if(1 <= code && code <= 31)
			{
				unsafe = true;
				break;
			}
		}
	}

	String* result = path;

	if(unsafe)
	{
		String_View chr = {};
		String_Builder* builder = builder_create(path->code_count);

		for(int i = 0; string_next_char(path, &chr); i += 1)
		{
			if(chr.code_count > 1)
			{
				builder_append(&builder, chr);
				continue;
			}

			TCHAR code = chr.data[0];

			if(array_has(reserved, code))
			{
				builder_append(&builder, T("_"));
			}
			else if(code == T(':') && i != 1)
			{
				builder_append(&builder, T("_"));
			}
			else if(code == T('/'))
			{
				builder_append(&builder, T("\\"));
			}
			else if(1 <= code && code <= 31)
			{
				// Skip character.
			}
			else
			{
				builder_append(&builder, chr);
			}
		}

		result = builder_terminate(&builder);
	}

	return result;
}

static String* path_safe_slashes(String* path)
{
	bool unsafe = false;

	{
		Split_State state = {};
		state.str = path;
		state.delimiters = PATH_DELIMITERS;
		state.keep_empty = true; // Catch consecutive slashes.

		String_View component = {};

		while(string_split(&state, &component))
		{
			if(component.char_count == 0)
			{
				unsafe = true;
				break;
			}
		}
	}

	String* result = path;

	if(unsafe)
	{
		Split_State state = {};
		state.str = path;
		state.delimiters = PATH_DELIMITERS;

		String_View component = {};
		String_Builder* builder = builder_create(path->code_count);

		while(string_split(&state, &component))
		{
			builder_append_path(&builder, component);
		}

		result = builder_terminate(&builder);
	}

	return result;
}

static String* path_safe_components(String* path)
{
	bool unsafe = false;

	{
		Split_State state = {};
		state.str = path;
		state.delimiters = PATH_DELIMITERS;

		String_View component = {};

		while(string_split(&state, &component))
		{
			if(component.code_count > context.max_component_count)
			{
				unsafe = true;
				break;
			}
		}
	}

	String* result = path;

	if(unsafe)
	{
		Split_State state = {};
		state.str = path;
		state.delimiters = PATH_DELIMITERS;

		String_View component = {};
		String_Builder* builder = builder_create(path->code_count);

		while(string_split(&state, &component))
		{
			String_View truncated = string_slice(component, 0, context.max_component_count);
			builder_append_path(&builder, truncated);
		}

		result = builder_terminate(&builder);
	}

	return result;
}

static String* path_safe_names(String* path)
{
	bool unsafe = false;

	const TCHAR* _reserved[] =
	{
		T("AUX"),
		T("COM1"), T("COM2"), T("COM3"), T("COM4"), T("COM5"), T("COM6"), T("COM7"), T("COM8"), T("COM9"),
		T("CON"),
		T("LPT1"), T("LPT2"), T("LPT3"), T("LPT4"), T("LPT5"), T("LPT6"), T("LPT7"), T("LPT8"), T("LPT9"),
		T("NUL"),
		T("PRN"),
	};

	Array_View<const TCHAR*> reserved = ARRAY_VIEW_FROM_C(_reserved);

	{
		Split_State state = {};
		state.str = path;
		state.delimiters = PATH_DELIMITERS;

		String_View component = {};

		while(string_split(&state, &component))
		{
			if(path_ends_with(component, T(" ")) || path_ends_with(component, T(".")))
			{
				unsafe = true;
				break;
			}

			Split_State state = {};
			state.view = component;
			state.delimiters = T(".");

			String_View stem = {};
			String_View rest = {};
			string_partition(&state, &stem, &rest);
			stem = string_trim(stem);

			for(int i = 0; i < reserved.count; i += 1)
			{
				const TCHAR* name = reserved.data[i];
				if(path_is_equal(stem, name))
				{
					unsafe = true;
					goto break_outer;
				}
			}
		}

		break_outer:;
	}

	String* result = path;

	if(unsafe)
	{
		Split_State state = {};
		state.str = path;
		state.delimiters = PATH_DELIMITERS;

		String_View component = {};
		String_Builder* builder = builder_create(path->code_count);

		while(string_split(&state, &component))
		{
			bool escape_begin = false;
			bool escape_end = false;

			escape_end = path_ends_with(component, T(" ")) || path_ends_with(component, T("."));

			Split_State state = {};
			state.view = component;
			state.delimiters = T(".");

			String_View stem = {};
			String_View rest = {};
			string_partition(&state, &stem, &rest);
			stem = string_trim(stem);

			for(int i = 0; i < reserved.count; i += 1)
			{
				const TCHAR* name = reserved.data[i];
				if(path_is_equal(stem, name))
				{
					escape_begin = true;
					break;
				}
			}

			if(escape_begin)
			{
				builder_append_path(&builder, T("_"));
				builder_append(&builder, component);
			}
			else
			{
				builder_append_path(&builder, component);
			}

			if(escape_end) builder_append(&builder, T("_"));
		}

		result = builder_terminate(&builder);
	}

	return result;
}

String* path_safe(String* path)
{
	// See: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file#naming-conventions
	String* result = path_safe_chars(path);
	result = path_safe_slashes(result);
	result = path_safe_components(result);
	result = path_safe_names(result);
	return result;
}

bool path_from_csidl(int csidl, String** path)
{
	bool success = false;
	String_Builder* builder = builder_create(MAX_PATH_COUNT);

	#ifdef WCE_9X
		success = SHGetSpecialFolderPath(NULL, builder->data, csidl, FALSE) != FALSE;
	#else
		// In Windows 2000 and older: hToken must be NULL.
		success = SUCCEEDED(SHGetFolderPath(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, builder->data));
	#endif

	if(success) *path = builder_terminate(&builder);
	else log_error("Failed with the error: %s", last_error_message());

	return success;
}

const GUID KFID_LOCAL_LOW_APPDATA = {0xA520A1A4, 0x1780, 0x4FF6, 0xBD, 0x18, 0x16, 0x73, 0x43, 0xC5, 0xAF, 0x16};

bool path_from_kfid(const GUID & kfid, String** path)
{
	bool success = false;

	wchar_t* result = NULL;
	success = SUCCEEDED(SHGetKnownFolderPath(kfid, 0, NULL, &result));

	if(success) *path = string_from_utf_16_le(result);
	else log_error("Failed with the error: %s", last_error_message());

	CoTaskMemFree(result);

	return success;
}

void walk_begin(Walk_State* state)
{
	ASSERT(state->base_path != NULL, "Missing base path");
	ASSERT(state->query != NULL, "Missing query");
	ASSERT(state->files || state->directories, "Must visit files or directories");
	ASSERT(state->max_depth >= -1, "Invalid max depth");

	state->_handle = INVALID_HANDLE_VALUE;
	state->_builder = builder_create(MAX_PATH_COUNT);

	Walk_Node first = {};
	first.path = state->base_path;
	first.depth = 0;

	const int NODE_CAPACITY = (state->max_depth == 0) ? (1) : (8);
	state->_next_nodes = array_create<Walk_Node>(NODE_CAPACITY);
	array_add(&state->_next_nodes, first);

	state->_saved_size = arena_save(context.current_arena);

	#ifdef WCE_DEBUG
		context.debug_walk_balance += 1;
	#endif
}

void walk_end(Walk_State* state)
{
	if(state->_handle != INVALID_HANDLE_VALUE) FindClose(state->_handle);

	state->_handle = INVALID_HANDLE_VALUE;
	state->_builder = NULL;
	state->_next_nodes = NULL;

	arena_restore(context.current_arena, state->_saved_size);

	#ifdef WCE_DEBUG
		context.debug_walk_balance -= 1;
	#endif
}

static void walk_add_subdirectories(Walk_State* state)
{
	builder_clear(state->_builder);
	builder_append_path(&state->_builder, state->_current.path);
	builder_append_path(&state->_builder, T("*"));

	WIN32_FIND_DATA find = {};
	HANDLE handle = FindFirstFile(state->_builder->data, &find);
	bool found = (handle != INVALID_HANDLE_VALUE);

	while(found)
	{
		bool is_directory = (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		bool is_valid = !string_is_equal(find.cFileName, T(".")) && !string_is_equal(find.cFileName, T(".."));

		if(is_directory && is_valid)
		{
			builder_clear(state->_builder);
			builder_append_path(&state->_builder, state->_current.path);
			builder_append_path(&state->_builder, find.cFileName);

			Walk_Node node = {};
			node.path = builder_to_string(state->_builder);
			node.depth = state->_current.depth + 1;
			array_add(&state->_next_nodes, node);

			// Save the new path so it doesn't get clobbered if the arena is cleared.
			// The saved size isn't updated because we want to restore the initial one.
			arena_save(context.current_arena);
		}

		found = FindNextFile(handle, &find) != FALSE;
	}

	if(handle != INVALID_HANDLE_VALUE) FindClose(handle);
}

bool walk_next(Walk_State* state, Walk_Info* info)
{
	bool success = false;

	info->_state = state;

	while(true)
	{
		WIN32_FIND_DATA find = {};
		bool found = false;

		if(state->_handle == INVALID_HANDLE_VALUE)
		{
			bool popped = array_pop_end(state->_next_nodes, 0, &state->_current);
			if(!popped) break;

			if(state->max_depth == -1 || state->_current.depth + 1 <= state->max_depth)
			{
				walk_add_subdirectories(state);
			}

			builder_clear(state->_builder);
			builder_append_path(&state->_builder, state->_current.path);
			builder_append_path(&state->_builder, state->query);

			state->_handle = FindFirstFile(state->_builder->data, &find);
			found = state->_handle != INVALID_HANDLE_VALUE;
		}
		else
		{
			found = FindNextFile(state->_handle, &find) != FALSE;
		}

		if(found)
		{
			bool is_directory = (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			bool can_process = (state->files && !is_directory) || (state->directories && is_directory);
			bool is_valid = !string_is_equal(find.cFileName, T(".")) && !string_is_equal(find.cFileName, T(".."));

			if(can_process && is_valid)
			{
				success = true;

				builder_clear(state->_builder);
				builder_append_path(&state->_builder, state->_current.path);
				builder_append_path(&state->_builder, find.cFileName);

				if(state->copy)
				{
					info->path = builder_to_string(state->_builder);
				}
				else
				{
					info->iter_path = state->_builder->data;
				}

				info->size = u32s_to_u64(find.nFileSizeLow, find.nFileSizeHigh);
				info->is_directory = is_directory;
				info->depth = state->_current.depth;

				info->creation_time = find.ftCreationTime;
				info->last_access_time = find.ftLastAccessTime;
				info->last_write_time = find.ftLastWriteTime;

				break;
			}
		}
		else
		{
			if(state->_handle != INVALID_HANDLE_VALUE) FindClose(state->_handle);
			state->_handle = INVALID_HANDLE_VALUE;
		}
	}

	return success;
}

const bool SORT_PATHS = true;

Array<Walk_Info>* walk_all(Walk_State* state, bool sort_paths)
{
	Array<Walk_Info>* array = array_create<Walk_Info>(0);

	state->copy = true;

	WALK_DEFER(state)
	{
		Walk_Info info = {};
		while(walk_next(state, &info))
		{
			array_add(&array, info);
		}
	}

	if(sort_paths) array_sort(array);

	return array;
}

void path_tests(void)
{
	console_info("Running path tests");
	log_info("Running path tests");

	{
		String* file = CSTR("Tests\\IO\\file.txt");
		String* directory = CSTR("Tests\\IO");

		TEST(path_is_file(file), true);
		TEST(path_is_file(directory), false);

		TEST(path_is_directory(directory), true);
		TEST(path_is_directory(file), false);
	}

	{
		String* file_1 = CSTR("Tests\\Path\\1.txt");
		String* file_2 = CSTR("Tests\\Path\\2.txt");
		String* file_3 = CSTR("Tests\\.\\Path\\..\\Path\\2.txt");

		String* directory_1 = CSTR("Tests");
		String* directory_2 = CSTR("Tests\\Path");
		String* directory_3 = CSTR("Tests\\.\\Path\\..\\Path");
		String* directory_4 = CSTR("Tests\\Decompress");
		String* directory_5 = CSTR("Tests\\DECOMP~1");

		bool result = false;

		result = path_refers_to_same_object(file_1, file_2);
		TEST(result, false);

		result = path_refers_to_same_object(file_2, file_3);
		TEST(result, true);

		result = path_refers_to_same_object(directory_1, directory_2);
		TEST(result, false);

		result = path_refers_to_same_object(directory_2, directory_3);
		TEST(result, true);

		result = path_refers_to_same_object(directory_4, directory_5);
		TEST(result, true);

		Array<String*>* paths = array_create<String*>(0);
		array_add(&paths, file_1);
		array_add(&paths, file_2);
		array_add(&paths, file_3);
		array_add(&paths, directory_1);
		array_add(&paths, directory_2);
		array_add(&paths, directory_3);
		array_add(&paths, directory_4);
		array_add(&paths, directory_5);

		TEST(paths->count, 8);
		paths = path_unique_directories(paths);
		TEST(paths->count, 3);
		TEST(paths->data[0], directory_1);
		TEST(paths->data[1], directory_2);
		TEST(paths->data[2], directory_4);
	}

	{
		#define TEST_PARSE(_path, _parent, _name, _stem, _extension) \
			do \
			{ \
				Path_Parts parts = path_parse(CSTR(_path)); \
				TEST(parts.parent, T(_parent)); \
				TEST(parts.name, T(_name)); \
				TEST(parts.stem, T(_stem)); \
				TEST(parts.extension, T(_extension)); \
			} while(false);

		TEST_PARSE("C:\\Path\\file.ext", "C:\\Path", "file.ext", "file", "ext");
		TEST_PARSE("C:\\Path\\file.ext.gz", "C:\\Path", "file.ext.gz", "file.ext", "gz");
		TEST_PARSE("C:\\Path\\file.", "C:\\Path", "file.", "file", "");
		TEST_PARSE("C:\\Path\\file", "C:\\Path", "file", "file", "");
		TEST_PARSE("C:\\Path\\", "C:\\Path", "", "", "");

		TEST_PARSE("file.ext", "", "file.ext", "file", "ext");
		TEST_PARSE("file.ext.gz", "", "file.ext.gz", "file.ext", "gz");
		TEST_PARSE("file.", "", "file.", "file", "");
		TEST_PARSE("file", "", "file", "file", "");
		TEST_PARSE("", "", "", "", "");

		#undef TEST_PARSE
	}

	{
		TEST(path_has_extension(CSTR("C:\\Path\\file.ext"), T("ext")), true);
		TEST(path_has_extension(CSTR("C:\\Path\\file.ext.gz"), T("gz")), true);
		TEST(path_has_extension(CSTR("file.ext"), T("ext")), true);
		TEST(path_has_extension(CSTR("file.ext.gz"), T("gz")), true);
		TEST(path_has_extension(CSTR(""), T("")), true);
	}

	{
		String* relative = CSTR("Tests\\IO");
		String* absolute = path_absolute(relative);
		TEST_NOT(relative, absolute);
	}

	{
		TEST(path_safe(CSTR("C:\\Path\\file.ext")), CSTR("C:\\Path\\file.ext"));
		TEST(path_safe(CSTR("C:\\Path \\file.")), CSTR("C:\\Path _\\file._"));
		TEST(path_safe(CSTR("C:\\AUX\\con.ext\\NUL.ext.gz")), CSTR("C:\\_AUX\\_con.ext\\_NUL.ext.gz"));
		TEST(path_safe(CSTR("C:\\NULA\\NUL~\\NUL.\\ NUL .ext")), CSTR("C:\\NULA\\NUL~\\_NUL._\\_ NUL .ext"));
		TEST(path_safe(CSTR("C:\\\\\\Path\\\\\\file.ext")), CSTR("C:\\Path\\file.ext"));
		TEST(path_safe(CSTR("C:\\<>\"|?*:\t\r\n\\file.ext")), CSTR("C:\\_______\\file.ext"));
		TEST(path_safe(CSTR("//path//file.ext//")), CSTR("path\\file.ext"));
		TEST(path_safe(CSTR("")), CSTR(""));

		{
			String_Builder* builder = builder_create(MAX_PATH_COUNT);
			builder_append_path(&builder, T("C:"));

			for(int i = 0; i < context.max_component_count; i += 1)
			{
				if(i == 0) builder_append_path(&builder, T("A"));
				else builder_append(&builder, T("A"));
			}

			builder_append(&builder, T("BBBBB"));
			builder_append_path(&builder, T("CCCCC"));

			Split_State state = {};
			state.str = path_safe(builder_terminate(&builder));
			state.delimiters = PATH_DELIMITERS;

			Array<String_View>* components = string_split_all(&state);
			TEST(components->count, 3);
			TEST(components->data[0], T("C:"));
			TEST(components->data[1].code_count, context.max_component_count);
			TEST(string_begins_with(components->data[1], T("A")), true);
			TEST(string_ends_with(components->data[1], T("A")), true);
			TEST(components->data[2], T("CCCCC"));
		}
	}

	{
		String* path = CSTR("Tests\\Path");

		{
			Walk_State state = {};
			state.base_path = path;
			state.query = T("*.test");
			state.files = true;
			state.max_depth = -1;

			Array<Walk_Info>* array = walk_all(&state);
			TEST(array->count, 1);
			TEST(array->data[0].size, 44ULL);
			TEST(array->data[0].is_directory, false);
			TEST(array->data[0].depth, 2);
		}

		{
			Walk_State state = {};
			state.base_path = path;
			state.query = T("*");
			state.files = true;
			state.directories = true;
			state.max_depth = 1;

			Array<Walk_Info>* array = walk_all(&state);
			TEST(array->count, 7);
		}
	}
}