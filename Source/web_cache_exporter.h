#ifndef WEB_CACHE_EXPORTER_H
#define WEB_CACHE_EXPORTER_H

#ifndef BUILD_VERSION
	#ifdef DEBUG
		#define BUILD_VERSION "UNKNOWN-debug"
	#else
		#define BUILD_VERSION "UNKNOWN-release"
	#endif
#endif

#ifdef BUILD_9X
	// Target Windows 98 and ME (9x, ANSI).
	// Minimum Windows Version: Windows 98 (release version 4.10 -> 0x0410).
	// Minimum Internet Explorer Version: IE 4.01 (0x0401, which corresponds to version 4.72 of both Shell32.dll and Shlwapi.dll).
	// See MSDN: "Shell and Shlwapi DLL Versions".

	// Default values if not defined (in Visual Studio 2005):
	// WINVER 			0x0501 (in windows.h)
	// _WIN32_WINDOWS 	0x0410 (in WinResrc.h)
	// _WIN32_WINNT 	0x0500 (in WinResrc.h)
	// _WIN32_IE 		0x0501 (in WinResrc.h)
	// NTDDI_VERSION 	(not defined)

	#undef UNICODE
	#undef _UNICODE
	#define WINVER 0x0410
	#define _WIN32_WINDOWS 0x0410
	#define _WIN32_WINNT 0
	#define _WIN32_IE 0x0401

	// Windows 95 for future reference:
	//#define WINVER          0x0400
	//#define _WIN32_WINDOWS  0x0400
	//#define _WIN32_WINNT    0
	//#define _WIN32_IE       0x0300
#else
	// Target Windows 2000, XP, Vista, 7, 8.1, and 10 (NT, Unicode).
	// Minimum Windows Version: Windows 2000 (release version NT 5.0 -> 0x0500).
	// Minimum Internet Explorer Version: IE 5 (0x0500, which corresponds to version 5.0 of both Shell32.dll and Shlwapi.dll).
	#define UNICODE
	#define _UNICODE
	#define WINVER 0x0500
	#define _WIN32_WINNT 0x0500 // Checks done for some version of _WIN32_WINDOWS are also done first for the same version of _WIN32_WINNT.
	#define _WIN32_IE 0x0500
	#define NTDDI_VERSION 0x05000000 // NTDDI_WIN2K
#endif

#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <stdarg.h>
#include <direct.h>

#pragma warning(push)
#pragma warning(disable : 4995) // For the deprecation warnings of StrNCatA, StrNCatW, StrCatW, and StrCpyW.
	#include <shlwapi.h>
#pragma warning(pop)
#include <shlobj.h>

#include <crtdbg.h>

typedef __int8 s8;
typedef __int16 s16;
typedef __int32 s32;
typedef __int64 s64;

typedef unsigned __int8 u8;
typedef unsigned __int16 u16;
typedef unsigned __int32 u32;
typedef unsigned __int64 u64;

typedef float float32;
typedef double float64;

enum Exporter_Cache_Type
{
	CACHE_UNKNOWN = 0,
	CACHE_INTERNET_EXPLORER = 1,
	CACHE_SHOCKWAVE_PLUGIN = 2,
	CACHE_JAVA_PLUGIN = 3,
	NUM_CACHE_TYPES = 4
};
const char* const CACHE_TYPE_TO_STRING[NUM_CACHE_TYPES] = {"Unknown", "Internet Explorer", "Shockwave Plugin", "Java Plugin"};

#include "memory_and_file_io.h"
struct Exporter
{
	bool should_copy_files;
	bool should_create_csv;
	bool should_add_csv_header;

	Exporter_Cache_Type cache_type;
	char cache_path[MAX_PATH_CHARS];
	char output_path[MAX_PATH_CHARS];

	Arena arena;
};

#endif
