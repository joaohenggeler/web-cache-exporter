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

#include <time.h> // For _gmtime64_s() and _tcsftime().
#include <crtdbg.h> // For _ASSERT() and _STATIC_ASSERT().

// A handy shorthand for the TEXT() macro. See the comment at the top of "web_cache_exporter.cpp" for more details.
#define T(char_or_string) TEXT(char_or_string)

// Information about the current build that is passed by the Build.bat batch file.
#ifdef BUILD_TARGET
	const char* const EXPORTER_BUILD_TARGET = BUILD_TARGET;
#else
	const char* const EXPORTER_BUILD_TARGET = "?";
#endif

#ifdef BUILD_VERSION
	const char* const EXPORTER_BUILD_VERSION = BUILD_VERSION;
#else
	const char* const EXPORTER_BUILD_VERSION = "0.0.0";
#endif

#ifdef BUILD_DEBUG
	const char* const EXPORTER_BUILD_MODE = "debug";
#else
	const char* const EXPORTER_BUILD_MODE = "release";
#endif

// Prevent the use of the /J compiler option where the default 'char' type is changed from 'signed char' to 'unsigned char'.
// We want the range of 'char' to match '__int8' (-128 to 127). See "Data Type Ranges" in the Win32 API Reference.
#ifdef _CHAR_UNSIGNED
	_STATIC_ASSERT(false);
#endif

// Define sized integers and floats. These are useful when defining tighly packed structures that represent various parts of cache
// database file formats.
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

typedef SSIZE_T ssize_t;

// The cache types of the supported web browser and web plugins.
enum Cache_Type
{
	CACHE_UNKNOWN = 0,
	CACHE_ALL = 1,
	CACHE_EXPLORE = 2,

	CACHE_INTERNET_EXPLORER = 3,
	CACHE_MOZILLA = 4,

	CACHE_FLASH = 5,
	CACHE_SHOCKWAVE = 6,
	CACHE_JAVA = 7,
	CACHE_UNITY = 8,
	
	NUM_CACHE_TYPES = 9
};

// An array that maps the previous values to full and short names.
const TCHAR* const CACHE_TYPE_TO_FULL_NAME[] =
{
	T("Unknown"), T("All"), T("Explore"),
	T("Internet Explorer"), T("Mozilla"),
	T("Flash Player"), T("Shockwave Player"), T("Java Plugin"), T("Unity Web Player")
};
_STATIC_ASSERT(_countof(CACHE_TYPE_TO_FULL_NAME) == NUM_CACHE_TYPES);

const TCHAR* const CACHE_TYPE_TO_SHORT_NAME[] =
{
	T("unknown"), T("all"), T("explore"),
	T("ie"), T("mozilla"),
	T("flash"), T("shockwave"), T("java"), T("unity")
};
_STATIC_ASSERT(_countof(CACHE_TYPE_TO_SHORT_NAME) == NUM_CACHE_TYPES);

// Whether a given cache type belongs to a web plugin or not (i.e. a web browser).
const bool IS_CACHE_TYPE_PLUGIN[] =
{
	false, false, false,
	false, false,
	true, true, true, true
};
_STATIC_ASSERT(_countof(IS_CACHE_TYPE_PLUGIN) == NUM_CACHE_TYPES);

#include "memory_and_file_io.h"

struct Exporter;
#include "custom_groups.h"

// A structure that represents a user profile and that contains the locations of any system directories used by each cache exporter.
// See: load_external_locations().
struct Profile
{
	TCHAR* name;

	TCHAR* drive_path;
	TCHAR* windows_path;
	TCHAR* windows_temporary_path;
	TCHAR* user_profile_path;
	TCHAR* appdata_path;
	TCHAR* local_appdata_path;
	TCHAR* local_low_appdata_path;
	TCHAR* wininet_cache_path;
};

// An array of user profile locations to export.
// See: load_external_locations().
struct External_Locations
{
	int num_profiles;
	Profile profiles[ANYSIZE_ARRAY];
};

const size_t MAX_EXPORTER_WARNING_CHARS = 1000;

// A structure that represents a cache exporter. 
struct Exporter
{
	// WCE.exe [Optional Arguments] <Export Argument>
	// Where <Export Argument> is: <Export Option> [Optional Cache path] [Optional Output Path]

