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
void separate_u32_into_high_and_low_u16s(u32 value, u16* high, u16* low);
void* advance_bytes(void* pointer, u16 num_bytes);
void* advance_bytes(void* pointer, u32 num_bytes);
void* advance_bytes(void* pointer, u64 num_bytes);
void* retreat_bytes(void* pointer, u32 num_bytes);
void* retreat_bytes(void* pointer, u64 num_bytes);
ptrdiff_t pointer_difference(void* a, void* b);
size_t kilobytes_to_bytes(size_t kilobytes);
size_t megabytes_to_bytes(size_t megabytes);
u8 swap_byte_order(u8 value);
s8 swap_byte_order(s8 value);
u16 swap_byte_order(u16 value);
s16 swap_byte_order(s16 value);
u32 swap_byte_order(u32 value);
s32 swap_byte_order(s32 value);
u64 swap_byte_order(u64 value);
s64 swap_byte_order(s64 value);
bool memory_is_equal(const void* buffer_1, const void* buffer_2, size_t size_to_compare);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> DATE AND TIME FORMATTING
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

// A structure that represents an MS-DOS date and time values.
// See: format_dos_date_time().
struct Dos_Date_Time
{
	u16 date;
	u16 time;
};

const size_t MAX_FORMATTED_DATE_TIME_CHARS = 32;
bool format_filetime_date_time(FILETIME date_time, TCHAR* formatted_string);
bool format_dos_date_time(Dos_Date_Time date_time, TCHAR* formatted_string);
bool format_time64_t_date_time(__time64_t date_time, TCHAR* formatted_string);

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
bool convert_s32_to_string(s32 value, TCHAR* result_string);
bool convert_u64_to_string(u64 value, TCHAR* result_string);
bool convert_s64_to_string(s64 value, TCHAR* result_string);

bool convert_hexadecimal_string_to_byte(const TCHAR* byte_string, u8* result_byte);

// Any code page identifiers that don't have a readily available macro or constant. Used by MultiByteToWideChar() and WideCharToMultiByte() in the Win32 API,
// and also convert_code_page_string_to_tchar().
enum Code_Page
{
	CP_UTF16 = 1200
};

TCHAR* convert_code_page_string_to_tchar(Arena* final_arena, Arena* intermediary_arena, u32 code_page, const char* string);
TCHAR* convert_code_page_string_to_tchar(Arena* arena, u32 code_page, const char* string);
TCHAR* convert_ansi_string_to_tchar(Arena* arena, const char* ansi_string);
TCHAR* convert_utf_16_string_to_tchar(Arena* arena, const wchar_t* utf_16_string);
TCHAR* convert_utf_8_string_to_tchar(Arena* final_arena, Arena* intermediary_arena, const char* utf_8_string);
TCHAR* convert_utf_8_string_to_tchar(Arena* arena, const char* utf_8_string);

char* skip_to_end_of_string(char* str);
wchar_t* skip_to_end_of_string(wchar_t* str);
TCHAR** build_array_from_contiguous_strings(Arena* arena, TCHAR* first_string, u32 num_strings);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> URL  MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

// The components of a URL.
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
TCHAR* decode_url(Arena* arena, const TCHAR* url);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> PATH MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

const size_t MAX_PATH_CHARS = MAX_PATH + 1;
const size_t MAX_TEMPORARY_PATH_CHARS = MAX_PATH_CHARS - 14;

TCHAR* skip_to_file_extension(TCHAR* path, bool optional_include_period = false, bool optional_get_first_extension = false);
TCHAR* find_last_path_components(TCHAR* path, u32 desired_num_components);
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
void safe_unmap_view_of_file(void** base_address);

// Defines a combination of options for traverse_directory_objects().
enum Traversal_Flag
{
	TRAVERSE_FILES = 1 << 0,
	TRAVERSE_DIRECTORIES = 1 << 1
};

