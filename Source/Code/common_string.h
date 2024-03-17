#ifndef COMMON_STRING_H
#define COMMON_STRING_H

#include "common_core.h"
#include "common_array.h"

struct String
{
	int char_count;
	int code_count;
	TCHAR data[ANYSIZE_ARRAY];
};

struct String_View
{
	int char_count;
	int code_count;
	const TCHAR* data;
};

enum String_Type
{
	STRING_C,
	STRING_WITH_COUNT,
	STRING_VIEW,
	STRING_SENTINEL,
};

struct Any_String
{
	String_Type type;

	union
	{
		const TCHAR* c_str;
		String* str;
		String_View view;
	};
};

extern String* EMPTY_STRING;
extern const String_View EMPTY_VIEW;
extern const char* EMPTY_UTF_8;
extern const TCHAR* NEW_LINE;
extern const Any_String ANY_STRING_SENTINEL;

extern const bool IGNORE_CASE;

int c_string_code_count(const TCHAR* c_str);
String* string_from_c(const TCHAR* c_str);
String_View view_from_c(const TCHAR* c_str);
String* string_from_view(String_View view);

#define CSTR(x) string_from_c(T(x))
#define CVIEW(x) view_from_c(T(x))

Any_String any_string(const TCHAR* c_str);
Any_String any_string(String* str);
Any_String any_string(String_View str);

#define CANY(x) any_string(x)

size_t string_size(const TCHAR* c_str);
size_t string_size(String* str);
size_t string_size(String_View str);

String* string_from_utf_8(const char* str);
const char* string_to_utf_8(const TCHAR* str, size_t* size = NULL);
const char* string_to_utf_8(String* str, size_t* size = NULL);
String* string_from_utf_16_le(const wchar_t* str);

#define CUTF8(x) string_from_utf_8(x)

bool string_next_char(const TCHAR* c_str, String_View* chr);
bool string_next_char(String* str, String_View* chr);
bool string_next_char(String_View str, String_View* chr);

bool string_previous_char(const TCHAR* c_str, String_View* chr);
bool string_previous_char(String* str, String_View* chr);
bool string_previous_char(String_View str, String_View* chr);

String_View string_char_at(const TCHAR* c_str, int char_index);
String_View string_char_at(String* str, int char_index);
String_View string_char_at(String_View str, int char_index);

String_View string_char_at_end(const TCHAR* c_str, int char_index);
String_View string_char_at_end(String* str, int char_index);
String_View string_char_at_end(String_View str, int char_index);

String_View string_slice(String* str, int begin_char, int end_char);
String_View string_slice(String_View str, int begin_char, int end_char);

int string_comparator(const TCHAR* a, const TCHAR* b);
int string_comparator(String* a, String* b);
int string_comparator(String_View a, String_View b);

int string_ignore_case_comparator(const TCHAR* a, const TCHAR* b);
int string_ignore_case_comparator(String* a, String* b);
int string_ignore_case_comparator(String_View a, String_View b);

bool string_is_equal(const TCHAR* a, int a_code_count, const TCHAR* b, int b_code_count, bool ignore_case = false);
bool string_is_equal(const TCHAR* a, const TCHAR* b, bool ignore_case = false);
bool string_is_equal(String* a, const TCHAR* b, bool ignore_case = false);
bool string_is_equal(String* a, String* b, bool ignore_case = false);
bool string_is_equal(String_View a, const TCHAR* b, bool ignore_case = false);
bool string_is_equal(String_View a, String* b, bool ignore_case = false);
bool string_is_equal(String_View a, String_View b, bool ignore_case = false);

bool string_is_at_most_equal(String* a, const TCHAR* b, int max_char_count, bool ignore_case = false);
bool string_is_at_most_equal(String_View a, const TCHAR* b, int max_char_count, bool ignore_case = false);

bool string_begins_with(String* str, const TCHAR* prefix, bool ignore_case = false);
bool string_begins_with(String* str, String* prefix, bool ignore_case = false);
bool string_begins_with(String_View str, const TCHAR* prefix, bool ignore_case = false);
bool string_begins_with(String_View str, String* prefix, bool ignore_case = false);

bool string_ends_with(String* str, const TCHAR* suffix, bool ignore_case = false);
bool string_ends_with(String_View str, const TCHAR* suffix, bool ignore_case = false);

String_View string_remove_prefix(String* str, const TCHAR* prefix);
String_View string_remove_prefix(String_View str, const TCHAR* prefix);

String_View string_remove_suffix(String* str, const TCHAR* suffix);
String_View string_remove_suffix(String_View str, const TCHAR* suffix);

String* string_lower(String* str);
String* string_lower(String_View str);

String* string_upper(String* str);
String* string_upper(String_View str);

struct Split_State
{
	String* str;
	String_View view;

	const TCHAR* delimiters;
	int max_tokens;
	bool keep_empty;
	bool reverse;

	bool split;
	String_View delimiter;
	String_View remaining;

	int _index;
	String_View _char;
	int _token_count;
	bool _ends_with_delimiter;
};

extern const TCHAR* SPACE_DELIMITERS;
extern const TCHAR* LINE_DELIMITERS;
extern const TCHAR* PATH_DELIMITERS;

void string_split_move_iter(Split_State* state);
bool string_split(Split_State* state, String_View* token);
Array<String_View>* string_split_all(Split_State* state);

bool string_partition(Split_State* state, String_View* first, String_View* delimiter, String_View* second);
bool string_partition(Split_State* state, String_View* first, String_View* second);

String_View string_trim(String* str, const TCHAR* delimiters = SPACE_DELIMITERS);
String_View string_trim(String_View str, const TCHAR* delimiters = SPACE_DELIMITERS);

String_View view_advance(String_View str, String_View amount);
String_View view_retreat(String_View str, String_View amount);
String_View view_extend(String_View str);

String* string_from_num(s32 num);
String* string_from_num(u32 num);
String* string_from_num(s64 num);
String* string_from_num(u64 num);

bool string_hex_to_num(String* str, u32* num);
bool string_hex_to_num(String_View str, u32* num);

struct String_Builder
{
	int _reserved;
	int capacity;
	TCHAR data[ANYSIZE_ARRAY];
};

String_Builder* builder_create(int code_count);
String* builder_to_string(String_Builder* builder);
String* builder_terminate(String_Builder** builder_ptr);
void builder_append(String_Builder** builder_ptr, const TCHAR* c_str);
void builder_append(String_Builder** builder_ptr, String* str);
void builder_append(String_Builder** builder_ptr, String_View str);
void builder_append_path(String_Builder** builder_ptr, const TCHAR* path);
void builder_append_path(String_Builder** builder_ptr, String* path);
void builder_append_path(String_Builder** builder_ptr, String_View path);
void builder_append_format(String_Builder** builder_ptr, const TCHAR* format, ...);
void builder_clear(String_Builder* builder);

void string_tests(void);

#endif