#include "web_cache_exporter.h"
#include "memory_and_file_io.h"

// @Dependencies: Headers for third-party libraries:

// "Portable C++ Hashing Library" by Stephan Brumme.
#pragma warning(push)
#pragma warning(disable : 4244 4530 4995)
	// It might seem a little weird to import the source file itself, but it works for
	// this simple case and it allows us to supress any warnings without having to
	// modify it.
	#include "SHA-256/sha256.cpp"
#pragma warning(pop)

// "Zlib" by Jean-loup Gailly and Mark Adler.
#include "Zlib/zlib.h"

// "Brotli" by Google.
#include "brotli/decode.h"

/*
	This file defines functions for memory management, file I/O (including creating and writing to the log and CSV files), date time
	formatting, string, path, and URL manipulation, and basic numeric operations.

	@Resources: Geoff Chappell's software analysis website was used to check the minimum supported Windows version for some functions:

	- KERNEL32: https://www.geoffchappell.com/studies/windows/win32/kernel32/api/index.htm
	- ADVAPI32: https://www.geoffchappell.com/studies/windows/win32/advapi32/api/index.htm
	- SHELL32: https://www.geoffchappell.com/studies/windows/shell/shell32/api/index.htm
	- SHLWAPI: https://www.geoffchappell.com/studies/windows/shell/shlwapi/api/index.htm

	---------------------------------------------------------------------------

	TABLE OF CONTENTS:

	1. MEMORY ALLOCATION
	2. BASIC OPERATIONS
	3. DATE AND TIME FORMATTING
	4. STRING MANIPULATION
	5. URL MANIPULATION
	6. PATH MANIPULATION
	7. FILE I/O

	Find @NextSection to navigate through each section.
*/

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> MEMORY ALLOCATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// Base addresses that are used by the create_arena() and memory_map_entire_file() functions
// in the debug builds. These are incremented by a set amount if these functions are called
// multiple times.
#ifdef BUILD_DEBUG
	#ifdef BUILD_9X
		static void* DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = NULL;
		static void* DEBUG_MEMORY_MAPPING_BASE_ADDRESS = NULL;
		static const size_t DEBUG_BASE_ADDRESS_INCREMENT = 0;
	#else
		static void* DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = 	(void*) 0x10000000;
		static void* DEBUG_MEMORY_MAPPING_BASE_ADDRESS = 	(void*) 0x50000000;
		static const size_t DEBUG_BASE_ADDRESS_INCREMENT = 			0x02000000;
	#endif

	// In the debug builds, we'll set the arena's memory to specific values so it's easier to keep track of allocations
	// in the debugger's Memory Window.
	// These are set to 0xDE in create_arena() and clear_arena(), and to zero in the push_arena() functions.
	static const u8 DEBUG_ARENA_DEALLOCATED_VALUE = 0xDE;
#endif