#define TRAVERSE_DIRECTORY_CALLBACK(function_name) bool function_name(const TCHAR* callback_directory_path, WIN32_FIND_DATA* callback_find_data, void* callback_user_data)
typedef TRAVERSE_DIRECTORY_CALLBACK(Traverse_Directory_Callback);
void traverse_directory_objects(const TCHAR* path, const TCHAR* search_query,
								u32 traversal_flags, bool should_traverse_subdirectories,
								Traverse_Directory_Callback* callback_function, void* user_data);

void create_directories(const TCHAR* path_to_create);
bool delete_directory_and_contents(const TCHAR* directory_path);

#define _TEMPORARY_NAME_PREFIX TEXT("WCE")
#define _TEMPORARY_NAME_SEARCH_QUERY _TEMPORARY_NAME_PREFIX TEXT("*")
const TCHAR* const TEMPORARY_NAME_PREFIX = _TEMPORARY_NAME_PREFIX;
const TCHAR* const TEMPORARY_NAME_SEARCH_QUERY = _TEMPORARY_NAME_SEARCH_QUERY;
bool create_temporary_directory(const TCHAR* base_temporary_path, TCHAR* result_directory_path);
void delete_all_temporary_directories(const TCHAR* base_temporary_path);
bool copy_to_temporary_file(const TCHAR* file_source_path, const TCHAR* base_temporary_path, TCHAR* result_file_destination_path, HANDLE* result_handle);
bool copy_file_using_url_directory_structure(	Arena* arena, const TCHAR* full_file_path, 
												const TCHAR* full_base_directory_path, const TCHAR* url, const TCHAR* filename);

void* memory_map_entire_file(HANDLE file_handle, u64* file_size_result, bool optional_read_only = true);
void* memory_map_entire_file(const TCHAR* file_path, HANDLE* result_file_handle, u64* result_file_size, bool optional_read_only = true);
void* read_entire_file(Arena* arena, const TCHAR* file_path, u64* result_file_size, bool optional_add_null_terminator = false, size_t optional_alignment_size = 0);
bool read_first_file_bytes(	const TCHAR* path, void* file_buffer, u32 num_bytes_to_read,
							bool optional_allow_reading_fewer_bytes = false, u32* optional_result_num_bytes_read = NULL);

bool tchar_query_registry(HKEY hkey, const TCHAR* key_name, const TCHAR* value_name, TCHAR* value_data, u32 value_data_size);
#define query_registry(hkey, key_name, value_name, value_data, value_data_size) tchar_query_registry(hkey, TEXT(key_name), TEXT(value_name), value_data, value_data_size)

// The types of property strings stored in an executable or DLL file's information.
// See: get_file_info().
enum File_Info_Type
{
	INFO_COMMENTS = 0,
	INFO_INTERNAL_NAME = 1,
	INFO_PRODUCT_NAME = 2,
	INFO_COMPANY_NAME = 3,
	INFO_LEGAL_COPYRIGHT = 4,
	INFO_PRODUCT_VERSION = 5,
	INFO_FILE_DESCRIPTION = 6,
	INFO_LEGAL_TRADEMARKS = 7,
	INFO_PRIVATE_BUILD = 8,
	INFO_FILE_VERSION = 9,
	INFO_ORIGINAL_FILENAME = 10,
	INFO_SPECIAL_BUILD = 11,

	NUM_FILE_INFO_TYPES = 12
};

// An array that maps the previous values to ANSI strings.
const char* const FILE_INFO_TYPE_TO_STRING[NUM_FILE_INFO_TYPES] =
{
	"Comments", "InternalName", "ProductName",
	"CompanyName", "LegalCopyright", "ProductVersion",
	"FileDescription", "LegalTrademarks", "PrivateBuild",
	"FileVersion", "OriginalFilename", "SpecialBuild"
};

bool get_file_info(Arena* arena, const TCHAR* full_file_path, File_Info_Type info_type, TCHAR** result_info);

// The types of log lines in the global log file.
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
// 1. string_format - The format string. Note that %hs is used for narrow ANSI strings, %ls for wide UTF-16 strings, and %s for TCHAR
// strings (narrow ANSI or wide UTF-16 depending on the build target).
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
	unsigned int / UNINT		= %u

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
	[enum]						= %d
