#include "common.h"

static String _EMPTY_STRING = {};
String* EMPTY_STRING = &_EMPTY_STRING;
const String_View EMPTY_VIEW = {};
const char* EMPTY_UTF_8 = "";
const TCHAR* NEW_LINE = T("\r\n");
const Any_String ANY_STRING_SENTINEL = {STRING_SENTINEL};

const bool IGNORE_CASE = true;

static void c_string_count(const TCHAR* c_str, int* char_count, int* code_count)
{
	*char_count = 0;
	*code_count = 0;

	const TCHAR* current = c_str;
	while(*current != T('\0'))
	{
		const TCHAR* next = CharNext(current);
		*char_count += 1;
		*code_count += (int) (next - current);
		current = next;
	}

	ASSERT(*char_count <= *code_count, "Code count lower than char count");
}

static int c_string_char_count(const TCHAR* c_str)
{
	int char_count = 0;
	int _ = 0;
	c_string_count(c_str, &char_count, &_);
	return char_count;
}

int c_string_code_count(const TCHAR* c_str)
{
	int _ = 0;
	int code_count = 0;
	c_string_count(c_str, &_, &code_count);
	return code_count;
}

String* string_from_c(const TCHAR* c_str)
{
	int char_count = 0;
	int code_count = 0;
	c_string_count(c_str, &char_count, &code_count);

	size_t size = sizeof(String) + code_count * sizeof( ((String*) 0)->data[0] );
	String* str = arena_push(context.current_arena, size, String);

	str->char_count = char_count;
	str->code_count = code_count;
	StringCchCopy(str->data, code_count + 1, c_str);
	ASSERT(str->data[code_count] == T('\0'), "String is not null-terminated");

	return str;
}

static String* string_from_c(const TCHAR* c_str, int code_count)
{
	size_t size = sizeof(String) + code_count * sizeof( ((String*) 0)->data[0] );
	String* str = arena_push(context.current_arena, size, String);

	str->code_count = code_count;
	StringCchCopy(str->data, code_count + 1, c_str);
	ASSERT(str->data[code_count] == T('\0'), "String is not null-terminated");

	str->char_count = c_string_char_count(str->data);

	return str;
}

String_View view_from_c(const TCHAR* c_str)
{
	String_View view = {};
	c_string_count(c_str, &view.char_count, &view.code_count);
	view.data = c_str;
	return view;
}

String* string_from_view(String_View view)
{
	size_t str_size = sizeof(String) + view.code_count * sizeof( ((String*) 0)->data[0] );
	String* str = arena_push(context.current_arena, str_size, String);

	str->char_count = view.char_count;
	str->code_count = view.code_count;

	size_t view_size = view.code_count * sizeof(view.data[0]);
	CopyMemory(str->data, view.data, view_size);
	str->data[view.code_count] = T('\0');

	return str;
}

Any_String any_string(const TCHAR* c_str)
{
	Any_String any = {};
	any.type = STRING_C;
	any.c_str = c_str;
	return any;
}

Any_String any_string(String* str)
{
	Any_String any = {};
	any.type = STRING_WITH_COUNT;
	any.str = str;
	return any;
}

Any_String any_string(String_View str)
{
	Any_String any = {};
	any.type = STRING_VIEW;
	any.view = str;
	return any;
}

size_t string_size(const TCHAR* c_str)
{
	return c_string_code_count(c_str) * sizeof(c_str[0]);
}

size_t string_size(String* str)
{
	return str->code_count * sizeof(str->data[0]);
}

size_t string_size(String_View str)
{
	return str.code_count * sizeof(str.data[0]);
}

String* string_from_utf_8(const char* str)
{
	// dwFlags must be 0 or MB_ERR_INVALID_CHARS for UTF-8.
	// The null terminator is included when cbMultiByte is -1.
	int utf_16_capacity = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	if(utf_16_capacity == 0) return EMPTY_STRING;

	#ifdef WCE_9X
		wchar_t* utf_16_buffer = arena_push_buffer(context.current_arena, utf_16_capacity, wchar_t);
		int result = MultiByteToWideChar(CP_UTF8, 0, str, -1, utf_16_buffer, utf_16_capacity);
		return (result != 0) ? (string_from_utf_16_le(utf_16_buffer)) : (EMPTY_STRING);
	#else
		String_Builder* builder = builder_create(utf_16_capacity);
		int result = MultiByteToWideChar(CP_UTF8, 0, str, -1, builder->data, builder->capacity);
		return (result != 0) ? (builder_terminate(&builder)) : (EMPTY_STRING);
	#endif
}

const char* string_to_utf_8(const TCHAR* str, size_t* size)
{
	if(size != NULL) *size = 0;

	#ifdef WCE_9X
		// The null terminator is included when cbMultiByte is -1.
		int utf_16_capacity = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
		if(utf_16_capacity == 0) return EMPTY_UTF_8;

		wchar_t* utf_16_buffer = arena_push_buffer(context.current_arena, utf_16_capacity, wchar_t);
		int utf_16_result = MultiByteToWideChar(CP_ACP, 0, str, -1, utf_16_buffer, utf_16_capacity);
		if(utf_16_result == 0) return EMPTY_UTF_8;
	#else
		const wchar_t* utf_16_buffer = str;
	#endif

	// dwFlags must be 0 or MB_ERR_INVALID_CHARS for UTF-8.
	// The null terminator is included when cchWideChar is -1.
	int utf_8_capacity = WideCharToMultiByte(CP_UTF8, 0, utf_16_buffer, -1, NULL, 0, NULL, NULL);
	if(utf_8_capacity == 0) return EMPTY_UTF_8;

	char* utf_8_buffer = arena_push_buffer(context.current_arena, utf_8_capacity, char);
	int result = WideCharToMultiByte(CP_UTF8, 0, utf_16_buffer, -1, utf_8_buffer, utf_8_capacity, NULL, NULL);
	if(result == 0) return EMPTY_UTF_8;

	if(size != NULL) *size = (utf_8_capacity - 1) * sizeof(char);

	return utf_8_buffer;
}

const char* string_to_utf_8(String* str, size_t* size)
{
	return string_to_utf_8(str->data, size);
}

String* string_from_utf_16_le(const wchar_t* str)
{
	#ifdef WCE_9X
		// The null terminator is included when cchWideChar is -1.
		int ansi_capacity = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
		if(ansi_capacity == 0) return EMPTY_STRING;

		String_Builder* builder = builder_create(ansi_capacity);
		int result = WideCharToMultiByte(CP_ACP, 0, str, -1, builder->data, builder->capacity, NULL, NULL);
		return (result != 0) ? (builder_terminate(&builder)) : (EMPTY_STRING);
	#else
		return string_from_c(str);
	#endif
}

static bool string_next_char(const TCHAR* str, int code_count, String_View* chr)
{
	if(code_count == 0) return false;

	chr->data = (chr->data == NULL) ? (str) : (CharNext(chr->data));

	const TCHAR* stop = str + code_count;
	if(chr->data >= stop) return false;

	const TCHAR* next = CharNext(chr->data);
	chr->char_count = 1;
	chr->code_count = (int) (next - chr->data);

	ASSERT(chr->code_count > 0, "Code count is zero");

	return true;
}

