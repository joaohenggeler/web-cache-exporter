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
bool format_filetime_date_time(FILETIME date_time, char* formatted_string);
bool format_dos_date_time(Dos_Date_Time date_time, char* formatted_string);

char* skip_leading_whitespace(char* str);
char* skip_to_file_extension(char* str);

bool decode_url(const char* url, char* decoded_url);
void create_directories(const char* path_to_create);
bool copy_file_using_url_directory_structure(Arena* arena, const char* full_file_path, const char* base_export_path, const char* url, const char* filename);

const size_t MAX_UINT32_CHARS = 33;
const size_t MAX_UINT64_CHARS = 65;
const size_t INT_FORMAT_RADIX = 10;
const size_t MAX_PATH_CHARS = MAX_PATH + 1;
//const size_t MAX_PATH_SIZE = MAX_PATH_CHARS * sizeof(char); // @TODO: wide version?

u64 combine_high_and_low_u32s(u32 high, u32 low);
bool get_file_size(HANDLE file_handle, u64* file_size_result);
void* memory_map_entire_file(char* path);
bool read_first_file_bytes(char* path, void* file_buffer, DWORD num_bytes_to_read);
bool query_registry(HKEY hkey, const char* key_name, const char* value_name, char* value_data, DWORD value_data_size);

enum Log_Type
{
	LOG_NONE = 0,
	LOG_INFO = 1,
	LOG_WARNING = 2,
	LOG_ERROR = 3,
	LOG_DEBUG = 4,
	NUM_LOG_TYPES = 5
};
const char* const LOG_TYPE_TO_STRING[NUM_LOG_TYPES] = {"", "[INFO] ", "[WARNING] ", "[ERROR] ", "[DEBUG] "};

bool create_log_file(const char* filename);
void close_log_file(void);
void log_print(Log_Type log_type, const char* string_format, ...);
void log_print(Log_Type log_type, const wchar_t* string_format, ...);
#ifdef DEBUG
	#define debug_log_print(string_format, ...) log_print(LOG_DEBUG, string_format, __VA_ARGS__)
#else
	#define debug_log_print(...)
#endif

#define console_print(string_format, ...) printf(string_format, __VA_ARGS__)

HANDLE create_csv_file(const char* file_path);
void close_csv_file(HANDLE csv_file);
void csv_print_header(HANDLE csv_file, const char* header);
void csv_print_row(Arena* arena, HANDLE csv_file, char* rows[], size_t num_columns);

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

HANDLE windows_nt_query_file_handle_from_file_path(Arena* arena, char* file_path);
#ifdef BUILD_9X
	#define windows_nt_query_file_handle_from_file_path(...)
#endif

#endif