*/

// The types of columns in the CSV files. These may be shared by all of them or only apply to one cache database file format.
enum Csv_Type
{
	CSV_NONE = 0,

	CSV_FILENAME,
	CSV_URL,
	CSV_FILE_EXTENSION,
	CSV_FILE_SIZE,

	CSV_LAST_WRITE_TIME,
	CSV_LAST_MODIFIED_TIME,
	CSV_CREATION_TIME,
	CSV_LAST_ACCESS_TIME,
	CSV_EXPIRY_TIME,

	CSV_RESPONSE,
	CSV_SERVER,
	CSV_CACHE_CONTROL,
	CSV_PRAGMA,
	CSV_CONTENT_TYPE,
	CSV_CONTENT_LENGTH,
	CSV_CONTENT_ENCODING,

	CSV_LOCATION_ON_CACHE,
	CSV_CACHE_VERSION,
	CSV_LOCATION_ON_DISK,
	CSV_MISSING_FILE,

	CSV_CUSTOM_FILE_GROUP,
	CSV_CUSTOM_URL_GROUP,
	
	// Internet Explorer specific.
	CSV_HITS,
	
	// Shockwave Plugin specific.
	CSV_LIBRARY_SHA_256,

	// Shockwave Plugin specific.
	CSV_DIRECTOR_FILE_TYPE,
	CSV_XTRA_DESCRIPTION,
	CSV_XTRA_VERSION,

	// Java Plugin specific.
	CSV_CODEBASE_IP,
	CSV_VERSION,

	NUM_CSV_TYPES
};

// An array that maps the previous values to UTF-8 strings. Used to write the columns' names directly to the CSV file
// (which uses UTF-8 as the character encoding). This project's source files are stored in UTF-8.
const char* const CSV_TYPE_TO_UTF_8_STRING[NUM_CSV_TYPES] =
{
	"None",
	"Filename", "URL", "File Extension", "File Size",
	"Last Write Time", "Last Modified Time", "Creation Time", "Last Access Time", "Expiry Time",
	"Response", "Server", "Cache Control", "Pragma", "Content Type", "Content Length", "Content Encoding",
	"Location On Cache", "Cache Version", "Location On Disk", "Missing File",
	"Custom File Group", "Custom URL Group",
	"Hits",
	"Library SHA-256",
	"Director File Type", "Xtra Description", "Xtra Version",
	"Codebase IP", "Version"
};

// A helper structure used to write values to the CSV file. The 'value' member is the ANSI or UTF-16 string to write, and
// 'utf_16_value' an intermediary variable that is used to convert the string to UTF-8.
// See: csv_print_row().
struct Csv_Entry
{
	TCHAR* value;
	wchar_t* utf_16_value;
};

bool create_csv_file(const TCHAR* csv_file_path, HANDLE* result_file_handle);
void csv_print_header(Arena* arena, HANDLE csv_file_handle, const Csv_Type* column_types, size_t num_columns);
void csv_print_row(Arena* arena, HANDLE csv_file_handle, Csv_Entry* column_values, size_t num_columns);

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
} while(false, false)

// These functions are only meant to be used in the Windows 2000 through 10 builds. In the Windows 98 and ME builds, attempting
// to call these functions will result in a compile time error.
// If we want to use them, we have to explicitly wrap the code with #ifndef BUILD_9X [...] #endif.
#ifndef BUILD_9X
	void load_ntdll_functions(void);
	void free_ntdll_functions(void);

	void load_kernel32_functions(void);
	void free_kernel32_functions(void);
	
	bool force_copy_open_file(Arena* arena, const wchar_t* copy_source_path, const wchar_t* copy_destination_path);
#else
	#define load_ntdll_functions(...) _STATIC_ASSERT(false)
	#define free_ntdll_functions(...) _STATIC_ASSERT(false)
	
	#define load_kernel32_functions(...) _STATIC_ASSERT(false)
	#define free_kernel32_functions(...) _STATIC_ASSERT(false)

	#define force_copy_open_file(...) _STATIC_ASSERT(false)
#endif

#endif