bool string_next_char(const TCHAR* c_str, String_View* chr)
{
	int code_count = c_string_code_count(c_str);
	return string_next_char(c_str, code_count, chr);
}

bool string_next_char(String* str, String_View* chr)
{
	return string_next_char(str->data, str->code_count, chr);
}

bool string_next_char(String_View str, String_View* chr)
{
	return string_next_char(str.data, str.code_count, chr);
}

static bool string_previous_char(const TCHAR* str, int code_count, String_View* chr)
{
	if(code_count == 0) return false;

	if(chr->data == str) return false;

	const TCHAR* stop = str + code_count;
	chr->data = (chr->data == NULL) ? (CharPrev(str, stop)) : (CharPrev(str, chr->data));

	const TCHAR* next = CharNext(chr->data);
	chr->char_count = 1;
	chr->code_count = (int) (next - chr->data);

	ASSERT(chr->code_count > 0, "Code count is zero");

	return true;
}

bool string_previous_char(const TCHAR* c_str, String_View* chr)
{
	int code_count = c_string_code_count(c_str);
	return string_previous_char(c_str, code_count, chr);
}

bool string_previous_char(String* str, String_View* chr)
{
	return string_previous_char(str->data, str->code_count, chr);
}

bool string_previous_char(String_View str, String_View* chr)
{
	return string_previous_char(str.data, str.code_count, chr);
}

static String_View string_char_at(const TCHAR* str, int char_count, int code_count, int char_index)
{
	if(char_index < 0 || char_index > char_count - 1) return EMPTY_VIEW;

	String_View chr = {};

	for(int i = 0; string_next_char(str, code_count, &chr); i += 1)
	{
		if(i == char_index) break;
	}

	return chr;
}

String_View string_char_at(const TCHAR* c_str, int char_index)
{
	int char_count = 0;
	int code_count = 0;
	c_string_count(c_str, &char_count, &code_count);
	return string_char_at(c_str, char_count, code_count, char_index);
}

String_View string_char_at(String* str, int char_index)
{
	return string_char_at(str->data, str->char_count, str->code_count, char_index);
}

String_View string_char_at(String_View str, int char_index)
{
	return string_char_at(str.data, str.char_count, str.code_count, char_index);
}

static String_View string_char_at_end(const TCHAR* str, int char_count, int code_count, int char_index)
{
	if(char_index < 0 || char_index > char_count - 1) return EMPTY_VIEW;

	String_View chr = {};

	for(int i = 0; string_previous_char(str, code_count, &chr); i += 1)
	{
		if(i == char_index) break;
	}

	return chr;
}

String_View string_char_at_end(const TCHAR* c_str, int char_index)
{
	int char_count = 0;
	int code_count = 0;
	c_string_count(c_str, &char_count, &code_count);
	return string_char_at_end(c_str, char_count, code_count, char_index);
}

String_View string_char_at_end(String* str, int char_index)
{
	return string_char_at_end(str->data, str->char_count, str->code_count, char_index);
}

String_View string_char_at_end(String_View str, int char_index)
{
	return string_char_at_end(str.data, str.char_count, str.code_count, char_index);
}

static String_View string_slice(const TCHAR* str, int char_count, int code_count, int begin_char, int end_char)
{
	int last_char = char_count - 1;
	if(code_count == 0 || begin_char > last_char || end_char < 0 || begin_char > end_char) return EMPTY_VIEW;

	// Inclusive begin and exclusive end.
	begin_char = MAX(begin_char, 0);
	end_char = MIN(end_char, last_char + 1);

	String_View begin_chr = string_char_at(str, char_count, code_count, begin_char);
	ASSERT(begin_chr.char_count > 0, "Begin index is out of bounds");

	String_View end_chr = {};

	if(end_char <= last_char)
	{
		end_chr = string_char_at(str, char_count, code_count, end_char);
		ASSERT(end_chr.char_count > 0, "End index is out of bounds");
	}
	else
	{
		// A fake character that appears after the string
		// so the math adds up when slicing past the end.
		// We have to do this since it's an exclusive index.
		end_chr.data = str + code_count;
		end_chr.char_count = 1;
		end_chr.code_count = 1;
	}

	int begin_code = (int) (begin_chr.data - str);
	int end_code = (int) (end_chr.data - str) + end_chr.code_count - 1;

	String_View slice = {};
	slice.char_count = end_char - begin_char;
	slice.code_count = end_code - begin_code;
	slice.data = str + begin_code;

	return slice;
}

String_View string_slice(String* str, int begin_char, int end_char)
{
	return string_slice(str->data, str->char_count, str->code_count, begin_char, end_char);
}

String_View string_slice(String_View str, int begin_char, int end_char)
{
	return string_slice(str.data, str.char_count, str.code_count, begin_char, end_char);
}

static int string_comparator(const TCHAR* a, int a_code_count, const TCHAR* b, int b_code_count, bool ignore_case = false)
{
	ASSERT(a_code_count >= 0 && b_code_count >= 0, "Negative code counts");

	// Returning immediately when the two strings are empty is required for string views that use a NULL pointer.
	if(a_code_count == 0 && b_code_count == 0) return 0;

	u32 flags = (ignore_case) ? (NORM_IGNORECASE) : (0);
	int comparator = CompareString(LOCALE_SYSTEM_DEFAULT, flags, a, a_code_count, b, b_code_count);

	if(comparator == CSTR_LESS_THAN) return -1;
	else if(comparator == CSTR_EQUAL) return 0;
	else return 1;
}

int string_comparator(const TCHAR* a, const TCHAR* b)
{
	int a_code_count = c_string_code_count(a);
	int b_code_count = c_string_code_count(b);
	return string_comparator(a, a_code_count, b, b_code_count);
}

int string_comparator(String* a, String* b)
{
	return string_comparator(a->data, a->code_count, b->data, b->code_count);
}

int string_comparator(String_View a, String_View b)
{
	return string_comparator(a.data, a.code_count, b.data, b.code_count);
}

int string_ignore_case_comparator(const TCHAR* a, const TCHAR* b)
{
	int a_code_count = c_string_code_count(a);
	int b_code_count = c_string_code_count(b);
	return string_comparator(a, a_code_count, b, b_code_count, IGNORE_CASE);
}

int string_ignore_case_comparator(String* a, String* b)
{
	return string_comparator(a->data, a->code_count, b->data, b->code_count, IGNORE_CASE);
}

int string_ignore_case_comparator(String_View a, String_View b)
{
	return string_comparator(a.data, a.code_count, b.data, b.code_count, IGNORE_CASE);
}

bool string_is_equal(const TCHAR* a, int a_code_count, const TCHAR* b, int b_code_count, bool ignore_case)
{
	return string_comparator(a, a_code_count, b, b_code_count, ignore_case) == 0;
}

