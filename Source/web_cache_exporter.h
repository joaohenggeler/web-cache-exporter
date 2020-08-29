#ifndef WEB_CACHE_EXPORTER_H
#define WEB_CACHE_EXPORTER_H

// Define the minimum supported target version. Using functions that are not supported on older systems will result in a compile time
// error. This application targets Windows 98, ME, 2000, XP, Vista, 7, 8.1, and 10. See "Using the Windows Headers" in the Win32 API
// Reference.
#ifdef BUILD_9X
	// Target Windows 98 and ME (9x, ANSI).
	// Minimum Windows Version: Windows 98 (release version 4.10 -> 0x0410).
	// Minimum Internet Explorer Version: IE 4.01 (0x0401, which corresponds to version 4.72 of both Shell32.dll and Shlwapi.dll).
	// See "Shell and Shlwapi DLL Versions" in the Win32 API Reference.

	// Default values if not defined (in Visual Studio 2005 Professional):
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
	// Checks done for some version of _WIN32_WINDOWS are also done first for the same version of _WIN32_WINNT.
	#define _WIN32_WINNT 0x0500
	#define _WIN32_IE 0x0500
	#define NTDDI_VERSION 0x05000000
#endif

// Exclude unnecessary API declarations when including Windows.h. The WIN32_LEAN_AND_MEAN macro would exclude some necessary ones.
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

#include <ntstatus.h> // For the NTSTATUS error code constants.
#include <winternl.h> // For NtQuerySystemInformation()'s definition and any necessary structs and constants that are used by it.
#include <stierr.h> // For the NT_SUCCESS() macro.

#include <tchar.h>
#include <strsafe.h>
#include <stdarg.h>

#include <shlobj.h>
// Disable the deprecation warnings for the following functions: StrNCatA(), StrNCatW(), StrCatW(), and StrCpyW(). We won't use these.
#pragma warning(push)
#pragma warning(disable : 4995)
	#include <shlwapi.h>
#pragma warning(pop)

#include <crtdbg.h> // For _ASSERT() and _STATIC_ASSERT()

// Information about the current build that is passed by the Build.bat batch file.
#ifdef BUILD_VERSION
	const char* const EXPORTER_BUILD_VERSION = BUILD_VERSION;
#else
	const char* const EXPORTER_BUILD_VERSION = "Unknown";
#endif

#ifdef DEBUG
	const char* const EXPORTER_BUILD_MODE = "debug";
#else
	const char* const EXPORTER_BUILD_MODE = "release";
#endif

#ifdef BUILD_9X
	const char* const EXPORTER_BUILD_TARGET = "9x-x86-32";
#else
	#ifdef BUILD_32_BIT
		const char* const EXPORTER_BUILD_TARGET = "NT-x86-32";
	#else
		const char* const EXPORTER_BUILD_TARGET = "NT-x86-64";
	#endif
#endif

// Prevent the use of the /J compiler option where the default 'char' type is changed from 'signed char' to 'unsigned char'.
// We want the range of 'char' to match '__int8' (-128 to 127). See "Data Type Ranges" in the Win32 API Reference.
#ifdef _CHAR_UNSIGNED
	_STATIC_ASSERT(false);
#endif

// Define sized integers and floats. These are useful when defining tighly packed structures that represent various parts of cache
// database file formats.
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

// The cache types of the support web browser and web plugins.
enum Cache_Type
{
	CACHE_UNKNOWN = 0,
	CACHE_ALL = 1,
	CACHE_EXPLORE = 2,

	CACHE_INTERNET_EXPLORER = 3,
	CACHE_SHOCKWAVE_PLUGIN = 4,
	CACHE_JAVA_PLUGIN = 5,
	
	NUM_CACHE_TYPES = 6
};

// An array that maps the previous values to full names.
const TCHAR* const CACHE_TYPE_TO_STRING[NUM_CACHE_TYPES] =
{
	TEXT("Unknown"), TEXT("All"), TEXT("Explore"),
	TEXT("Internet Explorer"), TEXT("Shockwave Plugin"), TEXT("Java Plugin")
};

#include "memory_and_file_io.h"
struct Exporter;
#include "custom_groups.h"

// A structure that represents a cache exporter. 
struct Exporter
{
	// WCE.exe [Optional Arguments] <Export Argument>
	// Where <Export Argument> is: -export-<Cache Type> [Optional Cache path] [Optional Output Path]

	// The optional command line arguments.
	bool should_copy_files;
	bool should_create_csv;
	bool should_overwrite_previous_output;
	bool should_filter_by_groups;

	bool should_load_specific_groups_files;
	size_t num_group_filenames_to_load;
	TCHAR** group_filenames_to_load;
	
	bool should_use_ie_hint;
	TCHAR ie_hint_path[MAX_PATH_CHARS];

	// The export command line arguments.
	Cache_Type cache_type;
	TCHAR cache_path[MAX_PATH_CHARS];
	TCHAR output_path[MAX_PATH_CHARS];
	bool is_exporting_from_default_locations;

	// The current Windows version. Used to determine how much memory to allocate for the temporary memory.
	OSVERSIONINFO os_version;

	// The permanent memory arena that persists throughout the application's execution.
	Arena permanent_arena;
	// The temporary memory arena that is used and overwritten when processing each cached file.
	Arena temporary_arena;

	// The loaded group file data that is stored in the permanent memory arena.
	Custom_Groups* custom_groups;

	// The paths to relevant exporter locations.
	TCHAR executable_path[MAX_PATH_CHARS];
	bool was_temporary_exporter_directory_created;
	TCHAR exporter_temporary_path[MAX_PATH_CHARS];
	
	// The paths to relevant Windows locations. These are sometimes used to find cache directories.
	TCHAR windows_temporary_path[MAX_PATH_CHARS];
	TCHAR roaming_appdata_path[MAX_PATH_CHARS];
	TCHAR local_appdata_path[MAX_PATH_CHARS];
	TCHAR local_low_appdata_path[MAX_PATH_CHARS];

	// General purpose variables that are freely changed by each cache exporter:
	// - The currently open CSV file.
	HANDLE csv_file_handle;
	// - The number of columns in the CSV file.
	size_t num_csv_columns;
	// - The types of each column as an arrayof length 'num_csv_columns'.
	const Csv_Type* csv_column_types;
	// - The path to the base directory where the cached files will be copied to.
	TCHAR output_copy_path[MAX_PATH_CHARS];
	// - The path to the currently open CSV file.
	TCHAR output_csv_path[MAX_PATH_CHARS];
	// - The path to the index/database file that contains a cached file's metadata.
	// - The contents of this path vary between different cache types and versions.
	TCHAR index_path[MAX_PATH_CHARS];

	// Used to count how many cache files were exported.
	size_t num_csv_files_created;
	size_t num_processed_files;
	size_t num_copied_files;
};

void initialize_cache_exporter(	Exporter* exporter, const TCHAR* cache_identifier,
														const Csv_Type column_types[], size_t num_columns);

void export_cache_entry(Exporter* exporter, Csv_Entry column_values[],
						TCHAR* full_entry_path, TCHAR* entry_url, TCHAR* entry_filename,
						WIN32_FIND_DATA* optional_find_data = NULL);

void terminate_cache_exporter(Exporter* exporter);

#endif
