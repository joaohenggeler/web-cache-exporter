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

// The maximum number of locks in a memory arena. See: lock_arena().
const size_t MAX_NUM_ARENA_LOCKS = 30;

struct Arena
{
	size_t used_size;
	size_t total_size;
	void* available_memory;

	int num_locks;
	size_t locked_sizes[MAX_NUM_ARENA_LOCKS];
};

// A helper constant used to mark uninitialized or destroyed memory arenas.
const Arena NULL_ARENA = {};

bool create_arena(Arena* arena, size_t total_size);
void* aligned_push_arena(Arena* arena, size_t push_size, size_t alignment_size);
void* aligned_push_arena_64(Arena* arena, u64 push_size, size_t alignment_size);
void* aligned_push_and_copy_to_arena(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size);
#define push_arena(arena, push_size, Type) ((Type*) aligned_push_arena(arena, push_size, __alignof(Type)))
#define push_and_copy_to_arena(arena, push_size, Type, data, data_size) ((Type*) aligned_push_and_copy_to_arena(arena, push_size, __alignof(Type), data, data_size))
TCHAR* push_string_to_arena(Arena* arena, const TCHAR* string_to_copy);
f32 get_used_arena_capacity(Arena* arena);
u32 get_arena_file_buffer_size(Arena* arena, HANDLE file_handle);
u32 get_arena_chunk_buffer_size(Arena* arena, size_t min_size);
void lock_arena(Arena* arena);
void unlock_arena(Arena* arena);
void clear_arena(Arena* arena);
bool destroy_arena(Arena* arena);

// Used to simulate the behavior of malloc() or calloc() (properly aligned for any type) when using aligned_push_arena() or read_entire_file().
const size_t MAX_SCALAR_ALIGNMENT_SIZE = 16;

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> BASIC OPERATIONS
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

#define MIN(a, b) ( ((a) < (b)) ? (a) : (b) )
#define MAX(a, b) ( ((a) > (b)) ? (a) : (b) )
#define IS_POWER_OF_TWO(value) ( ((value) > 0) && (( (value) & ((value) - 1) ) == 0) )

// Aligns a value up given an alignment size. This value is offset so that it is the next multiple of the alignment size.
//
// @Parameters:
// 1. value - The value to align.
// 2. alignment_size - The alignment size in bytes. This value must be a power of two.
#define ALIGN_UP(value, alignment_size) ( ( (value) + ((alignment_size) - 1) ) & ~((alignment_size) - 1) )
#define ALIGN_OFFSET(value, alignment_size) (ALIGN_UP(value, alignment_size) - (value))

#define IS_POINTER_ALIGNED_TO_SIZE(pointer, alignment_size) ( ((uintptr_t) (pointer)) % (alignment_size) == 0 )
#define IS_POINTER_ALIGNED_TO_TYPE(pointer, Type) IS_POINTER_ALIGNED_TO_SIZE(pointer, __alignof(Type))

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

#ifdef BUILD_BIG_ENDIAN
	#define BIG_ENDIAN_TO_HOST(variable)
	#define LITTLE_ENDIAN_TO_HOST(variable) variable = swap_byte_order(variable)
#else
	#define BIG_ENDIAN_TO_HOST(variable) variable = swap_byte_order(variable)
	#define LITTLE_ENDIAN_TO_HOST(variable)
#endif

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
bool format_filetime_date_time(u64 value, TCHAR* formatted_string);
bool format_dos_date_time(Dos_Date_Time date_time, TCHAR* formatted_string);
bool format_dos_date_time(u32 value, TCHAR* formatted_string);
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

bool string_begins_with(const char* str, const char* prefix, bool optional_case_insensitive = false);
bool string_begins_with(const wchar_t* str, const wchar_t* prefix, bool optional_case_insensitive = false);

bool string_ends_with(const TCHAR* str, const TCHAR* suffix, bool optional_case_insensitive = false);

void string_to_uppercase(TCHAR* str);
void string_unescape(TCHAR* str);

char* skip_leading_whitespace(char* str);
wchar_t* skip_leading_whitespace(wchar_t* str);

