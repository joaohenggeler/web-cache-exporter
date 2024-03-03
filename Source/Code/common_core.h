#ifndef COMMON_CORE_H
#define COMMON_CORE_H

#ifndef WCE_VERSION
	#define WCE_VERSION "?"
#endif

#ifndef WCE_DATE
	#define WCE_DATE "?"
#endif

#ifndef WCE_MODE
	#define WCE_MODE "?"
#endif

#ifndef WCE_FAMILY
	#define WCE_FAMILY "?"
#endif

#ifndef WCE_ARCH
	#define WCE_ARCH "?"
#endif

#ifdef WCE_9X
	// Windows 98 and ME (9x, ANSI).
	// Minimum Windows Version: Windows 98 (version 4.10 -> 0x0410).
	// Minimum Internet Explorer Version: IE 4.01 (0x0401, version 4.72 of Shell32.dll and Shlwapi.dll).
	#undef UNICODE
	#undef _UNICODE
	#undef _MBCS
	#define WINVER 0x0410
	#define _WIN32_WINDOWS 0x0410
	#define _WIN32_WINNT 0
	#define _WIN32_IE 0x0401
#else
	// Windows 2000, XP, Vista, 7, 8.1, and 10 (NT, Unicode).
	// Minimum Windows Version: Windows 2000 (version NT 5.0 -> 0x0500).
	// Minimum Internet Explorer Version: IE 5 (0x0500, version 5.0 of Shell32.dll and Shlwapi.dll).
	#define UNICODE
	#define _UNICODE
	#undef _MBCS
	#define WINVER 0x0500
	#define _WIN32_WINNT 0x0500
	#define _WIN32_IE 0x0500
	#define NTDDI_VERSION 0x05000000
#endif

// Exclude unnecessary API declarations when including windows.h.
// The WIN32_LEAN_AND_MEAN macro would exclude some necessary ones.
#define NOATOM
//#define NOGDI
#define NOGDICA
#define NOMETAF
#define NOMINMA
//#define NOMSG
#define NOOPENF
#define NORASTE
#define NOSCROL
#define NOSOUND
#define NOSYSME
#define NOTEXTM
#define NOWH
#define NOCOMM
#define NOKANJI
#define NOCRYPT
#define NOMCX

// Avoid preprocessor redefinition warnings when including both windows.h and ntstatus.h.
#define WIN32_NO_STATUS
	// Enable STRICT type checking.
	#define STRICT
	#include <windows.h>
#undef WIN32_NO_STATUS

#include <tchar.h> // Generic-text mappings
#include <strsafe.h> // StringCch*, StringCb* functions (must appear after tchar.h)

#include <crtdbg.h> // _ASSERT_EXPR, _STATIC_ASSERT
#include <math.h> // ceil
#include <stdarg.h> // va_list, va_start, va_end
#include <stdio.h> // printf, fflush, getchar.
#include <stdlib.h> // errno, _set_errno
#include <string.h> // strerror
#include <time.h> // gmtime_s, strftime

#include <shlobj.h> // SH* functions
// Disable deprecation warnings for unused functions: StrNCatA, StrNCatW, StrCatW, StrCpyW
#pragma warning(push)
#pragma warning(disable : 4995)
	#include <shlwapi.h> // Path* functions
#pragma warning(pop)

#include <ntstatus.h> // NTSTATUS
#include <stierr.h> // NT_SUCCESS
#include <winternl.h> // NtQuerySystemInformation

// Define sized integers and floats. Note that stdint.h was only added in Visual Studio 2010.
typedef signed __int8 s8;
typedef signed __int16 s16;
typedef signed __int32 s32;
typedef signed __int64 s64;

typedef unsigned __int8 u8;
typedef unsigned __int16 u16;
typedef unsigned __int32 u32;
typedef unsigned __int64 u64;

typedef float f32;
typedef double f64;

// Require the use of the /J compiler option so that the default char type is unsigned.
// This is useful for handling character data since it makes char behave like the char8_t type added in C++20.
// See: https://learn.microsoft.com/en-us/cpp/cpp/char-wchar-t-char16-t-char32-t?view=msvc-170
#ifndef _CHAR_UNSIGNED
	_STATIC_ASSERT(false);
