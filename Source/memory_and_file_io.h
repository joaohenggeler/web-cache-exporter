#ifndef MEMORY_AND_FILE_IO_H
#define MEMORY_AND_FILE_IO_H

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> MEMORY ALLOCATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

// A structure that represents a memory arena of with a given capacity in bytes. The 'available_memory' member points to the next free
// memory location.
// See: create_arena() and aligned_push_arena().
struct Arena
{
	size_t used_size;
	size_t total_size;
	void* available_memory;
};

// A helper constant used to identify uninitialized or destroyed memory arenas.
const Arena NULL_ARENA = {0, 0, NULL};

bool create_arena(Arena* arena, size_t total_size);
void* aligned_push_arena(Arena* arena, size_t push_size, size_t alignment_size);
void* aligned_push_and_copy_to_arena(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size);
#define push_arena(arena, push_size, Type) ((Type*) aligned_push_arena(arena, push_size, __alignof(Type)))
#define push_and_copy_to_arena(arena, push_size, Type, data, data_size) ((Type*) aligned_push_and_copy_to_arena(arena, push_size, __alignof(Type), data, data_size))
TCHAR* push_string_to_arena(Arena* arena, const TCHAR* string_to_copy);
float32 get_used_arena_capacity(Arena* arena);
void clear_arena(Arena* arena);
bool destroy_arena(Arena* arena);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> BASIC OPERATIONS
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

#define WHILE_TRUE(...) for(;;)
#define MIN(a, b) ( ((a) < (b)) ? (a) : (b) )
#define MAX(a, b) ( ((a) > (b)) ? (a) : (b) )
#define IS_POWER_OF_TWO(value) ( ((value) > 0) && (( (value) & ((value) - 1) ) == 0) )

// Aligns a value up given an alignment size. This value is offset so that it is the next multiple of the alignment size.
//
// @Parameters:
// 1. value - The value to align.
// 2. alignment - The alignment size in bytes. This value must be a power of two.
#define ALIGN_UP(value, alignment) ( ( (value) + ((alignment) - 1) ) & ~((alignment) - 1) )

u64 combine_high_and_low_u32s_into_u64(u32 high, u32 low);
void separate_u64_into_high_and_low_u32s(u64 value, u32* high, u32* low);
void* advance_bytes(void* pointer, size_t num_bytes);
void* retreat_bytes(void* pointer, size_t num_bytes);
ptrdiff_t pointer_difference(void* a, void* b);
size_t kilobytes_to_bytes(size_t kilobytes);
size_t megabytes_to_bytes(size_t megabytes);

/*
_byteswap_ushort
_byteswap_ulong
_byteswap_uint64
*/

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> DATE AND TIME FORMATTING
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

// A structure that represents an MS-DOS date and time values.
// See: format_dos_date_time().
// Tightly packed because it will be used when defining other packed structures that represent different file formats.
#pragma pack(push, 1)
struct Dos_Date_Time
{
	u16 date;
	u16 time;
};
#pragma pack(pop)
_STATIC_ASSERT(sizeof(Dos_Date_Time) == sizeof(u32));
_STATIC_ASSERT(sizeof(FILETIME) == sizeof(u64));

const size_t MAX_FORMATTED_DATE_TIME_CHARS = 32;
bool format_filetime_date_time(FILETIME date_time, TCHAR* formatted_string);
bool format_dos_date_time(Dos_Date_Time date_time, TCHAR* formatted_string);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> STRING MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

size_t string_size(const char* str);
size_t string_size(const wchar_t* str);

size_t string_length(const char* str);
size_t string_length(const wchar_t* str);

bool string_is_empty(const char* str);
bool string_is_empty(const wchar_t* str);

bool strings_are_equal(const char* str_1, const char* str_2, bool optional_case_insensitive = false);
bool strings_are_equal(const wchar_t* str_1, const wchar_t* str_2, bool optional_case_insensitive = false);

bool strings_are_at_most_equal(const char* str_1, const char* str_2, size_t max_num_chars, bool optional_case_insensitive = false);
bool strings_are_at_most_equal(const wchar_t* str_1, const wchar_t* str_2, size_t max_num_chars, bool optional_case_insensitive = false);

bool string_starts_with(const TCHAR* str, const TCHAR* prefix, bool optional_case_insensitive = false);

