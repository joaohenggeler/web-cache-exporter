#ifndef COMMON_TEST_H
#define COMMON_TEST_H

#include "common_core.h"
#include "common_string.h"

bool test_box(const TCHAR* format, ...);

bool test_value(const TCHAR* function, int line, bool got, bool expected);
bool test_value(const TCHAR* function, int line, int got, int expected);
bool test_value(const TCHAR* function, int line, u32 got, u32 expected);
bool test_value(const TCHAR* function, int line, u64 got, u64 expected);
bool test_value(const TCHAR* function, int line, void* got, void* expected);
bool test_value(const TCHAR* function, int line, const TCHAR* got, const TCHAR* expected);
bool test_value(const TCHAR* function, int line, String* got, const TCHAR* expected);
bool test_value(const TCHAR* function, int line, String* got, String* expected);
bool test_value(const TCHAR* function, int line, String_View got, const TCHAR* expected);
bool test_value(const TCHAR* function, int line, String_View got, String* expected);

bool test_not_value(const TCHAR* function, int line, int got, int expected);
bool test_not_value(const TCHAR* function, int line, void* got, void* expected);
bool test_not_value(const TCHAR* function, int line, String* got, const TCHAR* expected);

#define TEST(got, expected) \
	( \
		(context.total_test_count += 1, true) \
		&& test_value(T(__FUNCTION__), __LINE__, (got), (expected)) \
		&& (__debugbreak(), true) \
	)

#define TEST_NOT(got, expected) \
	( \
		(context.total_test_count += 1, true) \
		&& test_not_value(T(__FUNCTION__), __LINE__, (got), (expected)) \
		&& (__debugbreak(), true) \
	)

#define TEST_EXPR(expr) \
	( \
		(context.total_test_count += 1, true) \
		&& !(expr) \
		&& (console_error("Test failed in %s:%d (%s)", T(__FUNCTION__), __LINE__, TEXT_EXPAND(expr)), true) \
		&& (log_error("Test failed in line %d (%s).", __LINE__, TEXT_EXPAND(expr)), true) \
		&& test_box(T("Function: %s\nLine: %d\nExpression: %s"), T(__FUNCTION__), __LINE__, TEXT_EXPAND(expr)) \
		&& (__debugbreak(), true) \
	)

#define TEST_UNREACHABLE() TEST(true, false)

#endif