// u16 = "0" to "65535", s16 = "-32768" to "32767", MAX = 6 characters.
// u32 = "0" to "4294967295", s32 = "-2147483648" to "2147483647", MAX = 11 characters.
// u64 = "0" to "18446744073709551615", s64 = "-9223372036854775808" to "9223372036854775807", MAX = 20 characters.
const size_t MAX_INT16_CHARS = 6 + 1;
const size_t MAX_INT32_CHARS = 11 + 1;
const size_t MAX_INT64_CHARS = 20 + 1; 
bool convert_u32_to_string(u32 value, TCHAR* result_string);
bool convert_s32_to_string(s32 value, TCHAR* result_string);
bool convert_u64_to_string(u64 value, TCHAR* result_string);
bool convert_s64_to_string(s64 value, TCHAR* result_string);
bool convert_string_to_u64(const char* str, u64* result_value);

bool convert_hexadecimal_string_to_byte(const TCHAR* byte_string, u8* result_byte);

// Any code page identifiers that don't have a readily available macro or constant. May be used by MultiByteToWideChar() and WideCharToMultiByte()
// in the Win32 API, and also convert_code_page_string_to_tchar().
enum Code_Page
{
	CODE_PAGE_UTF_16_LE = 1200,
	CODE_PAGE_UTF_16_BE = 1201,
	CODE_PAGE_UTF_32_LE = 12000,
	CODE_PAGE_UTF_32_BE = 12001,
};

TCHAR* convert_code_page_string_to_tchar(Arena* final_arena, Arena* intermediary_arena, u32 code_page, const char* string);
TCHAR* convert_code_page_string_to_tchar(Arena* arena, u32 code_page, const char* string);
TCHAR* convert_ansi_string_to_tchar(Arena* arena, const char* ansi_string);
TCHAR* convert_utf_16_string_to_tchar(Arena* arena, const wchar_t* utf_16_string);
TCHAR* convert_utf_8_string_to_tchar(Arena* final_arena, Arena* intermediary_arena, const char* utf_8_string);
TCHAR* convert_utf_8_string_to_tchar(Arena* arena, const char* utf_8_string);

char* skip_to_next_string(char* str);
wchar_t* skip_to_next_string(wchar_t* str);
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

	TCHAR* filename;
};

bool partition_url(Arena* arena, const TCHAR* original_url, Url_Parts* url_parts);
TCHAR* decode_url(Arena* arena, const TCHAR* url);
TCHAR* skip_url_scheme(TCHAR* url);
void correct_url_path_characters(TCHAR* path);
bool convert_url_to_path(Arena* arena, const TCHAR* url, TCHAR* result_path);

// Any relevant fields we want to retrieve from HTTP headers.
// See: parse_http_headers().
struct Http_Headers
{
	TCHAR* response;
	TCHAR* server;
	TCHAR* cache_control;
	TCHAR* pragma;
	TCHAR* content_type;
	TCHAR* content_length;
	TCHAR* content_range;
	TCHAR* content_encoding;
};

void parse_http_headers(Arena* arena, const char* original_headers, size_t headers_size, Http_Headers* result_headers);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> PATH MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

const size_t MAX_PATH_CHARS = MAX_PATH + 1;
const size_t MAX_PATH_SIZE = MAX_PATH_CHARS * sizeof(TCHAR);

bool filenames_are_equal(const TCHAR* filename_1, const TCHAR* filename_2);
bool filename_ends_with(const TCHAR* filename, const TCHAR* suffix);
TCHAR* skip_to_file_extension(TCHAR* path, bool optional_include_period = false, bool optional_get_first_extension = false);
TCHAR* skip_to_last_path_components(TCHAR* path, int desired_num_components);
int count_path_components(const TCHAR* path);
TCHAR* find_path_component(Arena* arena, const TCHAR* path, int component_index);
bool get_full_path_name(const TCHAR* path, TCHAR* result_full_path, u32 optional_num_result_path_chars = MAX_PATH_CHARS);
bool get_full_path_name(TCHAR* result_full_path);
bool get_special_folder_path(int csidl, TCHAR* result_path);
void truncate_path_components(TCHAR* path);
void correct_reserved_path_components(TCHAR* path);

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> FILE I/O
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
*/

HANDLE create_handle(const TCHAR* path, DWORD desired_access, DWORD shared_mode, DWORD creation_disposition, DWORD flags_and_attributes);

#ifndef BUILD_9X
	HANDLE create_directory_handle(const TCHAR* path, DWORD desired_access, DWORD shared_mode, DWORD creation_disposition, DWORD flags_and_attributes);
#else
	#define create_directory_handle(...) _STATIC_ASSERT(false)
#endif

