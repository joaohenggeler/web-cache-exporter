#ifndef COMMON_LOG_H
#define COMMON_LOG_H

#include "common_core.h"

void log_create(void);
void log_close(void);
void internal_log_print(const TCHAR* format, ...);

#define log_print(type, format, ...) \
	( \
		context.log_enabled \
		&& (internal_log_print(T("[") T(type) T("] ") T("[") T(__FUNCTION__) T("] ") T(format) T("."), __VA_ARGS__), true) \
	)

#define log_info(format, ...) log_print("INFO", format, __VA_ARGS__)
#define log_warning(format, ...) log_print("WARNING", format, __VA_ARGS__)
#define log_error(format, ...) log_print("ERROR", format, __VA_ARGS__)

#ifdef WCE_DEBUG
	#define log_debug(format, ...) log_print("DEBUG", format, __VA_ARGS__)
#else
	#define log_debug(...)
#endif

#define console_print(type, format, ...) \
	( \
		context.console_enabled \
		&& (_tprintf(T("[") T(type) T("] ") T(format) T(".\n"), __VA_ARGS__), true) \
	)

#define console_info(format, ...) console_print("INFO", format, __VA_ARGS__)
#define console_warning(format, ...) console_print("WARNING", format, __VA_ARGS__)
#define console_error(format, ...) console_print("ERROR", format, __VA_ARGS__)

#define console_prompt(format, ...) \
	( \
		(_tprintf(T("[PROMPT] ") T(format) T(" "), __VA_ARGS__), true) \
		&& (fflush(stdout), true) \
	)

#define console_progress(format, ...) \
	( \
		context.console_enabled \
		&& (context.current_progress_count = _tprintf(T("\r[PROGRESS] ") T(format), __VA_ARGS__), true) \
		&& (_tprintf(T("%*s"), MAX(context.previous_progress_count - context.current_progress_count, 0), T("")), true) \
		&& (fflush(stdout), true) \
		&& (ASSERT(context.current_progress_count >= 0, "Negative progress count."), true) \
		&& (context.previous_progress_count = context.current_progress_count, true) \
	)

#define console_progress_end() \
	( \
		context.console_enabled \
		&& (context.previous_progress_count = 0, true) \
		&& (context.current_progress_count = 0, true) \
		&& (_tprintf(T("\n")), true) \
	)

/*
	Format Specifiers For Printf Functions:

	char*						= %hs
	wchar_t*					= %ls
	TCHAR*						= %s

	char						= %hc / %hhd
	s8							= %hhd
	unsigned char / u8			= %hhu
	wchar_t						= %lc
	TCHAR						= %c

	short / s16					= %hd
	unsigned short / u16		= %hu

	long						= %ld
	unsigned long				= %lu
	long long					= %lld
	unsigned long long			= %llu

	int							= %d
	unsigned int				= %u

	size_t						= %Iu
	ptrdiff_t					= %Id

	u32							= %I32u
	s32							= %I32d
	u64							= %I64u
	s64							= %I64d

	BYTE						= %hhu
	WORD						= %hu
	DWORD / GetLastError()		= %lu
	QWORD						= %I64u

	u8 / BYTE in hexadecimal	= 0x%02X
	u16 / WORD in hexadecimal	= 0x%04X
	u32 / DWORD in hexadecimal	= 0x%08X

	void* / HANDLE				= %p
	HRESULT						= %ld
	enum / BOOL					= %d
*/

#endif