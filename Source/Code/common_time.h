#ifndef COMMON_TIME_H
#define COMMON_TIME_H

#include "common_core.h"
#include "common_string.h"

String* filetime_format(FILETIME time);
String* filetime_format(u64 time);
String* dos_time_format(u32 time);
String* unix_time_format(u64 time);

LARGE_INTEGER timer_begin(const char* name);
void timer_end(const char* name, LARGE_INTEGER begin_time);
#define TIMER(name) LARGE_INTEGER LINE_VAR(_begin_); DEFER(LINE_VAR(_begin_) = timer_begin(name), timer_end(name, LINE_VAR(_begin_)))

#ifdef WCE_DEBUG
	#define DEBUG_TIMER(name) TIMER(name " (Debug)")
#else
	#define DEBUG_TIMER(...)
#endif

void time_tests(void);

#endif