bool do_handles_refer_to_the_same_file_or_directory(HANDLE file_handle_1, HANDLE file_handle_2);
bool do_paths_refer_to_the_same_directory(const TCHAR* path_1, const TCHAR* path_2);
bool does_file_exist(const TCHAR* file_path);
bool does_directory_exist(const TCHAR* directory_path);
bool get_file_size(HANDLE file_handle, u64* file_size_result);
bool get_file_size(const TCHAR* file_path, u64* result_file_size);
void safe_close_handle(HANDLE* handle);
void safe_find_close(HANDLE* search_handle);
void safe_unmap_view_of_file(void** base_address);

// Defines a combination of options for traverse_directory_objects().
enum Traversal_Flag
{
	TRAVERSE_FILES = 1 << 0,
	TRAVERSE_DIRECTORIES = 1 << 1,
};

// Information (paths, sizes, date times, etc) about each traversed object (directory or file).
// See: traverse_directory_objects() and find_objects_in_directory().
struct Traversal_Object_Info
{
	const TCHAR* directory_path;
	TCHAR* object_name;
	TCHAR* object_path;

	u64 object_size;
	bool is_directory; // True for directories, and false for files.

	FILETIME creation_time;
	FILETIME last_access_time;
	FILETIME last_write_time;

	void* user_data; // Only used for callbacks. Unused for the array elements in Traversal_Result.
};

const TCHAR* const ALL_OBJECTS_SEARCH_QUERY = T("*");

#define TRAVERSE_DIRECTORY_CALLBACK(function_name) bool function_name(Traversal_Object_Info* callback_info)
typedef TRAVERSE_DIRECTORY_CALLBACK(Traverse_Directory_Callback);
void traverse_directory_objects(const TCHAR* directory_path, const TCHAR* search_query,
								u32 traversal_flags, bool should_traverse_subdirectories,
								Traverse_Directory_Callback* callback_function, void* user_data);

// An array with information about each object.
// See: find_objects_in_directory().
struct Traversal_Result
{
	int num_objects;
	Traversal_Object_Info object_info[ANYSIZE_ARRAY];
};

Traversal_Result* find_objects_in_directory(Arena* arena, const TCHAR* directory_path, const TCHAR* search_query,
											u32 traversal_flags, bool should_traverse_subdirectories);

bool create_directories(const TCHAR* path_to_create, bool optional_resolve_naming_collisions = false, TCHAR* optional_result_path = NULL);
bool delete_directory_and_contents(const TCHAR* directory_path);

bool create_temporary_directory(const TCHAR* base_temporary_path, TCHAR* result_directory_path);

bool create_empty_file(const TCHAR* file_path, bool overwrite);

void* memory_map_entire_file(HANDLE file_handle, u64* file_size_result, bool optional_read_only = true);

void* read_entire_file(Arena* arena, const TCHAR* file_path, u64* result_file_size, bool optional_treat_as_text = false);

bool read_file_chunk(	HANDLE file_handle, void* file_buffer, u32 num_bytes_to_read, u64 file_offset,
						bool optional_allow_reading_fewer_bytes = false, u32* optional_result_num_bytes_read = NULL);

bool read_file_chunk(	const TCHAR* file_path, void* file_buffer, u32 num_bytes_to_read, u64 file_offset,
						bool optional_allow_reading_fewer_bytes = false, u32* optional_result_num_bytes_read = NULL);

bool read_first_file_bytes(	HANDLE file_handle, void* file_buffer, u32 num_bytes_to_read,
							bool optional_allow_reading_fewer_bytes = false, u32* optional_result_num_bytes_read = NULL);

bool read_first_file_bytes(	const TCHAR* file_path, void* file_buffer, u32 num_bytes_to_read,
							bool optional_allow_reading_fewer_bytes = false, u32* optional_result_num_bytes_read = NULL);

bool write_to_file(HANDLE file_handle, const void* data, u32 num_bytes_to_write, u32* optional_result_num_bytes_written = NULL);

bool copy_file_chunks(Arena* arena, const TCHAR* source_file_path, u64 num_bytes_to_copy, u64 file_offset, HANDLE destination_file_handle);
bool copy_file_chunks(Arena* arena, const TCHAR* source_file_path, u64 total_bytes_to_copy, u64 file_offset, const TCHAR* destination_file_path, bool overwrite);

bool empty_file(HANDLE file_handle);

TCHAR* generate_sha_256_from_file(Arena* arena, const TCHAR* file_path);

bool decompress_gzip_zlib_deflate_file(Arena* arena, const TCHAR* source_file_path, HANDLE destination_file_handle, int* result_error_code);
bool decompress_brotli_file(Arena* arena, const TCHAR* source_file_path, HANDLE destination_file_handle, int* result_error_code);

