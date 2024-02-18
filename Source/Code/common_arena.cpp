#include "common.h"

#define ALIGN_UP(num, alignment) ( ((num) + (alignment) - 1) & ~((alignment) - 1) )
#define ALIGN_OFFSET(num, alignment) ( ALIGN_UP((num), (alignment)) - (num) )

bool arena_create(Arena* arena, size_t initial_size)
{
	#ifdef WCE_32_BIT
		size_t total_size = from_megabytes(500);
	#else
		size_t total_size = from_gigabytes(500);
	#endif

	initial_size = MIN(initial_size, total_size);

	do
	{
		arena->base_memory = VirtualAlloc(NULL, total_size, MEM_RESERVE, PAGE_NOACCESS);

		if(arena->base_memory == NULL && GetLastError() == ERROR_NOT_ENOUGH_MEMORY)
		{
			log_warning("Could not reserve %Iu bytes", total_size);
			total_size /= 2;
		}
		else
		{
			break;
		}

	} while(initial_size <= total_size);

	bool success = (arena->base_memory != NULL);

	if(success)
	{
		void* initial_memory = VirtualAlloc(arena->base_memory, initial_size, MEM_COMMIT, PAGE_READWRITE);
		success = (initial_memory != NULL);

		if(success)
		{
			#ifdef WCE_DEBUG
				FillMemory(initial_memory, initial_size, 0xDD);
			#endif
		}
		else
		{
			log_error("Failed to commit %Iu bytes with the error: %s", initial_size, last_error_message());
			VirtualFree(arena->base_memory, 0, MEM_RELEASE);
			arena->base_memory = NULL;
		}
	}
	else
	{
		log_error("Failed to reserve %Iu bytes with the error: %s", total_size, last_error_message());
	}

	arena->last_memory = NULL;

	arena->commited_pages = (success) ? ((int) CEIL_DIV(initial_size, context.page_size)) : (0);
	arena->total_pages = (success) ? ((int) CEIL_DIV(total_size, context.page_size)) : (0);

	arena->used_size = 0;
	arena->saved_size = 0;

	return success;
}

void arena_destroy(Arena* arena)
{
	if(arena->base_memory == NULL)
	{
		log_error("The arena was not created");
		return;
	}

	VirtualFree(arena->base_memory, 0, MEM_RELEASE);
	ZeroMemory(arena, sizeof(Arena));
}

void* arena_aligned_push(Arena* arena, size_t push_size, size_t alignment_size)
{
	ASSERT(arena->base_memory != NULL, "The arena was not created");
	ASSERT(is_power_of_two(alignment_size), "The alignment size is not a power of two");

	// We can't allow zero size allocations because of an optimization on how
	// certain data structures (arrays, maps, string builders) expand their size.
	if(push_size == 0)
	{
		log_debug("Zero size allocation");
		push_size = 1;
	}

	void* next_memory = advance(arena->base_memory, arena->used_size);
	size_t alignment_offset = ALIGN_OFFSET((uintptr_t) next_memory, alignment_size);
	size_t required_size = alignment_offset + push_size;
	size_t available_size = arena->commited_pages * context.page_size;

	ASSERT(required_size > 0, "Required size is zero");

	if(arena->used_size + required_size > available_size)
	{
		int required_pages = (int) CEIL_DIV(required_size, context.page_size);
		int remaining_pages = arena->total_pages - arena->commited_pages;

		if(required_pages > remaining_pages)
		{
			log_error("Ran out of pages to commit (required = %d, remaining = %d)", required_pages, remaining_pages);
			return NULL;
		}

		// Always commit a reasonable number of pages unless there are very few remaining.
		const int MIN_PAGES = 10;
		if(MIN_PAGES <= remaining_pages)
		{
			required_pages = MAX(required_pages, MIN_PAGES);
		}

		void* next_page = advance(arena->base_memory, available_size);
		size_t grow_size = required_pages * context.page_size;

		if(VirtualAlloc(next_page, grow_size, MEM_COMMIT, PAGE_READWRITE) == NULL)
		{
			log_error("Failed to commit %Iu bytes with the error: %s", grow_size, last_error_message());
			return NULL;
		}

		arena->commited_pages += required_pages;

		#ifdef WCE_DEBUG
			FillMemory(next_page, grow_size, 0xDD);
		#endif
	}

	void* result = advance(next_memory, alignment_offset);
	arena->last_memory = result;
	arena->used_size += required_size;

	#ifdef WCE_DEBUG
		FillMemory(next_memory, required_size, 0xAA);
	#endif

	ASSERT(POINTER_IS_ALIGNED_TO_SIZE(result, alignment_size), "Misaligned result");

	return result;
}

void* arena_aligned_push_and_copy(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size)
{
	ASSERT(push_size >= data_size, "Data size is greater than push size");
	void* result = arena_aligned_push(arena, push_size, alignment_size);
	CopyMemory(result, data, data_size);
	return result;
}

void* arena_push_any(Arena* arena, size_t size)
{
	const size_t MAX_SCALAR_ALIGNMENT = 16;
	return arena_aligned_push(arena, size, MAX_SCALAR_ALIGNMENT);
}

