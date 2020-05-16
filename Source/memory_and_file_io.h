#ifndef MEMORY_AND_FILE_IO_H
#define MEMORY_AND_FILE_IO_H

struct Arena
{
	size_t used_size;
	size_t total_size;
	void* available_memory;
};

bool create_arena(Arena* arena, size_t total_size);
void* aligned_push_arena(Arena* arena, size_t push_size, size_t alignment_size);
void* aligned_push_and_copy_to_arena(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size);
#define push_arena(arena, push_size, type) ((type*) aligned_push_arena(arena, push_size, __alignof(type)))
#define push_and_copy_to_arena(arena, push_size, type, data, data_size) ((type*) aligned_push_and_copy_to_arena(arena, push_size, __alignof(type), data, data_size))
void clear_arena(Arena* arena);
bool destroy_arena(Arena* arena);

char* skip_leading_whitespace(char* str);
char* skip_to_file_extension(char* str);

bool decode_url(const char* url, char* decoded_url);
bool copy_file_using_url_directory_structure(Arena* arena, const char* full_file_path, const char* base_export_path, const char* url, const char* filename);

const size_t MAX_UINT32_CHARS = 33;
const size_t MAX_UINT64_CHARS = 65;
const size_t INT_FORMAT_RADIX = 10;
const size_t MAX_PATH_CHARS = MAX_PATH + 1;
const size_t MAX_PATH_SIZE = MAX_PATH_CHARS * sizeof(char); // @TODO: wide version

void* memory_map_entire_file(char* path);
bool query_registry(HKEY base_key, const char* key_name, const char* value_name, char* value_data);

bool create_log_file(const char* filename);
void close_log_file(void);
void log_print(const char* string_format, ...);
#ifdef DEBUG
	#define debug_log_print(string_format, ...) log_print(string_format, __VA_ARGS__)
#else
	#define debug_log_print(...)
#endif

HANDLE create_csv_file(const char* file_path);
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

inline size_t string_size(const char* str)
{
	return (strlen(str) + 1) * sizeof(char);
}

#endif