bool tchar_query_registry(HKEY hkey, const TCHAR* key_name, const TCHAR* value_name, TCHAR* value_data, u32 value_data_size);
#define query_registry(hkey, key_name, value_name, value_data, value_data_size) tchar_query_registry(hkey, T(key_name), T(value_name), value_data, value_data_size)

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

	NUM_FILE_INFO_TYPES = 12,
};

// An array that maps the previous values to ANSI strings.
const char* const FILE_INFO_TYPE_TO_STRING[] =
{
	"Comments", "InternalName", "ProductName",
	"CompanyName", "LegalCopyright", "ProductVersion",
	"FileDescription", "LegalTrademarks", "PrivateBuild",
	"FileVersion", "OriginalFilename", "SpecialBuild"
};
_STATIC_ASSERT(_countof(FILE_INFO_TYPE_TO_STRING) == NUM_FILE_INFO_TYPES);

bool get_file_info(Arena* arena, const TCHAR* full_file_path, File_Info_Type info_type, TCHAR** result_info);

extern bool GLOBAL_LOG_ENABLED;
extern bool GLOBAL_CONSOLE_ENABLED;

// The types of log lines in the global log file.
// See: tchar_log_print().
enum Log_Type
{
	LOG_NONE = 0,
	LOG_INFO = 1,
	LOG_WARNING = 2,
	LOG_ERROR = 3,
	LOG_DEBUG = 4,

	NUM_LOG_TYPES = 5,
};

// An array that maps the previous values to TCHAR strings. These strings will appear at the beginning of every log line.
const TCHAR* const LOG_TYPE_TO_STRING[] = {T(""), T("[INFO] "), T("[WARNING] "), T("[ERROR] "), T("[DEBUG] ")};
_STATIC_ASSERT(_countof(LOG_TYPE_TO_STRING) == NUM_LOG_TYPES);

bool create_log_file(const TCHAR* log_file_path);
void close_log_file(void);
void tchar_log_print(Log_Type log_type, const TCHAR* string_format, ...);
#define log_print(log_type, string_format, ...) if(GLOBAL_LOG_ENABLED) tchar_log_print(log_type, T(string_format), __VA_ARGS__)

#define log_newline() log_print(LOG_NONE, "")
#define log_info(string_format, ...) log_print(LOG_INFO, string_format, __VA_ARGS__)
#define log_warning(string_format, ...) log_print(LOG_WARNING, string_format, __VA_ARGS__)
#define log_error(string_format, ...) log_print(LOG_ERROR, string_format, __VA_ARGS__)

#ifdef BUILD_DEBUG
	#define log_debug(string_format, ...) log_print(LOG_DEBUG, string_format, __VA_ARGS__)
#else
	#define log_debug(...)
#endif

// Writes a formatted TCHAR string to the standard output stream. This string will be followed by two newline characters.
//
// @Parameters:
// 1. string_format - The format string. Note that %hs is used for narrow ANSI strings, %ls for wide UTF-16 strings, and %s for TCHAR
// strings (narrow ANSI or wide UTF-16 depending on the build target).
// 2. ... - Zero or more arguments to be inserted in the format string.
#define console_print(string_format, ...) if(GLOBAL_CONSOLE_ENABLED) _tprintf(T(string_format) T("\n\n"), __VA_ARGS__)

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
	CSV_REQUEST_ORIGIN,
	CSV_FILE_EXTENSION,
	CSV_FILE_SIZE,

	CSV_LAST_MODIFIED_TIME,
	CSV_CREATION_TIME,
	CSV_LAST_WRITE_TIME,
	CSV_LAST_ACCESS_TIME,
	CSV_EXPIRY_TIME,

	CSV_ACCESS_COUNT,

	CSV_RESPONSE,
	CSV_SERVER,
	CSV_CACHE_CONTROL,
	CSV_PRAGMA,
	CSV_CONTENT_TYPE,
	CSV_CONTENT_LENGTH,
	CSV_CONTENT_RANGE,
	CSV_CONTENT_ENCODING,

	CSV_CACHE_ORIGIN,
	CSV_CACHE_VERSION,

	// Always set automatically:
	CSV_DECOMPRESSED_FILE_SIZE,
	CSV_LOCATION_ON_CACHE,
	CSV_LOCATION_ON_DISK,
	CSV_MISSING_FILE,
	
	CSV_LOCATION_IN_OUTPUT,
	CSV_COPY_ERROR,
	CSV_EXPORTER_WARNING,
	
	CSV_CUSTOM_FILE_GROUP,
	CSV_CUSTOM_URL_GROUP,
	CSV_SHA_256,

	// For the Flash Player:
	CSV_LIBRARY_SHA_256,

	// For the Shockwave Player:
	CSV_DIRECTOR_FILE_TYPE,
	CSV_XTRA_DESCRIPTION,
	CSV_XTRA_VERSION,

	// For the Java Plugin:
	CSV_CODEBASE_IP,
	CSV_VERSION,

	NUM_CSV_TYPES,
};

