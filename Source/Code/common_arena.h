#ifndef COMMON_ARENA_H
#define COMMON_ARENA_H

#include "common_core.h"

struct Arena
{
	void* base_memory;
	void* last_memory;

	int commited_pages;
	int total_pages;

	size_t used_size;
	size_t saved_size;
};

bool arena_create(Arena* arena, size_t initial_size);
void arena_destroy(Arena* arena);

void* arena_aligned_push(Arena* arena, size_t push_size, size_t alignment_size);
void* arena_aligned_push_and_copy(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size);
#define arena_push(arena, push_size, Type) (Type*) arena_aligned_push(arena, (push_size), __alignof(Type))
#define arena_push_and_copy(arena, push_size, Type, data, data_size) (Type*) arena_aligned_push_and_copy(arena, push_size, __alignof(Type), data, data_size)
#define arena_push_buffer(arena, count, Type) arena_push(arena, (count) * sizeof(Type), Type);
void* arena_push_any(Arena* arena, size_t size);
void arena_extend(Arena* arena, size_t size);

size_t arena_save(Arena* arena);
void arena_restore(Arena* arena, size_t saved_size);
void arena_clear(Arena* arena);

size_t arena_file_buffer_size(Arena* arena, HANDLE handle);

void arena_tests(void);

#endif