bool string_is_equal(const TCHAR* a, const TCHAR* b, bool ignore_case)
{
	int a_code_count = c_string_code_count(a);
	int b_code_count = c_string_code_count(b);
	return string_is_equal(a, a_code_count, b, b_code_count, ignore_case);
}

bool string_is_equal(String* a, const TCHAR* b, bool ignore_case)
{
	int b_code_count = c_string_code_count(b);
	return string_is_equal(a->data, a->code_count, b, b_code_count, ignore_case);
}

bool string_is_equal(String* a, String* b, bool ignore_case)
{
	return string_is_equal(a->data, a->code_count, b->data, b->code_count, ignore_case);
}

bool string_is_equal(String_View a, const TCHAR* b, bool ignore_case)
{
	int b_code_count = c_string_code_count(b);
	return string_is_equal(a.data, a.code_count, b, b_code_count, ignore_case);
}

bool string_is_equal(String_View a, String* b, bool ignore_case)
{
	return string_is_equal(a.data, a.code_count, b->data, b->code_count, ignore_case);
}

bool string_is_equal(String_View a, String_View b, bool ignore_case)
{
	return string_is_equal(a.data, a.code_count, b.data, b.code_count, ignore_case);
}

static bool string_is_at_most_equal(const TCHAR* a, int a_char_count, int a_code_count, const TCHAR* b, int b_char_count, int b_code_count, int max_char_count, bool ignore_case = false)
{
	ASSERT(max_char_count >= 0, "Max count is negative");
	String_View a_slice = string_slice(a, a_char_count, a_code_count, 0, max_char_count);
	String_View b_slice = string_slice(b, b_char_count, b_code_count, 0, max_char_count);
	return string_is_equal(a_slice, b_slice, ignore_case);
}

bool string_is_at_most_equal(String* a, const TCHAR* b, int max_char_count, bool ignore_case)
{
	int b_char_count = 0;
	int b_code_count = 0;
	c_string_count(b, &b_char_count, &b_code_count);
	return string_is_at_most_equal(a->data, a->char_count, a->code_count, b, b_char_count, b_code_count, max_char_count, ignore_case);
}

bool string_is_at_most_equal(String_View a, const TCHAR* b, int max_char_count, bool ignore_case)
{
	int b_char_count = 0;
	int b_code_count = 0;
	c_string_count(b, &b_char_count, &b_code_count);
	return string_is_at_most_equal(a.data, a.char_count, a.code_count, b, b_char_count, b_code_count, max_char_count, ignore_case);
}

static bool string_begins_with(const TCHAR* str, int code_count, const TCHAR* prefix, int prefix_code_count, bool ignore_case = false)
{
	if(prefix_code_count > code_count) return false;
	return string_is_equal(str, prefix_code_count, prefix, prefix_code_count, ignore_case);
}

bool string_begins_with(String* str, const TCHAR* prefix, bool ignore_case)
{
	int prefix_code_count = c_string_code_count(prefix);
	return string_begins_with(str->data, str->code_count, prefix, prefix_code_count, ignore_case);
}

bool string_begins_with(String* str, String* prefix, bool ignore_case)
{
	return string_begins_with(str->data, str->code_count, prefix->data, prefix->code_count, ignore_case);
}

bool string_begins_with(String_View str, const TCHAR* prefix, bool ignore_case)
{
	int prefix_code_count = c_string_code_count(prefix);
	return string_begins_with(str.data, str.code_count, prefix, prefix_code_count, ignore_case);
}

bool string_begins_with(String_View str, String* prefix, bool ignore_case)
{
	return string_begins_with(str.data, str.code_count, prefix->data, prefix->code_count, ignore_case);
}

static bool string_ends_with(const TCHAR* str, int code_count, const TCHAR* suffix, int suffix_code_count, bool ignore_case = false)
{
	if(suffix_code_count > code_count) return false;
	const TCHAR* suffix_in_str = str + code_count - suffix_code_count;
	return string_is_equal(suffix_in_str, suffix_code_count, suffix, suffix_code_count, ignore_case);
}

bool string_ends_with(String* str, const TCHAR* suffix, bool ignore_case)
{
	int suffix_code_count = c_string_code_count(suffix);
	return string_ends_with(str->data, str->code_count, suffix, suffix_code_count, ignore_case);
}

bool string_ends_with(String_View str, const TCHAR* suffix, bool ignore_case)
{
	int suffix_code_count = c_string_code_count(suffix);
	return string_ends_with(str.data, str.code_count, suffix, suffix_code_count, ignore_case);
}

static String_View string_remove_prefix(const TCHAR* str, int char_count, int code_count, const TCHAR* prefix, int prefix_char_count, int prefix_code_count)
{
	int begin_char = (string_begins_with(str, code_count, prefix, prefix_code_count)) ? (prefix_char_count) : (0);
	return string_slice(str, char_count, code_count, begin_char, char_count);
}

String_View string_remove_prefix(String* str, const TCHAR* prefix)
{
	int prefix_char_count = 0;
	int prefix_code_count = 0;
	c_string_count(prefix, &prefix_char_count, &prefix_code_count);
	return string_remove_prefix(str->data, str->char_count, str->code_count, prefix, prefix_char_count, prefix_code_count);
}

String_View string_remove_prefix(String_View str, const TCHAR* prefix)
{
	int prefix_char_count = 0;
	int prefix_code_count = 0;
	c_string_count(prefix, &prefix_char_count, &prefix_code_count);
	return string_remove_prefix(str.data, str.char_count, str.code_count, prefix, prefix_char_count, prefix_code_count);
}

static String_View string_remove_suffix(const TCHAR* str, int char_count, int code_count, const TCHAR* suffix, int suffix_char_count, int suffix_code_count)
{
	int end_char = (string_ends_with(str, code_count, suffix, suffix_code_count)) ? (char_count - suffix_char_count) : (char_count);
	return string_slice(str, char_count, code_count, 0, end_char);
}

String_View string_remove_suffix(String* str, const TCHAR* suffix)
{
	int suffix_char_count = 0;
	int suffix_code_count = 0;
	c_string_count(suffix, &suffix_char_count, &suffix_code_count);
	return string_remove_suffix(str->data, str->char_count, str->code_count, suffix, suffix_char_count, suffix_code_count);
}

String_View string_remove_suffix(String_View str, const TCHAR* suffix)
{
	int suffix_char_count = 0;
	int suffix_code_count = 0;
	c_string_count(suffix, &suffix_char_count, &suffix_code_count);
	return string_remove_suffix(str.data, str.char_count, str.code_count, suffix, suffix_char_count, suffix_code_count);
}

static String* string_map(const TCHAR* str, int code_count, u32 flags)
{
	// cchSrc cannot be zero.
	if(code_count == 0) return EMPTY_STRING;

	int lower_count = LCMapString(LOCALE_SYSTEM_DEFAULT, flags, str, code_count, NULL, 0);
	if(lower_count == 0) return string_from_c(str, code_count);

	String_Builder* builder = builder_create(lower_count);
	int result = LCMapString(LOCALE_SYSTEM_DEFAULT, flags, str, code_count, builder->data, builder->capacity);
	return (result != 0) ? (builder_terminate(&builder)) : (string_from_c(str, code_count));
}