// An array that maps the previous values to UTF-8 strings. Used to write the columns' names directly to the CSV file
// (which uses UTF-8 as the character encoding). This project's source files are stored in UTF-8.
const char* const CSV_TYPE_TO_UTF_8_STRING[] =
{
	"None",
	
	"Filename", "URL", "Request Origin", "File Extension", "File Size",
	"Last Modified Time", "Creation Time", "Last Write Time", "Last Access Time", "Expiry Time",
	"Access Count",
	"Response", "Server", "Cache Control", "Pragma", "Content Type", "Content Length", "Content Range", "Content Encoding",
	"Cache Origin", "Cache Version",
	
	"Decompressed File Size",
	"Location On Cache", "Location On Disk", "Missing File",
	"Location In Output", "Copy Error", "Exporter Warning",
	"Custom File Group", "Custom URL Group", "SHA-256",
	
	"Library SHA-256",
	"Director File Type", "Xtra Description", "Xtra Version",
	"Codebase IP", "Version"
};
_STATIC_ASSERT(_countof(CSV_TYPE_TO_UTF_8_STRING) == NUM_CSV_TYPES);

// A helper structure used to write values to the CSV file. The 'value' member is the ANSI or UTF-16 string to write, and
// 'utf_16_value' an intermediary variable that is used to convert the string to UTF-8.
// See: csv_print_row().
struct Csv_Entry
{
	TCHAR* value;
	wchar_t* utf_16_value;
};

bool create_csv_file(const TCHAR* csv_file_path, HANDLE* result_file_handle);
void csv_print_header(Arena* arena, HANDLE csv_file_handle, Csv_Type* column_types, int num_columns);
void csv_print_row(Arena* arena, HANDLE csv_file_handle, Csv_Entry* column_values, int num_columns);

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
		log_error("Get Function Address: Failed to retrieve the function address for '%hs' with the error code %lu.", function_name, GetLastError());\
	}\
} while(false, false)

// Define custom error codes that are passed to SetLastError(). This is only done for functions that mix Windows errors (e.g. copying a file) with their
// own error conditions (e.g. failed to resolve a naming collision). See: functions with the @GetLastError annotation.
// @Docs: "Bit 29 is reserved for application-defined error codes; no system error code has this bit set." - SetLastError - Win32 API Reference.
#define CUSTOM_WIN32_ERROR_CODE(n) ( (1 << 28) | (n) )
enum Custom_Error_Code
{
	// For copy_file_using_url_directory_structure().
	CUSTOM_ERROR_EMPTY_OR_NULL_SOURCE_PATH 					= CUSTOM_WIN32_ERROR_CODE(1),
	CUSTOM_ERROR_FAILED_TO_BUILD_VALID_DESTINATION_PATH 	= CUSTOM_WIN32_ERROR_CODE(2),
	CUSTOM_ERROR_TOO_MANY_NAMING_COLLISIONS 				= CUSTOM_WIN32_ERROR_CODE(3),
	CUSTOM_ERROR_UNRESOLVED_NAMING_COLLISION 				= CUSTOM_WIN32_ERROR_CODE(4),
	// For copy_exporter_file() and copy_file_chunks(6).
	CUSTOM_ERROR_FAILED_TO_COPY_FILE_CHUNKS 				= CUSTOM_WIN32_ERROR_CODE(5),
	CUSTOM_ERROR_FAILED_TO_GET_FILE_SIZE 					= CUSTOM_WIN32_ERROR_CODE(6),
};

#ifndef BUILD_9X
	void load_ntdll_functions(void);
	void free_ntdll_functions(void);

	void load_kernel32_functions(void);
	void free_kernel32_functions(void);
#else
	#define load_ntdll_functions(...) _STATIC_ASSERT(false)
	#define free_ntdll_functions(...) _STATIC_ASSERT(false)
	
	#define load_kernel32_functions(...) _STATIC_ASSERT(false)
	#define free_kernel32_functions(...) _STATIC_ASSERT(false)