void arena_extend(Arena* arena, size_t size)
{
	void* last_memory = arena->last_memory;
	arena_aligned_push(arena, size, 1);
	arena->last_memory = last_memory;
}

size_t arena_save(Arena* arena)
{
	ASSERT(arena->base_memory != NULL, "The arena was not created");
	size_t saved_size = arena->saved_size;
	arena->saved_size = arena->used_size;
	return saved_size;
}

void arena_restore(Arena* arena, size_t saved_size)
{
	ASSERT(arena->base_memory != NULL, "The arena was not created");
	arena->saved_size = saved_size;
}

void arena_clear(Arena* arena)
{
	ASSERT(arena->base_memory != NULL, "The arena was not created");
	ASSERT(arena->used_size >= arena->saved_size, "The saved size is greater than the used size");

	#ifdef WCE_DEBUG
		void* cleared_memory = advance(arena->base_memory, arena->saved_size);
		size_t cleared_size = arena->used_size - arena->saved_size;
		FillMemory(cleared_memory, cleared_size, 0xCC);
	#endif

	arena->used_size = arena->saved_size;
}

size_t arena_file_buffer_size(Arena* arena, HANDLE handle)
{
	size_t result = 0;

	u64 file_size = 0;
	if(file_size_get(handle, &file_size))
	{
		result = size_clamp(file_size);
	}
	else
	{
		result = from_megabytes(50);
	}

	int remaining_pages = arena->total_pages - arena->commited_pages;
	size_t max_size = remaining_pages * context.page_size / 4;
	result = MAX(result, 1);
	result = MIN(result, max_size);

	if(context.tiny_file_buffers) result = MIN(result, 101);

	return result;
}

void arena_tests(void)
{
	console_info("Running arena tests");
	log_info("Running arena tests");

	{
		TEST(ALIGN_UP(0, 4), 0);
		TEST(ALIGN_UP(1, 4), 4);
		TEST(ALIGN_UP(4, 4), 4);
		TEST(ALIGN_UP(6, 4), 8);
		TEST(ALIGN_UP(8, 4), 8);
		TEST(ALIGN_UP(11, 4), 12);

		TEST(ALIGN_OFFSET(0, 4), 0);
		TEST(ALIGN_OFFSET(1, 4), 3);
		TEST(ALIGN_OFFSET(4, 4), 0);
		TEST(ALIGN_OFFSET(6, 4), 2);
		TEST(ALIGN_OFFSET(8, 4), 0);
		TEST(ALIGN_OFFSET(11, 4), 1);
	}

	{
		Arena* arena = context.current_arena;
		TEST(arena->used_size, (size_t) 0);

		char* ptr_1 = arena_push(arena, sizeof(char), char);
		TEST(arena->used_size, sizeof(char));

		#ifdef WCE_DEBUG
			TEST(*ptr_1, 0xAA);
		#endif

		*ptr_1 = 0x11;

		int count_2 = 8;
		size_t size_2 = count_2 * sizeof(wchar_t);
		wchar_t* ptr_2 = arena_push_buffer(arena, count_2, wchar_t);
		StringCchCopyW(ptr_2, count_2, L"Testing");

		TEST(memory_is_equal(ptr_2, L"Testing", size_2), true);
		TEST(arena->used_size, (sizeof(char) + 1 + size_2));

		u64* ptr_3 = (u64*) arena_push_any(arena, sizeof(u64));
		TEST(POINTER_IS_ALIGNED_TO_TYPE(ptr_3, u64), true);

		ARENA_SAVEPOINT()
		{
			int count_4_5 = 10;
			size_t size_4_5 = count_4_5 * sizeof(char);

			char* ptr_4 = arena_push_buffer(arena, count_4_5, char);
			FillMemory(ptr_4, size_4_5, 0x44);

			ARENA_SAVEPOINT()
			{
				char* ptr_5 = arena_push_buffer(arena, count_4_5, char);
				FillMemory(ptr_5, size_4_5, 0x55);

				ARENA_SAVEPOINT()
				{
					char* ptr_6 = arena_push(arena, sizeof(char), char);
					*ptr_6 = 0x66;
				}
				TEST(*ptr_5, 0x55);
			}
			TEST(*ptr_4, 0x44);
		}
		TEST(*ptr_1, 0x11);

		void* last_memory = arena->last_memory;
		size_t previous_size = arena->used_size;
		arena_extend(arena, 100);
		TEST(arena->last_memory, last_memory);
		TEST(arena->used_size, previous_size + 100);

		arena_clear(arena);
		TEST(arena->used_size, (size_t) 0);
		TEST(arena->saved_size, (size_t) 0);

		#ifdef WCE_DEBUG
			TEST(*ptr_1, 0xCC);
		#endif

		int commited_pages = arena->commited_pages;

		size_t max_size = arena->commited_pages * context.page_size;
		arena_push(arena, max_size, char);
		TEST(arena->used_size, max_size);
		TEST(arena->commited_pages, commited_pages);

		arena_push(arena, 1, char);
		TEST(arena->used_size, max_size + 1);
		TEST_NOT(arena->commited_pages, commited_pages);
	}
}