static String* string_lower(const TCHAR* str, int code_count)
{
	return string_map(str, code_count, LCMAP_LOWERCASE);
}

String* string_lower(String* str)
{
	return string_lower(str->data, str->code_count);
}

String* string_lower(String_View str)
{
	return string_lower(str.data, str.code_count);
}

static String* string_upper(const TCHAR* str, int code_count)
{
	return string_map(str, code_count, LCMAP_UPPERCASE);
}

String* string_upper(String* str)
{
	return string_upper(str->data, str->code_count);
}

String* string_upper(String_View str)
{
	return string_upper(str.data, str.code_count);
}

const TCHAR* SPACE_DELIMITERS = T(" \t");
const TCHAR* LINE_DELIMITERS = T("\r\n");
const TCHAR* PATH_DELIMITERS = T("\\/");

static bool c_string_has_char(const TCHAR* c_str, String_View chr)
{
	bool result = false;

	String_View _chr = {};
	while(string_next_char(c_str, &_chr))
	{
		if(string_is_equal(_chr, chr))
		{
			result = true;
			break;
		}
	}

	return result;
}

static void string_split_move_char(Split_State* state)
{
	const TCHAR* str = (state->str != NULL) ? (state->str->data) : (state->view.data);
	int code_count = (state->str != NULL) ? (state->str->code_count) : (state->view.code_count);

	if(state->reverse) string_previous_char(str, code_count, &state->_char);
	else string_next_char(str, code_count, &state->_char);
}

void string_split_move_iter(Split_State* state)
{
	string_split_move_char(state);
	state->_index += 1;
}

bool string_split(Split_State* state, String_View* token)
{
	ASSERT(state->delimiters != NULL, "Missing delimiters");
	ASSERT(state->max_tokens >= 0, "Invalid max tokens");
	ASSERT(state->max_tokens == 0 || (0 <= state->_token_count && state->_token_count <= state->max_tokens), "Invalid number of tokens");

	const TCHAR* str = (state->str != NULL) ? (state->str->data) : (state->view.data);
	int char_count = (state->str != NULL) ? (state->str->char_count) : (state->view.char_count);
	int code_count = (state->str != NULL) ? (state->str->code_count) : (state->view.code_count);

	ASSERT(char_count == 0 || str != NULL, "Missing string or view");
	ASSERT(0 <= state->_index && state->_index < char_count + 1, "State index out of bounds");

	#define SLICE(begin, end) ( (state->reverse) ? (string_slice(str, char_count, code_count, char_count - (end), char_count - (begin))) : (string_slice(str, char_count, code_count, (begin), (end))) )

	state->split = false;
	state->remaining = EMPTY_VIEW;
	if(state->_char.data == NULL) string_split_move_char(state);

	// Skip consecutive delimiters if we don't want empty tokens.
	if(!state->keep_empty)
	{
		for(; state->_index < char_count; string_split_move_iter(state))
		{
			if(!c_string_has_char(state->delimiters, state->_char))
			{
				break;
			}
		}
	}

	if(state->max_tokens > 0)
	{
		if(state->_token_count == state->max_tokens)
		{
			return false;
		}
		// Return the remaining string if we reached
		// the maximum (even if it contains delimiters).
		else if(state->_token_count == state->max_tokens - 1)
		{
			state->_token_count += 1;
			*token = SLICE(state->_index, char_count);
			return true;
		}
	}

	bool has_token = false;
	int token_index = state->_index;

	for(; state->_index < char_count; string_split_move_iter(state))
	{
		bool is_end = (state->_index == char_count - 1);

		if(c_string_has_char(state->delimiters, state->_char))
		{
			has_token = true;
			state->split = true;
			state->delimiter = state->_char;
			state->_token_count += 1;
			*token = SLICE(token_index, state->_index);

			// If we want empty tokens, we have to move the index
			// here since we don't skip the delimiters above.
			if(state->keep_empty) string_split_move_iter(state);

			// Catch empty tokens when a delimiter appears at the end.
			if(is_end) state->_ends_with_delimiter = true;

			break;
		}
		else if(is_end)
		{
			has_token = true;
			state->_token_count += 1;
			*token = SLICE(token_index, state->_index + 1);
		}
	}

	if(state->keep_empty && !has_token)
	{
		// If we want empty tokens and the string is non-empty,
		// then this flag is set in the loop above.
		// E.g. "," -> ["", ""]
		if(char_count > 0 && state->_ends_with_delimiter)
		{
			has_token = true;
			state->_ends_with_delimiter = false;
			state->_token_count += 1;
			*token = EMPTY_VIEW;
		}
		// If we want empty tokens and the string is empty,
		// then this flag is not set in the loop above.
		// E.g. "" -> [""]
		else if(char_count == 0 && !state->_ends_with_delimiter)
		{
			has_token = true;
			state->_ends_with_delimiter = true;
			state->_token_count += 1;
			*token = EMPTY_VIEW;
		}
	}

	if(has_token) state->remaining = SLICE(state->_index, char_count);

	#undef SLICE

	return has_token;
}

Array<String_View>* string_split_all(Split_State* state)
{
	Array<String_View>* result = array_create<String_View>(0);
	String_View token = {};
	while(string_split(state, &token)) array_add(&result, token);
	return result;
}

bool string_partition(Split_State* state, String_View* first, String_View* delimiter, String_View* second)
{
	if(string_split(state, first))
	{
		*second = state->remaining;
	}
	else
	{
		*first = EMPTY_VIEW;
		*second = EMPTY_VIEW;
	}

	if(state->split)
	{
		if(delimiter != NULL) *delimiter = state->delimiter;
		if(!state->keep_empty)
		{
			// When we don't want empty tokens, the remaining
			// string starts with the split delimiter.
			ASSERT(second->char_count > 0, "Second token is empty");
			*second = view_advance(*second, state->delimiter);
		}
	}

	return state->split;
}

bool string_partition(Split_State* state, String_View* first, String_View* second)
{
	return string_partition(state, first, NULL, second);
}

static String_View string_trim(const TCHAR* str, int char_count, int code_count, const TCHAR* delimiters)
{
	String_View trim = {};
	trim.char_count = char_count;
	trim.code_count = code_count;
	trim.data = str;

	{
		String_View chr = {};
		while(string_next_char(str, code_count, &chr))
		{
			if(c_string_has_char(delimiters, chr))
			{
				trim = view_advance(trim, chr);
			}
			else
			{
				break;
			}
		}
	}

	{
		String_View chr = {};
		while(string_previous_char(str, code_count, &chr))
		{
			if(c_string_has_char(delimiters, chr))
			{
				trim = view_retreat(trim, chr);
			}
			else
			{
				break;
			}
		}
	}

	return trim;
}

String_View string_trim(String* str, const TCHAR* delimiters)
{
	return string_trim(str->data, str->char_count, str->code_count, delimiters);
}

String_View string_trim(String_View str, const TCHAR* delimiters)
{
	return string_trim(str.data, str.char_count, str.code_count, delimiters);
}