bool string_ends_with(const TCHAR* str, const TCHAR* suffix, bool optional_case_insensitive = false);

char* skip_leading_whitespace(char* str);
wchar_t* skip_leading_whitespace(wchar_t* str);

// u32 = "0" to "4294967295", s32 = "-2147483648" to "2147483647", MAX = 11 characters
// u64 = "0" to "18446744073709551615", s64 = "-9223372036854775808" to "9223372036854775807", MAX = 20 characters
const size_t MAX_INT32_CHARS = 11 + 1;
const size_t MAX_INT64_CHARS = 20 + 1; 
bool convert_u32_to_string(u32 value, TCHAR* result_string);
bool convert_u64_to_string(u64 value, TCHAR* result_string);
bool convert_s64_to_string(s64 value, TCHAR* result_string);

bool convert_hexadecimal_string_to_byte(const TCHAR* byte_string, u8* result_byte);

TCHAR* copy_ansi_string_to_tchar(Arena* arena, const char* ansi_string);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> URL  MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

// The various components of a URL.
// See: partition_url().
struct Url_Parts
{
	TCHAR* scheme;

	TCHAR* userinfo;
	TCHAR* host;
	TCHAR* port;

	TCHAR* path;
	TCHAR* query;
	TCHAR* fragment;
};

bool partition_url(Arena* arena, const TCHAR* original_url, Url_Parts* url_parts);
bool decode_url(TCHAR* url);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> PATH MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

const size_t MAX_PATH_CHARS = MAX_PATH + 1;
const size_t MAX_TEMPORARY_PATH_CHARS = MAX_PATH_CHARS - 14;

TCHAR* skip_to_file_extension(TCHAR* path, bool optional_include_period = false);
bool get_full_path_name(const TCHAR* path, TCHAR* result_full_path, u32 optional_num_result_path_chars = MAX_PATH_CHARS);
bool get_full_path_name(TCHAR* result_full_path);
bool get_special_folder_path(int csidl, TCHAR* result_path);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> FILE I/O
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

bool does_file_exist(const TCHAR* file_path);
bool get_file_size(HANDLE file_handle, u64* file_size_result);
void safe_close_handle(HANDLE* handle);
void safe_find_close(HANDLE* search_handle);
// Unmaps a mapped view of a file and sets its value to NULL.
//
// @Parameters:
// 1. base_address - The address of the view to unmap.
#define SAFE_UNMAP_VIEW_OF_FILE(base_address)\
do\
{\
	if(base_address != NULL)\
	{\
		UnmapViewOfFile(base_address);\
		base_address = NULL;\
	}\
	else\
	{\
		log_print(LOG_WARNING, "Safe Unmap View Of File: Attempted to unmap an invalid address.");\
		_ASSERT(false);\
	}\
}\
while(false, false)

// Defines a combination of options for traverse_directory_objects().
enum Traversal_Flag
{
	TRAVERSE_FILES = 1 << 0,
	TRAVERSE_DIRECTORIES = 1 << 1
};

#define TRAVERSE_DIRECTORY_CALLBACK(function_name) void function_name(const TCHAR* directory_path, WIN32_FIND_DATA* find_data, void* user_data)
typedef TRAVERSE_DIRECTORY_CALLBACK(Traverse_Directory_Callback);
void traverse_directory_objects(const TCHAR* path, const TCHAR* search_query,
								u32 traversal_flags, bool should_traverse_subdirectories,
								Traverse_Directory_Callback* callback_function, void* user_data);

void create_directories(const TCHAR* path_to_create);
bool delete_directory_and_contents(const TCHAR* directory_path);
bool create_temporary_directory(const TCHAR* base_temporary_path, TCHAR* result_directory_path);
bool copy_to_temporary_file(const TCHAR* file_source_path, const TCHAR* base_temporary_path, TCHAR* result_file_destination_path, HANDLE* result_handle);
bool copy_file_using_url_directory_structure(	Arena* arena, const TCHAR* full_file_path, 
												const TCHAR* full_base_directory_path, const TCHAR* url, const TCHAR* filename);

void* memory_map_entire_file(HANDLE file_handle, u64* file_size_result, bool optional_read_only = true);
void* memory_map_entire_file(const TCHAR* file_path, HANDLE* result_file_handle, u64* result_file_size, bool optional_read_only = true);
bool read_first_file_bytes(	const TCHAR* path, void* file_buffer, u32 num_bytes_to_read,
							bool optional_allow_reading_fewer_bytes = false, u32* optional_result_num_bytes_read = NULL);