#endif

bool copy_open_file(Arena* arena, const TCHAR* copy_source_path, const TCHAR* copy_destination_path);

// For measuring times in the debug builds. Only DEBUG_BEGIN_MEASURE_TIME() and DEBUG_END_MEASURE_TIME() should be used directly.
#ifdef BUILD_DEBUG
	void debug_measure_time(bool is_start, const char* identifier);
	#define DEBUG_BEGIN_MEASURE_TIME(identifier) debug_measure_time(true, identifier)
	#define DEBUG_END_MEASURE_TIME() debug_measure_time(false, NULL)
#else
	#define debug_measure_time(...) _STATIC_ASSERT(false)
	#define DEBUG_BEGIN_MEASURE_TIME(...)
	#define DEBUG_END_MEASURE_TIME(...)
#endif

// Any structs or functions that use templates:

// Represents an array of ANSI or UTF-16 strings.
template<typename Char_Type>
struct String_Array
{
	int num_strings;
	Char_Type* strings[ANYSIZE_ARRAY];
};

// Splits a string into an array of tokens using one or more separators. This function will only consider non-empty tokens.
//
// For example, the string ",abc,def,,,ghi," with the separator "," is split into an array of three elements: "abc", "def", "ghi".
//
// @Parameters:
// 1. arena - The Arena structure that will receive the array of tokens.
// 2. str - The string to split.
// 3. delimiters - A string containing one or more characters to be used as separators.
// 4. optional_max_splits - An optional parameter that specifies the maximum number of splits to perform. This value defaults to -1, which specifies
// an unlimited number of splits. A value of zero will result in an array with one element (the entire string) or no elements (if the string is empty).
//
// @Returns: The array of split tokens. If the string is empty, this array will have zero elements.
template<typename Char_Type>
String_Array<Char_Type>* split_string(Arena* arena, Char_Type* str, const Char_Type* delimiters, int optional_max_splits = -1)
{
	_ASSERT(string_length(delimiters) >= 1);
	_ASSERT(optional_max_splits >= -1);

	String_Array<Char_Type>* result = push_arena(arena, sizeof(String_Array<Char_Type>), String_Array<Char_Type>);
	result->num_strings = 0;

	// Helper macro function that adds any non-empty tokens to the array.
	#define ADD_STRING_TO_RESULT(value)\
	do\
	{\
		if(!string_is_empty(value))\
		{\
			if(result->num_strings > 0) push_arena(arena, sizeof(Char_Type*), u8);\
			result->strings[result->num_strings] = value;\
			++(result->num_strings);\
		}\
	} while(false, false)

	if(optional_max_splits == 0)
	{
		ADD_STRING_TO_RESULT(str);
		return result;
	}

	size_t num_string_chars = string_length(str);
	size_t num_delimiter_chars = string_length(delimiters);

	Char_Type* last_value_begin = str;
	bool found_delimiter = false;

	for(size_t i = 0; i < num_string_chars; ++i)
	{
		for(size_t j = 0; j < num_delimiter_chars; ++j)
		{
			if(str[i] == delimiters[j])
			{
				str[i] = '\0';
				found_delimiter = true;

				// Skip consecutive delimiters.
				goto continue_outer_loop;
			}
		}

		if(found_delimiter)
		{
			ADD_STRING_TO_RESULT(last_value_begin);
			found_delimiter = false;
			last_value_begin = str + i;

			if(optional_max_splits != -1 && result->num_strings >= optional_max_splits)
			{
				break;
			}
		}

		continue_outer_loop:;
	}

	// Add the remainder of the string (which might include delimiters) as the last value.
	ADD_STRING_TO_RESULT(last_value_begin);
	last_value_begin = NULL;

	#undef ADD_STRING_TO_RESULT

	_ASSERT(optional_max_splits == -1 || result->num_strings <= optional_max_splits + 1);

	return result;
}

// Behaves like split_string() but takes a constant string instead.
//
// @Parameters: All parameters are the same except 'str' is constant. A copy will be made in the Arena structure.
// 
// @Returns: See split_string().
template<typename Char_Type>
String_Array<Char_Type>* split_string(Arena* arena, const Char_Type* str, const Char_Type* delimiters, int optional_max_splits = -1)
{
	TCHAR* str_copy = push_string_to_arena(arena, str);
	return split_string(arena, str_copy, delimiters, optional_max_splits);
}

#endif