String_View view_advance(String_View str, String_View amount)
{
	if(str.data == NULL) return EMPTY_VIEW;

	String_View view = {};
	view.char_count = str.char_count - amount.char_count;
	view.code_count = str.code_count - amount.code_count;
	view.data = str.data + amount.code_count;
	return view;
}

String_View view_retreat(String_View str, String_View amount)
{
	if(str.data == NULL) return EMPTY_VIEW;

	String_View view = {};
	view.char_count = str.char_count - amount.char_count;
	view.code_count = str.code_count - amount.code_count;
	view.data = str.data;
	return view;
}

String_View view_extend(String_View str)
{
	if(str.data == NULL) return EMPTY_VIEW;
	return view_from_c(str.data);
}

static const size_t NUM_RADIX = 10;

#pragma warning(push)
#pragma warning(disable : 4189)

String* string_from_num(s32 num)
{
	// "-2147483648" to "2147483647" (11)
	String_Builder* builder = builder_create(11);
	bool success = _ltot_s(num, builder->data, builder->capacity, NUM_RADIX) == 0;
	ASSERT(success, "Conversion failed");
	return builder_terminate(&builder);
}

String* string_from_num(u32 num)
{
	// "0" to "4294967295" (10)
	String_Builder* builder = builder_create(10);
	bool success = _ultot_s(num, builder->data, builder->capacity, NUM_RADIX) == 0;
	ASSERT(success, "Conversion failed");
	return builder_terminate(&builder);
}

String* string_from_num(s64 num)
{
	// "-9223372036854775808" to "9223372036854775807" (20)
	String_Builder* builder = builder_create(20);
	bool success = _i64tot_s(num, builder->data, builder->capacity, NUM_RADIX) == 0;
	ASSERT(success, "Conversion failed");
	return builder_terminate(&builder);
}

String* string_from_num(u64 num)
{
	// "0" to "18446744073709551615" (20)
	String_Builder* builder = builder_create(20);
	bool success = _ui64tot_s(num, builder->data, builder->capacity, NUM_RADIX) == 0;
	ASSERT(success, "Conversion failed");
	return builder_terminate(&builder);
}

#pragma warning(pop)

static bool string_hex_to_num(const TCHAR* str, int code_count, u32* num)
{
	if(code_count == 0) return false;

	bool success = true;
	*num = 0;

	String_View chr = {};
	while(string_next_char(str, code_count, &chr))
	{
		if(chr.code_count > 1)
		{
			success = false;
			break;
		}

		TCHAR code = chr.data[0];
		u8 byte = 0;

		if(T('0') <= code && code <= T('9'))
		{
			byte = (u8) (code - T('0'));
		}
		else if(T('a') <= code && code <= T('f'))
		{
			byte = (u8) (code - T('a') + 0x0A);
		}
		else if(T('A') <= code && code <= T('F'))
		{
			byte = (u8) (code - T('A') + 0x0A);
		}
		else
		{
			success = false;
			break;
		}

		*num = (*num << 4) | (byte & 0x0F);
	}

	return success;
}

bool string_hex_to_num(String* str, u32* num)
{
	return string_hex_to_num(str->data, str->code_count, num);
}

bool string_hex_to_num(String_View str, u32* num)
{
	return string_hex_to_num(str.data, str.code_count, num);
}

String_Builder* builder_create(int code_count)
{
	ASSERT(code_count >= 0, "Count is negative");

	size_t size = sizeof(String_Builder) + code_count * sizeof( ((String_Builder*) 0)->data[0] );
	String_Builder* builder = arena_push(context.current_arena, size, String_Builder);

	builder->capacity = code_count + 1;
	builder->data[0] = T('\0');

	// Null terminating the entire buffer is useful when converting string views
	// with functions like MultiByteToWideChar and LCMapString (i.e. when the
	// string doesn't have a null terminator).
	int last_index = builder->capacity - 1;
	builder->data[last_index] = T('\0');

	return builder;
}

String* builder_to_string(String_Builder* builder)
{
	ASSERT(builder != NULL, "Builder was terminated");
	return string_from_c(builder->data);
}

String* builder_terminate(String_Builder** builder_ptr)
{
	_STATIC_ASSERT(sizeof(String) == sizeof(String_Builder));
	_STATIC_ASSERT(__alignof(String) == __alignof(String_Builder));

	ASSERT(*builder_ptr != NULL, "Builder was terminated");

	// @NoStrictAliasing
	String_Builder* builder = *builder_ptr;
	String* str = (String*) builder;
	c_string_count(str->data, &str->char_count, &str->code_count);
	*builder_ptr = NULL;

	return str;
}

static void builder_expand(String_Builder** builder_ptr)
{
	ASSERT(*builder_ptr != NULL, "Builder was terminated");

	Arena* arena = context.current_arena;

	String_Builder* old_builder = *builder_ptr;
	int new_capacity = old_builder->capacity * 2;

	void* saved_marker = advance(arena->base_memory, arena->saved_size);
	bool was_saved = saved_marker > old_builder;

	if(arena->last_memory == old_builder)
	{
		size_t size = (new_capacity - old_builder->capacity) * sizeof(old_builder->data[0]);
		arena_extend(arena, size);
		old_builder->capacity = new_capacity;
	}
	else
	{
		size_t old_size = sizeof(String_Builder) + (old_builder->capacity - 1) * sizeof(old_builder->data[0]);
		size_t new_size = sizeof(String_Builder) + (new_capacity - 1) * sizeof(old_builder->data[0]);

		String_Builder* new_builder = arena_push(arena, new_size, String_Builder);
		CopyMemory(new_builder, old_builder, old_size);
		new_builder->capacity = new_capacity;

		*builder_ptr = new_builder;
	}

	if(was_saved) arena_save(arena);
}

void builder_append(String_Builder** builder_ptr, const TCHAR* c_str)
{
	ASSERT(*builder_ptr != NULL, "Builder was terminated");

	String_Builder* builder = *builder_ptr;

	while(true)
	{
		HRESULT error = S_OK;

		if(builder->data[0] == T('\0'))
		{
			error = StringCchCopyEx(builder->data, builder->capacity, c_str, NULL, NULL, STRSAFE_NO_TRUNCATION);
		}
		else
		{
			error = StringCchCatEx(builder->data, builder->capacity, c_str, NULL, NULL, STRSAFE_NO_TRUNCATION);
		}

		if(error == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			builder_expand(builder_ptr);
			builder = *builder_ptr;
		}
		else
		{
			break;
		}
	}
}

void builder_append(String_Builder** builder_ptr, String* str)
{
	builder_append(builder_ptr, str->data);
}

void builder_append(String_Builder** builder_ptr, String_View str)
{
	builder_append_format(builder_ptr, T("%.*s"), str.code_count, str.data);
}