bool tchar_query_registry(HKEY hkey, const TCHAR* key_name, const TCHAR* value_name, TCHAR* value_data, u32 value_data_size);
#define query_registry(hkey, key_name, value_name, value_data, value_data_size) tchar_query_registry(hkey, TEXT(key_name), TEXT(value_name), value_data, value_data_size)

// The types of the various log lines in the global log file.
// See: tchar_log_print().
enum Log_Type
{
	LOG_NONE = 0,
	LOG_INFO = 1,
	LOG_WARNING = 2,
	LOG_ERROR = 3,
	LOG_DEBUG = 4,
	NUM_LOG_TYPES = 5
};

// An array that maps the previous values to TCHAR strings. These strings will appear at the beginning of every log line.
const TCHAR* const LOG_TYPE_TO_STRING[NUM_LOG_TYPES] = {TEXT(""), TEXT("[INFO] "), TEXT("[WARNING] "), TEXT("[ERROR] "), TEXT("[DEBUG] ")};

bool create_log_file(const TCHAR* log_file_path);
void close_log_file(void);
void tchar_log_print(Log_Type log_type, const TCHAR* string_format, ...);
#define log_print(log_type, string_format, ...) tchar_log_print(log_type, TEXT(string_format), __VA_ARGS__)
#define log_print_newline() log_print(LOG_NONE, "")
#ifdef DEBUG
	#define debug_log_print(string_format, ...) log_print(LOG_DEBUG, string_format, __VA_ARGS__)
#else
	#define debug_log_print(...)
#endif

// Writes a formatted TCHAR string to the standard output stream. This string will be followed by two newline characters.
//
// @Parameters:
// 1. string_format - The format string. Note that %hs is used for narrow ANSI strings, %ls for wide Unicode strings, and %s for TCHAR
// strings (ANSI or Wide depending on the build target).
// 2. ... - Zero or more arguments to be inserted in the format string.
#define console_print(string_format, ...) _tprintf(TEXT(string_format) TEXT("\n\n"), __VA_ARGS__)

/*
	Format Specifiers For Printf Functions:
	- e.g. StringCchPrintf(), log_print(), console_print(), etc.

	char* 						= %hs
	wchar_t*  					= %ls
	TCHAR* 						= %s

	char 						= %hc / %hhd (character / value)
	s8							= %hhd
	unsigned char / u8			= %hhu
	wchar_t 					= %lc
	TCHAR 						= %c

	short / s16					= %hd
	unsigned short / u16 		= %hu

	long						= %ld
	unsigned long				= %lu
	long long 					= %lld
	unsigned long long 			= %llu

	int 						= %d
	unsigned int 				= %u

	size_t 						= %Iu
	ptrdiff_t					= %Id

	u32							= %I32u
	s32							= %I32d
	u64							= %I64u
	s64							= %I64d

	BYTE 						= %hhu
	WORD 						= %hu
	DWORD (GetLastError())		= %lu
	QWORD 						= %I64u

	u8 / BYTE in hexadecimal	= 0x%02X
	u16 / WORD in hexadecimal	= 0x%04X
	u32 / DWORD in hexadecimal	= 0x%08X

	void* / HANDLE 				= %p
	BOOL 						= %d
	HRESULT / JET_ERR 			= %ld
*/

// The types of the various columns in the CSV files. These may be shared by all of them or only apply to one cache database file format.
enum Csv_Type
{
	CSV_NONE = 0,

	CSV_FILENAME = 1,
	CSV_URL = 2,
	CSV_FILE_EXTENSION = 3,
	CSV_FILE_SIZE = 4,

	CSV_LAST_WRITE_TIME = 5,
	CSV_LAST_MODIFIED_TIME = 6,
	CSV_CREATION_TIME = 7,
	CSV_LAST_ACCESS_TIME = 8,
	CSV_EXPIRY_TIME = 9,

	CSV_RESPONSE = 10,
	CSV_SERVER = 11,
	CSV_CACHE_CONTROL = 12,
	CSV_PRAGMA = 13,
	CSV_CONTENT_TYPE = 14,
	CSV_CONTENT_LENGTH = 15,
	CSV_CONTENT_ENCODING = 16,