#endif

#define PROTECT(...) __VA_ARGS__

#define STR(x) #x
#define STR_EXPAND(x) STR(x)

#define CONCAT(a, b) a ## b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define WIDE(x) CONCAT_EXPAND(L, #x)
#define WIDE_EXPAND(x) CONCAT_EXPAND(L, STR_EXPAND(x))

#ifdef WCE_9X
	#define T_(x) x
#else
	#define T_(x) L ## x
#endif

#define T(x) T_(x)
#define TEXT_STR(x) T_(#x)
#define TEXT_EXPAND(x) TEXT_STR(x)

#define LINE_VAR(name) CONCAT_EXPAND(name, __LINE__)
#define REPEAT(num) for(int LINE_VAR(_) = 0; LINE_VAR(_) < (num); LINE_VAR(_) += 1)
#define DEFER(begin_expr, end_expr) for(bool LINE_VAR(_) = ((begin_expr), true); LINE_VAR(_); (LINE_VAR(_) = false, (end_expr)))
#define DEFER_IF(begin_expr, end_expr) for(bool LINE_VAR(_) = (begin_expr); LINE_VAR(_); (LINE_VAR(_) = false, (end_expr)))

#define ASSERT(expr, msg) _ASSERT_EXPR((expr), L"[" __LPREFIX(__FUNCTION__) L"] " WIDE_EXPAND(msg) L".")

extern const u32 MAX_U32;

u64 u32s_to_u64(u32 low, u32 high);
void u32_to_u16s(u32 num, u16* low, u16* high);
void u64_to_u32s(u64 num, u32* low, u32* high);

bool flag_has_one(u32 flags);
u32 flag_to_index(u32 flag);

size_t from_kilobytes(size_t kilobytes);
size_t from_megabytes(size_t megabytes);
size_t from_gigabytes(size_t gigabytes);

bool memory_is_equal(const void* a, const void* b, size_t size);
ptrdiff_t ptr_diff(const void* a, const void* b);

const TCHAR* last_error_message(void);
const TCHAR* errno_string(void);

#define MIN(a, b) ( ((a) < (b)) ? (a) : (b) )
#define MAX(a, b) ( ((a) > (b)) ? (a) : (b) )

#define CEIL_DIV(a, b) ( ((a) % (b) == 0) ? ((a) / (b)) : ((a) / (b) + 1) )

#define ROUND_UP(num, multiple) ( ((num) % (multiple) == 0) ? (num) : ((num) + (multiple) - ((num) % (multiple))) )
#define ROUND_UP_OFFSET(num, multiple) ( ROUND_UP((num), (multiple)) - (num) )

#define POINTER_IS_ALIGNED_TO_SIZE(ptr, size) ( (uintptr_t) (ptr) % (size) == 0 )
#define POINTER_IS_ALIGNED_TO_TYPE(ptr, Type) POINTER_IS_ALIGNED_TO_SIZE((ptr), __alignof(Type))

u8 byte_order_swap(u8 num);
u16 byte_order_swap(u16 num);
u32 byte_order_swap(u32 num);
u64 byte_order_swap(u64 num);

// We'll always target a little endian architecture on Windows.
#define LITTLE_ENDIAN_TO_HOST(var)
#define BIG_ENDIAN_TO_HOST(var) var = byte_order_swap(var)

template<typename Int_Type>
u32 u32_clamp(Int_Type num)
{
	return (u32) MIN(num, MAX_U32);
}

template<typename Int_Type>
size_t size_clamp(Int_Type num)
{
	return (size_t) MIN(num, SIZE_MAX);
}

template<typename Int_Type>
u16 u16_truncate(Int_Type num)
{
	return (u16) (num & 0xFFFF);
}

template<typename Int_Type>
bool is_power_of_two(Int_Type num)
{
	return num > 0 && (num & (num - 1)) == 0;
}

template<typename Int_Type>
void* advance(const void* ptr, Int_Type size)
{
	ASSERT(size >= 0, "Negative size");
	return (char*) ptr + size;
}

void core_tests(void);

#endif