// Creates an arena and allocates enough memory to read/write a given number of bytes.
//
// Memory allocated by this function is automatically initialized to zero in the release builds, and to DEBUG_ARENA_DEALLOCATED_VALUE
// in the debug builds.
//
// @Parameters:
// 1. arena - The Arena structure that receives the address of the allocated memory and its total size.
// 2. total_size - How many bytes to allocate.
//
// @Returns: True if the arena was created successfully. In this case, the member available_memory is set to the allocated
// memory's address, total_size is set to whatever value was passed, and used_size is set to zero.
// Otherwise, it returns false, available_memory is set to NULL, and both size fields are set to zero.
bool create_arena(Arena* arena, size_t total_size)
{
	#ifdef BUILD_DEBUG
		arena->available_memory = VirtualAlloc(DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = advance_bytes(DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS, DEBUG_BASE_ADDRESS_INCREMENT);
	#else
		arena->available_memory = VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	#endif

	bool success = arena->available_memory != NULL;
	
	if(!success)
	{
		log_error("Create Arena: Failed to allocate %Iu bytes with the error code %lu.", total_size, GetLastError());
	}

	arena->used_size = 0;
	arena->total_size = (success) ? (total_size) : (0);
	arena->num_locks = 0;

	#ifdef BUILD_DEBUG
		if(success)
		{
			FillMemory(arena->available_memory, arena->total_size, DEBUG_ARENA_DEALLOCATED_VALUE);
		}
	#endif

	return success;
}

// Moves an arena's available memory pointer by a certain number, giving back the aligned address to a memory location
// where you can write at least that number of bytes.
//
// This function is usually called by using the macro push_arena(arena, push_size, Type), which determines the alignment of
// that Type. This macro also casts the resulting memory address to Type*.
//
// In the debug builds, this function clears this memory amount to zero.
//
// @Parameters:
// 1. arena - The Arena structure whose available_memory member will be increased to accommodate at least push_size bytes.
// 2. push_size - How many bytes to increase the current available memory.
// 3. alignment_size - The alignment size in bytes used to align the resulting address. This value must be a power of two
// and should correspond to the alignment size of the type of data you want to read from or write to this memory location.
//
// @Returns: The aligned memory address capable of holding the requested amount of bytes, if it succeeds. Otherwise, it
// returns NULL and the arena's available_memory and used_size members are not modified. This function fails if there's not
// enough memory to push the requested size.
void* aligned_push_arena(Arena* arena, size_t push_size, size_t alignment_size)
{
	if(arena->available_memory == NULL)
	{
		log_error("Aligned Push Arena: Failed to push %Iu bytes since no memory was previously allocated.", push_size);
		return NULL;
	}

	if(alignment_size == 0)
	{
		log_error("Aligned Push Arena: Failed to push %Iu bytes since the alignment size is zero.", push_size);
		_ASSERT(false);
		return NULL;
	}

	void* aligned_address = arena->available_memory;
	size_t alignment_offset = ALIGN_OFFSET((uintptr_t) arena->available_memory, alignment_size);

	if(alignment_size > 1)
	{
		if(IS_POWER_OF_TWO(alignment_size))
		{
			aligned_address = advance_bytes(arena->available_memory, alignment_offset);
		}
		else
		{
			log_error("Aligned Push Arena: Failed to align the memory address since the alignment size %Iu is not a power of two.", alignment_size);
			_ASSERT(false);
			return NULL;
		}
	}
	
	size_t aligned_push_size = push_size + alignment_offset;
	_ASSERT(push_size <= aligned_push_size);
	_ASSERT(IS_POINTER_ALIGNED_TO_SIZE(aligned_address, alignment_size));

	if(arena->used_size + aligned_push_size > arena->total_size)
	{
		log_error("Aligned Push Arena: Ran out of memory. Pushing %Iu more bytes to %Iu would exceed the total size of %Iu bytes.", aligned_push_size, arena->used_size, arena->total_size);
		_ASSERT(false);
		return NULL;
	}

	#ifdef BUILD_DEBUG
		ZeroMemory(aligned_address, aligned_push_size);
	#endif

	arena->available_memory = advance_bytes(arena->available_memory, aligned_push_size);
	arena->used_size += aligned_push_size;

	// Keep track of the maximum used size starting at a certain value. This gives us an idea of how
	// much memory each cache type and Windows version uses before clearing the arena.
	#ifdef BUILD_DEBUG
		static size_t max_used_size_marker = kilobytes_to_bytes(256);
		
		if(arena->used_size > max_used_size_marker)
		{
			size_t previous_marker_size = max_used_size_marker;
			max_used_size_marker += max_used_size_marker / 2;
			log_debug("Aligned Push Arena: Moved the maximum used size marker from %Iu to %Iu after pushing %Iu bytes. The arena is now at %.2f%% used capacity.", previous_marker_size, max_used_size_marker, aligned_push_size, get_used_arena_capacity(arena));
		}
	#endif

	return aligned_address;
}

// Behaves like aligned_push_arena() but always takes a 64-bit push size. This function has to be called directly and does
// not have any convenience macro.
//
// @Parameters: See aligned_push_arena().
//
// @Returns: See aligned_push_arena().
void* aligned_push_arena_64(Arena* arena, u64 push_size, size_t alignment_size)
{
	void* result = aligned_push_arena(arena, 0, alignment_size);
	
	// This is a silly way to be able to push more than 4 GB even in 32-bit builds but it works for now.
	for(u64 i = 0; i < push_size / ULONG_MAX; ++i)
	{
		if(push_arena(arena, ULONG_MAX, u8) == NULL)
		{
			result = NULL;
			break;
		}
	}	
	
	if(push_arena(arena, (u32) (push_size % ULONG_MAX), u8) == NULL) result = NULL;

	return result;
}

// Behaves like aligned_push_arena() but also copies a given amount of bytes from one memory location to the arena's memory.
//
// @Parameters: In addition to the previous ones, this function also takes the following parameters:
// 4. data - The address of the block of memory to copy to the arena.
// 5. data_size - The size in bytes of the block of memory to copy to the arena.
// 
// @Returns: See aligned_push_arena().
void* aligned_push_and_copy_to_arena(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size)
{
	if(data_size > push_size)
	{
		log_error("Aligned Push And Copy To Arena: Attempted to copy %Iu bytes from %p while only pushing %Iu bytes.", data_size, data, push_size);
		_ASSERT(false);
		return NULL;
	}

	void* copy_address = aligned_push_arena(arena, push_size, alignment_size);
	CopyMemory(copy_address, data, data_size);
	return copy_address;
}

// A helper function that copies a string to an arena.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the string.
// 2. string_to_copy - The string to copy.
//
// @Returns: The copied string's address in the arena if it succeeds. Otherwise, this function returns NULL.
TCHAR* push_string_to_arena(Arena* arena, const TCHAR* string_to_copy)
{
	size_t size = string_size(string_to_copy);
	return push_and_copy_to_arena(arena, size, TCHAR, string_to_copy, size);
}

// Retrieves the used capacity of an arena as a percentage (0 through 100).
//
// @Parameters:
// 1. arena - The Arena structure of interest.
//
// @Returns: The used capacity as a percentage. If the arena is uninitialized or destroyed, this function returns zero.
f32 get_used_arena_capacity(Arena* arena)
{
	if(arena->available_memory == NULL || arena->total_size == 0)
	{
		return 0.0f;
	}

	return ((f32) arena->used_size) / arena->total_size * 100;
}

// Retrieves an appropriate buffer size that may be pushed into the arena for processing a given file.
//
// This value has the following properties:
// - Fits small enough files into memory, avoiding fragmentation when allocating multiple buffers.
// - Allows reading large files in chunks whose size doesn't cause any problems when calling ReadFile().
// - Always greater than zero. For empty files or full arenas, a size of one byte is returned. For the
// first case, calling ReadFile() will let us know we finished reading the file. For the second case,
// calling push_arena() will return NULL.
//
// @Parameters:
// 1. arena - The Arena structure whose remaining available size is used to determine the result.
// 2. file_handle - The file whose size is used to determine the result.
//
// @Returns: The appropriate file buffer size for the given arena in bytes.
u32 get_arena_file_buffer_size(Arena* arena, HANDLE file_handle)
{
	// This should be kept at around a quarter of the arena's remaining size for operations like
	// decompressing files, where we have one quarter for the file buffer (worst case), one half
	// for the decompressed data, and the remaining quarter for a third-party library's requested
	// memory.
	size_t remaining_arena_size = arena->total_size - arena->used_size;
	u32 max_size = (u32) MIN(remaining_arena_size / 4, megabytes_to_bytes(50));
	u64 file_size = 0;
	if(!get_file_size(file_handle, &file_size)) file_size = ULONG_MAX;
	u32 buffer_size = (u32) MIN(file_size, (u64) max_size);
	return MAX(buffer_size, 1);
}

// Retrieves an appropriate buffer size that may be pushed into the arena for processing a chunk of data.
//
// See get_arena_file_buffer_size() for more details.
//
// @Parameters:
// 1. arena - The Arena structure whose remaining available size is used to determine the result.
// 2. min_size - The minimum desired size used to determine the result.
//
// @Returns: The appropriate chunk buffer size for the given arena in byte
u32 get_arena_chunk_buffer_size(Arena* arena, size_t min_size)
{
	size_t remaining_arena_size = arena->total_size - arena->used_size;
	u32 chunk_size = (u32) MAX(remaining_arena_size / 4, min_size);
	return MAX(chunk_size, 1);
}

// Adds a new lock to the arena. A lock marks the currently used size and prevents clear_arena() from clearing any of
// this memory. The arena keeps track of these locked regions by using a stack that holds at most MAX_NUM_ARENA_LOCKS
// locks. This operation is reversed using unlock_arena(). For example:
//
// lock_arena(arena)
// push_string_to_arena(arena, "ABC")
//
//		lock_arena(arena)
//		push_string_to_arena(arena, "123")
//		clear_arena(arena) -> "ABC" still exists but "123" was cleared.
//		unlock_arena(arena)
//
// clear_arena(arena) -> Any data before "ABC" still exists.
// unlock_arena(arena)
//
// @Note: This function should be used sparingly as it can increase the code's complexity if the same lock is only
// unlocked in another function call. The pair of lock and unlock operations should be done in the same function
// scope.
//
// @Parameters:
// 1. arena - The Arena structure whose num_locks member will be incremented, and whose current used size will be saved
// in the next free locked_sizes slot.
//
// @Returns: Nothing.
void lock_arena(Arena* arena)
{
	if(arena->available_memory == NULL)
	{
		log_error("Lock Arena: Failed to lock the arena since no memory was previously allocated.");
		return;
	}

	if(arena->num_locks < MAX_NUM_ARENA_LOCKS)
	{
		arena->locked_sizes[arena->num_locks] = arena->used_size;
		++(arena->num_locks);
	}
	else
	{
		log_error("Lock Arena: Ran out of lock slots in the arena.");
		_ASSERT(false);
	}
}

// Removes the last lock from the arena. An arena with no locks cannot be unlocked. See: lock_arena().
//
// @Parameters:
// 1. arena - The Arena structure whose num_locks member will be decremented, and whose last saved used size will be
// removed from the last locked_sizes slot.
//
// @Returns: Nothing.
void unlock_arena(Arena* arena)
{
	if(arena->available_memory == NULL)
	{
		log_error("Unlock Arena: Failed to unlock the arena since no memory was previously allocated.");
		return;
	}

	if(arena->num_locks > 0)
	{
		--(arena->num_locks);
		arena->locked_sizes[arena->num_locks] = 0;
	}
	else
	{
		log_error("Unlock Arena: Attempted to unlock an arena without any previously locked sizes.");
		_ASSERT(false);
	}
}

// Clears the entire arena, resetting the pointer to the available memory back to the original address and the number of used
// bytes to zero.
//
// In the debug builds, this function clears the entire allocated memory to zero.
//
// @Parameters:
// 1. arena - The Arena structure whose available_memory and used_size members will be reset.
//
// @Returns: Nothing.
void clear_arena(Arena* arena)
{
	if(arena->available_memory == NULL)
	{
		log_error("Clear Arena: Failed to clear the arena since no memory was previously allocated.");
		return;
	}

	size_t num_bytes_to_clear = arena->used_size;
	if(arena->num_locks > 0)
	{
		num_bytes_to_clear -= arena->locked_sizes[arena->num_locks - 1];
	}

	arena->available_memory = retreat_bytes(arena->available_memory, num_bytes_to_clear);
	#ifdef BUILD_DEBUG
		FillMemory(arena->available_memory, num_bytes_to_clear, DEBUG_ARENA_DEALLOCATED_VALUE);
	#endif
	arena->used_size -= num_bytes_to_clear;
}

// Destroys an arena and deallocates all of its memory.
//
// @Parameters:
// 1. arena - The Arena structure whose available_memory will be deallocated.
//
// @Returns: True if the arena was destroyed successfully. In this case, the arena is set to NULL_ARENA.
// Otherwise, it returns false and the arena's members are not modified.
bool destroy_arena(Arena* arena)
{
	if(arena->available_memory == NULL)
	{
		return true;
	}

	void* base_memory = retreat_bytes(arena->available_memory, arena->used_size);
	bool success = VirtualFree(base_memory, 0, MEM_RELEASE) != FALSE;
	#ifdef BUILD_DEBUG
		DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = retreat_bytes(DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS, DEBUG_BASE_ADDRESS_INCREMENT);
	#endif
	
	if(success)
	{
		*arena = NULL_ARENA;
	}

	return success;
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> BASIC OPERATIONS
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// Combines the high and low 32-bit parts of an unsigned 64-bit integer.
//
// @Parameters:
// 1. high - The high 32 bits of a 64-bit integer.
// 2. low - The low 32 bits of a 64-bit integer.
//
// @Returns: The combined 64-bit integer.
u64 combine_high_and_low_u32s_into_u64(u32 high, u32 low)
{
	return (((u64) high) << 32) | low;
}

// Separates an unsigned 64-bit integer into its high and low 32-bit parts.
//
// @Parameters:
// 1. value - The 64-bit value to separate into two 32-bit integers.
// 2. high - The resulting high 32 bits of the 64-bit value.
// 3. low - The resulting low 32 bits of the 64-bit value.
//
// @Returns: Nothing.
void separate_u64_into_high_and_low_u32s(u64 value, u32* high, u32* low)
{
	*low = (u32) (value & 0xFFFFFFFF);
	*high = (u32) (value >> 32);
}

// Separates an unsigned 32-bit integer into its high and low 16-bit parts.
//
// @Parameters:
// 1. value - The 32-bit value to separate into two 16-bit integers.
// 2. high - The resulting high 16 bits of the 32-bit value.
// 3. low - The resulting low 16 bits of the 32-bit value.
//
// @Returns: Nothing.
void separate_u32_into_high_and_low_u16s(u32 value, u16* high, u16* low)
{
	*low = (u16) (value & 0xFFFF);
	*high = (u16) (value >> 16);
}

// Advances a pointer by a given number of bytes.
//
// @Parameters:
// 1. pointer - The pointer to move forward.
// 2. num_bytes - The number of bytes to move.
//
// @Returns: The moved pointer value.
void* advance_bytes(void* pointer, u16 num_bytes)
{
	return ((char*) pointer) + num_bytes;
}

void* advance_bytes(void* pointer, u32 num_bytes)
{
	return ((char*) pointer) + num_bytes;
}

void* advance_bytes(void* pointer, u64 num_bytes)
{
	return ((char*) pointer) + num_bytes;
}

// Retreats a pointer by a given number of bytes.
//
// @Parameters:
// 1. pointer - The pointer to move backwards.
// 2. num_bytes - The number of bytes to move.
//
// @Returns: The moved pointer value.
void* retreat_bytes(void* pointer, u32 num_bytes)
{
	return ((char*) pointer) - num_bytes;
}

void* retreat_bytes(void* pointer, u64 num_bytes)
{
	return ((char*) pointer) - num_bytes;
}

// Subtracts two pointers.
//
// @Parameters:
// 1. a - The left pointer operand.
// 2. b - The right pointer operand.
//
// @Returns: The subtraction result in bytes.
ptrdiff_t pointer_difference(void* a, void* b)
{
	return ((char*) a) - ((char*) b);
}

// Converts a value in kilobytes to bytes.
//
// @Parameters:
// 1. kilobytes - The value in kilobytes.
//
// @Returns: The value in bytes.
size_t kilobytes_to_bytes(size_t kilobytes)
{
	return kilobytes * 1024;
}

// Converts a value in megabytes to bytes.
//
// @Parameters:
// 1. megabytes - The value in megabytes.
//
// @Returns: The value in bytes.
size_t megabytes_to_bytes(size_t megabytes)
{
	return megabytes * 1024 * 1024;
}

// Swaps the byte order of an integer. Supported types: u8, s8, u16, s16, u32, s32, u64, s64.
//
// @Parameters:
// 1. value - The integer.
//
// @Returns: The integer with reversed byte order.

// For function overloading convenience only.
u8 swap_byte_order(u8 value)
{
	return value;
}

// For function overloading convenience only.
s8 swap_byte_order(s8 value)
{
	return value;
}

u16 swap_byte_order(u16 value)
{
	return _byteswap_ushort(value);
}

s16 swap_byte_order(s16 value)
{
	return (s16) _byteswap_ushort((u16) value);
}

u32 swap_byte_order(u32 value)
{
	return _byteswap_ulong(value);
}

s32 swap_byte_order(s32 value)
{
	return (s32) _byteswap_ulong((u32) value);
}

u64 swap_byte_order(u64 value)
{
	return _byteswap_uint64(value);
}

s64 swap_byte_order(s64 value)
{
	return (s64) _byteswap_uint64((s64) value);
}

// Compares two buffers.
//
// @Parameters:
// 1. buffer_1 - The first buffer.
// 2. buffer_2 - The second buffer.
// 3. size_to_compare - The number of bytes to compare.
//
// @Returns: True if the buffers are equal. Otherwise, false.
bool memory_is_equal(const void* buffer_1, const void* buffer_2, size_t size_to_compare)
{
	return memcmp(buffer_1, buffer_2, size_to_compare) == 0;
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> DATE AND TIME FORMATTING
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// Converts a date time value to a string with the format "YYYY-MM-DD hh:mm:ss".
// Supported date time types and structures: FILETIME, Dos_Date_Time, __time64_t.
//
// @Parameters:
// 1. date_time - The date time value to format. If this value is zero, the resulting string will be empty.
// 2. formatted_string - The resulting formatted string. This string must be able to hold MAX_FORMATTED_DATE_TIME_CHARS
// characters.
//
// @Returns: True if the date time value was formatted correctly. Otherwise, it returns false and the resulting
// formatted string will be empty.

bool format_filetime_date_time(FILETIME date_time, TCHAR* formatted_string)
{
	if(date_time.dwLowDateTime == 0 && date_time.dwHighDateTime == 0)
	{
		*formatted_string = T('\0');
		return true;
	}

	SYSTEMTIME dt = {};
	bool success = FileTimeToSystemTime(&date_time, &dt) != 0;
	success = success && SUCCEEDED(StringCchPrintf(formatted_string, MAX_FORMATTED_DATE_TIME_CHARS, T("%hu-%02hu-%02hu %02hu:%02hu:%02hu"), dt.wYear, dt.wMonth, dt.wDay, dt.wHour, dt.wMinute, dt.wSecond));

	if(!success)
	{
		log_error("Format Filetime Date Time: Failed to format the FILETIME date time (high = %lu, low = %lu) with the error code %lu.", date_time.dwHighDateTime, date_time.dwLowDateTime, GetLastError());
		*formatted_string = T('\0');
	}

	return success;
}

bool format_filetime_date_time(u64 value, TCHAR* formatted_string)
{
	u32 high_value = 0;
	u32 low_value = 0;
	separate_u64_into_high_and_low_u32s(value, &high_value, &low_value);
	
	FILETIME date_time = {};
	date_time.dwHighDateTime = high_value;
	date_time.dwLowDateTime = low_value;

	return format_filetime_date_time(date_time, formatted_string);
}

bool format_dos_date_time(Dos_Date_Time date_time, TCHAR* formatted_string)
{
	if(date_time.date == 0 && date_time.time == 0)
	{
		*formatted_string = T('\0');
		return true;
	}

	FILETIME filetime = {};
	bool success = DosDateTimeToFileTime(date_time.date, date_time.time, &filetime) != 0;
	success = success && format_filetime_date_time(filetime, formatted_string);

	if(!success)
	{
		log_error("Format Dos Date Time: Failed to format the DOS date time (date = %I32u, time = %I32u) with the error code %lu.", date_time.date, date_time.time, GetLastError());
		*formatted_string = T('\0');
	}

	return success;
}

bool format_dos_date_time(u32 value, TCHAR* formatted_string)
{
	Dos_Date_Time date_time = {};
	separate_u32_into_high_and_low_u16s(value, &(date_time.time), &(date_time.date));
	return format_dos_date_time(date_time, formatted_string);
}

bool format_time64_t_date_time(__time64_t date_time, TCHAR* formatted_string)
{
	if(date_time == 0)
	{
		*formatted_string = T('\0');
		return true;
	}

	struct tm time = {};
	bool success = (_gmtime64_s(&time, &date_time) == 0)
				&& (_tcsftime(formatted_string, MAX_FORMATTED_DATE_TIME_CHARS, T("%Y-%m-%d %H:%M:%S"), &time) > 0);

	if(!success)
	{
		log_error("Format Time64_t: Failed to format the time64_t date time (time = %I64u) with the error code %d.", date_time, errno);
		*formatted_string = T('\0');
	}

	return success;
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> STRING MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// @Note: Many of these functions are split into two overloads (instead of using the TCHAR macro) so we can use them on ANSI strings
// in the Windows 2000 through 10 (Unicode) builds. For example, when reading ANSI strings from some file format.

// Gets the size of a string in bytes, including the null terminator.
//
// @Parameters:
// 1. str - The string.
//
// @Returns: The number of bytes the string occupies in memory.
size_t string_size(const char* str)
{
	return (strlen(str) + 1) * sizeof(char);
}

size_t string_size(const wchar_t* str)
{
	return (wcslen(str) + 1) * sizeof(wchar_t);
}

// Gets the length of a string in characters, excluding the null terminator.
//
// This is used to determine the number of elements in a TCHAR (char or wchar_t) array, and might not necessarily represent the
// number of characters in a UTF-16 string.
//
// @Parameters:
// 1. str - The string.
//
// @Returns: The number of characters in the string.
size_t string_length(const char* str)
{
	return strlen(str);
}

size_t string_length(const wchar_t* str)
{
	return wcslen(str);
}

// Checks if a string is empty.
//
// @Parameters:
// 1. str - The string.
//
// @Returns: True if the string is empty. Otherwise, false.
bool string_is_empty(const char* str)
{
	return *str == '\0';
}

bool string_is_empty(const wchar_t* str)
{
	return *str == L'\0';
}

// Compares two strings.
//
// @Parameters:
// 1. str_1 - The first string.
// 2. str_2 - The second string.
// 3. optional_case_insensitive - An optional parameter that specifies if the comparison should be case insensitive (true) or not (false).
// This value defaults to false.
//
// @Returns: True if the strings are equal. Otherwise, false.
bool strings_are_equal(const char* str_1, const char* str_2, bool optional_case_insensitive)
{
	if(optional_case_insensitive) 	return _stricmp(str_1, str_2) == 0;
	else 							return strcmp(str_1, str_2) == 0;
}

bool strings_are_equal(const wchar_t* str_1, const wchar_t* str_2, bool optional_case_insensitive)
{
	if(optional_case_insensitive) 	return _wcsicmp(str_1, str_2) == 0;
	else 							return wcscmp(str_1, str_2) == 0;
}

// Compares two strings up to a number of characters.
//
// @Parameters:
// 1. str_1 - The first string.
// 2. str_2 - The second string.
// 3. max_num_chars - The maximum number of characters to compare.
// 4. optional_case_insensitive - An optional parameter that specifies if the comparison should be case insensitive (true) or not (false).
// This value defaults to false.
//
// @Returns: True if the strings are equal up to that number of characters. Otherwise, false.
bool strings_are_at_most_equal(const char* str_1, const char* str_2, size_t max_num_chars, bool optional_case_insensitive)
{
	if(optional_case_insensitive) 	return _strnicmp(str_1, str_2, max_num_chars) == 0;
	else 							return strncmp(str_1, str_2, max_num_chars) == 0;
}

bool strings_are_at_most_equal(const wchar_t* str_1, const wchar_t* str_2, size_t max_num_chars, bool optional_case_insensitive)
{
	if(optional_case_insensitive) 	return _wcsnicmp(str_1, str_2, max_num_chars) == 0;
	else 							return wcsncmp(str_1, str_2, max_num_chars) == 0;
}

// Checks if a string begins with a given prefix.
//
// @Parameters:
// 1. str - The string to check.
// 2. prefix - The prefix string to use.
// 3. optional_case_insensitive - An optional parameter that specifies if the comparison should be case insensitive (true) or not (false).
// This value defaults to false.
//
// @Returns: True if the string begins with that prefix. Otherwise, false.
bool string_begins_with(const char* str, const char* prefix, bool optional_case_insensitive)
{
	if(optional_case_insensitive) 	return _strnicmp(str, prefix, string_length(prefix)) == 0;
	else 							return strncmp(str, prefix, string_length(prefix)) == 0;
}

bool string_begins_with(const wchar_t* str, const wchar_t* prefix, bool optional_case_insensitive)
{
	if(optional_case_insensitive) 	return _wcsnicmp(str, prefix, string_length(prefix)) == 0;
	else 							return wcsncmp(str, prefix, string_length(prefix)) == 0;
}

// Checks if a string ends with a given suffix.
//
// @Parameters:
// 1. str - The string to check.
// 2. suffix - The suffix string to use.
// 3. optional_case_insensitive - An optional parameter that specifies if the comparison should be case insensitive (true) or not (false).
// This value defaults to false.
//
// @Returns: True if the string ends with that suffix. Otherwise, false.
bool string_ends_with(const TCHAR* str, const TCHAR* suffix, bool optional_case_insensitive)
{
	size_t str_length = string_length(str);
	size_t suffix_length = string_length(suffix);
	if(suffix_length > str_length) return false;

	const TCHAR* suffix_in_str = str + str_length - suffix_length;
	return strings_are_equal(suffix_in_str, suffix, optional_case_insensitive);
}

// Converts a string to uppercase.
//
// @Parameters:
// 1. str - The string to convert.
//
// @Returns: Nothing.
void string_to_uppercase(TCHAR* str)
{
	while(*str != T('\0'))
	{
		*str = (TCHAR) _totupper(*str);
		++str;
	}
}

// Unescapes a string containing a few common escape characters. 
//
// @Parameters:
// 1. str - The string to unescape.
//
// @Returns: Nothing.
void string_unescape(TCHAR* str)
{
	while(*str != T('\0'))
	{
		if(*str == T('\\'))
		{
			TCHAR* next_char = str + 1;

			switch(*next_char)
			{
				case(T('\\')):
				case(T('\"')):
				{
					MoveMemory(str, next_char, string_size(next_char));
				} break;
			}
		}

		++str;
	}
}

// Skips the leading whitespace (spaces and tabs) in a string.
// These are split into two functions (instead of using the TCHAR macro) so we can use it on an ANSI string even
// in the Windows 2000 through 10 (Unicode) builds. For example, when reading ANSI strings from some file format.
//
// @Parameters:
// 1. str - The string whose leading whitespace will be skipped.
//
// @Returns: The address of the character after any leading whitespace in the string. If the string is NULL, this
// function returns NULL.

char* skip_leading_whitespace(char* str)
{
	if(str != NULL)
	{
		while(*str == ' ' || *str == '\t') ++str;
	}
	
	return str;
}

wchar_t* skip_leading_whitespace(wchar_t* str)
{
	if(str != NULL)
	{
		while(*str == L' ' || *str == L'\t') ++str;
	}
	
	return str;
}

// Converts an integer of a given size in base 10 to a string.
//
// @Parameters:
// 1. value - The integer value to convert.
// 2. result_string - The converted string.
//
// @Returns: True if the integer was converted successfully. Otherwise, false.

static const size_t INT_FORMAT_RADIX = 10;

bool convert_u32_to_string(u32 value, TCHAR* result_string)
{
	bool success = _ultot_s(value, result_string, MAX_INT32_CHARS, INT_FORMAT_RADIX) == 0;
	if(!success) *result_string = T('\0');
	return success;
}
bool convert_s32_to_string(s32 value, TCHAR* result_string)
{
	bool success = _ltot_s(value, result_string, MAX_INT32_CHARS, INT_FORMAT_RADIX) == 0;
	if(!success) *result_string = T('\0');
	return success;
}
bool convert_u64_to_string(u64 value, TCHAR* result_string)
{
	bool success = _ui64tot_s(value, result_string, MAX_INT64_CHARS, INT_FORMAT_RADIX) == 0;
	if(!success) *result_string = T('\0');
	return success;
}
bool convert_s64_to_string(s64 value, TCHAR* result_string)
{
	bool success = _i64tot_s(value, result_string, MAX_INT64_CHARS, INT_FORMAT_RADIX) == 0;
	if(!success) *result_string = T('\0');
	return success;
}

// Converts a string containing a value in base 10 to an integer of a given size.
//
// @Parameters:
// 1. str - The string to convert.
// 2. result_value - The converted integer value.
//
// @Returns: True if the string was converted successfully. Otherwise, false.

bool convert_string_to_u64(const char* str, u64* result_value)
{
	char* end = NULL;
	*result_value = _strtoui64(str, &end, INT_FORMAT_RADIX);
	return (end != str);
}

// Converts a single hexadecimal character to an integer.
//
// @Parameters:
// 1. hex_char - The character to convert.
// 2. result_integer - A pointer to an unsigned 8-bit integer that receives the integer result.
//
// @Returns: True if the conversion was successfully, i.e., if the character is 0-9, a-f, or A-F.
// Otherwise, it returns false and the resulting integer is unchanged.
static bool convert_hexadecimal_char_to_integer(TCHAR hex_char, u8* result_integer)
{
	bool success = true;

	if(T('0') <= hex_char && hex_char <= T('9'))
	{
		*result_integer = (u8) (hex_char - T('0'));
	}
	else if(T('a') <= hex_char && hex_char <= T('f'))
	{
		*result_integer = (u8) (hex_char - T('a') + 0x0A);
	}
	else if(T('A') <= hex_char && hex_char <= T('F'))
	{
		*result_integer = (u8) (hex_char - T('A') + 0x0A);
	}
	else
	{
		success = false;
		log_error("Convert Hexadecimal Character To Integer: The character '%c' (%hhd) cannot be converted into an integer.", hex_char, (char) hex_char);
	}

	return success;
}

// Converts the first two hexadecimal characters in a string to a byte. Any remaining characters are ignored.
//
// @Parameters:
// 1. byte_string - The string to convert into a byte.
// 2. result_byte - A pointer to an unsigned 8-bit integer that receives the byte result.
//
// @Returns: True if the conversion was successfully, i.e., if the characters are 0-9, a-f, or A-F.
// Otherwise, it returns false and the resulting byte is unchanged. This function fails if the string
// to convert is NULL, or if it has fewer than two characters.
bool convert_hexadecimal_string_to_byte(const TCHAR* byte_string, u8* result_byte)
{
	if(byte_string == NULL || string_length(byte_string) < 2)
	{
		return false;
	}

	bool success = false;

	TCHAR char_1 = *byte_string;
	TCHAR char_2 = (char_1 != T('\0')) ? (*(byte_string+1)) : (T('\0'));
	u8 integer_1 = 0;
	u8 integer_2 = 0;

	if(		convert_hexadecimal_char_to_integer(char_1, &integer_1)
		&& 	convert_hexadecimal_char_to_integer(char_2, &integer_2))
	{
		*result_byte = integer_1 * 16 + integer_2;
		success = true;
	}

	return success;
}

// Converts a string encoded with a given code page to to a TCHAR one and copies the final result to a memory arena. On the Windows 98 and ME
// builds, the intermediary UTF-16 string is stored in an intermediary arena. If this is irrelevant, use convert_code_page_string_to_tchar(2)
// instead.
//
// On the Windows 98 and ME builds, this function first converts the string to UTF-16, and then to an ANSI one.
// On the Windows 2000 through 10 builds, this function converts the string to a UTF-16 one.
//
// This function will fail for the code pages 1200, 1201, 12000, and 12001 (UTF-16 LE, UTF-16 BE, UTF-32 LE, and UTF-32 BE).
//
// @Parameters:
// 1. final_arena - The Arena structure that will receive the final converted TCHAR string.
// 2. intermediary_arena - The Arena structure that will receive the intermediary converted UTF-16 string. This only applies to Windows 98
// and ME. On the Windows 2000 to 10 builds, this parameter is unused.
// 3. code_page - The code page used by the string.
// 4. string - The string to convert.
//
// @Returns: The pointer to the TCHAR string on success. Otherwise, it returns NULL.
TCHAR* convert_code_page_string_to_tchar(Arena* final_arena, Arena* intermediary_arena, u32 code_page, const char* string)
{
	_ASSERT(code_page != CODE_PAGE_UTF_16_LE && code_page != CODE_PAGE_UTF_16_BE && code_page != CODE_PAGE_UTF_32_LE && code_page != CODE_PAGE_UTF_32_BE);

	int num_chars_required_utf_16 = MultiByteToWideChar(code_page, 0, string, -1, NULL, 0);

	if(num_chars_required_utf_16 == 0)
	{
		log_error("Convert Code Page String To Tchar: Failed to determine the number of characters required to convert the code page %I32u string '%hs' into a UTF-16 string with the error code %lu.", code_page, string, GetLastError());
		return NULL;
	}

	#ifdef BUILD_9X
		Arena* utf_16_string_arena = intermediary_arena;
	#else
		Arena* utf_16_string_arena = final_arena;
	#endif

	int size_required_utf_16 = num_chars_required_utf_16 * sizeof(wchar_t);
	wchar_t* utf_16_string = push_arena(utf_16_string_arena, size_required_utf_16, wchar_t);

	if(MultiByteToWideChar(code_page, 0, string, -1, utf_16_string, num_chars_required_utf_16) == 0)
	{
		log_error("Convert Code Page String To Tchar: Failed to convert the code page %I32u string '%hs' into a UTF-16 string with the error code %lu.", code_page, string, GetLastError());
		return NULL;
	}

	#ifdef BUILD_9X
		return convert_utf_16_string_to_tchar(final_arena, utf_16_string);
	#else
		return utf_16_string;
	#endif
}

// Behaves like convert_code_page_string_to_tchar(3) but instead copies both the final and intermediary strings to the same memory arena.
// 
// @Returns: See convert_code_page_string_to_tchar(3).
TCHAR* convert_code_page_string_to_tchar(Arena* arena, u32 code_page, const char* string)
{
	return convert_code_page_string_to_tchar(arena, arena, code_page, string);
}

// Converts an ANSI string to a TCHAR one.
//
// On the Windows 98 and ME builds, this function simply copies the ANSI string.
// On the Windows 2000 through 10 builds, this function converts the ANSI string to a UTF-16 one.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the converted TCHAR string.
// 2. ansi_string - The ANSI string to convert.
//
// @Returns: The pointer to the TCHAR string on success. Otherwise, it returns NULL.
TCHAR* convert_ansi_string_to_tchar(Arena* arena, const char* ansi_string)
{
	#ifdef BUILD_9X
		return push_string_to_arena(arena, ansi_string);
	#else
		return convert_code_page_string_to_tchar(arena, CP_ACP, ansi_string);
	#endif
}

// Converts a UTF-16 LE string to a TCHAR one.
//
// On the Windows 98 and ME builds, this function converts the UTF-16 string to an ANSI one.
// On the Windows 2000 through 10 builds, simply copies the UTF-16 string.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the converted TCHAR string.
// 2. utf_16_string - The UTF-16 string to convert.
//
// @Returns: The pointer to the TCHAR string on success. Otherwise, it returns NULL.
TCHAR* convert_utf_16_string_to_tchar(Arena* arena, const wchar_t* utf_16_string)
{
	#ifdef BUILD_9X
		int size_required_ansi = WideCharToMultiByte(CP_ACP, 0, utf_16_string, -1, NULL, 0, NULL, NULL);
		
		if(size_required_ansi == 0)
		{
			log_error("Convert Utf-16 String To Tchar: Failed to determine the size required to convert the UTF-16 string '%ls' into an ANSI string with the error code %lu.", utf_16_string, GetLastError());
			return NULL;
		}

		char* ansi_string = push_arena(arena, size_required_ansi, char);
		
		if(WideCharToMultiByte(CP_UTF8, 0, utf_16_string, -1, ansi_string, size_required_ansi, NULL, NULL) == 0)
		{
			log_error("Convert Utf-16 String To Tchar: Failed to convert the UTF-16 string '%ls' into an ANSI string with the error code %lu.", utf_16_string, GetLastError());
			return NULL;
		}

		return ansi_string;
	#else
		return push_string_to_arena(arena, utf_16_string);
	#endif
}

// Converts a UTF-8 string to a TCHAR one and copies the final result to a memory arena. On the Windows 98 and ME builds, the
// intermediary UTF-16 string is stored in an intermediary arena. If this is irrelevant, use convert_utf_8_string_to_tchar(2)
// instead.
//
// On the Windows 98 and ME builds, this function first converts the UTF-8 string to UTF-16, and then to an ANSI one.
// On the Windows 2000 through 10 builds, this function converts the UTF-8 string to a UTF-16 one.
//
// @Parameters:
// 1. final_arena - The Arena structure that will receive the final converted TCHAR string.
// 2. intermediary_arena - The Arena structure that will receive the intermediary converted UTF-16 string. This only applies to Windows 98
// and ME. On the Windows 2000 to 10 builds, this parameter is unused.
// 3. utf_8_string - The UTF-8 string to convert.
//
// @Returns: The pointer to the TCHAR string on success. Otherwise, it returns NULL.
TCHAR* convert_utf_8_string_to_tchar(Arena* final_arena, Arena* intermediary_arena, const char* utf_8_string)
{
	return convert_code_page_string_to_tchar(final_arena, intermediary_arena, CP_UTF8, utf_8_string);
}

// Behaves like convert_utf_8_string_to_tchar(3) but instead copies both the final and intermediary strings to the same memory arena.
// 
// @Returns: See convert_utf_8_string_to_tchar(3).
TCHAR* convert_utf_8_string_to_tchar(Arena* arena, const char* utf_8_string)
{
	return convert_utf_8_string_to_tchar(arena, arena, utf_8_string);
}

// Skips to the character immediately after the current string's null terminator.
//
// @Parameters:
// 1. str - The string.
//
// @Returns: The beginning of the next contiguous string.
char* skip_to_next_string(char* str)
{
	while(*str != '\0') ++str;
	return ++str;
}

wchar_t* skip_to_next_string(wchar_t* str)
{
	while(*str != L'\0') ++str;
	return ++str;
}

// Creates an array of strings based on a number of strings that are contiguously stored in memory.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the string array.
// 2. first_string - The first string that is contiguously stored in memory with any remaining ones. The next string starts after the
// previous one's null terminator.
// 3. num_strings - The number of strings.
//
// @Returns: The array of strings with a length of 'num_strings'.
TCHAR** build_array_from_contiguous_strings(Arena* arena, TCHAR* first_string, u32 num_strings)
{
	TCHAR** string_array = push_arena(arena, num_strings * sizeof(TCHAR*), TCHAR*);

	for(u32 i = 0; i < num_strings; ++i)
	{
		string_array[i] = first_string;
		first_string = skip_to_next_string(first_string);
	}

	return string_array;
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> URL MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// Partitions a URL into specific components using the following syntax:
// - URL = scheme:[//authority]path[?query][#fragment]
// - Authority = [userinfo@]host[:port]
// The resulting Url_Parts structure will contain the pointer to a copy of these components or NULL if they don't exist.
//
// Empty path and host components are accepted in order to accommodate certain URL structures that may show up in cache databases.
// For example:
// - No path: "http://www.example.com" or http://www.example.com/", where the server would serve "http://www.example.com/index.html".
// - No Host: "file:///C:\Users\<Username>\Desktop", where the host is localhost.
//
// In these cases, the resulting component would be an empty string, and not a NULL pointer. This means that, on success,
// the host (if the authority exists) and path members in the Url_Parts structure will always be strings (empty or otherwise).
//
// @Parameters:
// 1. arena - The Arena structure where the resulting URL components and any intermediary strings are stored.
// 2. original_url - The URL string to be separated into different components.
// 3. url_parts - The Url_Parts structure that receives the pointers to the various URL components that were copied to the
// memory arena. All these pointers are initially set to NULL.
//
// @Returns: True if the function succeeds. Otherwise, false. This function fails in the following situations:
// 1. The supplied URL is NULL or empty.
// 2. The supplied URL doesn't have a scheme.
// 3. The supplied URL has an authority identifier but nothing after it (e.g. "http://").
bool partition_url(Arena* arena, const TCHAR* original_url, Url_Parts* url_parts)
{
	ZeroMemory(url_parts, sizeof(Url_Parts));

	if(original_url == NULL || string_is_empty(original_url)) return false;

	TCHAR* url = push_string_to_arena(arena, original_url);
	TCHAR* remaining_url = NULL;
	TCHAR* scheme = _tcstok_s(url, T(":"), &remaining_url);

	if(scheme == NULL || string_is_empty(scheme) || string_is_empty(remaining_url))
	{
		log_warning("Partition Url: Missing the scheme in '%s'.", original_url);
		return false;
	}

	url_parts->scheme = push_string_to_arena(arena, scheme);

	// Check if the authority exists.
	if(*remaining_url == T('/') && *(remaining_url+1) == T('/'))
	{
		remaining_url += 2;

		// If the authority is empty (e.g. "file:///C:\Path\File.ext")
		if(*remaining_url == T('/'))
		{
			url_parts->host = push_string_to_arena(arena, T(""));
			remaining_url += 1;
		}
		else
		{
			// Split the authority from the path. If there's no forward slash or backslash separator (either
			// a URL path or Windows directory separator), the remaining URL is the host.
			// For example, "http://www.example.com" results in the host "www.example.com" and an empty path.
			TCHAR* authority = _tcstok_s(NULL, T("/\\"), &remaining_url);
			if(authority != NULL)
			{
				TCHAR* remaining_authority = NULL;
				
				// Split the userinfo from the rest of the authority (host and port) if it exists.
				// Otherwise, the userinfo would be set to the remaining authority when it didn't exist.
				bool has_userinfo = _tcschr(authority, T('@')) != NULL;
				TCHAR* userinfo = (has_userinfo) ? (_tcstok_s(authority, T("@"), &remaining_authority)) : (NULL);

				// If the userinfo exists, we'll split the remaining authority into the host and port.
				// E.g. "userinfo@www.example.com:80" -> "www.example.com:80" -> "www.example.com" + "80"
				// If it doesn't, we'll split starting at the beginning of the authority.
				// E.g. "www.example.com:80" -> "www.example.com" + "80"
				TCHAR* string_to_split = (has_userinfo) ? (NULL) : (authority);
				TCHAR* host = _tcstok_s(string_to_split, T(":"), &remaining_authority);
				// If the remaining authority now points to the end of the string, then there's no port.
				// Otherwise, whatever remains of the authority is the port. We can do this because of that
				// initial split with the path separator.
				TCHAR* port = (!string_is_empty(remaining_authority)) ? (remaining_authority) : (NULL);

				if(host == NULL) host = T("");
				url_parts->host = push_string_to_arena(arena, host);

				// Leave the userinfo and port set to NULL or copy their actual value if they exist.
				if(userinfo != NULL) url_parts->userinfo = push_string_to_arena(arena, userinfo);
				if(port != NULL) url_parts->port = push_string_to_arena(arena, port);

				_ASSERT(url_parts->host != NULL);
			}
			else
			{
				// For cases like "scheme://". Note that "scheme:///" is allowed, and results in an empty host and path.
				log_warning("Partition Url: Found authority identifier but missing the value itself in '%s'.", original_url);
				return false;
			}
		}
	}

	// The path starts after the scheme or authority (if it exists) and ends at the query symbol or at the
	// end of the string (if there's no query).
	TCHAR* query_in_path = _tcschr(remaining_url, T('?'));
	TCHAR* fragment_in_path = _tcschr(remaining_url, T('#'));
	// Check if there's a fragment but no query symbol, or if both exist and the fragment appears before the query.
	// For example, "http://www.example.com/path#top" or "http://www.example.com/path#top?".
	bool does_fragment_appear_before_query = 	(fragment_in_path != NULL && query_in_path == NULL)
											|| 	(fragment_in_path != NULL && query_in_path != NULL && fragment_in_path < query_in_path);

	TCHAR* path = _tcstok_s(NULL, T("?#"), &remaining_url);
	// We'll allow empty paths because the cache's index/database file might store some of them like this.
	// E.g. the resource "http://www.example.com/index.html" might have its URL stored as "http://www.example.com/"
	// since the server would know to serve the index.html file for that request. URLs with no slash after the
	// host (e.g. "http://www.example.com") are treated the same way (see the authority above).
	if(path == NULL) path = T("");
	url_parts->path = push_string_to_arena(arena, path);

	// Search for the resource's name starting at the end of the path.
	TCHAR* last_forward_slash = _tcsrchr(path, T('/'));
	TCHAR* last_backslash = _tcsrchr(path, T('\\'));

	// Use whichever separator appears last in the path. For example:
	// - "http://www.example.com/path/file.ext" -> "path/file.ext" 		-> "file.ext"
	// - "file:///C:\Path\File.ext" 			-> "C:\Path\File.ext" 	-> "File.ext"
	// - "http://www.example.com/" 				-> "" 					-> ""
	// - "http://www.example.com"				-> "" 					-> ""
	last_forward_slash = (last_forward_slash != NULL) ? (last_forward_slash + 1) : (path);
	last_backslash = (last_backslash != NULL) ? (last_backslash + 1) : (path);
	
	TCHAR* filename = (last_forward_slash > last_backslash) ? (last_forward_slash) : (last_backslash);
	url_parts->filename = push_string_to_arena(arena, filename);

	TCHAR* query = (does_fragment_appear_before_query) ? (NULL) : (_tcstok_s(NULL, T("#"), &remaining_url));
	TCHAR* fragment = (!string_is_empty(remaining_url)) ? (remaining_url) : (NULL);

	// Leave the query and fragment set to NULL or copy their actual value if they exist.
	if(query != NULL) url_parts->query = push_string_to_arena(arena, query);
	if(fragment != NULL) url_parts->fragment = push_string_to_arena(arena, fragment);

	_ASSERT(url_parts->path != NULL);
	_ASSERT(url_parts->filename != NULL);

	return true;
}

// Decodes a percent-encoded URL using UTF-8 as the character encoding.
//
// This function will convert this decoded UTF-8 URL into a TCHAR (ANSI or UTF-16) string. This means that the final ANSI
// string on the Windows 98 and ME builds may not be able to represent every character in the UTF-8 data.
//
// If an encoded character cannot be properly decoded to a byte, this function will end prematurely and truncate the URL.
//
// @Parameters:
// 1. arena - The Arena structure that receives the decoded URL and where any intermediary strings are stored.
// 2. url - The URL string to decode.
//
// @Returns: The decoded URL on success. If this function fails, it returns NULL. 
TCHAR* decode_url(Arena* arena, const TCHAR* url)
{
	// @Future: Right now this function takes a TCHAR but it would probably be more useful if it could take an ANSI or UTF-16
	// string on both the Windows 98/ME and 2000 to 10 builds, which would avoid useless conversions.

	if(url == NULL) return NULL;
	
	const TCHAR* original_url = url;
	char* utf_8_url = push_arena(arena, (string_length(url) + 1) * sizeof(char), char);
	size_t utf_8_index = 0;

	while(*url != T('\0'))
	{
		// Decode percent-encoded characters.
		if(*url == T('%'))
		{
			u8 decoded_char = 0;
			if(convert_hexadecimal_string_to_byte(url + 1, &decoded_char))
			{
				utf_8_url[utf_8_index] = decoded_char;
				++utf_8_index;

				// Skip the two hexadecimal characters.
				url += 2;
			}
			else
			{
				// If the percent sign isn't followed by two characters or if the characters
				// don't represent a valid hexadecimal value.
				log_warning("Decode Url: Found an invalid percent-encoded character while decoding the URL. The remaining encoded URL is '%s'.", url);
				break;
			}
		}
		else if(*url == T('+'))
		{
			utf_8_url[utf_8_index] = ' ';
			++utf_8_index;
		}
		else
		{
			utf_8_url[utf_8_index] = (char) *url;
			++utf_8_index;
		}
		
		++url;
	}

	utf_8_url[utf_8_index] = '\0';
	TCHAR* tchar_url = convert_utf_8_string_to_tchar(arena, utf_8_url);

	if(tchar_url == NULL)
	{
		log_error("Decode Url: Failed to convert the decoded URL from UTF-8 to an ANSI or UTF-16 string: '%s'.", original_url);
		// It's possible that converting the decoded UTF-8 URL to the current active code page on Windows 98/ME is not possible
		// without losing data (since it can't represent every character used in the specific Unicode string).
		//
		// Instead of returning NULL (which would leave an empty URL in the CSV file and would prevent the exporter from using
		// the URL's host and path to create the output directories), we'll return the current UTF-8 data as is if it was an
		// ANSI string. Any byte above 0x7F will be mapped to an incorrect character in the code page.
		//
		// This scenario is unlikely in the Windows 2000 to 10 builds (which use UTF-16), so if it does fail here for whatever
		// reason, we will want to return NULL and make the error obvious.
		//
		// @Future: This is a weird behavior and this function might change in the future.
		#ifdef BUILD_9X
			tchar_url = utf_8_url;
		#endif
	}

	return tchar_url;
}

// Skips the scheme in a URL, including the authority separator if it exists
//
// For example:
// 1. "http://www.example.com" 			-> "www.example.com"
// 2. "HTTP:http://www.example.com" 	-> "http://www.example.com"
// 3. "example" 						-> ""
//
// @Parameters:
// 1. url - The url whose scheme will be skipped.
//
// @Returns: The first character in the URL after the scheme and authority separator. If the URL is NULL, this function returns NULL.
// If the URL doesn't contain a scheme, the end of the string is returned.
TCHAR* skip_url_scheme(TCHAR* url)
{
	if(url == NULL) return NULL;

	while(*url != T('\0'))
	{
		if(*url == T(':'))
		{
			++url;

			if(*url == T('/') && *(url+1) == T('/'))
			{
				url += 2;
			}

			break;
		}
		++url;
	}
	
	return url;
}

// Corrects the path extracted from a URL (host and resource path) so it may be used by certain functions from the Win32 API (like
// CreateDirectory(), CopyFile(), etc) that would otherwise reject invalid paths. This function should only be used in paths from URLs.
//
// This function makes the following changes to the path:
//
// 1. Replaces any forward slashes with backslashes.
// 2. Replaces two consecutive slashes with a single one.
// 3. Replaces any reserved characters (<, >, :, ", |, ?, *) with underscores.
// 4. Replaces characters whose integer representations are in the range from 1 through 31 with underscores.
// 5. If the path ends in a period or space, this character is replaced with an underscore.
// 6. If the path contains a colon followed by a slash in the first component, it will remove the colon character instead of replacing
// it with an underscore.
//
// For example:
//
// 1. "www.example.com/path/file.ext" 		-> "www.example.com\path\file.ext"
// 2. "www.example.com//path//file.ext" 	-> "www.example.com\path\file.ext"
// 3. "www.example.com/<path>/file::ext" 	-> "www.example.com\_path_\file__ext"
// 4. "www.example.com/path/\x20file.ext" 	-> "www.example.com\path\_file.ext"
// 5. "www.example.com/path/file." 			-> "www.example.com\path\file_"
// 6. "C:\Path\File.ext" 					-> "C\Path\File.ext".
//
// See the "Naming Files, Paths, and Namespaces" in the Win32 API Reference.
//
// @Parameters:
// 1. path - The path to modify.
//
// @Returns: Nothing.
void correct_url_path_characters(TCHAR* path)
{
	if(path == NULL) return;

	bool is_first_path_segment = true;
	TCHAR* last_char = NULL;

	while(*path != T('\0'))
	{
		last_char = path;

		switch(*path)
		{
			case(T('/')):
			{
				*path = T('\\');
			} // Intentional fallthrough.

			case(T('\\')):
			{
				is_first_path_segment = false;

				// Remove double backslashes, leaving only one of them.
				// Otherwise, we'd run into ERROR_INVALID_NAME errors in copy_file_using_url_directory_structure().
				TCHAR* next_char = path + 1;
				if( *next_char == T('\\') || *next_char == T('/') )
				{
					MoveMemory(path, next_char, string_size(next_char));
					// Make sure to also replace the forward slash in the next character.
					*path = T('\\');
				}

			} break;

			case(T(':')):
			{
				// Remove the colon from the drive letter in the first segment.
				// Otherwise, replace it with an underscore like the rest of the reserved characters.
				//
				// This is just so the exported directories look nicer. If we didn't do this, we would
				// add an underscore after the drive letter if the URL had an authority segment:
				// - With authority: "res://C:\Path\File.ext" -> "C\Path\File.ext"
				// - Without authority: "ms-itss:C:\Path\File.ext" -> "C_\Path\File.ext"
				// In the first case, the drive letter colon is interpreted as the separator between the
				// host and port.
				TCHAR* next_char = path + 1;
				if( is_first_path_segment && (*next_char == T('\\') || *next_char == T('/')) )
				{
					MoveMemory(path, next_char, string_size(next_char));
				}
				else
				{
					*path = T('_');
				}

			} break;

			case(T('<')):
			case(T('>')):
			case(T('\"')):
			case(T('|')):
			case(T('?')):
			case(T('*')):
			{
				// Replace reserved characters with underscores.
				// Otherwise, we'd run into ERROR_INVALID_NAME errors in copy_file_using_url_directory_structure().
				*path = T('_');
			} break;

			default:
			{
				// Replace characters whose integer representations are in the range from 1 through 31 with underscores.
				if(1 <= *path && *path <= 31)
				{
					*path = T('_');
				}

			} break;
		}

		++path;
	}

	// Replace a trailing period or space with an underscore.
	// Otherwise, we'd run into problems when trying to delete these files or directories.
	if(last_char != NULL && (*last_char == T('.') || *last_char == T(' ')) )
	{
		*last_char = T('_');
	}
}

// Converts the host and path in a URL into a Windows directory path.
// For example: "http://www.example.com:80/path1/path2/file.ext?id=1#top" -> "www.example.com\path1\path2"
//
// @Parameters:
// 1. arena - The Arena structure where any intermediary strings are stored.
// 2. url - The URL to convert into a path.
// 3. result_path - The buffer which receives the converted path. This buffer must be able to hold MAX_PATH_CHARS characters.
//
// @Returns: True if it succeeds. Otherwise, false.
bool convert_url_to_path(Arena* arena, const TCHAR* url, TCHAR* result_path)
{
	bool success = true;

	Url_Parts url_parts = {};
	if(partition_url(arena, url, &url_parts))
	{
		result_path[0] = T('\0');

		if(url_parts.host != NULL)
		{
			success = success && PathAppend(result_path, url_parts.host);
		}

		correct_url_path_characters(url_parts.path);
		success = success && PathAppend(result_path, url_parts.path);

		// Remove the resource's filename so it's not part of the final path.
		// Because of the replacement above, we know that the path separator is a backslash.
		TCHAR* last_separator = _tcsrchr(result_path, T('\\'));
		if(last_separator != NULL) *last_separator = T('\0');

		truncate_path_components(result_path);
		correct_reserved_path_components(result_path);
	}
	else
	{
		success = false;
	}

	return success;
}

// Parses specific headers used by the cache exporters from an HTTP response.
//
// This function assumes that the headers use ASCII as their character encoding.
//
// @Parameters:
// 1. arena -  The Arena structure that receives the various headers' values as strings.
// 2. original_headers - A narrow ASCII string that contains the HTTP headers. This string isn't necessarily null terminated.
// 3. headers_size - The size of the headers string in bytes. This function does nothing if this value is zero.
// 4. result_headers - The structure that receives the parsed HTTP headers. The members of this structure are initialized to NULL.
//
// @Returns: Nothing.
void parse_http_headers(Arena* arena, const char* original_headers, size_t headers_size, Http_Headers* result_headers)
{
	ZeroMemory(result_headers, sizeof(Http_Headers));

	if(headers_size == 0) return;

	// The headers aren't necessarily null terminated.
	char* headers = push_and_copy_to_arena(arena, headers_size + 1, char, original_headers, headers_size);
	char* end_of_string = (char*) advance_bytes(headers, headers_size);
	*end_of_string = '\0';

	String_Array<char>* split_lines = split_string(arena, headers, "\r\n");

	for(int i = 0; i < split_lines->num_strings; ++i)
	{
		char* line = split_lines->strings[i];

		// Keep the first line intact since it's the server's response (e.g. "HTTP/1.1 200 OK"),
		// and not a key-value pair.
		if(i == 0)
		{
			result_headers->response = convert_ansi_string_to_tchar(arena, line);
		}
		// Handle some specific HTTP header response fields (e.g. "Content-Type: text/html",
		// where "Content-Type" is the key, and "text/html" is the value).
		else
		{
			String_Array<char>* split_fields = split_string(arena, line, ":", 1);

			if(split_fields->num_strings >= 2)
			{
				char* key = split_fields->strings[0];
				char* value = split_fields->strings[1];
				value = skip_leading_whitespace(value);

				if(strings_are_equal(key, "server", true))
				{
					result_headers->server = convert_ansi_string_to_tchar(arena, value);
				}
				else if(strings_are_equal(key, "cache-control", true))
				{
					result_headers->cache_control = convert_ansi_string_to_tchar(arena, value);
				}
				else if(strings_are_equal(key, "pragma", true))
				{
					result_headers->pragma = convert_ansi_string_to_tchar(arena, value);
				}
				else if(strings_are_equal(key, "content-type", true))
				{
					result_headers->content_type = convert_ansi_string_to_tchar(arena, value);
				}
				else if(strings_are_equal(key, "content-length", true))
				{
					result_headers->content_length = convert_ansi_string_to_tchar(arena, value);
				}
				else if(strings_are_equal(key, "content-range", true))
				{
					result_headers->content_range = convert_ansi_string_to_tchar(arena, value);
				}
				else if(strings_are_equal(key, "content-encoding", true))
				{
					result_headers->content_encoding = convert_ansi_string_to_tchar(arena, value);
				}
			}
			else
			{
				_ASSERT(!string_is_empty(split_fields->strings[0]));
				log_warning("Parse Http Headers: Could not separate the header line '%hs' into a key and value pair.", split_fields->strings[0]);
			}

		}
	}
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> PATH MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// Compares two filenames. This comparison is case insensitive as it assumes that the current file system is case insensitive.
// This might not always be true (e.g. see the FILE_FLAG_POSIX_SEMANTICS flag in CreateFile()) but it works for our use case.
//
// @Parameters:
// 1. filename_1 - The first filename.
// 2. filename_2 - The second filename.
//
// @Returns: True if the filenames are equal. Otherwise, false.
bool filenames_are_equal(const TCHAR* filename_1, const TCHAR* filename_2)
{
	return strings_are_equal(filename_1, filename_2, true);
}

// Checks if a filename ends with a given suffix (e.g. a file extension). This comparison is case insensitive as it assumes
// that the current file system is case insensitive. See filenames_are_equal().
//
// @Parameters:
// 1. filename - The filename to check.
// 2. suffix - The suffix string to use.
//
// @Returns: True if the filename ends with that suffix. Otherwise, false.
bool filename_ends_with(const TCHAR* filename, const TCHAR* suffix)
{
	return string_ends_with(filename, suffix, true);
}

// Skips to the file extension in a path. This function considers the substring after the last period character
// in a filename to be the file extension. This is useful so we can define what we consider a file extension
// instead of relying on the PathFindExtension() in the Shell API (we might change our own definition in the future).
//
// For example:
// 1. "file.ext" 			-> "ext"
// 2. "file.ext.gz" 		-> "gz"
// 3. "file." 				-> ""
// 4. "file"				-> ""
// 5. "C:\Path\File.ext"	-> "ext"
// 6. "C:\Path.ext\File"	-> ""
//
// @Parameters:
// 1. path - The path to check for a file extension.
// 2. optional_include_period - An optional parameter that specifies whether the file extension should include the
// period character or not. This value defaults to false.
// 3. optional_get_first_extension - An optional parameter that specifies whether the file extension starts at the
// first period character instead of the last. This value defaults to false.
//
// @Returns: The beginning of the file extension in the path. If the path is NULL, this function returns NULL.
// If the filename doesn't contain a file extension, the end of the string is returned.
TCHAR* skip_to_file_extension(TCHAR* path, bool optional_include_period, bool optional_get_first_extension)
{
	TCHAR* file_extension = NULL;

	if(path != NULL)
	{
		while(*path != T('\0'))
		{
			if( *path == T('.') && ( optional_include_period || *(path+1) != T('\0') ) )
			{
				file_extension = (optional_include_period) ? (path) : (path + 1);
				if(optional_get_first_extension) break;
			}
			else if(*path == T('\\'))
			{
				file_extension = NULL;
			}

			++path;
		}

		if(file_extension == NULL)
		{
			file_extension = path;
		}
	}

	return file_extension;
}

// Skips to the beginning of a substring in a path that contains a given number of components, where each one is delimited by a backslash.
// This search is done by starting at the end of the path and going backwards.
//
// This function is used to get a shortened version of the full path of a cached file, where only the file's name and one or two
// directories are relevant. If you just want to find the filename in a path (the number of components is one), use PathFindFileName()
// instead.
//
// An example where the path is "C:\DirectoryA\DirectoryB\File.ext", and the number of components is:
// 1 -> File.ext
// 2 -> DirectoryB\File.ext
// 3 -> DirectoryA\DirectoryB\File.ext
// 4 -> C:\DirectoryA\DirectoryB\File.ext
// 5 -> C:\DirectoryA\DirectoryB\File.ext
//
// @Parameters:
// 1. path - The path to be searched for components.
// 2. desired_num_components - The number of components to find. This value must be greater than zero.
//
// @Returns: The beginning of the substring in the path that contains the components. If the path is NULL, this function returns NULL.
TCHAR* skip_to_last_path_components(TCHAR* path, int desired_num_components)
{
	_ASSERT(desired_num_components > 0);

	if(path == NULL) return NULL;

	size_t num_chars = string_length(path);
	int current_num_components = 0;
	TCHAR* components_begin = path;
	
	// Traverse the path backwards.
	for(size_t i = num_chars; i-- > 0;)
	{
		if(path[i] == T('\\'))
		{
			++current_num_components;
			if(current_num_components == desired_num_components)
			{
				components_begin = path + i + 1;
				break;
			}
		}
	}

	return components_begin;
}

// Counts the number of components in a path. For example, the path "C:\DirectoryA\DirectoryB\File.ext" has four components, where
// each one is delimited by a backslash.
//
// @Parameters:
// 1. path - The path to be searched for components.
//
// @Returns: The number of components. If the path is NULL or empty, this function returns zero.
int count_path_components(const TCHAR* path)
{
	int count = 0;

	if(path != NULL && !string_is_empty(path))
	{
		++count;

		while(*path != T('\0'))
		{
			if(*path == T('\\'))
			{
				++count;
			}

			++path;
		}
	}

	return count;
}

// Retrieves a path component at a given index. For example, the path "C:\DirectoryA\DirectoryB\File.ext" has four components, where
// the following indexes can be used:
//
// [0] or [-4] -> C:
// [1] or [-3] -> DirectoryA
// [2] or [-2] -> DirectoryB
// [3] or [-1] -> File.ext
//
// @Parameters:
// 1. arena - The Arena structure that will receive the resulting component.
// 2. path - The path to be searched for components.
// 3. component_index - The index of the component to retrieve. If this value is negative, the component is retrieved starting at the
// end of the path. For negatives values, the index is one-based instead of zero-based.
//
// @Returns: The component if a valid one exists in the path at that index. Otherwise, this function returns NULL.
TCHAR* find_path_component(Arena* arena, const TCHAR* path, int component_index)
{
	TCHAR* result = NULL;

	String_Array<TCHAR>* split_path = split_string(arena, path, T("\\"));

	if(component_index < 0)
	{
		component_index = -component_index;
		component_index = split_path->num_strings - component_index;
	}

	if(0 <= component_index && component_index < split_path->num_strings)
	{
		result = push_string_to_arena(arena, split_path->strings[component_index]);
	}

	return result;
}

// Retrieves the absolute version of a specified path. The path may be relative or absolute. This function has two overloads: 
// - get_full_path_name(2 or 3), which copies the output to another buffer.
// - get_full_path_name(1), which copies the output to the same buffer.
//
// @GetLastError
//
// @Parameters:
// 1. path - The specified path to make absolute.
// 2. result_full_path - The buffer which receives the absolute path.
// 3. optional_num_buffer_chars - An optional parameter that specifies the size of the resulting buffer in characters.
// If this value isn't specified, the function assumes MAX_PATH_CHARS characters.
//
// OR
//
// @Parameters:
// 1. result_full_path - The buffer with the specified path which then receives the absolute path. This function overload
// assumes that this resulting buffer is able to hold MAX_PATH_CHARS characters.
//
// @Returns: True if it succeeds. Otherwise, false.
bool get_full_path_name(const TCHAR* path, TCHAR* result_full_path, u32 optional_num_result_path_chars)
{
	return GetFullPathName(path, optional_num_result_path_chars, result_full_path, NULL) != 0;
}

bool get_full_path_name(TCHAR* result_full_path)
{
	TCHAR full_path[MAX_PATH_CHARS] = T("");
	bool success = get_full_path_name(result_full_path, full_path);
	if(success) StringCchCopy(result_full_path, MAX_PATH_CHARS, full_path);
	return success;
}

// Retrieves the absolute path of a special folder, identified by its CSIDL (constant special item ID list).
//
// See this page for a list of possible CSIDLs: https://docs.microsoft.com/en-us/windows/win32/shell/csidl
// Note that each different target build has a version requirement for these CSIDLs:
// - Windows 98/ME: version 4.72 or older.
// - Windows 2000 through 10: version 5.0 or older.
// E.g. This function fails if you use CSIDL_LOCAL_APPDATA (available starting with version 5.0) in the Windows 98/ME builds.
//
// @GetLastError
//
// @Parameters:
// 1. csidl - The CSIDL that identifies the special folder.
// 2. result_path - The buffer which receives the absolute path to the special folder. This buffer must be at least MAX_PATH_CHARS
// characters in size.
//
// @Returns: True if it succeeds. Otherwise, false.
bool get_special_folder_path(int csidl, TCHAR* result_path)
{
	#ifdef BUILD_9X
		return SHGetSpecialFolderPathA(NULL, result_path, csidl, FALSE) != FALSE;
	#else
		// @Note: Third parameter (hToken): "Microsoft Windows 2000 and earlier: Always set this parameter to NULL."
		return SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, result_path));
	#endif
}

// Truncates each component in a path to the maximum component length supported by the current file system.
// For example, assuming that this limit is 255 characters:
// "C:\Path\<255 Characters>ABC\RemainingPath" -> "C:\Path\<255 Characters>\RemainingPath"
//
// @Parameters:
// 1. path - The path to modify.
//
// @Returns: Nothing.
static DWORD GLOBAL_MAXIMUM_COMPONENT_LENGTH = 0;
void truncate_path_components(TCHAR* path)
{
	TCHAR* component_begin = path;
	bool is_first_char = true;

	if(GLOBAL_MAXIMUM_COMPONENT_LENGTH == 0)
	{
		if(GetVolumeInformationA(NULL, NULL, 0, NULL, &GLOBAL_MAXIMUM_COMPONENT_LENGTH, NULL, NULL, 0) == FALSE)
		{
			GLOBAL_MAXIMUM_COMPONENT_LENGTH = 255;
			log_warning("Truncate Path Components: Failed to get the maximum component length with the error code %lu. This value will be set to %lu.", GetLastError(), GLOBAL_MAXIMUM_COMPONENT_LENGTH);	
		}
	}

	while(true, true)
	{
		bool is_end_of_string = (*path == T('\0'));

		if( (*path == T('\\') || is_end_of_string) && !is_first_char )
		{
			TCHAR* component_end = path;

			TCHAR previous_char = *component_end;
			*component_end = T('\0');
			size_t num_component_chars = string_length(component_begin);
			*component_end = previous_char;

			component_begin = component_end + 1;

			if(num_component_chars > GLOBAL_MAXIMUM_COMPONENT_LENGTH)
			{
				// "C:\Path\<255 chars>123\ABC" -> "C:\Path\<255 chars>\ABC"
				//          ^ begin       ^ end
				// "C:\Path\ABC\<255 chars>123" -> "C:\Path\ABC\<255 chars>"
				//              ^ begin       ^ end
				size_t num_chars_over_limit = num_component_chars - GLOBAL_MAXIMUM_COMPONENT_LENGTH;
				MoveMemory(component_end - num_chars_over_limit, component_end, string_size(component_end));
				component_begin -= num_chars_over_limit;
			}
		}

		++path;
		is_first_char = false;
		if(is_end_of_string) break;
	}
}

// Called by bsearch() to search the reserved filename array.
static int compare_reserved_names(const void* name_pointer, const void* reserved_name_pointer)
{
	TCHAR* name = *((TCHAR**) name_pointer);
	const TCHAR* reserved_name = *((TCHAR**) reserved_name_pointer);

	// Temporarily remove the file extension.
	TCHAR* file_extension = skip_to_file_extension(name, true, true);
	TCHAR previous_char = *file_extension;
	*file_extension = T('\0');

	int result = _tcsicmp(name, reserved_name);

	*file_extension = previous_char;

	return result;
}

// Corrects any component in a path that uses a reserved name (or a reserved name followed immediately by a file extension) so it may
// be used by certain functions from the Win32 API (like CreateDirectory(), CopyFile(), etc) that would otherwise reject invalid paths.
// This correction is done by replacing the first character with an underscore. The comparison between the component and reserved name
// is case insensitive.
//
// For example, consider the reserved names AUX, NUL, and CON:
//
// - "C:\Path\File.ext" -> "C:\Path\File.ext"
// - "C:\Path\AUX" -> "C:\Path\_UX"
// - "C:\NUL\nul.ext" -> "C:\_UL\_ul.ext"
// - "C:\Path\CONSOLE" -> "C:\Path\CONSOLE"
//
// See the "Naming Files, Paths, and Namespaces" in the Win32 API Reference.
//
// @Parameters:
// 1. path - The path to modify.
//
// @Returns: Nothing.
void correct_reserved_path_components(TCHAR* path)
{
	const TCHAR* const SORTED_RESERVED_NAMES[] =
	{
		T("AUX"),
		T("COM1"), T("COM2"), T("COM3"), T("COM4"), T("COM5"), T("COM6"), T("COM7"), T("COM8"), T("COM9"),
		T("CON"),
		T("LPT1"), T("LPT2"), T("LPT3"), T("LPT4"), T("LPT5"), T("LPT6"), T("LPT7"), T("LPT8"), T("LPT9"),
		T("NUL"),
		T("PRN")
	};
	const size_t NUM_RESERVED_NAMES = _countof(SORTED_RESERVED_NAMES);

	TCHAR* component_begin = path;
	while(true, true)
	{
		bool is_end_of_string = (*path == T('\0'));

		if((*path == T('\\') || is_end_of_string))
		{
			TCHAR previous_char = *path;
			*path = T('\0');

			void* search_result = bsearch(	&component_begin, SORTED_RESERVED_NAMES,
											NUM_RESERVED_NAMES, sizeof(TCHAR*),
											compare_reserved_names);
			if(search_result != NULL)
			{
				log_warning("Correct Reserved Path Components: Found a path component that uses a reserved name: '%s'.", component_begin);
				*component_begin = T('_');
			}

			*path = previous_char;
			component_begin = path + 1;
		}

		++path;
		if(is_end_of_string) break;
	}
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> FILE I/O
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// Creates or opens a file or I/O device. This function is essentially a convenience wrapper for using CreateFile() in Windows 98 to Windows 10.
//
// @GetLastError
//
// @Parameters: See CreateFile() in the Win32 API Reference:
// - https://web.archive.org/web/20030210222137/http://msdn.microsoft.com/library/en-us/fileio/base/createfile.asp
// - https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea
//
// @Returns: See CreateFile() in the Win32 API Reference.
HANDLE create_handle(const TCHAR* path, DWORD desired_access, DWORD shared_mode, DWORD creation_disposition, DWORD flags_and_attributes)
{
	// @Docs: CreateFile - Win32 API Reference.
	//
	// - "Windows 95/98/Me: You cannot open a directory, physical disk, or volume using CreateFile."
	// - "FILE_SHARE_DELETE - Windows 95/98/Me: This flag is not supported."
	// - "FILE_FLAG_BACKUP_SEMANTICS - Windows 95/98/Me: This flag is not supported."
	// - "Windows 95/98/Me: The hTemplateFile parameter must be NULL. If you supply a handle, the call fails and GetLastError returns ERROR_NOT_SUPPORTED."

	#ifdef BUILD_9X
		// Remove the unsupported flags for Windows 98/ME so CreateFile() doesn't fail with the error
		// code 87 (ERROR_INVALID_PARAMETER).
		shared_mode &= ~FILE_SHARE_DELETE;
		flags_and_attributes &= ~FILE_FLAG_BACKUP_SEMANTICS;
	#endif

	return CreateFile(path, desired_access, shared_mode, NULL, creation_disposition, flags_and_attributes, NULL);
}

// Creates a handle for a directory.
//
// @GetLastError
//
// @Compatibility: Windows 2000 to 10 only.
//
// @Parameters: See create_handle().
// 
// @Returns: See create_handle().
#ifndef BUILD_9X
	HANDLE create_directory_handle(const wchar_t* path, DWORD desired_access, DWORD shared_mode, DWORD creation_disposition, DWORD flags_and_attributes)
	{
		flags_and_attributes |= FILE_FLAG_BACKUP_SEMANTICS;
		return create_handle(path, desired_access, shared_mode, creation_disposition, flags_and_attributes);
	}
#endif

// Checks if two handles refer to the same file or directory.
//
// @Parameters:
// 1. handle_1 - The first handle.
// 2. handle_2 - The second handle.
// 
// @Returns: True if the handles refer to the same file or directory. Otherwise, false. This function also returns false if it's unable
// to retrieve the handles' information, or if either of the handles is invalid.
bool do_handles_refer_to_the_same_file_or_directory(HANDLE handle_1, HANDLE handle_2)
{
	if(handle_1 == INVALID_HANDLE_VALUE || handle_2 == INVALID_HANDLE_VALUE) return false;

	BY_HANDLE_FILE_INFORMATION handle_1_info = {};
	BY_HANDLE_FILE_INFORMATION handle_2_info = {};

	// @Docs: "The identifier (low and high parts) and the volume serial number uniquely identify a file on a single computer.
	// To determine whether two open handles represent the same file, combine the identifier and the volume serial number for
	// each file and compare them." - Win32 API reference.
	if(		GetFileInformationByHandle(handle_1, &handle_1_info) != 0
		&& 	GetFileInformationByHandle(handle_2, &handle_2_info) != 0)
	{
		return 		handle_1_info.nFileIndexHigh == handle_2_info.nFileIndexHigh
				&&	handle_1_info.nFileIndexLow == handle_2_info.nFileIndexLow
				&&	handle_1_info.dwVolumeSerialNumber == handle_2_info.dwVolumeSerialNumber;
	}

	return false;
}

// Checks if two paths refer to the same directory. This function is not very robust for Windows 98 and ME.
//
// For example, the following paths refer to the same directory: "C:\Program Files\Common Files" and "C:\PROGRA~1\..\PROGRA~1\.\COMMON~1".
//
// @Returns: True if the paths refer to the same directory. Otherwise, false. This function also returns false if a directory doesn't exist.
bool do_paths_refer_to_the_same_directory(const TCHAR* path_1, const TCHAR* path_2)
{
	#ifdef BUILD_9X
		char path_to_compare_1[MAX_PATH_CHARS] = "";
		char path_to_compare_2[MAX_PATH_CHARS] = "";
		
		// Remove the "." and ".." characters, convert any short paths to their long versions,
		// and perform a case insensitive string comparison. This is meant to make this check
		// more robust, but it's not a good approach...
		return 	does_directory_exist(path_1)
				&& does_directory_exist(path_2)
				&& (PathCanonicalizeA(path_to_compare_1, path_1) != FALSE)
				&& (PathCanonicalizeA(path_to_compare_2, path_2) != FALSE)
				&& (GetLongPathNameA(path_to_compare_1, path_to_compare_1, MAX_PATH_CHARS) != 0)
				&& (GetLongPathNameA(path_to_compare_2, path_to_compare_2, MAX_PATH_CHARS) != 0)
				&& filenames_are_equal(path_to_compare_1, path_to_compare_2);
	#else
		HANDLE handle_1 = create_directory_handle(path_1, 0, FILE_SHARE_READ, OPEN_EXISTING, 0);
		HANDLE handle_2 = create_directory_handle(path_2, 0, FILE_SHARE_READ, OPEN_EXISTING, 0);
		bool result = do_handles_refer_to_the_same_file_or_directory(handle_1, handle_2);
		safe_close_handle(&handle_1);
		safe_close_handle(&handle_2);
		return result;
	#endif
}

// Determines whether or not a file exists given its path.
//
// @Parameters:
// 1. file_path - The path to the file.
//
// @Returns: True if the file exists. Otherwise, false. This function returns false if the path points to a directory.
// This function fails if the path is empty or if the file's attributes cannot be determined.
bool does_file_exist(const TCHAR* file_path)
{
	if(file_path == NULL || string_is_empty(file_path)) return false;

	DWORD attributes = GetFileAttributes(file_path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

// Determines whether or not a directory exists given its path.
//
// @Parameters:
// 1. file_path - The path to the directory.
//
// @Returns: True if the directory exists. Otherwise, false. This function returns false if the path points to a file.
// This function fails if the path is empty or if the directory's attributes cannot be determined.
bool does_directory_exist(const TCHAR* directory_path)
{
	if(directory_path == NULL || string_is_empty(directory_path)) return false;

	DWORD attributes = GetFileAttributes(directory_path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

// Determines the size in bytes of a file from its handle.
//
// @GetLastError
//
// @Parameters:
// 1. file_handle - The handle of the file whose size is of interest.
// 2. result_file_size - The resulting file size in bytes.
//
// @Returns: True if the function succeeds. Otherwise, it returns false and the file size is not modified.
bool get_file_size(HANDLE file_handle, u64* result_file_size)
{
	*result_file_size = 0;
	#ifdef BUILD_9X
		DWORD file_size_high = 0;
		DWORD file_size_low = GetFileSize(file_handle, &file_size_high);
		bool success = !( (file_size_low == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR) );
		if(success) *result_file_size = combine_high_and_low_u32s_into_u64(file_size_high, file_size_low);
		else log_error("Get File Size: Failed to get the file size with the error code %lu.", GetLastError());
		return success;
	#else
		LARGE_INTEGER file_size = {};
		bool success = GetFileSizeEx(file_handle, &file_size) != FALSE;
		if(success) *result_file_size = file_size.QuadPart;
		else log_error("Get File Size: Failed to get the file size with the error code %lu.", GetLastError());
		return success;
	#endif
}

// Determines the size in bytes of a file from its path.
//
// @GetLastError
//
// @Parameters:
// 1. file_path - The path to the file.
// 2. result_file_size - The resulting file size in bytes.
//
// @Returns: True if the function succeeds. Otherwise, it returns false and the file size is not modified.
bool get_file_size(const TCHAR* file_path, u64* result_file_size)
{
	*result_file_size = 0;
	if(file_path == NULL) return false;

	bool success = false;

	// @TemporaryFiles: Used by temporary files, meaning it must share reading, writing, and deletion.
	HANDLE file_handle = create_handle(file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0);

	if(file_handle != INVALID_HANDLE_VALUE)
	{
		success = get_file_size(file_handle, result_file_size);
		safe_close_handle(&file_handle);
	}
	else
	{
		log_error("Get File Size: Failed to get the file handle for '%s' with the error code %lu.", file_path, GetLastError());
	}

	return success;
}

// Closes a handle and sets its value to INVALID_HANDLE_VALUE.
//
// @Parameters:
// 1. handle - The address of the handle to close.
//
// @Returns: Nothing.
void safe_close_handle(HANDLE* handle)
{
	DWORD previous_error_code = GetLastError();
	if(*handle != INVALID_HANDLE_VALUE && *handle != NULL)
	{
		CloseHandle(*handle);
	}

	*handle = INVALID_HANDLE_VALUE;
	SetLastError(previous_error_code);
}

// Closes a file search handle and sets its value to INVALID_HANDLE_VALUE.
//
// @Parameters:
// 1. search_handle - The address of the handle to close.
//
// @Returns: Nothing.
void safe_find_close(HANDLE* search_handle)
{
	DWORD previous_error_code = GetLastError();
	if(*search_handle != INVALID_HANDLE_VALUE && *search_handle != NULL)
	{
		FindClose(*search_handle);
	}

	*search_handle = INVALID_HANDLE_VALUE;
	SetLastError(previous_error_code);
}

// Unmaps a mapped view of a file and sets its value to NULL.
//
// @Parameters:
// 1. base_address - The address of the view to unmap.
//
// @Returns: Nothing.
void safe_unmap_view_of_file(void** base_address)
{
	if(*base_address != NULL)
	{
		DWORD previous_error_code = GetLastError();
		UnmapViewOfFile(*base_address);
		*base_address = NULL;
		#ifdef BUILD_DEBUG
			DEBUG_MEMORY_MAPPING_BASE_ADDRESS = retreat_bytes(DEBUG_MEMORY_MAPPING_BASE_ADDRESS, DEBUG_BASE_ADDRESS_INCREMENT);
		#endif
		SetLastError(previous_error_code);
	}
	else
	{
		log_warning("Safe Unmap View Of File: Attempted to unmap an invalid address.");
	}
}

// Traverses the objects (files and directories) inside a directory, and optionally its subdirectories, given a search query for the
// filename. When each desired file or directory is found, a given callback function is called.
//
// This function does not guarantee the order in which the files and directories are found.
//
// @Parameters:
// 1. path - The path to the directory whose files and subdirectories will be visited.
// 2. search_query - The file or directory's name to find. This name can include wildcard characters like an asterisk "*"" or a question
// mark "?". To find every file and directory, use "*".
// 3. traversal_flags - Defines which type of objects (files and/or directories) should be visited. This value should specify:
// - TRAVERSE_FILES, to visit files.
// - TRAVERSE_DIRECTORIES, to visit directories.
// - TRAVERSE_FILES | TRAVERSE_DIRECTORIES, to visit both files and directories.
// 4. should_traverse_subdirectories - True if subdirectories should be traversed too. Otherwise, false. The previous 'search_query'
// still applies.
// 5. callback_function - The callback function that is called every time a relevant file or directory is found. Whether or not this
// function is called for a given object depends on the 'traversal_flags'.
// 6. user_data - A pointer to any additional data that should be passed to the callback function. This parameter may be NULL.
//
// The callback function can be defined using the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @CallbackParameters:
// 1. callback_info - A pointer to a structure containing various information about the object (name, path, properties, user data).
//
// @CallbackReturns: True to keep traversing, or false to stop searching.
//
// @Returns: Nothing.
void traverse_directory_objects(const TCHAR* directory_path, const TCHAR* search_query,
								u32 traversal_flags, bool should_traverse_subdirectories,
								Traverse_Directory_Callback* callback_function, void* user_data)
{
	if(string_is_empty(directory_path)) return;

	bool should_continue_traversing = true;

	/*
		>>>> Traverse the files and directories that match the search query.
	*/

	TCHAR search_path[MAX_PATH_CHARS] = T("");
	PathCombine(search_path, directory_path, search_query);

	WIN32_FIND_DATA find_data = {};
	HANDLE search_handle = FindFirstFile(search_path, &find_data);
	
	bool found_object = (search_handle != INVALID_HANDLE_VALUE);
	while(found_object)
	{
		TCHAR* filename = find_data.cFileName;
		if(!strings_are_equal(filename, T(".")) && !strings_are_equal(filename, T("..")))
		{
			bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			bool should_process_object = 	( (traversal_flags & TRAVERSE_FILES) && !is_directory )
										|| 	( (traversal_flags & TRAVERSE_DIRECTORIES) && is_directory );

			if(should_process_object)
			{
				TCHAR full_path[MAX_PATH_CHARS] = T("");
				PathCombine(full_path, directory_path, filename);

				Traversal_Object_Info info = {};
				
				info.directory_path = directory_path;
				info.object_name = filename;
				info.object_path = full_path;

				info.object_size = combine_high_and_low_u32s_into_u64(find_data.nFileSizeHigh, find_data.nFileSizeLow);
				info.is_directory = is_directory;

				info.creation_time = find_data.ftCreationTime;
				info.last_access_time = find_data.ftLastAccessTime;
				info.last_write_time = find_data.ftLastWriteTime;

				info.user_data = user_data;

				should_continue_traversing = callback_function(&info);
				if(!should_continue_traversing) break;
			}
		}

		found_object = FindNextFile(search_handle, &find_data) != FALSE;
	}

	safe_find_close(&search_handle);

	/*
		>>>> Traverse every subdirectory in this directory. We can't use the same search query here, otherwise we'd exclude directories.
	*/

	if(should_traverse_subdirectories && should_continue_traversing)
	{
		PathCombine(search_path, directory_path, ALL_OBJECTS_SEARCH_QUERY);
		search_handle = FindFirstFile(search_path, &find_data);
		
		found_object = search_handle != INVALID_HANDLE_VALUE;
		while(found_object)
		{
			TCHAR* filename = find_data.cFileName;
			if(!strings_are_equal(filename, T(".")) && !strings_are_equal(filename, T("..")))
			{
				bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				if(is_directory)
				{
					TCHAR subdirectory_path[MAX_PATH_CHARS] = T("");
					PathCombine(subdirectory_path, directory_path, filename);

					traverse_directory_objects(	subdirectory_path, search_query,
												traversal_flags, should_traverse_subdirectories,
												callback_function, user_data);
				}			
			}

			found_object = FindNextFile(search_handle, &find_data) != FALSE;
		}

		safe_find_close(&search_handle);		
	}
}

// Helper structure to pass some values to and from count_objects_callback() and find_objects_callback().
struct Find_Objects_Params
{
	int expected_num_objects;

	Arena* arena;
	int current_num_objects;
	Traversal_Result* result;
};

// Called every time an object is found during traversal. Used to count the number of objects and preallocate the traversal result array.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True.
static TRAVERSE_DIRECTORY_CALLBACK(count_objects_callback)
{
	Find_Objects_Params* params = (Find_Objects_Params*) callback_info->user_data;
	++(params->expected_num_objects);
	return true;
}

// Called every time an object is found during traversal. Used to fill the members of each element in the traversal result array.
//
// @Parameters: See the TRAVERSE_DIRECTORY_CALLBACK macro.
//
// @Returns: True if the callback still expects to process more objects. Otherwise, false. 
static TRAVERSE_DIRECTORY_CALLBACK(find_objects_callback)
{
	Find_Objects_Params* params = (Find_Objects_Params*) callback_info->user_data;
	++(params->current_num_objects);

	if(params->current_num_objects > params->expected_num_objects) return false;

	Arena* arena = params->arena;
	Traversal_Result* result = params->result;

	Traversal_Object_Info* object_info = &(result->object_info[result->num_objects]);
	++(result->num_objects);

	*object_info = *callback_info;
	object_info->directory_path = push_string_to_arena(arena, callback_info->directory_path);
	object_info->object_name = push_string_to_arena(arena, callback_info->object_name);
	object_info->object_path = push_string_to_arena(arena, callback_info->object_path);
	object_info->user_data = NULL;

	return true;
}

// Traverses the objects (files and directories) inside a directory, and optionally its subdirectories, given a search query for the
// filename. Returns an array of every object found instead of calling a callback function.
//
// @Parameters: This function takes most of the same parameters as traverse_directory_objects(), except the callback function and user
// data. It also takes the following one:
// 1. arena - The Arena structure that will receive the resulting array of objects found.
//
// @Returns: An array with information about each object.
Traversal_Result* find_objects_in_directory(Arena* arena, const TCHAR* directory_path, const TCHAR* search_query,
											u32 traversal_flags, bool should_traverse_subdirectories)
{
	Find_Objects_Params params = {};
	traverse_directory_objects(	directory_path, search_query,
								traversal_flags, should_traverse_subdirectories,
								count_objects_callback, &params);

	size_t result_size = sizeof(Traversal_Result) + MAX(params.expected_num_objects - 1, 0) * sizeof(Traversal_Object_Info);
	Traversal_Result* result = push_arena(arena, result_size, Traversal_Result);
	result->num_objects = 0;

	params.arena = arena;
	params.result = result;
	traverse_directory_objects(	directory_path, search_query,
								traversal_flags, should_traverse_subdirectories,
								find_objects_callback, &params);

	if(result->num_objects != params.expected_num_objects)
	{
		log_warning("Find Objects In Directory: Found %d objects when %d were expected in the directory '%s'.", result->num_objects, params.expected_num_objects, directory_path);
	}

	return result;
}

// Creates a directory given its path, and any intermediate directories that don't exist.
//
// This function was created to replace SHCreateDirectoryEx() from the Shell API since it was only available from version 5.0
// onwards (Windows 2000 or later).
//
// @Parameters:
// 1. path_to_create - The path of the directory to create.
//
// 2. optional_resolve_file_naming_collisions - An optional parameter that tells the function to resolve any potential naming
// collisions with already existing files (but not other directories). This value defaults to false. A collision is resolved
// by adding a tilde followed by a number. For example, if we want to create "C:\Path\To\Dir" but a file called "To" already
// exists in "C:\Path", then the directory "C:\Path\To~1\Dir" is created instead. If "To" was a directory instead of a file,
// nothing would be done.
//
// 3. optional_result_path - An optional parameter that receives the path of the created directory. This value defaults to NULL,
// meaning it will be ignored. If this function succeedes and the previous parameter is false, this is the same as the fully
// qualified version the 'path_to_create' parameter. If that parameter is true, the resulting path may be different if a file
// naming collision had to be resolved.
//
// @Returns: True if the directory exists. Otherwise, false. If the fully qualified version of the path cannot be determined,
// this function returns false.
bool create_directories(const TCHAR* path_to_create, bool optional_resolve_file_naming_collisions, TCHAR* optional_result_path)
{
	if(optional_result_path != NULL) *optional_result_path = T('\0');

	// Default string length limit for the ANSI version of CreateDirectory().
	const size_t MAX_SHORT_FILENAME_CHARS = 12;
	const size_t MAX_CREATE_DIRECTORY_PATH_CHARS = MAX_PATH_CHARS - MAX_SHORT_FILENAME_CHARS;
	_STATIC_ASSERT(MAX_CREATE_DIRECTORY_PATH_CHARS <= MAX_PATH_CHARS);

	TCHAR path[MAX_CREATE_DIRECTORY_PATH_CHARS] = T("");

	if(!get_full_path_name(path_to_create, path, MAX_CREATE_DIRECTORY_PATH_CHARS))
	{
		log_error("Create Directory: Failed to create the directory in '%s' because its fully qualified path could not be determined with the error code %lu.", path_to_create, GetLastError());
		return false;
	}

	bool collision_resolution_success = true;
	size_t num_path_chars = string_length(path) + 1;
	for(size_t i = 0; i < num_path_chars; ++i)
	{
		if(path[i] == T('\\') || path[i] == T('\0'))
		{
			// Make sure we create any intermediate directories by truncating the string at each path separator.
			// We do it this way since CreateDirectory() fails if a single intermediate directory doesn't exist.
			// If this separator is the null terminator, this corresponds to creating the last directory in the path.
			bool last_directory = (path[i] == T('\0'));
			TCHAR previous_char = path[i];
			path[i] = T('\0');
			
			bool create_success = CreateDirectory(path, NULL) != FALSE;
			
			if(optional_resolve_file_naming_collisions)
			{
				u32 num_naming_collisions = 0;
				TCHAR unique_id[MAX_INT32_CHARS + 1] = T("~");
				TCHAR unique_path[MAX_CREATE_DIRECTORY_PATH_CHARS] = T("");

				while(!create_success && GetLastError() == ERROR_ALREADY_EXISTS && does_file_exist(path))
				{
					++num_naming_collisions;
					log_info("Create Directory: Resolving file naming collision number %I32u for the directory in '%s'.", num_naming_collisions, path);

					if(num_naming_collisions == 0)
					{
						log_error("Create Directory: Wrapped around the number of naming collisions for the directory '%s'. This directory will not be created.", path);
						collision_resolution_success = false;
						break;
					}

					bool naming_success = 	SUCCEEDED(StringCchCopy(unique_path, MAX_CREATE_DIRECTORY_PATH_CHARS, path))
											&& convert_u32_to_string(num_naming_collisions, unique_id + 1)
											&& SUCCEEDED(StringCchCat(unique_path, MAX_CREATE_DIRECTORY_PATH_CHARS, unique_id));

					if(!naming_success)
					{
						log_error("Create Directory: Failed to resolve the file naming collision %I32u for the directory '%s'. This directory will not be created.", num_naming_collisions, path);
						collision_resolution_success = false;
						break;
					}

					// Not being able to create a directory because another one already exists is acceptable.
					// We only want to resolve naming collisions with files.
					create_success = (CreateDirectory(unique_path, NULL) != FALSE ) || does_directory_exist(unique_path);

					if(create_success)
					{
						// Put back the rest of the path after the unique identifier we added to resolve the naming collision.
						if(!last_directory)
						{
							StringCchPrintf(unique_path, MAX_CREATE_DIRECTORY_PATH_CHARS, T("%s%c%s"), unique_path, previous_char, path + i + 1);
						}
						// Update the current path we're iterating over while taking into account the characters we inserted.
						StringCchCopy(path, MAX_CREATE_DIRECTORY_PATH_CHARS, unique_path);
						i += string_length(unique_id);
					}
					else
					{
						log_error("Create Directory: Failed to create the directory while trying to resolve the file name collision for '%s' with the error code %lu.", path, GetLastError());
						collision_resolution_success = false;
						break;
					}
				}
			}

			path[i] = previous_char;
			if(!collision_resolution_success) break;
		}
	}

	if(optional_result_path != NULL) StringCchCopy(optional_result_path, MAX_PATH_CHARS, path);

	return collision_resolution_success && does_directory_exist(path);
}

// Deletes a directory and all the files and subdirectories inside it.
//
// @Parameters:
// 1. directory_path - The path to the directory to delete.
// 
// @Returns: True if the directory was deleted successfully. Otherwise, false.
bool delete_directory_and_contents(const TCHAR* directory_path)
{
	if(string_is_empty(directory_path))
	{
		log_error("Delete Directory: Failed to delete the directory since its path was empty.");
		return false;
	}

	// Ensure that we have the fully qualified path since its required by SHFileOperation().
	TCHAR path_to_delete[MAX_PATH_CHARS + 1] = T("");
	if(!get_full_path_name(directory_path, path_to_delete))
	{
		log_error("Delete Directory: Failed to delete the directory '%s' since its fully qualified path could not be determined with the error code %lu.", directory_path, GetLastError());
		return false;
	}

	// Ensure that the path has two null terminators since its required by SHFileOperation().
	// Remember that MAX_PATH_CHARS already includes the first null terminator.
	size_t num_path_chars = string_length(path_to_delete);
	path_to_delete[num_path_chars + 1] = T('\0');

	SHFILEOPSTRUCT file_operation = {};
	file_operation.wFunc = FO_DELETE;
	file_operation.pFrom = path_to_delete;
	// Perform the operation silently, presenting no UI to the user.
	file_operation.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;

	int error_code = SHFileOperation(&file_operation);
	if(error_code != 0)
	{
		log_error("Delete Directory: Failed to delete the directory '%s' and its contents with error code %d.", directory_path, error_code);
	}

	return error_code == 0;
}

// Creates a directory with a unique name and returns its path.
//
// @Parameters:
// 1. base_temporary_path - The base path where the directory will be created.
// 2. result_directory_path - The resulting directory's path.
// 
// @Returns: True if the directory was created successfully. Otherwise, false.
bool create_temporary_directory(const TCHAR* base_temporary_path, TCHAR* result_directory_path)
{	
	*result_directory_path = T('\0');

	bool create_success = false;
	// We'll use this value to generate the directory's name since GetTempFileName() creates a file if we let it
	// generate the name. This isn't a robust way to generate unique names, but it works for our use case.
	u32 unique_id = GetTickCount();
	do
	{
		create_success = (GetTempFileName(base_temporary_path, TEMPORARY_NAME_PREFIX, unique_id, result_directory_path) != 0)
					  && (CreateDirectory(result_directory_path, NULL) != FALSE);
		++unique_id;
	} while(!create_success && GetLastError() == ERROR_ALREADY_EXISTS);

	if(!create_success) log_error("Create Temporary Directory: Failed to create a directory in '%s' with error code %lu.", base_temporary_path, GetLastError());

	return create_success;
}

// Creates an empty file with normal attributes.
//
// @Parameters:
// 1. file_path - The path where the file will be created.
// 2. overwrite - If this value is true, the function will overwrite any existing file with the same name. If it's false,
// the function will fail if the file already exists. In this case, GetLastError() returns ERROR_FILE_EXISTS.
//
// @Returns: True if the file was created successfully. Otherwise, false.
bool create_empty_file(const TCHAR* file_path, bool overwrite)
{
	DWORD creation_disposition = (overwrite) ? (CREATE_ALWAYS) : (CREATE_NEW);
	HANDLE file_handle = create_handle(file_path, 0, 0, creation_disposition, FILE_ATTRIBUTE_NORMAL);
	DWORD error_code = GetLastError();
	
	bool success = file_handle != INVALID_HANDLE_VALUE;
	safe_close_handle(&file_handle);

	// We'll set the error code to be the one returned by CreateFile() since CloseHandle() would overwrite it.
	// This way, if the file already exists, calling GetLastError() after using this function returns ERROR_FILE_EXISTS.
	SetLastError(error_code);
	return success;
}

// Maps an entire file into memory from its handle.
//
// @Parameters:
// 1. file_handle - The handle of the file to map into memory.
// 2. result_file_size - The address of the variable that receives the file's size in bytes.
// 3. optional_read_only - An optional parameter that specifies the desired protection of the file mapping. If this value is true, this
// function uses read-only access. Otherwise, it uses copy-on-write access, where any modifications made will not go to the original file.
// This value defaults to true.
//
// If optional_read_only is true (or not specified), the file_handle parameter must have been created with the GENERIC_READ access right.
// If optional_read_only is false, the file_handle parameter must have been created with the GENERIC_READ and GENERIC_WRITE  access right.
//
// @Returns: The address of the mapped memory if it succeeds. Otherwise, it returns NULL. This function fails if the file's
// size is zero. If the function succeeds, the resulting file size is always non-zero.
//
// In the debug builds, this address is picked in relation to a base address (see the debug constants at the top of this file).
//
// After being done with the file, this memory is unmapped using safe_unmap_view_of_file().
void* memory_map_entire_file(HANDLE file_handle, u64* result_file_size, bool optional_read_only)
{
	void* mapped_memory = NULL;
	*result_file_size = 0;

	// @Docs:
	// CreateFileMapping: http://web.archive.org/web/20021214190314/http://msdn.microsoft.com/library/en-us/fileio/base/createfilemapping.asp
	// MapViewOfFile: http://web.archive.org/web/20021222022224/http://msdn.microsoft.com/library/en-us/fileio/base/mapviewoffile.asp

	if(file_handle != INVALID_HANDLE_VALUE && file_handle != NULL)
	{
		u64 file_size = 0;
		if(get_file_size(file_handle, &file_size))
		{
			// Reject empty files.
			// @Docs: "An attempt to map a file with a length of 0 (zero) fails with an error code of ERROR_FILE_INVALID.
			// Applications should test for files with a length of 0 (zero) and reject those files."
			// - CreateFileMapping - Win32 API Reference.
			*result_file_size = file_size;
			if(file_size > 0)
			{
				// @Docs: "Windows 95/98/Me: You must pass PAGE_WRITECOPY to CreateFileMapping; otherwise, an error will be returned."
				// - MapViewOfFile / MapViewOfFileEx - Win32 API Reference.
				DWORD desired_protection = (optional_read_only) ? (PAGE_READONLY) : (PAGE_WRITECOPY);
				HANDLE mapping_handle = CreateFileMapping(file_handle, NULL, desired_protection, 0, 0, NULL);

				if(mapping_handle != NULL)
				{
					// @Docs: "If you create the map with PAGE_WRITECOPY and the view with FILE_MAP_COPY, you will receive a view to
					// file. If you write to it, the pages are automatically swappable and the modifications you make will not go to
					// the original data file."
					// - MapViewOfFile / MapViewOfFileEx - Win32 API Reference.
					DWORD desired_access = (optional_read_only) ? (FILE_MAP_READ) : (FILE_MAP_COPY);

					#ifdef BUILD_DEBUG
						mapped_memory = MapViewOfFileEx(mapping_handle, desired_access, 0, 0, 0, DEBUG_MEMORY_MAPPING_BASE_ADDRESS);
						DEBUG_MEMORY_MAPPING_BASE_ADDRESS = advance_bytes(DEBUG_MEMORY_MAPPING_BASE_ADDRESS, DEBUG_BASE_ADDRESS_INCREMENT);
					#else
						mapped_memory = MapViewOfFile(mapping_handle, desired_access, 0, 0, 0);
					#endif

					if(mapped_memory == NULL)
					{
						log_error("Memory Map Entire File: Failed to map a view of the file with the error code %lu.", GetLastError());
					}
				}
				else
				{
					log_error("Memory Map Entire File: Failed to create the file mapping with the error code %lu.", GetLastError());
				}

				// About CloseHandle() and UnmapViewOfFile():
				// @Docs: "The order in which these functions are called does not matter." - CreateFileMapping - Win32 API Reference.
				safe_close_handle(&mapping_handle);
			}
			else
			{
				log_error("Memory Map Entire File: Failed to create a file mapping since the file is empty.");
			}
		}
		else
		{
			log_error("Memory Map Entire File: Failed to get the file's size with the error code %lu.", GetLastError());
		}
	}
	else
	{
		log_error("Memory Map Entire File: Failed to map the entire file into memory since the file handle is invalid.");
	}

	return mapped_memory;
}

// Reads an entire file into memory.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the entire file's contents.
// 2. file_path - The path of the file to read.
// 3. result_file_size - The resulting file size in bytes.
//
// 4. optional_treat_as_text - An optional parameter that specifies whether to treat the data as text. If set to true,
// the resulting pointer is aligned to wchar_t and two zero bytes are added to the end. Otherwise, the data is aligned
// to MAX_SCALAR_ALIGNMENT_SIZE. This is done for convenience since it allows us to read text encoded using UTF-8 or UTF-16,
// or binary data that can then be cast to some structure. This value defaults to false.
//
// @Returns: A pointer to the file's contents if it was read successfully. Otherwise, NULL.
void* read_entire_file(Arena* arena, const TCHAR* file_path, u64* result_file_size, bool optional_treat_as_text)
{
	void* file_contents = NULL;
	*result_file_size = 0;

	HANDLE file_handle = create_handle(file_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, 0);

	if(file_handle != INVALID_HANDLE_VALUE && get_file_size(file_handle, result_file_size))
	{
		// These optional extra NUL bytes should not be added to the resulting file size.
		size_t alignment_size = (optional_treat_as_text) ? (__alignof(wchar_t)) : (MAX_SCALAR_ALIGNMENT_SIZE);
		u64 file_contents_size = (optional_treat_as_text) ? (*result_file_size + sizeof(wchar_t)) : (*result_file_size);
		u32 file_buffer_size = get_arena_file_buffer_size(arena, file_handle);

		lock_arena(arena);
		file_contents = aligned_push_arena_64(arena, file_contents_size, alignment_size);

		if(file_contents != NULL)
		{
			void* file_cursor = file_contents;
			u64 total_bytes_read = 0;
			bool reached_end_of_file = false;
			
			do
			{
				// We only want to read at most the file size we determined earlier. This calculation
				// reads the file in chunks (if it's too big) or in a single pass (if it's small, which
				// is usually the case). ReadFile() would fail if we asked it to read the maximum amount
				// possible.
				u32 num_bytes_to_read = (u32) MIN(*result_file_size - total_bytes_read, (u64) file_buffer_size);
				u32 num_bytes_read = 0;

				if(read_file_chunk(file_handle, file_cursor, num_bytes_to_read, total_bytes_read, true, &num_bytes_read))
				{
					if(num_bytes_read > 0)
					{
						file_cursor = advance_bytes(file_cursor, (u32) num_bytes_read);
						total_bytes_read += num_bytes_read;
					}
					else
					{
						reached_end_of_file = true;
						// Note that the allocated memory is initialized to zero so we don't need to
						// handle the optional null terminator explicitly.
					}
				}
				else
				{
					log_error("Read Entire File: Failed to read a chunk of the file '%s' with the error code %lu. Read %I64u bytes of %I64u.", file_path, GetLastError(), total_bytes_read, *result_file_size);
					reached_end_of_file = true;
					file_contents = NULL;
					file_cursor = NULL;
					clear_arena(arena);
				}

				_ASSERT(total_bytes_read <= *result_file_size);

			} while(!reached_end_of_file);
		}
		else
		{
			log_error("Read Entire File: Failed to allocate the %I64u bytes required to read the entirety of '%s' into memory.", file_path, file_contents_size);
		}

		unlock_arena(arena);
		safe_close_handle(&file_handle);
	}
	else
	{
		log_error("Read Entire File: Failed to get the file handle and size for '%s' with the error code %lu.", file_path, GetLastError());
	}
	
	return file_contents;
}

// Reads a given number of bytes at a specific offset from a file.
//
// @Parameters:
// 1. file_handle - The handle of the file to read.
// 2. file_buffer - The buffer that will receive the read bytes.
// 3. num_bytes_to_read - The size of the buffer in bytes.
// 4. file_offset - The offset in the file in bytes.
//
// 5. optional_allow_reading_fewer_bytes - An optional parameter that specifies if this function is allowed to read fewer bytes than
// the ones requested. This value defaults to false.
// 6. optional_result_num_bytes_read - An optional parameter that receives number of bytes read. This value defaults to NULL.
// 
// @Returns: True if the file's contents were read successfully. Otherwise, false. Reading zero bytes always succeeds.
// This function fails under the following conditions:
// 1. The file handle is invalid.
// 2. It read fewer bytes than the specified value and optional_allow_reading_fewer_bytes is false or not specified.
bool read_file_chunk(	HANDLE file_handle, void* file_buffer, u32 num_bytes_to_read, u64 file_offset,
						bool optional_allow_reading_fewer_bytes, u32* optional_result_num_bytes_read)
{
	if(optional_result_num_bytes_read != NULL) *optional_result_num_bytes_read = 0;

	if(file_handle == INVALID_HANDLE_VALUE)
	{
		log_error("Read File Chunk: Attempted to read %I32u bytes at %I64u from an invalid file handle.", num_bytes_to_read, file_offset);
		return false;
	}

	if(num_bytes_to_read == 0) return true;

	OVERLAPPED overlapped = {};
	u32 offset_high = 0;
	u32 offset_low = 0;
	separate_u64_into_high_and_low_u32s(file_offset, &offset_high, &offset_low);
	overlapped.OffsetHigh = offset_high;
	overlapped.Offset = offset_low;

	DWORD num_bytes_read = 0;
	bool success = (ReadFile(file_handle, file_buffer, num_bytes_to_read, &num_bytes_read, &overlapped) != FALSE) || (GetLastError() == ERROR_HANDLE_EOF);

	if(success)
	{
		success = (num_bytes_read == num_bytes_to_read) || optional_allow_reading_fewer_bytes;
		if(optional_result_num_bytes_read != NULL) *optional_result_num_bytes_read = num_bytes_read;
	}
	else
	{
		log_error("Read File Chunk: Failed to read %I32u bytes at %I64u with the error code %lu.", num_bytes_to_read, file_offset, GetLastError());
	}

	return success;
}

// Behaves like read_file_chunk() but takes the file's path instead of its handle.
//
// @Parameters: All parameters are the same except for the first one:
// 1. file_path - The path of the file to read.
// 
// @Returns: See read_file_chunk().
bool read_file_chunk(	const TCHAR* file_path, void* file_buffer, u32 num_bytes_to_read, u64 file_offset,
						bool optional_allow_reading_fewer_bytes, u32* optional_result_num_bytes_read)
{
	if(optional_result_num_bytes_read != NULL) *optional_result_num_bytes_read = 0;
	if(num_bytes_to_read == 0) return true;

	// @TemporaryFiles: Used by temporary files, meaning it must share reading, writing, and deletion.
	HANDLE file_handle = create_handle(file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0);
	
	bool success = false;
	if(file_handle != INVALID_HANDLE_VALUE)
	{
		success = read_file_chunk(file_handle, file_buffer, num_bytes_to_read, file_offset, optional_allow_reading_fewer_bytes, optional_result_num_bytes_read);
		safe_close_handle(&file_handle);
	}
	else
	{
		log_error("Read File Chunk: Failed to read %I32u bytes at %I64u from the file '%s' with the error code %lu", num_bytes_to_read, file_offset, file_path, GetLastError());
	}
	
	return success;
}

// Reads a given number of bytes from the beginning of a file. This function behaves like read_file_chunk() with an offset of zero.
//
// This function can be used with either the file's handle or its path as the first argument.
//
// @Parameters: See read_file_chunk().
// 
// @Returns: See read_file_chunk().
bool read_first_file_bytes(	HANDLE file_handle, void* file_buffer, u32 num_bytes_to_read,
							bool optional_allow_reading_fewer_bytes, u32* optional_result_num_bytes_read)
{
	return read_file_chunk(file_handle, file_buffer, num_bytes_to_read, 0, optional_allow_reading_fewer_bytes, optional_result_num_bytes_read);
}

bool read_first_file_bytes(	const TCHAR* file_path, void* file_buffer, u32 num_bytes_to_read,
							bool optional_allow_reading_fewer_bytes, u32* optional_result_num_bytes_read)
{
	return read_file_chunk(file_path, file_buffer, num_bytes_to_read, 0, optional_allow_reading_fewer_bytes, optional_result_num_bytes_read);
}

// Writes a given amount of bytes to a file.
//
// @Parameters:
// 1. file_handle - The handle of the file where the data will be written to.
// 2. data - The data to write.
// 3. num_bytes_to_write - The amount of data to write in bytes.
// 4. optional_result_num_bytes_written - An optional parameter that receives number of bytes written. This value defaults to NULL.
//
// @Returns: True if the data was written successfully. Otherwise, false. Writing zero bytes always succeeds.
bool write_to_file(HANDLE file_handle, const void* data, u32 num_bytes_to_write, u32* optional_result_num_bytes_written)
{
	if(optional_result_num_bytes_written != NULL) *optional_result_num_bytes_written = 0;

	if(file_handle == INVALID_HANDLE_VALUE)
	{
		log_error("Write To File: Attempted to write %I32u bytes to an invalid file handle.", num_bytes_to_write);
		return false;
	}

	if(num_bytes_to_write == 0) return true;

	bool success = false;
	DWORD num_bytes_written = 0;
	
	if(WriteFile(file_handle, data, num_bytes_to_write, &num_bytes_written, NULL) != FALSE)
	{
		success = (num_bytes_written == num_bytes_to_write);
		if(optional_result_num_bytes_written != NULL) *optional_result_num_bytes_written = num_bytes_written;
	}
	else
	{
		log_error("Write To File: Failed to write %I32u bytes with the error code %lu.", num_bytes_to_write, GetLastError());
	}

	return success;
}

// Copies part of a file at a given offset to another file.
//
// @Parameters:
// 1. arena - The Arena structure that is used as the intermediate file buffer.
// 2. source_file_path - The path of the source file where the chunk will be read from.
// 3. total_bytes_to_copy - The size of the chunk to copy in bytes.
// 4. file_offset - The offset of the chunk in the file in bytes.
// 5. destination_file_handle - The handle of the destination file where the chunk will be written to.
// 
// @Returns: True if the whole chunk was copied successfully. Otherwise, false. This functions returns true if the number of bytes to copy is zero and the
// source path and destination handle are valid.
bool copy_file_chunks(Arena* arena, const TCHAR* source_file_path, u64 total_bytes_to_copy, u64 file_offset, HANDLE destination_file_handle)
{
	if(source_file_path == NULL || string_is_empty(source_file_path) || destination_file_handle == INVALID_HANDLE_VALUE) return false;

	if(total_bytes_to_copy == 0) return true;

	bool success = true;

	// @TemporaryFiles: Used by temporary files, meaning it must share reading, writing, and deletion.
	HANDLE source_file_handle = create_handle(source_file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0);

	u32 file_buffer_size = get_arena_file_buffer_size(arena, source_file_handle);
	void* file_buffer = push_arena(arena, file_buffer_size, u8);
	u64 total_bytes_read = 0;

	do
	{
		// This calculation reads the file in chunks (if it's too big) or in a single pass (if it's
		// small, which is usually the case). ReadFile() would fail if we asked it to read the maximum
		// amount possible.
		u32 num_bytes_to_read = (u32) MIN(total_bytes_to_copy - total_bytes_read, (u64) file_buffer_size);

		u32 num_bytes_read = 0;
		success = success 	&& read_file_chunk(source_file_handle, file_buffer, num_bytes_to_read, file_offset + total_bytes_read, false, &num_bytes_read)
							&& write_to_file(destination_file_handle, file_buffer, num_bytes_read);

		total_bytes_read += num_bytes_read;

	} while(success && total_bytes_read < total_bytes_to_copy);

	safe_close_handle(&source_file_handle);
	return success;
}

// Behaves like copy_file_chunks() but takes the destination file's path instead of its handle.
//
// @GetLastError
//
// @Parameters: All parameters are the same except for the last one:
// 5. destination_file_path - The path of the destination file where the chunk will be written to.
// 6. overwrite - Whether or not to overwrite any existing file at that same path.
// 
// @Returns: See copy_file_chunks(). This function also returns false if 'overwrite' is false and a file already exists in the desination path.
// For this last cases, GetLastError() returns ERROR_FILE_EXISTS. For all other failure cases, GetLastError() returns CUSTOM_ERROR_FAILED_TO_COPY_FILE_CHUNKS.
// See copy_exporter_file() for more details.
bool copy_file_chunks(Arena* arena, const TCHAR* source_file_path, u64 total_bytes_to_copy, u64 file_offset, const TCHAR* destination_file_path, bool overwrite)
{
	DWORD creation_disposition = (overwrite) ? (CREATE_ALWAYS) : (CREATE_NEW);
	HANDLE destination_file_handle = create_handle(destination_file_path, GENERIC_WRITE, 0, creation_disposition, FILE_ATTRIBUTE_NORMAL);
	
	bool success = false;
	if(destination_file_handle != INVALID_HANDLE_VALUE)
	{
		success = copy_file_chunks(arena, source_file_path, total_bytes_to_copy, file_offset, destination_file_handle);
		safe_close_handle(&destination_file_handle);
		// Propagate a generic error code if the destination file was created successfully but copying the chunks failed anyways.
		if(!success) SetLastError(CUSTOM_ERROR_FAILED_TO_COPY_FILE_CHUNKS);
	}
	else
	{
		// Propagate the last Windows system error if we can't create the destination file (e.g. 'overwrite' is false and the file already
		// exists results in ERROR_FILE_EXISTS).
	}

	return success;
}

// Reduces a file's size to zero given its handle.
//
// @Parameters:
// 1. file_handle - The handle of the file to empty.
// 
// @Returns: True if the file size was reduced successfully. Otherwise, false.
bool empty_file(HANDLE file_handle)
{
	return (SetFilePointer(file_handle, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) && (SetEndOfFile(file_handle) != FALSE);
}

// Generates the SHA-256 hash of a file.
//
// @Dependencies: This function calls third-party code from the Portable C++ Hashing Library.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the computed hash as a hexadecimal character string.
// 2. file_path - The path of the file to hash.
// 
// @Returns: The computed hash as a string. If any part of the file cannot be read, this function returns NULL.
TCHAR* generate_sha_256_from_file(Arena* arena, const TCHAR* file_path)
{
	if(file_path == NULL) return NULL;

	TCHAR* result = NULL;

	// @TemporaryFiles: Used by temporary files, meaning it must share reading, writing, and deletion.
	HANDLE file_handle = create_handle(file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0);

	if(file_handle != INVALID_HANDLE_VALUE)
	{		
		u32 file_buffer_size = get_arena_file_buffer_size(arena, file_handle);
		void* file_buffer = push_arena(arena, file_buffer_size, u8);
		u64 total_bytes_read = 0;
		SHA256 hash_stream;

		bool reached_end_of_file = false;
		do
		{	
			u32 num_bytes_read = 0;
			if(read_file_chunk(file_handle, file_buffer, file_buffer_size, total_bytes_read, true, &num_bytes_read))
			{
				if(num_bytes_read > 0)
				{
					total_bytes_read += num_bytes_read;
					hash_stream.add(file_buffer, num_bytes_read);
				}
				else
				{
					reached_end_of_file = true;
					std::string hash_string_cpp = hash_stream.getHash();
					const char* hash_string = hash_string_cpp.c_str();
					result = convert_ansi_string_to_tchar(arena, hash_string);
					string_to_uppercase(result);
				}
			}
			else
			{
				reached_end_of_file = true;
				result = NULL;
				log_error("Generate Sha-256 From File: Failed to read a chunk of the file '%s' with the error code %lu. Read %I64u bytes so far.", file_path, GetLastError(), total_bytes_read);
			}

		} while(!reached_end_of_file);
		
		safe_close_handle(&file_handle);
	}
	else
	{
		log_error("Generate Sha-256 From File: Failed to get the file handle for '%s' with the error code %lu.", file_path, GetLastError());
	}

	return result;
}

// Custom memory allocation function passed to the Zlib library.
static voidpf zlib_alloc(voidpf opaque, uInt num_items, uInt item_size)
{
	Arena* arena = (Arena*) opaque;
	void* result = aligned_push_arena(arena, num_items * item_size, MAX_SCALAR_ALIGNMENT_SIZE);
	return (result != NULL) ? (result) : (Z_NULL);
}

// Custom memory deallocation function passed to the Zlib library.
static void zlib_free(voidpf opaque, voidpf address)
{
	// Leak memory since the arena is cleared when we finish decompressing a file.
}

// Decompresses a file using the Gzip, Zlib, or raw DEFLATE compression formats.
//
// @Dependencies: This function calls third-party code from the Zlib library.
//
// @Parameters:
// 1. arena - The Arena structure that is used as the intermediate file buffer.
// 2. source_file_path - The path of the source file to decompress.
// 3. destination_file_handle - The handle of the destination file where the decompressed data will be written to.
// 4. result_error_code - The error code generated by Zlib's functions if the file cannot be decompressed.
// 
// @Returns: True if the file was decompressed successfully. Otherwise, false.
bool decompress_gzip_zlib_deflate_file(Arena* arena, const TCHAR* source_file_path, HANDLE destination_file_handle, int* result_error_code)
{
	*result_error_code = Z_ERRNO;

	// @TemporaryFiles: Used by temporary files, meaning it must share reading, writing, and deletion.
	HANDLE source_file_handle = create_handle(source_file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);

	if(source_file_handle == INVALID_HANDLE_VALUE)
	{
		log_error("Decompress Gzip Zlib Deflate File: Failed to get the file handle for '%s' with the error code %lu.", source_file_path, GetLastError());
		return false;
	}

	bool success = false;

	lock_arena(arena);

	u32 source_file_buffer_size = get_arena_file_buffer_size(arena, source_file_handle);
	void* source_file_buffer = push_arena(arena, source_file_buffer_size, u8);
	
	u32 destination_file_buffer_size = source_file_buffer_size;
	void* destination_file_buffer = push_arena(arena, destination_file_buffer_size, u8);

	const size_t file_signature_size = 2;
	u8 file_signature[file_signature_size] = {};
	read_first_file_bytes(source_file_handle, file_signature, file_signature_size);

	// @Note: Assumes 0x78 as the first byte (DEFLATE with a 32K window) for the Zlib format.
	bool is_gzip_or_zlib = memory_is_equal(file_signature, "\x1F\x8B", file_signature_size)
						|| memory_is_equal(file_signature, "\x78\x01", file_signature_size)
						|| memory_is_equal(file_signature, "\x78\x5E", file_signature_size)
						|| memory_is_equal(file_signature, "\x78\x9C", file_signature_size)
						|| memory_is_equal(file_signature, "\x78\xDA", file_signature_size);

	// @Docs: "zlib 1.2.11 Manual"  - https://zlib.net/manual.html
	z_stream stream = {};
	stream.zalloc = zlib_alloc;
	stream.zfree = zlib_free;
	stream.opaque = arena;
	stream.next_in = Z_NULL;

	int window_bits = (is_gzip_or_zlib) ? (15 + 32) : (-15);
	int error_code = inflateInit2(&stream, window_bits);

	#define GET_ZLIB_ERROR_MESSAGE() ( (stream.msg != NULL) ? (stream.msg) : ("") )

	if(error_code == Z_OK)
	{
		u64 total_bytes_read = 0;

		do
		{
			u32 num_bytes_read = 0;
			if(read_file_chunk(source_file_handle, source_file_buffer, source_file_buffer_size, total_bytes_read, true, &num_bytes_read))
			{
				// End of file.
				if(num_bytes_read == 0)
				{
					error_code = Z_STREAM_END;
					break;
				}

				stream.avail_in = num_bytes_read;
				stream.next_in = (Bytef*) source_file_buffer;
				total_bytes_read += num_bytes_read;
			}
			else
			{
				log_error("Decompress Gzip Zlib Deflate File: Failed to read a chunk from '%s' after reading %I64u bytes.", source_file_path, total_bytes_read);
				error_code = Z_ERRNO;
				break;
			}

			do
			{
				stream.avail_out = destination_file_buffer_size;
				stream.next_out = (Bytef*) destination_file_buffer;
				
				error_code = inflate(&stream, Z_NO_FLUSH);

				if(error_code != Z_OK && error_code != Z_STREAM_END)
				{
					log_error("Decompress Gzip Zlib Deflate File: Failed to decompress a chunk from '%s' after reading %I64u bytes with the error %d '%hs'.", source_file_path, total_bytes_read, error_code, GET_ZLIB_ERROR_MESSAGE());
					goto break_outer_loop;
				}

				u32 num_bytes_to_write = destination_file_buffer_size - stream.avail_out;
				if(!write_to_file(destination_file_handle, destination_file_buffer, num_bytes_to_write))
				{
					log_error("Decompress Gzip Zlib Deflate File: Failed to write a decompressed chunk from '%s' to the destination file after reading %I64u bytes.", source_file_path, total_bytes_read);
					error_code = Z_ERRNO;
					goto break_outer_loop;
				}

			} while(stream.avail_out == 0);

		} while(error_code != Z_STREAM_END);

		break_outer_loop:;
		success = (error_code == Z_STREAM_END);
	}
	else
	{
		log_error("Decompress Gzip Zlib Deflate File: Failed to initialize the stream to decompress the file '%s' with the error %d '%hs'.", source_file_path, error_code, GET_ZLIB_ERROR_MESSAGE());
	}

	#undef GET_ZLIB_ERROR_MESSAGE

	inflateEnd(&stream);

	clear_arena(arena);
	unlock_arena(arena);

	safe_close_handle(&source_file_handle);

	*result_error_code = error_code;
	return success;
}

// Custom memory allocation function passed to the Brotli library.
static void* brotli_alloc(void* opaque, size_t size)
{
	Arena* arena = (Arena*) opaque;
	return aligned_push_arena(arena, size, MAX_SCALAR_ALIGNMENT_SIZE);
}

// Custom memory deallocation function passed to the Brotli library.
static void brotli_free(void* opaque, void* address)
{
	// Leak memory since the arena is cleared when we finish decompressing a file.
}

// Decompresses a file using the Brotli compression format.
//
// @Dependencies: This function calls third-party code from the Brotli library.
//
// @Parameters:
// 1. arena - The Arena structure that is used as the intermediate file buffer.
// 2. source_file_path - The path of the source file to decompress.
// 3. destination_file_handle - The handle of the destination file where the decompressed data will be written to.
// 4. result_error_code - The error code generated by Brotli's functions if the file cannot be decompressed.
// 
// @Returns: True if the file was decompressed successfully. Otherwise, false.
bool decompress_brotli_file(Arena* arena, const TCHAR* source_file_path, HANDLE destination_file_handle, int* result_error_code)
{
	*result_error_code = BROTLI_LAST_ERROR_CODE;

	// @TemporaryFiles: Used by temporary files, meaning it must share reading, writing, and deletion.
	HANDLE source_file_handle = create_handle(source_file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);

	if(source_file_handle == INVALID_HANDLE_VALUE)
	{
		log_error("Decompress Brotli File: Failed to get the file handle for '%s' with the error code %lu.", source_file_path, GetLastError());
		return false;
	}

	bool success = false;

	lock_arena(arena);

	u32 source_file_buffer_size = get_arena_file_buffer_size(arena, source_file_handle);
	u8* source_file_buffer = push_arena(arena, source_file_buffer_size, u8);

	u32 destination_file_buffer_size = source_file_buffer_size;
	u8* destination_file_buffer = push_arena(arena, destination_file_buffer_size, u8);

	// @Docs: "decode.h File Reference"  - https://brotli.org/decode.html
	BrotliDecoderState* state = BrotliDecoderCreateInstance(brotli_alloc, brotli_free, arena);
	
	#define GET_BROTLI_ERROR_CODE_AND_MESSAGE() BrotliDecoderGetErrorCode(state), BrotliDecoderErrorString(BrotliDecoderGetErrorCode(state))

	if(state != NULL)
	{
		BrotliDecoderResult decoder_result = BROTLI_DECODER_RESULT_ERROR;
		u64 total_bytes_read = 0;

		size_t available_in = source_file_buffer_size; // As input: source buffer size. As output: unused size in buffer.
		const u8* next_in = source_file_buffer; // Next compressed byte.
		
		size_t available_out = destination_file_buffer_size; // As input: destination buffer size. As output: remaining size in buffer.
		u8* next_out = destination_file_buffer; // Next decompressed byte.

		do
		{
			u32 num_bytes_read = 0;
			if(read_file_chunk(source_file_handle, source_file_buffer, source_file_buffer_size, total_bytes_read, true, &num_bytes_read))
			{
				// End of file.
				if(num_bytes_read == 0)
				{
					decoder_result = BROTLI_DECODER_RESULT_SUCCESS;
					break;
				}
				
				available_in = num_bytes_read;
				next_in = source_file_buffer;
				total_bytes_read += num_bytes_read;
			}
			else
			{
				log_error("Decompress Brotli File: Failed to read a chunk from '%s' after reading %I64u bytes.", source_file_path, total_bytes_read);
				break;
			}

			do
			{	
				decoder_result = BrotliDecoderDecompressStream(state, &available_in, &next_in, &available_out, &next_out, NULL);

				if(decoder_result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
				{
					// Continue reading.
					log_warning("Decompress Brolit File: More input is required to decompress the file '%s' with a source buffer size of %I32u bytes and after reading %I64u bytes.", source_file_path, source_file_buffer_size, total_bytes_read);
					goto continue_outer_loop;
				}
				else if(decoder_result == BROTLI_DECODER_RESULT_ERROR)
				{
					log_error("Decompress Brolit File: Failed to decompress a chunk from '%s' after reading %I64u bytes with the error %d '%hs'.", source_file_path, total_bytes_read, GET_BROTLI_ERROR_CODE_AND_MESSAGE());
					goto break_outer_loop;
				}

				_ASSERT(decoder_result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT || decoder_result == BROTLI_DECODER_RESULT_SUCCESS);

				u32 num_bytes_to_write = destination_file_buffer_size - (u32) available_out;
				if(!write_to_file(destination_file_handle, destination_file_buffer, num_bytes_to_write))
				{
					log_error("Decompress Brotli File: Failed to write a decompressed chunk from '%s' to the destination file after reading %I64u bytes.", source_file_path, total_bytes_read);
					goto break_outer_loop;
				}

				// Unlike with Zlib, we only update these values after successfully writing the decompressed data to
				// the file since we would otherwise clobber any partially decompressed output data if the decoder
				// required more input.
				available_out = destination_file_buffer_size;
				next_out = destination_file_buffer;

			} while(BrotliDecoderHasMoreOutput(state) == BROTLI_TRUE);

			continue_outer_loop:;
		} while(decoder_result != BROTLI_DECODER_RESULT_SUCCESS);

		break_outer_loop:;
		success = (decoder_result == BROTLI_DECODER_RESULT_SUCCESS);
	}
	else
	{
		log_error("Decompress Brotli File: Failed to initialize the decoder state to decompress the file '%s' with the error %d '%hs'.", source_file_path, GET_BROTLI_ERROR_CODE_AND_MESSAGE());
	}

	#undef GET_BROTLI_ERROR_CODE_AND_MESSAGE

	_ASSERT( (success && BrotliDecoderIsFinished(state)) || !BrotliDecoderIsFinished(state) );

	*result_error_code = (int) BrotliDecoderGetErrorCode(state);
	BrotliDecoderDestroyInstance(state);
	state = NULL;

	clear_arena(arena);
	unlock_arena(arena);

	safe_close_handle(&source_file_handle);

	return success;
}

// Retrieves the data of a specified registry value of type string (REG_SZ).
//
// This function was created to replace RegGetValue() from ADVAPI32.DLL since it was only available from version 5.2 onwards
// (Windows Server 2003 or later).
//
// This function is usually called by using the macro query_registry(), which takes the same arguments but where key_name and
// value_name are ANSI strings (for convenience).
//
// @Parameters:
// 1. hkey - The handle to an open registry key. This can be one of the following predefined keys:
// - HKEY_CLASSES_ROOT
// - HKEY_CURRENT_USER
// - HKEY_LOCAL_MACHINE
// - HKEY_USERS
// (Note that you may use others if you're targeting versions that are more recent than Windows 98).
// 2. key_name - The name of the registry subkey to query.
// 3. value_name - The name of the registry value to query.
// 4. value_data - The buffer that receives the resulting string read from the registry value.
// 5. value_data_size - The size of the buffer in bytes. This must include the null terminator.
// 
// @Returns: True if the value was retrieved successfully. Otherwise, false.
bool tchar_query_registry(HKEY hkey, const TCHAR* key_name, const TCHAR* value_name, TCHAR* value_data, u32 value_data_size)
{
	bool success = true;

	HKEY opened_key = NULL;
	LONG error_code = RegOpenKeyEx(hkey, key_name, 0, KEY_QUERY_VALUE, &opened_key);
	success = (error_code == ERROR_SUCCESS);

	if(!success)
	{
		log_error("Query Registry: Failed to open the registry key '%s' with the error code %lu.", key_name, error_code);
		return success;
	}

	DWORD value_data_type = REG_NONE;
	DWORD actual_value_data_size = value_data_size;
	// When RegQueryValueEx() returns, actual_value_data_size contains the size of the data that was copied to the buffer.
	// For strings, this size includes any terminating null characters unless the data was stored without them.
	error_code = RegQueryValueEx(opened_key, value_name, NULL, &value_data_type, (LPBYTE) value_data, &actual_value_data_size);
	success = (error_code == ERROR_SUCCESS);
	RegCloseKey(opened_key);

	_ASSERT(actual_value_data_size > 0);

	if(success)
	{
		if(value_data_type == REG_SZ)
		{
			// According to the documentation, we need to ensure that string values are null terminated. Whoever calls this function should
			// always guarantee that the buffer is able to hold whatever the possible string values are, including the null terminator.
			size_t num_value_data_chars = actual_value_data_size / sizeof(TCHAR);
			value_data[num_value_data_chars] = T('\0');
		}
		else
		{
			// For now, we'll only handle simple strings. Strings with expandable environment variables are skipped (REG_EXPAND_SZ).
			log_error("Query Registry: Found the unsupported data type %lu in the registry value '%s\\%s'.", value_data_type, key_name, value_name);
			success = false;	
		}
	}
	else
	{
		log_error("Query Registry: Failed to query the registry key '%s' with the error code %lu.", key_name, error_code);
	}

	return success;
}

// Retrieves a specific file property from an executable or DLL.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the file property as a string.
// 2. full_file_path - The full path to the file.
// 3. info_type - The file property to retrieve.
// 4. result_info - The resulting file property string.
// 
// @Returns: True if the specified file property was read successfully. Otherwise, false.
bool get_file_info(Arena* arena, const TCHAR* full_file_path, File_Info_Type info_type, TCHAR** result_info)
{
	// @Assert: The requested file info type is handled and is associated with a string identifier.
	_ASSERT(0 <= info_type && info_type < NUM_FILE_INFO_TYPES);

	// @Assert: The file path argument is an absolute path. Otherwise, it searches for it in various locations
	// as specified by LoadLibrary().
	_ASSERT(PathIsRelative(full_file_path) == FALSE);

	bool success = false;

	DWORD handle = 0;
	DWORD info_size = GetFileVersionInfoSize(full_file_path, &handle);
	if(info_size > 0)
	{
		void* info_block = push_arena(arena, info_size, char);
		if(GetFileVersionInfo(full_file_path, handle, info_size, info_block) != FALSE)
		{
			struct Language_Code_Page_Info
			{
				WORD wLanguage;
				WORD wCodePage;
			};

			Language_Code_Page_Info* language_code_page_info = NULL;
			UINT queried_info_size = 0;

			if(VerQueryValue(info_block, T("\\VarFileInfo\\Translation"), (LPVOID*) &language_code_page_info, &queried_info_size) != FALSE)
			{
				UINT num_languages_and_code_pages = queried_info_size / sizeof(Language_Code_Page_Info);
				if(num_languages_and_code_pages > 0)
				{
					if(num_languages_and_code_pages > 1)
					{
						log_warning("Get File Info: Ignoring %d language and code page info structures for the file '%s' and info type %d.", num_languages_and_code_pages - 1, full_file_path, info_type);
					}

					WORD language = language_code_page_info[0].wLanguage;
					WORD code_page = language_code_page_info[0].wCodePage;
					const char* info_type_string = FILE_INFO_TYPE_TO_STRING[info_type];

					TCHAR* string_subblock = push_arena(arena, info_size, TCHAR);
					StringCbPrintf(string_subblock, info_size, T("\\StringFileInfo\\%04x%04x\\%hs"), language, code_page, info_type_string);

					TCHAR* file_information = NULL;
					UINT file_information_size = 0;

					if(VerQueryValue(info_block, string_subblock, (LPVOID*) &file_information, &file_information_size) != FALSE)
					{
						if(file_information_size > 0)
						{
							if(code_page != CODE_PAGE_UTF_16_LE)
							{
								log_info("Get File Info: Found code page %u in the info for the file '%s' and info type %d.", code_page, full_file_path, info_type);
							}

							// This file information was found in the info_block which is already part of our own memory arena.
							*result_info = file_information;
							success = true;
						}
						else
						{
							log_warning("Get File Info: No language and code page info for the file '%s' and info type %d.", full_file_path, info_type);
						}
					}
					else
					{
						log_error("Get File Info: Failed to query the language and code page info for the file '%s' and info type %d. The subblock query was '%s'.", full_file_path, info_type, string_subblock);
					}
				}
				else
				{
					log_warning("Get File Info: No translation info found for the file '%s' and info type %d.", full_file_path, info_type);
				}
			}
			else
			{
				log_error("Get File Info: Failed to query the translation info for the file '%s' and info type %d.", full_file_path, info_type);
			}
		}
		else
		{
			log_error("Get File Info: Failed to get the version info for the file '%s' and info type %d with the error code %lu.", full_file_path, info_type, GetLastError());
		}
	}
	else
	{
		log_error("Get File Info: Failed to determine the version info size for the file '%s' and info type %d with the error code %lu.", full_file_path, info_type, GetLastError());
	}

	return success;
}

// Creates the global log file and any missing intermediate directories in its path. After being created, you can append lines
// to this file by calling log_print().
//
// @Parameters:
// 1. log_file_path - The path of the log file to create.
// 
// @Returns: True if the log file was created successfully. Otherwise, it returns false and all future log_print() calls
// will do nothing.
static HANDLE GLOBAL_LOG_FILE_HANDLE = INVALID_HANDLE_VALUE;
bool create_log_file(const TCHAR* log_file_path)
{
	if(GLOBAL_LOG_FILE_HANDLE != INVALID_HANDLE_VALUE)
	{
		log_error("Create Log File: Attempted to create a second log file in '%s' when the current one is still open.", log_file_path);
		return false;
	}

	// Create any missing intermediate directories. 
	TCHAR full_log_directory_path[MAX_PATH_CHARS] = T("");
	get_full_path_name(log_file_path, full_log_directory_path);
	PathAppend(full_log_directory_path, T(".."));
	
	create_directories(full_log_directory_path);

	GLOBAL_LOG_FILE_HANDLE = create_handle(log_file_path, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
	if(GLOBAL_LOG_FILE_HANDLE == INVALID_HANDLE_VALUE) console_print("Error: Failed to create the log file with the error code %lu.", GetLastError());

	return (GLOBAL_LOG_FILE_HANDLE != INVALID_HANDLE_VALUE);
}

// Closes the global log file. After being closed, log_print() should no longer be called.
//
// @Parameters: None.
// 
// @Returns: Nothing.
void close_log_file(void)
{
	safe_close_handle(&GLOBAL_LOG_FILE_HANDLE);
}

// Appends a formatted TCHAR string to the global log file. This file must have been previously created using create_log_file().
// This ANSI or UTF-16 string is converted to UTF-8 before being written to the log file.
//
// This function is usually called by using the macro log_print(log_type, string_format, ...), which takes the same arguments but
// where string_format is an ANSI string (for convenience).
//
// Use log_newline() to add an empty line, and log_debug() to add a line of type LOG_DEBUG. This last function is only
// called if the BUILD_DEBUG macro is defined.
//
// @Parameters:
// 1. log_type - The type of log line to write. This is used to add a small string identifier to the beginning of the line.
// Use one of the following values:
// - LOG_INFO
// - LOG_WARNING
// - LOG_ERROR
// For LOG_DEBUG, use log_debug() instead. LOG_NONE is usually used when calling log_newline(), though it may be used
// directly with log_print() in certain cases.
// 2. string_format - The format string. Note that %hs is used for narrow ANSI strings, %ls for wide UTF-16 strings, and %s for TCHAR
// strings (narrow ANSI or wide UTF-16 depending on the build target).
// 3. ... - Zero or more arguments to be inserted in the format string.
//
// @Returns: Nothing.

static const size_t MAX_CHARS_PER_LOG_TYPE = 20;
static const size_t MAX_CHARS_PER_LOG_MESSAGE = 4000;
static const size_t MAX_CHARS_PER_LOG_WRITE = MAX_CHARS_PER_LOG_TYPE + MAX_CHARS_PER_LOG_MESSAGE + 2;

void tchar_log_print(Log_Type log_type, const TCHAR* string_format, ...)
{
	DWORD previous_error_code = GetLastError();

	_ASSERT(GLOBAL_LOG_FILE_HANDLE != INVALID_HANDLE_VALUE);

	TCHAR log_buffer[MAX_CHARS_PER_LOG_WRITE] = T("");
	
	// Add the log type identifier string to the beginning of the line.
	StringCchCopy(log_buffer, MAX_CHARS_PER_LOG_TYPE, LOG_TYPE_TO_STRING[log_type]);
	size_t num_log_type_chars = 0;
	StringCchLength(log_buffer, MAX_CHARS_PER_LOG_TYPE, &num_log_type_chars);

	// Insert any extra arguments into the format string.
	va_list arguments;
	va_start(arguments, string_format);
	StringCchVPrintf(&log_buffer[num_log_type_chars], MAX_CHARS_PER_LOG_MESSAGE, string_format, arguments);
	va_end(arguments);

	// Add the newline.
	StringCchCat(log_buffer, MAX_CHARS_PER_LOG_WRITE, T("\r\n"));

	// Convert the log line to UTF-8.
	#ifdef BUILD_9X
		// For Windows 98 and ME, we'll first convert the ANSI string to UTF-16.
		wchar_t utf_16_log_buffer[MAX_CHARS_PER_LOG_WRITE] = L"";
		MultiByteToWideChar(CP_ACP, 0, log_buffer, -1, utf_16_log_buffer, MAX_CHARS_PER_LOG_WRITE);

		wchar_t* utf_16_log_buffer_pointer = utf_16_log_buffer;
	#else
		wchar_t* utf_16_log_buffer_pointer = log_buffer;
	#endif

	char utf_8_log_buffer[MAX_CHARS_PER_LOG_WRITE] = "";
	WideCharToMultiByte(CP_UTF8, 0, utf_16_log_buffer_pointer, -1, utf_8_log_buffer, sizeof(utf_8_log_buffer), NULL, NULL);

	size_t num_bytes_to_write = 0;
	StringCbLengthA(utf_8_log_buffer, sizeof(utf_8_log_buffer), &num_bytes_to_write);

	write_to_file(GLOBAL_LOG_FILE_HANDLE, utf_8_log_buffer, (u32) num_bytes_to_write);

	SetLastError(previous_error_code);
}

// Checks if a CSV string needs to be escaped, and returns the number of bytes that are necessary to store the escaped string
// (including the null terminator). A CSV string is escaped by using the following rules:
// 1. If the string contains a comma, double quotation mark, or newline character, then it's wrapped in double quotes.
// 2. Every double quotation mark is escaped by adding another double quotes character after it.
//
// For example:
// abc1234 doesn't need to be escaped.
// abc"12,34 is escaped as "abc""12,34" and requires 13 or 26 bytes to store it as an ANSI or UTF-16 string, respectively.
//
// The worst case scenario is a string composed solely of quotes. For example: "" would need to be escaped as """""" (add a second
// quote for each character and surround the whole string with two quotes). In this case, we have 6 characters and 1 null terminator,
// meaning we'd need 7 or 14 bytes to store it as an ANSI or UTF-16 string, respectively.
//
// @Parameters:
// 1. str - The string to check.
// 2. size_required - The address to the variable that receives the number of bytes required to store the escaped string.
// 
// @Returns: True if the string needs escaping. In this case, size_required . Otherwise, it returns false and
// size_required is the string's size. If the string is NULL, this function returns false and size_required is zero.
static bool does_csv_string_require_escaping(const TCHAR* str, size_t* size_required)
{
	if(str == NULL)
	{
		*size_required = 0;
		return false;
	}

	bool needs_escaping = false;
	size_t total_num_chars = 0;

	while(*str != T('\0'))
	{
		total_num_chars += 1;

		if(*str == T(',') || *str == T('\n'))
		{
			needs_escaping = true;
		}
		else if(*str == T('\"'))
		{
			needs_escaping = true;
			// Extra quotation mark.
			total_num_chars += 1;
		}

		++str;
	}

	// Null terminator.
	total_num_chars += 1;

	if(needs_escaping)
	{
		// Two quotation marks to wrap the value.
		total_num_chars += 2;
	}

	*size_required = total_num_chars * sizeof(TCHAR);
	return needs_escaping;
}

// Escapes a CSV string in-place. Refer to does_csv_string_require_escaping()'s documentation to see how a CSV string is escaped.
// This function assumes that the passed buffer is large enough to accommodate the escaped string. This can be done by using
// does_csv_string_require_escaping() to determine the required size and only calling escape_csv_string() if the string needs
// to be escaped.
//
// If the string is NULL or doesn't need to be escaped, this function will not modify the buffer.
//
// @Parameters:
// 1. str - The buffer that contains the string to be escaped, and whose size is large enough to accommodate the escaped string.
// 
// @Returns: Nothing.
static void escape_csv_string(TCHAR* str)
{
	if(str == NULL) return;

	TCHAR* string_start = str;
	bool needs_escaping = false;

	while(*str != T('\0'))
	{
		if(*str == T(',') || *str == T('\n'))
		{
			needs_escaping = true;
		}
		else if(*str == T('\"'))
		{
			needs_escaping = true;

			TCHAR* next_char_1 = str + 1;
			TCHAR* next_char_2 = str + 2;
			// Add an extra double quotation mark.
			// E.g: ab"cd -> ab"ccd -> ab""cd
			// Where next_char_1 and next_char_2 point to 'c' and 'd', respectively.
			MoveMemory(next_char_2, next_char_1, string_size(next_char_1));
			*next_char_1 = T('\"');

			// Skip this new quotation mark.
			++str;
		}

		++str;
	}

	if(needs_escaping)
	{
		// Wrap the string in two quotation marks.
		// E.g: ab""cd -> aab""cd -> "aab""cd"
		size_t escaped_string_size = string_size(string_start);
		size_t num_escaped_string_chars = escaped_string_size / sizeof(TCHAR);
		MoveMemory(string_start + 1, string_start, escaped_string_size);
		string_start[0] = T('\"');
		string_start[num_escaped_string_chars] = T('\"');
		string_start[num_escaped_string_chars + 1] = T('\0');
	}
}

// Creates a CSV file and any missing intermediate directories in its path. After being created, you can append lines
// to this file by calling csv_print_header() and csv_print_row() with the resulting file handle.
//
// @Parameters:
// 1. csv_file_path - The path of the CSV file to create.
// 2. result_file_handle - The address of the variable that receives the handle for the CSV file.
// 
// @Returns: True if the CSV file was created successfully. Otherwise, false.
bool create_csv_file(const TCHAR* csv_file_path, HANDLE* result_file_handle)
{
	// Create any missing intermediate directories. 
	TCHAR full_csv_directory_path[MAX_PATH_CHARS] = T("");
	get_full_path_name(csv_file_path, full_csv_directory_path);
	PathAppend(full_csv_directory_path, T(".."));
	
	create_directories(full_csv_directory_path);

	// Normally we could just replace GENERIC_WRITE with FILE_APPEND_DATA, but that doesn't seem to be supported in
	// Windows 98/ME since CreateFile() fails with the error code 87 (ERROR_INVALID_PARAMETER). Instead of using
	// that parameter, we'll set the file pointer to the end of the CSV file.
	*result_file_handle = create_handle(csv_file_path, GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL);

	bool success = (*result_file_handle != INVALID_HANDLE_VALUE) && (SetFilePointer(*result_file_handle, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER);
	if(!success) log_error("Create Csv File: Failed to create the CSV file '%s' with the error code %lu.", csv_file_path, GetLastError());
	return success;
}

// Writes the header to a CSV file using UTF-8 as the character encoding. This header string is built using the Csv_Type enumeration
// values that correspond to each column. These values will be separated by commas.
// For example: the array {CSV_FILENAME, CSV_URL, CSV_RESPONSE} would write the string "Filename, URL, Server Response".
//
// This header is only added to empty CSV files. If the file already contains text, this function does nothing.
//
// @Parameters:
// 1. arena - The Arena structure where any intermediary strings are stored.
// 2. csv_file_handle - The handle to the CSV file.
// 3. column_types - The array of column types used to determine the column names.
// 4. num_columns - The number of elements in this array.
// 
// @Returns: Nothing.
void csv_print_header(Arena* arena, HANDLE csv_file_handle, Csv_Type* column_types, int num_columns)
{
	if(csv_file_handle == INVALID_HANDLE_VALUE)
	{
		log_error("Csv Print Header: Attempted to add the header to a CSV file that hasn't been opened yet.");
		return;
	}

	u64 file_size = 0;
	bool file_size_success = get_file_size(csv_file_handle, &file_size);

	// If we couldn't get the file size, we don't want to potentially add a header to the middle of non-empty CSV file.
	if(!file_size_success || file_size > 0) return;

	// Build the final CSV line where the strings are contiguous in memory.
	char* csv_header = push_arena(arena, 0, char);
	for(int i = 0; i < num_columns; ++i)
	{
		Csv_Type type = column_types[i];
		const char* column_name = CSV_TYPE_TO_UTF_8_STRING[type];
		size_t column_name_size = string_size(column_name);

		// Add a newline to the last value.
		if(i == num_columns - 1)
		{
			char* csv_column = push_and_copy_to_arena(arena, column_name_size + 1, char, column_name, column_name_size);
			csv_column[column_name_size - 1] = '\r';
			csv_column[column_name_size] = '\n';
		}
		// Separate each value before the last one with a comma.
		else
		{
			char* csv_column = push_and_copy_to_arena(arena, column_name_size, char, column_name, column_name_size);
			csv_column[column_name_size - 1] = ',';
		}
	}

	// @Note: Since sizeof(char) is always one, this means that no alignment took place in the previous push_arena() calls.
	// Otherwise, we'd write some garbage (null bytes) into the file.
	ptrdiff_t csv_header_size = pointer_difference(arena->available_memory, csv_header);
	_ASSERT(csv_header_size > 0);

	write_to_file(csv_file_handle, csv_header, (u32) csv_header_size);
}

// Writes a row of TCHAR values (ANSI or UTF-16 strings) to a CSV file using UTF-8 as the character encoding. These values will be
// separated by commas.
//
// @Parameters:
// 1. arena - The Arena structure where any intermediary strings are stored.
// 2. csv_file_handle - The handle to the CSV file.
// 3. column_values - The array of values to write. The TCHAR value strings must be contained in the 'value' field of the Csv_Entry
// structure. If a value is NULL, then nothing will be written in its cell.
// 4. num_columns - The number of elements in this array.
// 
// @Returns: Nothing.
void csv_print_row(Arena* arena, HANDLE csv_file_handle, Csv_Entry* column_values, int num_columns)
{
	_ASSERT(num_columns > 0);

	if(csv_file_handle == INVALID_HANDLE_VALUE)
	{
		log_error("Csv Print Row: Attempted to add the header to a CSV file that hasn't been opened yet.");
		return;
	}

	// First pass: escape values that require it and convert every value to UTF-16 (only necessary for ANSI strings
	// in Windows 98 and ME).
	for(int i = 0; i < num_columns; ++i)
	{
		TCHAR* value = column_values[i].value;
		// If there's no value, use an empty string never needs escaping. This string will still be copied.
		if(value == NULL) value = T("");

		// Escape the value if it requires it.
		size_t size_required_for_escaping = 0;
		if(does_csv_string_require_escaping(value, &size_required_for_escaping))
		{
			value = push_and_copy_to_arena(arena, size_required_for_escaping, TCHAR, value, string_size(value));
			escape_csv_string(value);
		}

		// Convert the values to UTF-16.
		#ifdef BUILD_9X
			wchar_t* utf_16_value = L"";

			int num_chars_required_utf_16 = MultiByteToWideChar(CP_ACP, 0, value, -1, NULL, 0);
			if(num_chars_required_utf_16 != 0)
			{
				int size_required_utf_16 = num_chars_required_utf_16 * sizeof(wchar_t);
				utf_16_value = push_arena(arena, size_required_utf_16, wchar_t);
				
				if(MultiByteToWideChar(CP_ACP, 0, value, -1, utf_16_value, num_chars_required_utf_16) == 0)
				{
					log_error("Csv Print Row: Failed to convert the ANSI string '%hs' into a UTF-16 string with the error code %lu. Using an empty string instead.", value, GetLastError());
					utf_16_value = L"";
				}
			}
			else
			{
				log_error("Csv Print Row: Failed to determine the size required to convert the ANSI string '%hs' into a UTF-16 string with the error code %lu. Using an empty string instead.", value, GetLastError());
			}

			column_values[i].utf_16_value = utf_16_value;
			column_values[i].value = NULL;
		#else
			column_values[i].utf_16_value = value;
			column_values[i].value = NULL;
		#endif
	}

	// Second pass: convert every value to UTF-8 and build the final CSV line where the strings are contiguous in memory.
	char* csv_row = push_arena(arena, 0, char);
	for(int i = 0; i < num_columns; ++i)
	{
		wchar_t* value = column_values[i].utf_16_value;

		int size_required_utf_8 = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);

		if(size_required_utf_8 == 0)
		{
			log_error("Csv Print Row: Failed to determine the size required to convert the UTF-16 string '%ls' into a UTF-8 string with the error code %lu. Using an empty string instead.", value, GetLastError());
			value = L"";
			size_required_utf_8 = (int) string_size(value);
		}

		// Add a newline to the last value.
		if(i == num_columns - 1)
		{
			char* csv_row_value = push_arena(arena, size_required_utf_8 + 1, char);
			if(WideCharToMultiByte(CP_UTF8, 0, value, -1, csv_row_value, size_required_utf_8, NULL, NULL) != 0)
			{
				csv_row_value[size_required_utf_8 - 1] = '\r';
				csv_row_value[size_required_utf_8] = '\n';
			}
			else
			{
				log_error("Csv Print Row: Failed to convert the UTF-16 string '%ls' into a UTF-8 string with the error code %lu. Using an empty string instead.", value, GetLastError());
				csv_row_value[0] = '\r';
				csv_row_value[1] = '\n';
			}

		}
		// Separate each value before the last one with a comma.
		else
		{
			char* csv_row_value = push_arena(arena, size_required_utf_8, char);
			if(WideCharToMultiByte(CP_UTF8, 0, value, -1, csv_row_value, size_required_utf_8, NULL, NULL) != 0)
			{
				csv_row_value[size_required_utf_8 - 1] = ',';
			}
			else
			{
				log_error("Csv Print Row: Failed to convert the UTF-16 string '%ls' into a UTF-8 string with the error code %lu. Using an empty string instead.", value, GetLastError());
				csv_row_value[0] = ',';
			}
		}
	}

	// @Note: Since sizeof(char) is always one, this means that no alignment took place in the previous push_arena() calls.
	// Otherwise, we'd write some garbage (null bytes) into the file.
	ptrdiff_t csv_row_size = pointer_difference(arena->available_memory, csv_row);
	_ASSERT(csv_row_size > 0);

	write_to_file(csv_file_handle, csv_row, (u32) csv_row_size);
}

#ifndef BUILD_9X

	// Define some undocumented structures and constants for NtQuerySystemInformation().
	// @Resources:
	// - https://www.geoffchappell.com/studies/windows/km/ntoskrnl/api/ex/sysinfo/handle_table_entry.htm
	// - https://www.geoffchappell.com/studies/windows/km/ntoskrnl/api/ex/sysinfo/handle.htm
	// - https://www.geoffchappell.com/studies/windows/km/ntoskrnl/api/ex/sysinfo/class.htm

	struct SYSTEM_HANDLE_TABLE_ENTRY_INFO
	{
		USHORT UniqueProcessId;
		USHORT CreatorBackTraceIndex;
		UCHAR ObjectTypeIndex;
		UCHAR HandleAttributes;
		USHORT HandleValue;
		PVOID Object;
		ULONG GrantedAccess;
	};

	#ifdef BUILD_32_BIT
		_STATIC_ASSERT(sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO) == 0x10);
	#else
		_STATIC_ASSERT(sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO) == 0x18);
	#endif

	struct SYSTEM_HANDLE_INFORMATION
	{
		ULONG NumberOfHandles;
		SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles[ANYSIZE_ARRAY];
	};

	#ifdef BUILD_32_BIT
		_STATIC_ASSERT(sizeof(SYSTEM_HANDLE_INFORMATION) == 0x14);
	#else
		_STATIC_ASSERT(sizeof(SYSTEM_HANDLE_INFORMATION) == 0x20);
	#endif

	const SYSTEM_INFORMATION_CLASS SystemHandleInformation = (SYSTEM_INFORMATION_CLASS) 0x10;

	// Define a stub version of the function we want to dynamically load, and a variable that will either contain the pointer to
	// this loaded function or to the stub version (if we can't load the real one).
	#define NT_QUERY_SYSTEM_INFORMATION(function_name) NTSTATUS WINAPI function_name(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength)
	static NT_QUERY_SYSTEM_INFORMATION(stub_nt_query_system_information)
	{
		log_warning("NtQuerySystemInformation: Calling the stub version of this function.");
		return STATUS_NOT_IMPLEMENTED;
	}
	typedef NT_QUERY_SYSTEM_INFORMATION(Nt_Query_System_Information);
	static Nt_Query_System_Information* dll_nt_query_system_information = stub_nt_query_system_information;
	#define NtQuerySystemInformation dll_nt_query_system_information

	// Dynamically load any necessary functions from Ntdll.dll. After being called, the following functions may used:
	//
	// - NtQuerySystemInformation()
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	static HMODULE ntdll_library = NULL;
	void load_ntdll_functions(void)
	{
		if(ntdll_library != NULL)
		{
			log_warning("Load Ntdll Functions: The library was already loaded.");
			return;
		}

		ntdll_library = LoadLibraryA("Ntdll.dll");
		if(ntdll_library != NULL)
		{
			GET_FUNCTION_ADDRESS(ntdll_library, "NtQuerySystemInformation", Nt_Query_System_Information*, NtQuerySystemInformation);
		}
		else
		{
			log_error("Load Ntdll Functions: Failed to load the library with error code %lu.", GetLastError());
		}
	}

	// Free any functions that were previously dynamically loaded from Ntdll.dll. After being called, these functions should
	// no longer be called.
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	void free_ntdll_functions(void)
	{
		if(ntdll_library == NULL)
		{
			log_error("Free Ntdll: Failed to free the library since it wasn't previously loaded.");
			return;
		}

		if(FreeLibrary(ntdll_library))
		{
			ntdll_library = NULL;
			NtQuerySystemInformation = stub_nt_query_system_information;
		}
		else
		{
			log_error("Free Ntdll: Failed to free the library with the error code %lu.", GetLastError());
		}
	}

	// Define a stub version of the function we want to dynamically load, and a variable that will either contain the pointer to
	// this loaded function or to the stub version (if we can't load the real one).
	// This function was added in Windows 8 (Kernel32.dll version 6.2), but in practice it may end up being called on Windows 7.
	// The stub version of this function will call GetOverlappedResult() which doesn't time out, and instead blocks until the
	// operation is finished (if the number of milliseconds if greater than zero).
	#define GET_OVERLAPPED_RESULT_EX(function_name) BOOL WINAPI function_name(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, DWORD dwMilliseconds, BOOL bAlertable)
	static GET_OVERLAPPED_RESULT_EX(stub_get_overlapped_result_ex)
	{
		log_warning("GetOverlappedResultEx: Calling the stub version of this function. The timeout and alertable arguments will be ignored. These were set to %lu and %d.", dwMilliseconds, bAlertable);
		BOOL bWait = (dwMilliseconds > 0) ? (TRUE) : (FALSE);
		return GetOverlappedResult(hFile, lpOverlapped, lpNumberOfBytesTransferred, bWait);
	}
	typedef GET_OVERLAPPED_RESULT_EX(Get_Overlapped_Result_Ex);
	static Get_Overlapped_Result_Ex* dll_get_overlapped_result_ex = stub_get_overlapped_result_ex;
	#define GetOverlappedResultEx dll_get_overlapped_result_ex

	// Dynamically load any necessary functions from Kernel32.dll. After being called, the following functions may used:
	//
	// - GetOverlappedResultEx()
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	static HMODULE kernel32_library = NULL;
	void load_kernel32_functions(void)
	{
		if(kernel32_library != NULL)
		{
			log_warning("Load Kernel32 Functions: The library was already loaded.");
			return;
		}

		kernel32_library = LoadLibraryA("Kernel32.dll");
		if(kernel32_library != NULL)
		{
			GET_FUNCTION_ADDRESS(kernel32_library, "GetOverlappedResultEx", Get_Overlapped_Result_Ex*, GetOverlappedResultEx);
		}
		else
		{
			log_error("Load Kernel32 Functions: Failed to load the library with error code %lu.", GetLastError());
		}
	}

	// Free any functions that were previously dynamically loaded from Kernel32.dll. After being called, these functions should
	// no longer be called.
	//
	// @Compatibility: Windows 2000 to 10 only.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	void free_kernel32_functions(void)
	{
		if(kernel32_library == NULL)
		{
			log_error("Free Kernel32: Failed to free the library since it wasn't previously loaded.");
			return;
		}

		if(FreeLibrary(kernel32_library))
		{
			kernel32_library = NULL;
			GetOverlappedResultEx = stub_get_overlapped_result_ex;
		}
		else
		{
			log_error("Free Kernel32: Failed to free the library with the error code %lu.", GetLastError());
		}
	}

	// Finds and creates a duplicated handle for a file that was opened by another process given its path on disk.
	//
	// @Compatibility: Compiles in the Windows 2000 to 10 builds, though it was only made to work in Windows 7 to 10.
	//
	// @Parameters:
	// 1. arena - The Arena structure where any intermediary information about the currently opened handles is stored.
	// 2. full_file_path - The full path to the file of interest.
	// 3. result_file_handle - The address to the variable that receives the resulting duplicated handle.
	// 
	// @Returns: True if it succeeds. Otherwise, false. This function fails under the following scenarios:
	// 1. If the file of interest is not opened by another process.
	// 2. If the file's handle cannot be duplicated.
	// 3. If it's not possible to get a handle with read attributes only access for the file. 
	// 4. If it couldn't query the system for information on all opened handles.
	static bool query_file_handle_from_file_path(Arena* arena, const wchar_t* full_file_path, HANDLE* result_file_handle)
	{
		bool success = false;
		*result_file_handle = INVALID_HANDLE_VALUE;

		lock_arena(arena);

		// List all opened handles. We may have to increase the size of the Arena structure to receive all the necessary handle
		// information. Note that, even if it succeeds, this information may already be stale...
		ULONG handle_info_size = (ULONG) megabytes_to_bytes(1);
		SYSTEM_HANDLE_INFORMATION* handle_info = push_arena(arena, handle_info_size, SYSTEM_HANDLE_INFORMATION);
		ULONG actual_handle_info_size = 0;
		
		bool used_virtual_alloc = false;
		#define FREE_IF_USED_VIRTUAL_ALLOC()\
		do\
		{\
			if(used_virtual_alloc)\
			{\
				VirtualFree(handle_info, 0, MEM_RELEASE);\
				used_virtual_alloc = false;\
				handle_info = NULL;\
			}\
		} while(false, false)

		NTSTATUS error_code = NtQuerySystemInformation(SystemHandleInformation, handle_info, handle_info_size, &actual_handle_info_size);
		while(error_code == STATUS_INFO_LENGTH_MISMATCH)
		{
			// Clear the previous allocated memory so we can try again with the actual size required. Remember that we locked
			// the arena before trying to query the information.
			FREE_IF_USED_VIRTUAL_ALLOC();
			clear_arena(arena);

			// On the next attempt, the overall size of this information may have changed (new handles could have been created),
			// so we'll go the extra mile and use the actual size plus an extra bit.
			handle_info_size = actual_handle_info_size + (ULONG) kilobytes_to_bytes(128);
			log_warning("Query File Handle: Insufficient buffer size while trying to query system information. Attempting to expand the buffer to %lu bytes.", handle_info_size);

			handle_info = push_arena(arena, handle_info_size, SYSTEM_HANDLE_INFORMATION);
			if(handle_info != NULL)
			{
				// Try to query the handle information again.
				error_code = NtQuerySystemInformation(SystemHandleInformation, handle_info, handle_info_size, &actual_handle_info_size);
			}
			else
			{
				// Ran out of memory in the arena to hold all the returned handle information.
				log_warning("Query File Handle: Ran out of memory in the arena while trying to query system information. Attempting to use VirtualAlloc to allocate %lu bytes.", handle_info_size);

				// Try to query the handle information again but with VirtualAlloc().
				handle_info = (SYSTEM_HANDLE_INFORMATION*) VirtualAlloc(NULL, handle_info_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
				if(handle_info != NULL)
				{
					used_virtual_alloc = true;
					error_code = NtQuerySystemInformation(SystemHandleInformation, handle_info, handle_info_size, &actual_handle_info_size);
				}
				else
				{
					// Ran out of memory in both the arena and VirtualAlloc().
					log_error("Query File Handle: Could not allocate enough memory query system information. The required buffer size was %lu bytes.", handle_info_size);
					break;
				}
			}
		}

		if(!NT_SUCCESS(error_code))
		{
			log_error("Query File Handle: Failed to query system information with error code %ld.", error_code);
			goto clean_up;
		}

		log_info("Query File Handle: Processing %lu bytes of handle information.", handle_info_size);

		// When we iterate over these handles, we'll want to check which one corresponds to the desired file path.
		// To do this robustly, we'll need another handle for this file. Since the whole point of this function is
		// getting a handle for a file that's being used by another process, we'll want to avoid asking for any
		// access rights that result in a sharing violation. Because of this, we'll ask for the right to read the
		// file's attributes.
		HANDLE read_attributes_file_handle = create_handle(full_file_path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, OPEN_EXISTING, 0);
		if(read_attributes_file_handle == INVALID_HANDLE_VALUE)
		{
			log_error("Query File Handle: Failed to get the read attributes files handle for '%ls' with error code %lu.", full_file_path, GetLastError());
			goto clean_up;
		}

		DWORD current_process_id = GetCurrentProcessId();
		HANDLE current_process_handle = GetCurrentProcess();
		// This pseudo handle doesn't have to be closed.

		for(ULONG i = 0; i < handle_info->NumberOfHandles; ++i)
		{
			SYSTEM_HANDLE_TABLE_ENTRY_INFO handle_entry = handle_info->Handles[i];

			// Skip handles from this process. Otherwise, we'd duplicate the read attributes handle we got above.
			DWORD process_id = handle_entry.UniqueProcessId;
			if(process_id == current_process_id) continue;

			// @Note: The PROCESS_DUP_HANDLE access right is required for DuplicateHandle().
			HANDLE process_handle = OpenProcess(PROCESS_DUP_HANDLE, FALSE, process_id);
			if(process_handle == NULL || process_handle == INVALID_HANDLE_VALUE) continue;

			HANDLE file_handle = (HANDLE) handle_entry.HandleValue;
			HANDLE duplicated_file_handle = INVALID_HANDLE_VALUE;

			// Duplicate the file handle and give it generic reading access rights.
			if(DuplicateHandle(	process_handle, file_handle,
								current_process_handle, &duplicated_file_handle,
								GENERIC_READ, FALSE, 0))
			{
				// Check if the handle belongs to a file object and if it refers to the file we're looking for.
				if(GetFileType(duplicated_file_handle) == FILE_TYPE_DISK
					&& do_handles_refer_to_the_same_file_or_directory(read_attributes_file_handle, duplicated_file_handle))
				{
					success = true;
					*result_file_handle = duplicated_file_handle;
					safe_close_handle(&process_handle);
					log_info("Query File Handle: Found handle with attributes 0x%02X and granted access 0x%08X.", handle_entry.HandleAttributes, handle_entry.GrantedAccess);
					break;
				}

				safe_close_handle(&duplicated_file_handle);
			}

			safe_close_handle(&process_handle);
		}

		safe_close_handle(&read_attributes_file_handle);

		clean_up:
		FREE_IF_USED_VIRTUAL_ALLOC();
		#undef FREE_IF_USED_VIRTUAL_ALLOC
		clear_arena(arena);
		unlock_arena(arena);

		return success;
	}

	// Forcibly copies a file that was opened by another process. This function is used to bypass sharing violation errors when
	// trying to read a file that is being used by another process.
	//
	// You should use the CopyFile() function from the Windows API first, and only use this one if CopyFile() fails with the error
	// code ERROR_SHARING_VIOLATION. This function should be used very sparingly.
	//
	// @Compatibility: Compiles in the Windows 2000 to 10 builds, though it was only made to work in Windows 7 to 10.
	//
	// A wrapper version of this function that works in the Windows 98 to 10 builds and that first tries to use CopyFile() can be
	// found in the copy_open_file() function.
	//
	// @Parameters:
	// 1. arena - The Arena structure where the file buffer and any intermediary information about the currently opened handles
	// is stored.
	// 2. copy_source_path - The full path to the source file.
	// 3. copy_destination_path - The destination path where the file will be copied to.
	// 
	// @Returns: True if the file was copied sucessfully. Otherwise, false. The copy operation is considered successfully if all
	// of the following conditions are true:
	// 1. The file specified in copy_source_path exists and is currently open by another process.
	// 2. The number of bytes written to disk matches the number of bytes read using the duplicated file handle.
	// 3. The number of bytes written to disk matches the source file's real size.
	static bool force_copy_open_file(Arena* arena, const wchar_t* copy_source_path, const wchar_t* copy_destination_path)
	{
		bool copy_success = false;

		lock_arena(arena);

		HANDLE source_file_handle = INVALID_HANDLE_VALUE;
		bool was_source_handle_found = query_file_handle_from_file_path(arena, copy_source_path, &source_file_handle);

		if(was_source_handle_found)
		{
			HANDLE destination_file_handle = create_handle(copy_destination_path, GENERIC_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);

			if(destination_file_handle != INVALID_HANDLE_VALUE)
			{
				// The handle we duplicated above may have been created with certain flags like FILE_FLAG_OVERLAPPED
				// or FILE_FLAG_NO_BUFFERING, which change how we should read the file's contents. We'll try to copy
				// the file in a way that addresses these. We'll handle both synchronous and asynchronous reads using
				// the OVERLAPPED structure, and align our file buffer's size and address to the current page size.
				//
				// @Docs:
				// "File access sizes, including the optional file offset in the OVERLAPPED structure, if specified,
				// must be for a number of bytes that is an integer multiple of the volume sector size."
				// "File access buffer addresses for read and write operations should be physical sector-aligned,
				// which means aligned on addresses in memory that are integer multiples of the volume's physical
				// sector size."
				// "Therefore, in most situations, page-aligned memory will also be sector-aligned, because the
				// case where the sector size is larger than the page size is rare."
				// - File Buffering - Win32 API Reference.
				SYSTEM_INFO system_info = {};
				GetSystemInfo(&system_info);

				u32 file_buffer_size = get_arena_file_buffer_size(arena, source_file_handle);
				file_buffer_size = ALIGN_UP(file_buffer_size, system_info.dwPageSize);
				_ASSERT(file_buffer_size % system_info.dwPageSize == 0);

				void* file_buffer = aligned_push_arena(arena, file_buffer_size, system_info.dwPageSize);
				
				u64 total_bytes_read = 0;
				u64 total_bytes_written = 0;
				OVERLAPPED overlapped = {}; // The offset in the file.
				
				bool reached_end_of_file = false;
				u32 num_read_retry_attempts = 0;
				const u32 MAX_READ_RETRY_ATTEMPTS = 10;

				// @Future: Could NtSuspendProcess() be used to suspend the process that is holding the file open
				// while we read it? Though that might not be a good idea when copying the cache database that is
				// being used by Windows...

				do
				{
					DWORD num_bytes_read = 0;
					bool read_success = ReadFile(source_file_handle, file_buffer, file_buffer_size, &num_bytes_read, &overlapped) != FALSE;
					
					if(!read_success)
					{
						DWORD read_error_code = GetLastError();

						// Asynchronous read.
						if(read_error_code == ERROR_IO_PENDING)
						{
							// Get the bytes that were read by this asynchronous operation. This call will block until this data is
							// read or the function times out.
							const DWORD TIMEOUT_IN_SECONDS = 3;
							const DWORD TIMEOUT_IN_MILLISECONDS = TIMEOUT_IN_SECONDS * 1000;
							if(!GetOverlappedResultEx(source_file_handle, &overlapped, &num_bytes_read, TIMEOUT_IN_MILLISECONDS, FALSE))
							{
								DWORD overlapped_result_error_code = GetLastError();
								if(overlapped_result_error_code == ERROR_HANDLE_EOF)
								{
									reached_end_of_file = true;
								}
								else if(overlapped_result_error_code == WAIT_TIMEOUT)
								{
									++num_read_retry_attempts;
									log_warning("Force Copy Open File: Failed to get the overlapped result because the function timed out after %lu seconds. Read %I64u and wrote %I64u bytes so far. Retrying read operation (attempt %I32u of %I32u).", TIMEOUT_IN_SECONDS, total_bytes_read, total_bytes_written, num_read_retry_attempts, MAX_READ_RETRY_ATTEMPTS);
									continue;
								}
								else
								{
									++num_read_retry_attempts;
									log_error("Force Copy Open File: Failed to get the overlapped result while reading the file '%ls' with the unhandled error code %lu. Read %I64u and wrote %I64u bytes so far. Retrying read operation (attempt %I32u of %I32u).", copy_source_path, overlapped_result_error_code, total_bytes_read, total_bytes_written, num_read_retry_attempts, MAX_READ_RETRY_ATTEMPTS);
									continue;
								}
							}
						}
						// Synchronous read.
						else if(read_error_code == ERROR_HANDLE_EOF)
						{
							reached_end_of_file = true;
						}
						else
						{
							++num_read_retry_attempts;
							log_error("Force Copy Open File: Failed to read the file '%ls' with the unhandled error code %lu. Read %I64u and wrote %I64u bytes so far. Retrying read operation (attempt %I32u of %I32u).", copy_source_path, read_error_code, total_bytes_read, total_bytes_written, num_read_retry_attempts, MAX_READ_RETRY_ATTEMPTS);
							continue;
						}
					}

					// Here the read is successful ('read_success' is true) or it was unsuccessful but because we reached
					// the end of file ('read_success' is false and 'reached_end_of_file' is true).

					// Reset the number of retries if the read operation was successful.
					num_read_retry_attempts = 0;

					if(!reached_end_of_file)
					{
						total_bytes_read += num_bytes_read;
					
						u32 num_bytes_written = 0;
						if(write_to_file(destination_file_handle, file_buffer, num_bytes_read, &num_bytes_written))
						{
							total_bytes_written += num_bytes_written;
						}
						else
						{
							log_error("Force Copy Open File: Failed to write %I32u bytes to the destination file in '%ls' with the error code %lu. Read %I64u and wrote %I64u bytes so far. This function will terminate prematurely.", num_bytes_read, copy_destination_path, GetLastError(), total_bytes_read, total_bytes_written);
							break;
						}

						// Move to the next offset in the file.
						u32 offset_high = 0;
						u32 offset_low = 0;
						separate_u64_into_high_and_low_u32s(total_bytes_read, &offset_high, &offset_low);					
						overlapped.OffsetHigh = offset_high;
						overlapped.Offset = offset_low;
					}
					else
					{
						_ASSERT(num_bytes_read == 0);
					}

				} while(!reached_end_of_file && num_read_retry_attempts < MAX_READ_RETRY_ATTEMPTS);

				if(num_read_retry_attempts > 0)
				{
					if(reached_end_of_file)
					{
						log_warning("Force Copy Open File: Retried read operations %I32u time(s) before reaching the end of file.", num_read_retry_attempts);
					}
					else
					{
						log_error("Force Copy Open File: Failed to reach the end of file after retrying the read operations %I32u times. Read %I64u and wrote %I64u bytes in total.", num_read_retry_attempts, total_bytes_read, total_bytes_written);
					}
				}

				// This isn't a great way to verify if we copied everything successfully but it works for now...
				copy_success = (total_bytes_read == total_bytes_written);

				u64 file_size = 0;
				if(copy_success && get_file_size(source_file_handle, &file_size))
				{
					copy_success = (total_bytes_read == file_size);
				}
			}

			safe_close_handle(&destination_file_handle);
		}
		
		safe_close_handle(&source_file_handle);
		
		clear_arena(arena);
		unlock_arena(arena);

		return copy_success;
	}

#endif

// Attempts to copy a file that was opened by another process. This function first tries to copy the file normally using CopyFile().
// If this fails with the error code ERROR_SHARING_VIOLATION and we're in the Windows 2000 to 10 builds, this function will the
// attempt to forcibly copy it.
//
// This function is essentially a wrapper of force_copy_open_file() that works in all supported Windows builds. Much like that one,
// this function should be used very sparingly.
//
// @Parameters: See force_copy_open_file().
// 
// @Returns: See force_copy_open_file(). Unlike this last one, this function will succeed and copy the file even if wasn't opened
// by another process.
bool copy_open_file(Arena* arena, const TCHAR* copy_source_path, const TCHAR* copy_destination_path)
{
	bool copy_success = CopyFile(copy_source_path, copy_destination_path, FALSE) != FALSE;

	if(!copy_success)
	{
		DWORD error_code = GetLastError();

		if( (error_code == ERROR_FILE_NOT_FOUND) || (error_code == ERROR_PATH_NOT_FOUND) )
		{
			log_error("Copy Open File: Failed to copy '%s' to '%s' since the file or path could not be found.", copy_source_path, copy_destination_path);
		}
		else if(error_code == ERROR_SHARING_VIOLATION)
		{
			#ifndef BUILD_9X
				log_info("Copy Open File: Attempting to forcibly copy the file '%s' since its being used by another process.", copy_source_path);
				copy_success = force_copy_open_file(arena, copy_source_path, copy_destination_path);
				if(!copy_success)
				{
					log_error("Copy Open File: Failed to forcibly copy the file '%s'.", copy_source_path);
				}
			#else
				log_error("Copy Open File: Failed to copy the file '%s' since its being used by another process.", copy_source_path);
			#endif
		}
		else
		{
			log_info("Copy Open File: Failed to copy the file '%s' normally with the error code %lu.", copy_source_path, error_code);
		}
	}

	return copy_success;
}

#ifdef BUILD_DEBUG
	
	// A debug function that measures the time between two calls. The macros DEBUG_BEGIN_MEASURE_TIME() and DEBUG_END_MEASURE_TIME() should
	// be used to start and stop the measurement.
	//
	// For example:
	//		DEBUG_BEGIN_MEASURE_TIME();
	//			function_1(...)
	//			{
	//				DEBUG_BEGIN_MEASURE_TIME();
	//					function_2(...);
	//				DEBUG_END_MEASURE_TIME();			
	//			}
	//		DEBUG_END_MEASURE_TIME();
	//
	// @Parameters:
	// 1. is_start - True when starting a new time measurement. False for stopping the last measurement.
	// 2. identifier - A constant string that can be used to identify different measurements in the log file. May be NULL when 'is_start' is false.
	// 
	// @Returns: Nothing.
	void debug_measure_time(bool is_start, const char* identifier)
	{
		// Excuse the messiness, this is only used in the debug builds.

		struct Debug_Time_Measurement
		{
			const char* identifier;
			LARGE_INTEGER start_counter;
			LARGE_INTEGER stop_counter;
		};

		static LARGE_INTEGER frequency = {}; // In counts per second.

		static const int MAX_TIME_MEASUREMENTS = 30;
		static Debug_Time_Measurement time_measurements[MAX_TIME_MEASUREMENTS] = {};
		
		static int current_measurement = 0;

		if(is_start)
		{
			Debug_Time_Measurement* measurement = &time_measurements[current_measurement];

			_ASSERT(identifier != NULL);
			measurement->identifier = identifier;

			log_debug("Debug Measure Time #%d [%hs]: Started time measurement.", current_measurement, measurement->identifier);
			
			if(QueryPerformanceFrequency(&frequency) == FALSE)
			{
				log_debug("Debug Measure Time #%d [%hs]: Failed to query the performance frequency with the error code %lu.", current_measurement, measurement->identifier, GetLastError());
			}

			if(QueryPerformanceCounter(&measurement->start_counter) == FALSE)
			{
				log_debug("Debug Measure Time #%d [%hs]: Failed to query the performance counter for the starting time with the error code %lu.", current_measurement, measurement->identifier, GetLastError());
			}

			++current_measurement;
			_ASSERT(current_measurement < MAX_TIME_MEASUREMENTS);
		}
		else
		{
			--current_measurement;
			_ASSERT(current_measurement >= 0);

			Debug_Time_Measurement* measurement = &time_measurements[current_measurement];

			if(QueryPerformanceCounter(&measurement->stop_counter) == FALSE)
			{
				log_debug("Debug Measure Time #%d [%hs]: Failed to query the performance counter for the stopping time with the error code %lu.", current_measurement, measurement->identifier, GetLastError());
			}
			
			f64 elapsed_time = (measurement->stop_counter.QuadPart - measurement->start_counter.QuadPart) / (f64) frequency.QuadPart;
			log_debug("Debug Measure Time #%d [%hs]: Stopped time measurement at %.9f seconds.", current_measurement, measurement->identifier, elapsed_time);

			measurement->identifier = NULL;
		}
	}

#endif