void builder_append_path(String_Builder** builder_ptr, const TCHAR* path)
{
	ASSERT(*builder_ptr != NULL, "Builder was terminated");

	String_Builder* builder = *builder_ptr;
	bool empty = builder->data[0] == T('\0');
	String_View last_chr = string_char_at_end(builder->data, 0);
	if(!empty && !string_is_equal(last_chr, T("\\"))) builder_append(builder_ptr, T("\\"));
	builder_append(builder_ptr, path);
}

void builder_append_path(String_Builder** builder_ptr, String* path)
{
	builder_append_path(builder_ptr, path->data);
}

void builder_append_path(String_Builder** builder_ptr, String_View path)
{
	ASSERT(*builder_ptr != NULL, "Builder was terminated");

	// We need to handle views separately since they're not null-terminated.
	String_Builder* builder = *builder_ptr;
	bool empty = builder->data[0] == T('\0');
	String_View last_chr = string_char_at_end(builder->data, 0);
	if(!empty && !string_is_equal(last_chr, T("\\"))) builder_append(builder_ptr, T("\\"));
	builder_append(builder_ptr, path);
}

void builder_append_format(String_Builder** builder_ptr, const TCHAR* format, ...)
{
	ASSERT(*builder_ptr != NULL, "Builder was terminated");

	String_Builder* builder = *builder_ptr;


	va_list args;
	va_start(args, format);

	// The initial string doesn't change when we expand.
	int code_count = c_string_code_count(builder->data);

	while(true)
	{
		TCHAR* concat_data = builder->data + code_count;
		int concat_capacity = builder->capacity - code_count;

		HRESULT error = S_OK;

		if(builder->data[0] == T('\0'))
		{
			error = StringCchVPrintf(builder->data, builder->capacity, format, args);
		}
		else
		{
			error = StringCchVPrintfEx(concat_data, concat_capacity, NULL, NULL, STRSAFE_NO_TRUNCATION, format, args);
		}

		if(error == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			builder_expand(builder_ptr);
			builder = *builder_ptr;
		}
		else
		{
			break;
		}
	}

	va_end(args);
}

void builder_clear(String_Builder* builder)
{
	ASSERT(builder != NULL, "Builder was terminated");
	builder->data[0] = T('\0');
}

