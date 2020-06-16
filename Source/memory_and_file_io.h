#ifndef MEMORY_AND_FILE_IO_H
#define MEMORY_AND_FILE_IO_H

struct Arena
{
	size_t used_size;
	size_t total_size;
	void* available_memory;
};
const Arena NULL_ARENA = {0, 0, NULL};

bool create_arena(Arena* arena, size_t total_size);
void* aligned_push_arena(Arena* arena, size_t push_size, size_t alignment_size);
void* aligned_push_and_copy_to_arena(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size);
#define push_arena(arena, push_size, type) ((type*) aligned_push_arena(arena, push_size, __alignof(type)))
#define push_and_copy_to_arena(arena, push_size, type, data, data_size) ((type*) aligned_push_and_copy_to_arena(arena, push_size, __alignof(type), data, data_size))
void clear_arena(Arena* arena);
bool destroy_arena(Arena* arena);

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

const size_t MAX_UINT32_CHARS = 33;
const size_t MAX_UINT64_CHARS = 65;
bool convert_u32_to_string(u32 value, TCHAR* result_string);
bool convert_u64_to_string(u64 value, TCHAR* result_string);

TCHAR* copy_ansi_string_to_tchar(Arena* arena, const char* ansi_string);
TCHAR* copy_ansi_string_to_tchar(Arena* arena, const char* ansi_string, size_t num_chars);

char* skip_leading_whitespace(char* str);
wchar_t* skip_leading_whitespace(wchar_t* str);
TCHAR* skip_to_file_extension(TCHAR* str);

bool decode_url(const TCHAR* url, TCHAR* decoded_url);
void create_directories(const TCHAR* path_to_create);
bool copy_file_using_url_directory_structure(Arena* arena, const TCHAR* full_file_path, const TCHAR* base_destination_path, const TCHAR* url, const TCHAR* filename);

const size_t MAX_PATH_CHARS = MAX_PATH;
//const size_t MAX_PATH_CHARS_WITH_NULL = MAX_PATH_CHARS + 1;

u64 combine_high_and_low_u32s(u32 high, u32 low);
bool get_file_size(HANDLE file_handle, u64* file_size_result);
void* memory_map_entire_file(TCHAR* file_path, u64* file_size_result);
bool copy_to_temporary_file(TCHAR* full_source_file_path, TCHAR* full_temporary_file_path_result);
bool read_first_file_bytes(TCHAR* path, void* file_buffer, DWORD num_bytes_to_read);
bool tchar_query_registry(HKEY hkey, const TCHAR* key_name, const TCHAR* value_name, TCHAR* value_data, DWORD value_data_size);
#define query_registry(hkey, key_name, value_name, value_data, value_data_size) tchar_query_registry(hkey, TEXT(key_name), TEXT(value_name), value_data, value_data_size)

enum Log_Type
{
	LOG_NONE = 0,
	LOG_INFO = 1,
	LOG_WARNING = 2,
	LOG_ERROR = 3,
	LOG_DEBUG = 4,
	NUM_LOG_TYPES = 5
};
const TCHAR* const LOG_TYPE_TO_STRING[NUM_LOG_TYPES] = {TEXT(""), TEXT("[INFO] "), TEXT("[WARNING] "), TEXT("[ERROR] "), TEXT("[DEBUG] ")};

bool create_log_file(const TCHAR* filename);
void close_log_file(void);
void tchar_log_print(Log_Type log_type, const TCHAR* string_format, ...);
#define log_print(log_type, string_format, ...) tchar_log_print(log_type, TEXT(string_format), __VA_ARGS__)
#ifdef DEBUG
	#define debug_log_print(string_format, ...) log_print(LOG_DEBUG, string_format, __VA_ARGS__)
#else
	#define debug_log_print(...)
#endif

#define console_print(string_format, ...) _tprintf(TEXT(string_format), __VA_ARGS__)

enum Csv_Type
{
	CSV_NONE = 0,

	CSV_FILENAME = 1,
	CSV_URL = 2,
	CSV_FILE_EXTENSION = 3,
	CSV_FILE_SIZE = 4,

	CSV_LAST_WRITE_TIME = 5,
	CSV_LAST_MODIFIED_TIME = 6,
	CSV_LAST_ACCESS_TIME = 7,
	CSV_CREATION_TIME = 8,
	CSV_EXPIRY_TIME = 9,

	CSV_SERVER_RESPONSE = 10,
	CSV_CONTENT_TYPE = 11,
	CSV_CONTENT_LENGTH = 12,
	CSV_CONTENT_ENCODING = 13,

	CSV_LOCATION_ON_CACHE = 14,
	CSV_MISSING_FILE = 15,
	
	// Internet Explorer specific.
	CSV_HITS = 16,
	CSV_LEAK_ENTRY = 17,
	
	// Shockwave Plugin specific.
	CSV_DIRECTOR_FILE_TYPE = 18,

	NUM_CSV_TYPES = 19
};
const char* const CSV_TYPE_TO_ASCII_STRING[NUM_CSV_TYPES] =
{
	"None",
	"Filename", "URL", "File Extension", "File Size",
	"Last Write Time", "Last Modified Time", "Last Access Time", "Creation Time", "Expiry Time",
	"Server Response", "Content-Type", "Content-Length", "Content-Encoding",
	"Location On Cache", "Missing File",
	"Hits", "Leak Entry",
	"Director File Type"
};

struct Csv_Entry
{
	TCHAR* value;
	wchar_t* utf_16_value; // Helper variable to convert each value to UTF-8 in Windows 98 and ME.
};

HANDLE create_csv_file(const TCHAR* file_path);
void close_csv_file(HANDLE csv_file);
void csv_print_header(Arena* arena, HANDLE csv_file, const Csv_Type row_types[], size_t num_columns);
void csv_print_row(Arena* arena, HANDLE csv_file, const Csv_Type row_types[], Csv_Entry row[], size_t num_columns);

inline void* advance_bytes(void* pointer, size_t num_bytes)
{
	return ((char*) pointer) + num_bytes;
}

inline void* retreat_bytes(void* pointer, size_t num_bytes)
{
	return ((char*) pointer) - num_bytes;
}

inline ptrdiff_t pointer_difference(void* a, void* b)
{
	return ((char*) a) - ((char*) b);
}

inline size_t kilobytes_to_bytes(size_t kilobytes)
{
	return kilobytes * 1024;
}

inline size_t megabytes_to_bytes(size_t megabytes)
{
	return megabytes * 1024 * 1024;
}

inline size_t string_size(const char* str)
{
	return (strlen(str) + 1) * sizeof(char);
}

inline size_t string_size(const wchar_t* str)
{
	return (wcslen(str) + 1) * sizeof(wchar_t);
}

inline bool is_string_empty(const char* str)
{
	return str[0] == '\0';
}

inline bool is_string_empty(const wchar_t* str)
{
	return str[0] == L'\0';
}

/*
_byteswap_ushort
_byteswap_ulong
_byteswap_uint64
*/

#ifndef BUILD_9X
	HANDLE windows_nt_query_file_handle_from_file_path(Arena* arena, char* file_path);
#else
	#define windows_nt_query_file_handle_from_file_path(...) _STATIC_ASSERT(false)
#endif

#endif
