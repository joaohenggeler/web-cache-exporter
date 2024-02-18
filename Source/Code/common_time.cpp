#include "common.h"

static const int FORMAT_COUNT = 20;

String* filetime_format(FILETIME time)
{
	if(time.dwLowDateTime == 0 && time.dwHighDateTime == 0) return EMPTY_STRING;

	String* str = EMPTY_STRING;

	SYSTEMTIME t = {};
	if(FileTimeToSystemTime(&time, &t) != 0)
	{
		String_Builder* builder = builder_create(FORMAT_COUNT);
		builder_append_format(&builder, T("%hu-%02hu-%02hu %02hu:%02hu:%02hu"), t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		str = builder_terminate(&builder);
	}

	return str;
}

String* filetime_format(u64 time)
{
	u32 low = 0;
	u32 high = 0;
	u64_to_u32s(time, &low, &high);

	FILETIME filetime = {};
	filetime.dwLowDateTime = low;
	filetime.dwHighDateTime = high;

	return filetime_format(filetime);
}

String* dos_time_format(u32 time)
{
	if(time == 0) return EMPTY_STRING;

	String* str = EMPTY_STRING;

	u16 dos_date = 0;
	u16 dos_time = 0;
	u32_to_u16s(time, &dos_date, &dos_time);

	FILETIME filetime = {};
	if(DosDateTimeToFileTime(dos_date, dos_time, &filetime) != 0)
	{
		str = filetime_format(filetime);
	}

	return str;
}

String* unix_time_format(u64 time)
{
	if(time == 0) return EMPTY_STRING;

	String* str = EMPTY_STRING;

	struct tm t = {};
	__time64_t _time = time;
	if(_gmtime64_s(&t, &_time) == 0)
	{
		String_Builder* builder = builder_create(FORMAT_COUNT);
		size_t result = _tcsftime(builder->data, builder->capacity, T("%Y-%m-%d %H:%M:%S"), &t);
		str = (result != 0) ? (builder_terminate(&builder)) : (EMPTY_STRING);
	}

	return str;
}

LARGE_INTEGER timer_begin(const char* name)
{
	log_info("%hs", name);
	LARGE_INTEGER begin_time = {};
	QueryPerformanceCounter(&begin_time);

	#ifdef WCE_DEBUG
		context.debug_timer_balance += 1;
	#endif

	return begin_time;
}

void timer_end(const char* name, LARGE_INTEGER begin_time)
{
	LARGE_INTEGER end_time = {};
	QueryPerformanceCounter(&end_time);
	f64 elapsed_time = (f64) (end_time.QuadPart - begin_time.QuadPart) / context.performance_counter_frequency;
	log_info("%hs: %.9f seconds", name, elapsed_time);

	#ifdef WCE_DEBUG
		context.debug_timer_balance -= 1;
	#endif
}

void time_tests(void)
{
	console_info("Running time tests");
	log_info("Running time tests");

	{
		String* str = NULL;

		str = filetime_format(125912558450000000ULL);
		TEST(str, T("2000-01-02 03:04:05"));

		str = filetime_format(0ULL);
		TEST(str, T(""));

		str = dos_time_format(411248674U);
		TEST(str, T("2000-01-02 03:04:06"));

		str = dos_time_format(0U);
		TEST(str, T(""));

		str = unix_time_format(946782245ULL);
		TEST(str, T("2000-01-02 03:04:05"));

		str = unix_time_format(0ULL);
		TEST(str, T(""));
	}
}