	// The optional command line arguments.
	bool should_copy_files;
	bool should_create_csvs;
	bool should_overwrite_previous_output;
	bool should_show_full_paths;
	bool should_group_by_request_origin;
	bool should_decompress_files;
	bool should_delete_previous_temporary_directories;

	bool should_filter_by_groups;
	String_Array<TCHAR>* group_files_for_filtering;
	
	bool should_ignore_filter_for_cache_type[NUM_CACHE_TYPES];
	
	bool should_use_ie_hint;
	TCHAR ie_hint_path[MAX_PATH_CHARS];

	// Whether or not the path to the external locations file was specified in the CACHE_ALL export option,
	// along with the path itself.
	bool should_load_external_locations;
	TCHAR external_locations_file_path[MAX_PATH_CHARS];
	// The name of the profile whose cache is current being exported. 
	TCHAR* current_profile_name;
	
	// The export command line arguments.
	Cache_Type command_line_cache_type;
	TCHAR cache_path[MAX_PATH_CHARS];
	TCHAR output_path[MAX_PATH_CHARS];
	bool is_exporting_from_default_locations;

	// The current cache type that is being exported. When exporting every type using CACHE_ALL, this member will
	// hold multiple values at different times.
	Cache_Type current_cache_type;

	// The current Windows version. Used to determine how much memory to allocate for the temporary memory.
	OSVERSIONINFO os_version;

	// The permanent memory arena that persists throughout the application's execution.
	Arena permanent_arena;
	// The temporary memory arena that is used and overwritten when processing each cached file.
	Arena temporary_arena;
	// A smaller temporary memory arena used specifically when loading group files in the Windows 98 and ME builds.
	Arena secondary_temporary_arena;

	// The loaded group file data that is stored in the permanent memory arena.
	Custom_Groups* custom_groups;

	// The loaded external locations file data that is stored in the permanent memory arena.
	External_Locations* external_locations;

	// The paths to relevant exporter locations.
	TCHAR executable_path[MAX_PATH_CHARS];
	TCHAR group_files_path[MAX_PATH_CHARS];
	TCHAR exporter_temporary_path[MAX_PATH_CHARS];
	bool was_temporary_exporter_directory_created;

	// The warning message for the current cache entry. This buffer is cleared after each cached file is exported.
	// See tchar_add_exporter_warning_message().
	TCHAR warning_message[MAX_EXPORTER_WARNING_CHARS];
	
	// The absolute paths to relevant Windows locations. These are used to find the default cache directories.
	// @DefaultCacheLocations:
	TCHAR drive_path[MAX_PATH_CHARS];
	// - 98, ME, 2000, XP, Vista, 7, 8.1, 10	C:\ 
	TCHAR windows_path[MAX_PATH_CHARS];
	// - 98, ME, XP, Vista, 7, 8.1, 10 			C:\WINDOWS
	// - 2000 									C:\WINNT
	TCHAR windows_temporary_path[MAX_PATH_CHARS];
	// - 98, ME									C:\WINDOWS\TEMP
	// - 2000, XP	 							C:\Documents and Settings\<Username>\Local Settings\Temp
	// - Vista, 7, 8.1, 10						C:\Users\<Username>\AppData\Local\Temp
	TCHAR user_profile_path[MAX_PATH_CHARS];
	// - 98, ME 								<None>
	// - 2000, XP	 							C:\Documents and Settings\<Username>
	// - Vista, 7, 8.1, 10						C:\Users\<Username>
	TCHAR appdata_path[MAX_PATH_CHARS];
	// - 98, ME 								C:\WINDOWS\Application Data
	// - 2000, XP	 							C:\Documents and Settings\<Username>\Application Data
	// - Vista, 7, 8.1, 10						C:\Users\<Username>\AppData\Roaming
	TCHAR local_appdata_path[MAX_PATH_CHARS];
	// - 98, ME 								<None>
	// - 2000, XP	 							C:\Documents and Settings\<Username>\Local Settings\Application Data
	// - Vista, 7, 8.1, 10						C:\Users\<Username>\AppData\Local
	TCHAR local_low_appdata_path[MAX_PATH_CHARS];
	// - 98, ME 								<None>
	// - 2000, XP	 							<None>
	// - Vista, 7, 8.1, 10						C:\Users\<Username>\AppData\LocalLow
	TCHAR wininet_cache_path[MAX_PATH_CHARS];
	// - 98, ME 								C:\WINDOWS\Temporary Internet Files
	// - 2000, XP	 							C:\Documents and Settings\<Username>\Local Settings\Temporary Internet Files
	// - Vista, 7								C:\Users\<Username>\AppData\Local\Microsoft\Windows\Temporary Internet Files
	// - 8.1, 10								C:\Users\<Username>\AppData\Local\Microsoft\Windows\INetCache

