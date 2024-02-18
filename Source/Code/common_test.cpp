#include "common.h"

bool test_box(const TCHAR* format, ...)
{
	// @NoArena

	context.failed_test_count += 1;

	bool breakpoint = false;

	const size_t MAX_MESSAGE_COUNT = 1000;
	TCHAR message[MAX_MESSAGE_COUNT] = T("");

	va_list args;
	va_start(args, format);
	StringCchVPrintf(message, MAX_MESSAGE_COUNT, format, args);
	va_end(args);

	int button = MessageBox(NULL, message, T("Test Failed"), MB_ABORTRETRYIGNORE | MB_ICONEXCLAMATION);

	switch(button)
	{
		case(IDABORT):
		{
			exit(1);
		} break;

		case(IDRETRY):
		{
			breakpoint = true;
		} break;

		case(IDIGNORE):
		{
			// Do nothing.
		} break;
	}

	return breakpoint;
}

bool test_value(const TCHAR* function, int line, bool got, bool expected)
{
	const TCHAR* got_str = (got) ? (T("true")) : (T("false"));
	const TCHAR* expected_str = (expected) ? (T("true")) : (T("false"));
	return (got != expected)
		&& (console_error("Test failed in %s:%d (got %s, expected %s)", function, line, got_str, expected_str), true)
		&& (log_error("Test failed in line %d (got %s, expected %s)", line, got_str, expected_str), true)
		&& test_box(T("Function: %s\nLine: %d\nGot: %s\nExpected: %s"), function, line, got_str, expected_str);
}

bool test_value(const TCHAR* function, int line, int got, int expected)
{
	return (got != expected)
		&& (console_error("Test failed in %s:%d (got %d, expected %d)", function, line, got, expected), true)
		&& (log_error("Test failed in line %d (got %d, expected %d)", line, got, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nGot: %d\nExpected: %d"), function, line, got, expected);
}

bool test_value(const TCHAR* function, int line, u32 got, u32 expected)
{
	return (got != expected)
		&& (console_error("Test failed in %s:%d (got %I32u, expected %I32u)", function, line, got, expected), true)
		&& (log_error("Test failed in line %d (got %I32u, expected %I32u)", line, got, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nGot: %I32u\nExpected: %I32u"), function, line, got, expected);
}

bool test_value(const TCHAR* function, int line, u64 got, u64 expected)
{
	return (got != expected)
		&& (console_error("Test failed in %s:%d (got %I64u, expected %I64u)", function, line, got, expected), true)
		&& (log_error("Test failed in line %d (got %I64u, expected %I64u)", line, got, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nGot: %I64u\nExpected: %I64u"), function, line, got, expected);
}

bool test_value(const TCHAR* function, int line, void* got, void* expected)
{
	return (got != expected)
		&& (console_error("Test failed in %s:%d (got %p, expected %p)", function, line, got, expected), true)
		&& (log_error("Test failed in line %d (got %p, expected %p)", line, got, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nGot: %p\nExpected: %p"), function, line, got, expected);
}

static bool test_value(const TCHAR* function, int line, const TCHAR* got, int got_code_count, const TCHAR* expected, int expected_code_count)
{
	return !string_is_equal(got, got_code_count, expected, expected_code_count)
		&& (console_error("Test failed in %s:%d (got '%.*s', expected '%.*s')", function, line, got_code_count, got, expected_code_count, expected), true)
		&& (log_error("Test failed in line %d (got '%.*s', expected '%.*s')", line, got_code_count, got, expected_code_count, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nGot: \"%.*s\"\nExpected: \"%.*s\""), function, line, got_code_count, got, expected_code_count, expected);
}

bool test_value(const TCHAR* function, int line, const TCHAR* got, const TCHAR* expected)
{
	int got_code_count = c_string_code_count(got);
	int expected_code_count = c_string_code_count(expected);
	return test_value(function, line, got, got_code_count, expected, expected_code_count);
}

bool test_value(const TCHAR* function, int line, String* got, const TCHAR* expected)
{
	int expected_code_count = c_string_code_count(expected);
	return test_value(function, line, got->data, got->code_count, expected, expected_code_count);
}

bool test_value(const TCHAR* function, int line, String* got, String* expected)
{
	return test_value(function, line, got->data, got->code_count, expected->data, expected->code_count);
}

bool test_value(const TCHAR* function, int line, String_View got, const TCHAR* expected)
{
	int expected_code_count = c_string_code_count(expected);
	return test_value(function, line, got.data, got.code_count, expected, expected_code_count);
}

bool test_value(const TCHAR* function, int line, String_View got, String* expected)
{
	return test_value(function, line, got.data, got.code_count, expected->data, expected->code_count);
}

bool test_not_value(const TCHAR* function, int line, int got, int expected)
{
	return (got == expected)
		&& (console_error("Test failed in %s:%d (not expected %d)", function, line, expected), true)
		&& (log_error("Test failed in line %d (not expected %d)", line, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nNot Expected: %d"), function, line, expected);
}

bool test_not_value(const TCHAR* function, int line, void* got, void* expected)
{
	return (got == expected)
		&& (console_error("Test failed in %s:%d (not expected %p)", function, line, expected), true)
		&& (log_error("Test failed in line %d (not expected %p)", line, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nNot Expected: %p"), function, line, expected);
}

static bool test_not_value(const TCHAR* function, int line, const TCHAR* got, int got_code_count, const TCHAR* expected, int expected_code_count)
{
	return string_is_equal(got, got_code_count, expected, expected_code_count)
		&& (console_error("Test failed in %s:%d (not expected '%.*s')", function, line, expected_code_count, expected), true)
		&& (log_error("Test failed in line %d (not expected '%.*s')", line, expected_code_count, expected), true)
		&& test_box(T("Function: %s\nLine: %d\nNot Expected: \"%.*s\""), function, line, expected_code_count, expected);
}

bool test_not_value(const TCHAR* function, int line, String* got, const TCHAR* expected)
{
	int expected_code_count = c_string_code_count(expected);
	return test_not_value(function, line, got->data, got->code_count, expected, expected_code_count);
}