void string_tests(void)
{
	console_info("Running string tests");
	log_info("Running string tests");

	{
		String* str_1 = CSTR("~a~b~");
		String* str_2 = CUTF8("~\x61\xCC\x8A~\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84~");
		String* str_3 = CSTR("");

		TEST(str_1->char_count, 5);
		TEST(str_1->code_count, 5);

		#ifdef WCE_9X
			TEST(str_2->char_count, 10);
			TEST(str_2->code_count, 10);
		#else
			TEST(str_2->char_count, 5);
			TEST(str_2->code_count, 10);
		#endif

		TEST(str_3->char_count, 0);
		TEST(str_3->code_count, 0);
	}

	{
		String_View view_1 = CVIEW("~a~b~");
		String_View view_2 = CVIEW("");

		TEST(view_1.char_count, 5);
		TEST(view_1.code_count, 5);

		TEST(view_2.char_count, 0);
		TEST(view_2.code_count, 0);
	}

	{
		String* str = NULL;

		str = string_from_view(CVIEW("abc"));
		TEST(str, T("abc"));

		str = string_from_view(CVIEW(""));
		TEST(str, T(""));
	}

	{
		size_t size = 3 * sizeof(TCHAR);
		TEST(string_size(T("abc")), size);
		TEST(string_size(CSTR("abc")), size);
		TEST(string_size(CVIEW("abc")), size);
	}

	{
		{
			String* str = CUTF8("~\x61\xCC\x8A~\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84~");

			size_t size = 0;
			TEST(CUTF8(string_to_utf_8(str, &size)), str);

			#ifdef WCE_9X
				TEST(size, (size_t) 13);
			#else
				TEST(size, (size_t) 15);
			#endif
		}

		{
			String* str = CSTR("");

			size_t size = 0;
			TEST(CUTF8(string_to_utf_8(str, &size)), str);
			TEST(size, (size_t) 0);
		}
	}

	{
		{
			String* str = CUTF8("~\x61\xCC\x8A~\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84~");

			{
				Array<String_View>* chars = array_create<String_View>(0);

				String_View chr = {};
				while(string_next_char(str, &chr))
				{
					array_add(&chars, chr);
				}

				#ifdef WCE_9X
					TEST(chars->count, 10);
					TEST(chars->data[0], T("~"));
					TEST(chars->data[1], T("a"));
					TEST(chars->data[2], CUTF8("\xC2\xB0"));
					TEST(chars->data[3], T("~"));
					TEST(chars->data[4], T("a"));
					TEST(chars->data[5], CUTF8("\xC2\xB4"));
					TEST(chars->data[6], T("^"));
					TEST(chars->data[7], T("~"));
					TEST(chars->data[8], CUTF8("\xC2\xAF"));
					TEST(chars->data[9], T("~"));
				#else
					TEST(chars->count, 5);
					TEST(chars->data[0], T("~"));
					TEST(chars->data[1], CUTF8("\x61\xCC\x8A"));
					TEST(chars->data[2], T("~"));
					TEST(chars->data[3], CUTF8("\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84"));
					TEST(chars->data[4], T("~"));
				#endif
			}

			{
				Array<String_View>* chars = array_create<String_View>(0);

				String_View chr = {};
				while(string_previous_char(str, &chr))
				{
					array_add(&chars, chr);
				}

				#ifdef WCE_9X
					TEST(chars->count, 10);
					TEST(chars->data[0], T("~"));
					TEST(chars->data[1], CUTF8("\xC2\xAF"));
					TEST(chars->data[2], T("~"));
					TEST(chars->data[3], T("^"));
					TEST(chars->data[4], CUTF8("\xC2\xB4"));
					TEST(chars->data[5], T("a"));
					TEST(chars->data[6], T("~"));
					TEST(chars->data[7], CUTF8("\xC2\xB0"));
					TEST(chars->data[8], T("a"));
					TEST(chars->data[9], T("~"));
				#else
					TEST(chars->count, 5);
					TEST(chars->data[0], T("~"));
					TEST(chars->data[1], CUTF8("\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84"));
					TEST(chars->data[2], T("~"));
					TEST(chars->data[3], CUTF8("\x61\xCC\x8A"));
					TEST(chars->data[4], T("~"));
				#endif
			}
		}

		{
			String* str = CSTR("");

			{
				String_View chr = {};
				while(string_next_char(str, &chr))
				{
					TEST_UNREACHABLE();
				}
			}

			{
				String_View chr = {};
				while(string_previous_char(str, &chr))
				{
					TEST_UNREACHABLE();
				}
			}
		}
	}

	{
		String* str = CUTF8("~\x61\xCC\x8A~\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84~");
		String_View chr = {};

		#ifdef WCE_9X
			chr = string_char_at(str, -1);
			TEST(chr, T(""));

			chr = string_char_at(str, 0);
			TEST(chr, T("~"));

			chr = string_char_at(str, 1);
			TEST(chr, T("a"));

			chr = string_char_at(str, 3);
			TEST(chr, T("~"));

			chr = string_char_at(str, 4);
			TEST(chr, T("a"));

			chr = string_char_at(str, 9);
			TEST(chr, T("~"));

			chr = string_char_at(str, 99);
			TEST(chr, T(""));
		#else
			chr = string_char_at(str, -1);
			TEST(chr, T(""));

			chr = string_char_at(str, 0);
			TEST(chr, T("~"));

			chr = string_char_at(str, 1);
			TEST(chr, CUTF8("\x61\xCC\x8A"));

			chr = string_char_at(str, 2);
			TEST(chr, T("~"));

			chr = string_char_at(str, 3);
			TEST(chr, CUTF8("\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84"));

			chr = string_char_at(str, 4);
			TEST(chr, T("~"));

			chr = string_char_at(str, 99);
			TEST(chr, T(""));
		#endif
	}

	{
		String* str = CUTF8("~\x61\xCC\x8A~\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84~");
		String_View chr = {};

		#ifdef WCE_9X
			chr = string_char_at_end(str, -1);
			TEST(chr, T(""));

			chr = string_char_at_end(str, 0);
			TEST(chr, T("~"));

			chr = string_char_at_end(str, 5);
			TEST(chr, T("a"));

			chr = string_char_at_end(str, 6);
			TEST(chr, T("~"));

			chr = string_char_at_end(str, 8);
			TEST(chr, T("a"));

			chr = string_char_at_end(str, 9);
			TEST(chr, T("~"));

			chr = string_char_at_end(str, 99);
			TEST(chr, T(""));
		#else
			chr = string_char_at_end(str, -1);
			TEST(chr, T(""));

			chr = string_char_at_end(str, 0);
			TEST(chr, T("~"));

			chr = string_char_at_end(str, 1);
			TEST(chr, CUTF8("\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84"));

			chr = string_char_at_end(str, 2);
			TEST(chr, T("~"));

			chr = string_char_at_end(str, 3);
			TEST(chr, CUTF8("\x61\xCC\x8A"));

			chr = string_char_at_end(str, 4);
			TEST(chr, T("~"));

			chr = string_char_at_end(str, 99);
			TEST(chr, T(""));
		#endif
	}

	{
		String* abcdef = CSTR("abcdef");
		String* utf8 = CUTF8("~\x61\xCC\x8A~\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84~");
		String* empty = CSTR("");

		String_View slice = {};

		slice = string_slice(abcdef, 0, 2);
		TEST(slice, T("ab"));

		slice = string_slice(abcdef, 0, 6);
		TEST(slice, T("abcdef"));

		slice = string_slice(abcdef, 0, 0);
		TEST(slice, T(""));

		slice = string_slice(abcdef, 2, 4);
		TEST(slice, T("cd"));

		slice = string_slice(abcdef, 2, 6);
		TEST(slice, T("cdef"));

		slice = string_slice(abcdef, 2, 0);
		TEST(slice, T(""));

		slice = string_slice(abcdef, 4, 2);
		TEST(slice, T(""));

		slice = string_slice(abcdef, 6, 10);
		TEST(slice, T(""));

		#ifdef WCE_9X
			slice = string_slice(utf8, 1, 4);
			TEST(slice, CUTF8("a\xC2\xB0~"));
		#else
			slice = string_slice(utf8, 1, 4);
			TEST(slice, CUTF8("\x61\xCC\x8A~\x61\xCC\x81\xCC\x82\xCC\x83\xCC\x84"));
		#endif

		slice = string_slice(empty, 0, 6);
		TEST(slice, T(""));
	}

	{
		String* abc = CSTR("abc");

		TEST(abc, T("abc"));
		TEST_NOT(abc, T("ABC"));
		TEST(string_is_equal(abc, T("ABC"), IGNORE_CASE), true);
	}

	{
		String* abcd = CSTR("abcd");
		String* abcD = CSTR("abcD");
		String* aB = CSTR("aB");

		TEST(string_is_at_most_equal(abcd, T("abcD"), 0), true);
		TEST(string_is_at_most_equal(abcd, T("abcD"), 3), true);
		TEST(string_is_at_most_equal(abcd, T("abcD"), 4), false);
		TEST(string_is_at_most_equal(abcd, T("abcD"), 4, IGNORE_CASE), true);
		TEST(string_is_at_most_equal(abcd, T("abcD"), 999), false);
		TEST(string_is_at_most_equal(abcd, T("abcd"), 999), true);
		TEST(string_is_at_most_equal(abcd, T("aB"), 1), true);
		TEST(string_is_at_most_equal(abcd, T("aB"), 2), false);
		TEST(string_is_at_most_equal(abcd, T("aB"), 2, IGNORE_CASE), true);
		TEST(string_is_at_most_equal(abcd, T("aB"), 3), false);
		TEST(string_is_at_most_equal(abcd, T("aB"), 999), false);

		TEST(string_begins_with(abcd, T("ab")), true);
		TEST(string_begins_with(aB, T("ab")), false);
		TEST(string_begins_with(aB, T("ab"), IGNORE_CASE), true);

		TEST(string_ends_with(abcd, T("cd")), true);
		TEST(string_ends_with(abcD, T("cd")), false);
		TEST(string_ends_with(abcD, T("cd"), IGNORE_CASE), true);
	}

	{
		TEST(string_remove_prefix(CSTR("__abc__"), T("__")), T("abc__"));
		TEST(string_remove_prefix(CSTR("__abc__"), T("_")), T("_abc__"));
		TEST(string_remove_prefix(CSTR("abc"), T("_")), T("abc"));
		TEST(string_remove_prefix(CSTR(""), T("_")), T(""));

		TEST(string_remove_suffix(CSTR("__abc__"), T("__")), T("__abc"));
		TEST(string_remove_suffix(CSTR("__abc__"), T("_")), T("__abc_"));
		TEST(string_remove_suffix(CSTR("abc"), T("_")), T("abc"));
		TEST(string_remove_suffix(CSTR(""), T("_")), T(""));
	}

	{
		String* lower = NULL;
		String* upper = NULL;

		lower = string_lower(CUTF8("ABC \xC3\x83 def"));
		TEST(lower, CUTF8("abc \xC3\xA3 def"));

		lower = string_lower(CSTR(""));
		TEST(lower, CSTR(""));

		upper = string_upper(CUTF8("abc \xC3\xA3 DEF"));
		TEST(upper, CUTF8("ABC \xC3\x83 DEF"));

		upper = string_upper(CSTR(""));
		TEST(upper, CSTR(""));
	}

	{
		#define TEST_SPLIT(_str, _delimiters, _max_tokens, _keep_empty, _reverse, _expected) \
		do \
		{ \
			Split_State state = {}; \
			state.str = CSTR(_str); \
			state.delimiters = T(_delimiters); \
			state.max_tokens = _max_tokens; \
			state.keep_empty = _keep_empty; \
			state.reverse = _reverse; \
			Array<String_View>* tokens = string_split_all(&state); \
			const TCHAR* expected[] = _expected; \
			TEST(tokens->count, (int) _countof(expected)); \
			for(int i = 0; i < _countof(expected); i += 1) \
			{ \
				TEST(tokens->data[i], expected[i]); \
			} \
		} while(false)

		#define TEST_EMPTY_SPLIT(_str, _delimiters, _max_tokens, _keep_empty, _reverse) \
		do \
		{ \
			Split_State state = {}; \
			state.str = CSTR(_str); \
			state.delimiters = T(_delimiters); \
			state.max_tokens = _max_tokens; \
			state.keep_empty = _keep_empty; \
			state.reverse = _reverse; \
			Array<String_View>* tokens = string_split_all(&state); \
			TEST(tokens->count, 0); \
		} while(false)

		TEST_SPLIT(",ab,cd,,ef,", ",", 0, false, false, PROTECT({T("ab"), T("cd"), T("ef")}));
		TEST_SPLIT(",ab,cd,,ef,", ",", 0, true, false, PROTECT({T(""), T("ab"), T("cd"), T(""), T("ef"), T("")}));
		TEST_SPLIT("ab,cd,ef", ",", 2, false, false, PROTECT({T("ab"), T("cd,ef")}));
		TEST_EMPTY_SPLIT("", ",", 0, false, false);
		TEST_SPLIT("", ",", 0, true, false, PROTECT({T("")}));

		TEST_SPLIT(",ab,cd,,ef,", ",", 0, false, true, PROTECT({T("ef"), T("cd"), T("ab")}));
		TEST_SPLIT(",ab,cd,,ef,", ",", 0, true, true, PROTECT({T(""), T("ef"), T(""), T("cd"), T("ab"), T("")}));
		TEST_SPLIT("ab,cd,ef", ",", 2, false, true, PROTECT({T("ef"), T("ab,cd")}));
		TEST_EMPTY_SPLIT("", ",", 0, false, true);
		TEST_SPLIT("", ",", 0, true, true, PROTECT({T("")}));

		#undef TEST_SPLIT
		#undef TEST_EMPTY_SPLIT
	}

	{
		#define TEST_PARTITION(_str, _delimiters, _max_tokens, _keep_empty, _reverse, _first, _delimiter, _second, _split) \
		do \
		{ \
			Split_State state = {}; \
			state.str = CSTR(_str); \
			state.delimiters = T(_delimiters); \
			state.max_tokens = _max_tokens; \
			state.keep_empty = _keep_empty; \
			state.reverse = _reverse; \
			String_View first = {}; \
			String_View delimiter = {}; \
			String_View second = {}; \
			bool split = string_partition(&state, &first, &delimiter, &second); \
			TEST(split, _split); \
			TEST(first, T(_first)); \
			TEST(!split || string_is_equal(delimiter, T(_delimiter)), true); \
			TEST(second, T(_second)); \
		} while(false)

		TEST_PARTITION("ab,cd", ",", 0, false, false, "ab", ",", "cd", true);
		TEST_PARTITION("ab,", ",", 0, false, false, "ab", ",", "", true);
		TEST_PARTITION(",cd", ",", 0, false, false, "cd", ",", "", false);
		TEST_PARTITION(",", ",", 0, false, false, "", ",", "", false);
		TEST_PARTITION("ab", ",", 0, false, false, "ab", "", "", false);

		TEST_PARTITION("ab,cd", ",", 0, true, false, "ab", ",", "cd", true);
		TEST_PARTITION("ab,", ",", 0, true, false, "ab", ",", "", true);
		TEST_PARTITION(",cd", ",", 0, true, false, "", ",", "cd", true);
		TEST_PARTITION(",", ",", 0, true, false, "", ",", "", true);
		TEST_PARTITION("ab", ",", 0, true, false, "ab", "", "", false);

		TEST_PARTITION("ab,cd", ",", 0, true, true, "cd", ",", "ab", true);
		TEST_PARTITION("ab,", ",", 0, true, true, "", ",", "ab", true);
		TEST_PARTITION(",cd", ",", 0, true, true, "cd", ",", "", true);
		TEST_PARTITION(",", ",", 0, true, true, "", ",", "", true);
		TEST_PARTITION("ab", ",", 0, true, true, "ab", "", "", false);

		#undef TEST_PARTITION
	}

	{
		String_View trim = {};

		trim = string_trim(CSTR("abc"));
		TEST(trim, T("abc"));

		trim = string_trim(CSTR("  abc  "));
		TEST(trim, T("abc"));

		trim = string_trim(CSTR("._ abc _."), T("._"));
		TEST(trim, T(" abc "));

		trim = string_trim(CSTR(""));
		TEST(trim, T(""));
	}

	{
		TEST(string_from_num(-2147483647 - 1), T("-2147483648"));
		TEST(string_from_num(2147483647), T("2147483647"));

		TEST(string_from_num(0U), T("0"));
		TEST(string_from_num(4294967295U), T("4294967295"));

		TEST(string_from_num(-9223372036854775808LL), T("-9223372036854775808"));
		TEST(string_from_num(9223372036854775807LL), T("9223372036854775807"));

		TEST(string_from_num(0ULL), T("0"));
		TEST(string_from_num(18446744073709551615ULL), T("18446744073709551615"));
	}

	{
		bool success = false;
		u32 num = 0;

		success = string_hex_to_num(CSTR("FF"), &num);
		TEST(success, true);
		TEST(num, 0xFFU);

		success = string_hex_to_num(CSTR("0F"), &num);
		TEST(success, true);
		TEST(num, 0x0FU);

		success = string_hex_to_num(CSTR("F"), &num);
		TEST(success, true);
		TEST(num, 0x0FU);

		success = string_hex_to_num(CSTR("DEADBEEF"), &num);
		TEST(success, true);
		TEST(num, 0xDEADBEEF);

		success = string_hex_to_num(CSTR("deadbeef"), &num);
		TEST(success, true);
		TEST(num, 0xDEADBEEF);

		success = string_hex_to_num(CSTR("wrong"), &num);
		TEST(success, false);

		success = string_hex_to_num(CSTR(""), &num);
		TEST(success, false);
	}

	{
		String_Builder* builder = builder_create(10);
		TEST(builder->capacity, 11);

		builder_append(&builder, T("Foo"));
		builder_append(&builder, CSTR("Bar"));
		builder_append(&builder, CVIEW("Foozle"));
		builder_append_format(&builder, T("%hs %d"), "Testing", 123);
		TEST(builder->capacity, 44);

		String* str = builder_to_string(builder);
		TEST(str, T("FooBarFoozleTesting 123"));

		builder_clear(builder);
		TEST(builder->capacity, 44);

		builder_append_path(&builder, T("Foo"));
		builder_append_path(&builder, CSTR("Bar\\"));
		builder_append_path(&builder, CVIEW("Foozle"));

		String* path = builder_to_string(builder);
		TEST(path, T("Foo\\Bar\\Foozle"));

		builder_clear(builder);
		builder_append(&builder, T("Final"));
		builder_append(&builder, T("String"));

		String* final = builder_terminate(&builder);
		TEST(final, T("FinalString"));
		TEST(builder, NULL);
	}
}