	CSV_LOCATION_ON_CACHE = 17,

	// Automatically set by export_cache_entry():
	CSV_MISSING_FILE = 18, 
	CSV_CUSTOM_FILE_GROUP = 19, // Depends on CSV_FILE_EXTENSION and CSV_CONTENT_TYPE.
	CSV_CUSTOM_URL_GROUP = 20,
	
	// Internet Explorer specific.
	CSV_HITS = 21,
	
	// Shockwave Plugin specific.
	CSV_DIRECTOR_FILE_TYPE = 22,

	NUM_CSV_TYPES = 23
};

// An array that maps the previous values to UTF-8 strings. Used to write the columns' names directly to the CSV file
// (which uses UTF-8 as the character encoding). This project's source files are stored in UTF-8.
const char* const CSV_TYPE_TO_UTF_8_STRING[NUM_CSV_TYPES] =
{
	"None",
	"Filename", "URL", "File Extension", "File Size",
	"Last Write Time", "Last Modified Time", "Creation Time", "Last Access Time", "Expiry Time",
	"Response", "Server", "Cache-Control", "Pragma", "Content-Type", "Content-Length", "Content-Encoding",
	"Location On Cache", "Missing File",
	"Custom File Group", "Custom URL Group",
	"Hits",
	"Director File Type"
};

// A helper structure used to write values to the CSV file. The 'value' member is the ANSI or Wide string to write, and
// 'utf_16_value' an intermediary variable that is used to convert the string to UTF-8.
// See: csv_print_row().
struct Csv_Entry
{
	TCHAR* value;
	wchar_t* utf_16_value;
};

// A helper constant used to identify uninitialized CSV entries. These are used when the value of some column is automatically
// set by export_cache_entry() and doesn't need to be handled by each exporter specifically. For example, the custom group
// columns (see the enumeration above).
const Csv_Entry NULL_CSV_ENTRY = {NULL, NULL};

bool create_csv_file(const TCHAR* csv_file_path, HANDLE* result_file_handle);
void csv_print_header(Arena* arena, HANDLE csv_file, const Csv_Type row_types[], size_t num_columns);
void csv_print_row(Arena* arena, HANDLE csv_file_handle, Csv_Entry column_values[], size_t num_columns);

// Retrieves the address of a function from a loaded library and sets a given variable to this value.
//
// @Parameters:
// 1. library - A handle (HMODULE) to the DLL module that contains the function.
// 2. function_name - A narrow ANSI string with the function's name.
// 3. Function_Pointer_Type - The function pointer type of the variable that will receive the retrieved address.
// 4. function_pointer_variable - The name of this variable in the source code.
#define GET_FUNCTION_ADDRESS(library, function_name, Function_Pointer_Type, function_pointer_variable)\
do\
{\
	Function_Pointer_Type address = (Function_Pointer_Type) GetProcAddress(library, function_name);\
	if(address != NULL)\
	{\
		function_pointer_variable = address;\
	}\
	else\
	{\
		log_print(LOG_ERROR, "Get Function Address: Failed to retrieve the function address for '%hs' with the error code %lu.", function_name, GetLastError());\
	}\
}\
while(false, false)

// These functions are only meant to be used in the Windows 2000 through 10 builds. In the Windows 98 and ME builds, attempting
// to call these functions will result in a compile time error.
// If we want to use them, we have to explicitly wrap the code with #ifndef BUILD_9X [...] #endif.
#ifndef BUILD_9X
	void windows_nt_load_ntdll_functions(void);
	void windows_nt_free_ntdll_functions(void);

	void windows_nt_load_kernel32_functions(void);
	void windows_nt_free_kernel32_functions(void);
	
	bool windows_nt_force_copy_open_file(Arena* arena, const wchar_t* copy_source_path, const wchar_t* copy_destination_path);
#else
	#define windows_nt_load_ntdll_functions(...) _STATIC_ASSERT(false)
	#define windows_nt_free_ntdll_functions(...) _STATIC_ASSERT(false)
	
	#define windows_nt_load_kernel32_functions(...) _STATIC_ASSERT(false)
	#define windows_nt_free_kernel32_functions(...) _STATIC_ASSERT(false)

	#define windows_nt_force_copy_open_file(...) _STATIC_ASSERT(false)
#endif

#endif
