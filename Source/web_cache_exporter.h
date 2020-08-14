#ifndef WEB_CACHE_EXPORTER_H
#define WEB_CACHE_EXPORTER_H

// Passed by the Build.bat batch file.
#ifndef BUILD_VERSION
	#define BUILD_VERSION "UNKNOWN"
#endif

#ifdef DEBUG
	#define BUILD_MODE "debug"
#else
	#define BUILD_MODE "release"
#endif

#ifdef BUILD_9X
	#define BUILD_TARGET "9x-x86-32"
#else
	#ifdef BUILD_32_BIT
		#define BUILD_TARGET "NT-x86-32"
	#else
		#define BUILD_TARGET "NT-x86-64"
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
	#undef _MBCS
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
	#undef _MBCS
	#define WINVER 0x0500
	#define _WIN32_WINNT 0x0500 // Checks done for some version of _WIN32_WINDOWS are also done first for the same version of _WIN32_WINNT.
	#define _WIN32_IE 0x0500
	#define NTDDI_VERSION 0x05000000 // NTDDI_WIN2K
#endif

// Avoid preprocessor redefinition warnings when including both windows.h and ntstatus.h.
#define WIN32_NO_STATUS
	#include <windows.h>
#undef WIN32_NO_STATUS

#include <ntstatus.h>
#include <winternl.h>
#include <stierr.h>

#include <tchar.h>
#include <strsafe.h>
#include <stdarg.h>

#include <shlobj.h>
// Disable the deprecation warnings for the following functions: StrNCatA, StrNCatW, StrCatW, and StrCpyW.
#pragma warning(push)
#pragma warning(disable : 4995)
	#include <shlwapi.h>
#pragma warning(pop)

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
	CACHE_ALL = 1,

	CACHE_INTERNET_EXPLORER = 2,
	CACHE_SHOCKWAVE_PLUGIN = 3,
	CACHE_JAVA_PLUGIN = 4,
	
	NUM_CACHE_TYPES = 5
};
const TCHAR* const CACHE_TYPE_TO_STRING[NUM_CACHE_TYPES] =
{
	TEXT("Unknown"), TEXT("All"),
	TEXT("Internet Explorer"), TEXT("Shockwave Plugin"), TEXT("Java Plugin")
};

#include "memory_and_file_io.h"
struct Exporter
{
	bool should_copy_files;
	bool should_create_csv;
	bool should_merge_copied_files;

	Exporter_Cache_Type cache_type;
	TCHAR cache_path[MAX_PATH_CHARS];
	TCHAR output_path[MAX_PATH_CHARS];
	bool is_exporting_from_default_locations;

	OSVERSIONINFO os_version;

	Arena permanent_arena;
	Arena temporary_arena;

	TCHAR executable_path[MAX_PATH_CHARS];
	bool was_temporary_directory_created;
	TCHAR exporter_temporary_path[MAX_PATH_CHARS];
	
	TCHAR windows_temporary_path[MAX_PATH_CHARS];
	TCHAR roaming_appdata_path[MAX_PATH_CHARS];
	TCHAR local_appdata_path[MAX_PATH_CHARS];
	TCHAR local_low_appdata_path[MAX_PATH_CHARS];

	bool should_use_ie_hint;
	TCHAR ie_hint_path[MAX_PATH_CHARS];

	HANDLE csv_file_handle;
	TCHAR output_copy_path[MAX_PATH_CHARS];
	TCHAR output_csv_path[MAX_PATH_CHARS];
	TCHAR index_path[MAX_PATH_CHARS];

	u32 num_copied_files;
};

void resolve_exporter_output_paths_and_create_csv_file(	Exporter* exporter, const TCHAR* cache_identifier,
												const Csv_Type column_types[], size_t num_columns);
void export_cache_entry(Exporter* exporter,
						const Csv_Type column_types[], Csv_Entry column_values[], size_t num_columns,
						const TCHAR* full_entry_path, const TCHAR* entry_url, const TCHAR* entry_filename);
void close_exporter_csv_file(Exporter* exporter);

#endif
