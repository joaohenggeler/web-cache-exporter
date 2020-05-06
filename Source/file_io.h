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
void* aligned_push_and_copy_to_arena(Arena* arena, const void* data, size_t data_size, size_t alignment_size);
#define push_arena(arena, push_size, type) ((type*) aligned_push_arena(arena, push_size, __alignof(type)))
#define push_and_copy_to_arena(arena, data, data_size, type) ((type*) aligned_push_and_copy_to_arena(arena, data, data_size, __alignof(type)))
void clear_arena(Arena* arena);
bool destroy_arena(Arena* arena);

size_t string_size(const char* str);
char* skip_leading_whitespace(char* str);
char* skip_to_file_extension(char* str);

bool decode_url(const char* url, char* decoded_url);

const size_t MAX_FORMATTED_DATE_TIME_CHARS = 32;
bool format_date_time(FILETIME* date_time, char* formatted_string);

void* memory_map_entire_file(char* path);
void* advance_bytes(void* pointer, size_t num_bytes);
void* retreat_bytes(void* pointer, size_t num_bytes);
void copy_byte_range(void* source, void* destination, /*size_t destination_size,*/ size_t offset, size_t num_bytes_to_copy);
bool query_registry(HKEY base_key, const char* key_name, const char* value_name, char* value_data);
void log_print(const char* string_format, ...);

#ifdef DEBUG
	#define debug_log_print(string_format, ...) log_print(string_format, __VA_ARGS__)
#else
	#define debug_log_print(...)
#endif

#endif