	// TCHAR program_files_path[MAX_PATH_CHARS];
	// - 98, ME, 2000, XP, Vista, 7, 8.1, 10	C:\Program Files

	// General purpose variables that are freely changed by each cache exporter:
	
	// - The currently open CSV file.
	HANDLE csv_file_handle;
	// - Whether we tried to export at least one file since the exporter was initialized.
	bool exported_at_least_one_file;

	// - The identifier that's used to name the output directory.
	// - Each cache exporter may use one or more identifiers.
	const TCHAR* cache_identifier;
	// - The types of each column as an array of length 'num_csv_columns'.
	Csv_Type* csv_column_types;
	// - The number of columns in the CSV file.
	int num_csv_columns;

	// - The number of components in the relative path containing composed of the current cache exporter's identifier and subdirectories.
	int num_output_components;
	// - The path to the base directory where the cached files will be copied to.
	TCHAR output_copy_path[MAX_PATH_CHARS];
	// - The path to the currently open CSV file.
	TCHAR output_csv_path[MAX_PATH_CHARS];
	// - The path to the index/database file that contains a cached file's metadata.
	// - The contents of this path vary between different cache types and versions.
	TCHAR index_path[MAX_PATH_CHARS];
	// - The current browser name.
	TCHAR* browser_name;
	// - The current browser profile.
	TCHAR* browser_profile;

	// Used to count how many cached files were exported.
	int total_csv_files_created;
	int total_processed_files;
	int total_copied_files;

	int num_assigned_filenames;
	int total_assigned_filenames;
};

// The basic parameters used to build the output locations and fill certain CSV columns for each cache exporter.
// See: export_cache_entry().
struct Exporter_Params
{
	TCHAR* copy_source_path;
	TCHAR* url;
	TCHAR* filename;

	TCHAR* request_origin;
	Http_Headers headers;

	TCHAR* short_location_on_cache;
	TCHAR* full_location_on_cache;

	Traversal_Object_Info* file_info;
};

void initialize_cache_exporter(Exporter* exporter, Cache_Type cache_type, const TCHAR* cache_identifier, Csv_Type* column_types, int num_columns);

void set_exporter_output_copy_subdirectory(Exporter* exporter, const TCHAR* subdirectory_name);

void tchar_add_exporter_warning_message(Exporter* exporter, const TCHAR* string_format, ...);
#define add_exporter_warning_message(exporter, string_format, ...) tchar_add_exporter_warning_message(exporter, T(string_format), __VA_ARGS__)

void export_cache_entry(Exporter* exporter, Csv_Entry* column_values, Exporter_Params* params);

void reset_temporary_exporter_members(Exporter* exporter);

void terminate_cache_exporter(Exporter* exporter);

#define _TEMPORARY_NAME_PREFIX T("WCE")
#define _TEMPORARY_NAME_SEARCH_QUERY _TEMPORARY_NAME_PREFIX T("*")
const TCHAR* const TEMPORARY_NAME_PREFIX = _TEMPORARY_NAME_PREFIX;
const TCHAR* const TEMPORARY_NAME_SEARCH_QUERY = _TEMPORARY_NAME_SEARCH_QUERY;
#undef _TEMPORARY_NAME_PREFIX
#undef _TEMPORARY_NAME_SEARCH_QUERY

bool create_placeholder_exporter_file(Exporter* exporter, TCHAR* result_file_path, const TCHAR* optional_filename = NULL);
bool create_temporary_exporter_file(Exporter* exporter, TCHAR* result_file_path, HANDLE* result_file_handle);
void clear_temporary_exporter_directory(Exporter* exporter);
void delete_all_temporary_exporter_directories(Exporter* exporter);

bool resolve_exporter_external_locations_path(Exporter* exporter, const TCHAR* path, TCHAR* result_path);

#endif
