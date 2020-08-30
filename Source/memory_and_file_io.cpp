#include "web_cache_exporter.h"
#include "memory_and_file_io.h"

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
#ifdef DEBUG
	#ifdef BUILD_9X
		static void* DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = NULL;
		static void* DEBUG_MEMORY_MAPPING_BASE_ADDRESS = NULL;
		static const size_t DEBUG_BASE_ADDRESS_INCREMENT = 0;
	#else
		static void* DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = (void*) 0x10000000;
		static void* DEBUG_MEMORY_MAPPING_BASE_ADDRESS = (void*) 0x50000000;
		static const size_t DEBUG_BASE_ADDRESS_INCREMENT = 0x01000000;
	#endif
#endif

// Creates an arena and allocates enough memory to read/write a given number of bytes.
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
	#ifdef DEBUG
		arena->available_memory = VirtualAlloc(DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = advance_bytes(DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS, DEBUG_BASE_ADDRESS_INCREMENT);
	#else
		arena->available_memory = VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	#endif

	bool success = arena->available_memory != NULL;
	arena->used_size = 0;
	arena->total_size = (success) ? (total_size) : (0);

	#ifdef DEBUG
		if(success)
		{
			FillMemory(arena->available_memory, arena->total_size, 0xFF);
		}
	#endif

	if(!success)
	{
		log_print(LOG_ERROR, "Create Arena: Failed to allocate %Iu bytes with the error code %lu.", total_size, GetLastError());
		_ASSERT(false);
	}

	return success;
}

// Moves an arena's available memory pointer by a certain number, giving back the aligned address to a memory location
// where you can write at least that number of bytes.
// This function is usually called by using the macro push_arena(arena, push_size, Type), which determines the alignment of
// that Type. This macro also casts the resulting memory address to Type*. 
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
		log_print(LOG_ERROR, "Aligned Push Arena: Failed to push %Iu bytes since no memory was previously allocated.", push_size);
		_ASSERT(false);
		return NULL;
	}

	if(alignment_size == 0)
	{
		log_print(LOG_ERROR, "Aligned Push Arena: Failed to push %Iu bytes since the alignment size is zero.", push_size);
		_ASSERT(false);
		return NULL;
	}

	void* misaligned_address = arena->available_memory;
	void* aligned_address = misaligned_address;
	if(alignment_size > 1)
	{
		if(IS_POWER_OF_TWO(alignment_size))
		{
			aligned_address = (void*) ALIGN_UP((uintptr_t) misaligned_address, alignment_size);
		}
		else
		{
			log_print(LOG_ERROR, "Aligned Push Arena: Failed to align the memory address since the alignment size %Iu is not a power of two.", alignment_size);
			_ASSERT(false);
			return NULL;
		}
	}
	
	size_t aligned_push_size = push_size + pointer_difference(aligned_address, misaligned_address);
	_ASSERT(push_size <= aligned_push_size);
	_ASSERT(((uintptr_t) aligned_address) % alignment_size == 0);
	
	if(arena->used_size + aligned_push_size > arena->total_size)
	{
		log_print(LOG_ERROR, "Aligned Push Arena: Ran out of memory. Pushing %Iu more bytes to %Iu would exceed the total size of %Iu bytes.", aligned_push_size, arena->used_size, arena->total_size);
		_ASSERT(false);
		return NULL;
	}

	// Set the requested bytes to FF so it's easier to keep track of in the debugger's Memory Window.
	// These are initially set to zero in create_arena().
	#ifdef DEBUG
		ZeroMemory(aligned_address, aligned_push_size);
	#endif

	arena->available_memory = advance_bytes(arena->available_memory, aligned_push_size);
	arena->used_size += aligned_push_size;

	// Keep track of the maximum used size starting at a certain value. This gives us an idea of how
	// much memory each cache type and Windows version uses before clearing the arena.
	#ifdef DEBUG
		static size_t max_used_size = kilobytes_to_bytes(128);
		if(arena->used_size > max_used_size)
		{
			max_used_size = arena->used_size;
			debug_log_print("Aligned Push Arena: Increased the maximum used size to %Iu after pushing %Iu bytes. The arena is now at %.2f%% used capacity.", max_used_size, aligned_push_size, get_used_arena_capacity(arena));
		}
	#endif

	return aligned_address;
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
		log_print(LOG_ERROR, "Aligned Push And Copy To Arena: Attempted to copy %Iu bytes from %p while only pushing %Iu bytes.", data_size, data, push_size);
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
float32 get_used_arena_capacity(Arena* arena)
{
	if(arena->available_memory == NULL || arena->total_size == 0)
	{
		return 0.0f;
	}

	return ((float32) arena->used_size) / arena->total_size * 100;
}

// Clears the entire arena, resetting the pointer to the available memory back to the original address and the number of used
// bytes to zero.
//
// @Parameters:
// 1. arena - The Arena structure whose available_memory and used_size members will be reset.
//
// @Returns: Nothing.
void clear_arena(Arena* arena)
{
	if(arena->available_memory == NULL)
	{
		log_print(LOG_ERROR, "Clear Arena: Failed to clear the arena since no memory was previously allocated.");
		_ASSERT(false);
		return;
	}

	arena->available_memory = retreat_bytes(arena->available_memory, arena->used_size);
	// Set all the bytes to zero so it's easier to keep track of in the debugger's Memory Window.
	// These are initially set to zero in create_arena() and to FF in the push_arena() functions.
	#ifdef DEBUG
		FillMemory(arena->available_memory, arena->used_size, 0xFF);
	#endif
	arena->used_size = 0;
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
	bool success = VirtualFree(base_memory, 0, MEM_RELEASE) == TRUE;
	#ifdef DEBUG
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

// Advances a pointer by a given number of bytes.
//
// @Parameters:
// 1. pointer - The pointer to move forward.
// 2. num_bytes - The number of bytes to move.
//
// @Returns: The moved pointer value.
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

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> DATE AND TIME FORMATTING
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

// Converts a datetime value to a string with the format "YYYY-MM-DD hh:mm:ss".
// Supported datetime structures: FILETIME and Dos_Date_Time.
//
// @Parameters:
// 1. date_time - The datetime value to format. If this value is zero, the resulting string will be empty.
// 2. formatted_string - The resulting formatted string. This string must be able to hold MAX_FORMATTED_DATE_TIME_CHARS
// characters.
//
// @Returns: True if the datetime value was formatted correctly. Otherwise, it returns false and the resulting
// formatted string will be empty.

bool format_filetime_date_time(FILETIME date_time, TCHAR* formatted_string)
{
	if(date_time.dwLowDateTime == 0 && date_time.dwHighDateTime == 0)
	{
		*formatted_string = TEXT('\0');
		return true;
	}

	SYSTEMTIME dt = {};
	bool success = FileTimeToSystemTime(&date_time, &dt) != 0;
	success = success && SUCCEEDED(StringCchPrintf(formatted_string, MAX_FORMATTED_DATE_TIME_CHARS, TEXT("%4hu-%02hu-%02hu %02hu:%02hu:%02hu"), dt.wYear, dt.wMonth, dt.wDay, dt.wHour, dt.wMinute, dt.wSecond));

	if(!success)
	{
		log_print(LOG_ERROR, "Format Filetime DateTime: Failed to format the FILETIME datetime (high = %lu, low = %lu) with the error code.", date_time.dwHighDateTime, date_time.dwLowDateTime, GetLastError());
		*formatted_string = TEXT('\0');
	}

	return success;
}

bool format_dos_date_time(Dos_Date_Time date_time, TCHAR* formatted_string)
{
	if(date_time.date == 0 && date_time.time == 0)
	{
		*formatted_string = TEXT('\0');
		return true;
	}

	FILETIME filetime = {};
	bool success = DosDateTimeToFileTime(date_time.date, date_time.time, &filetime) != 0;
	success = success && format_filetime_date_time(filetime, formatted_string);

	if(!success)
	{
		log_print(LOG_ERROR, "Format Dos DateTime: Failed to format the DOS datetime (date = %I32u, time = %I32u) with the error code.", date_time.date, date_time.time, GetLastError());
		*formatted_string = TEXT('\0');
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
bool string_starts_with(const TCHAR* str, const TCHAR* prefix, bool optional_case_insensitive)
{
	if(optional_case_insensitive) 	return _tcsncicmp(str, prefix, _tcslen(prefix)) == 0;
	else 							return _tcsnccmp(str, prefix, _tcslen(prefix)) == 0;
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
// Supported integer sizes: u32, u64, and s64.
//
// @Parameters:
// 1. value - The integer value to convert.
// 2. result_string - The resulting string conversion.
//
// @Returns: True if the integer was converted correctly. Otherwise, it returns false.

static const size_t INT_FORMAT_RADIX = 10;
bool convert_u32_to_string(u32 value, TCHAR* result_string)
{
	bool success = _ultot_s(value, result_string, MAX_INT32_CHARS, INT_FORMAT_RADIX) == 0;
	if(!success) *result_string = TEXT('\0');
	return success;
}
bool convert_u64_to_string(u64 value, TCHAR* result_string)
{
	bool success = _ui64tot_s(value, result_string, MAX_INT64_CHARS, INT_FORMAT_RADIX) == 0;
	if(!success) *result_string = TEXT('\0');
	return success;
}
bool convert_s64_to_string(s64 value, TCHAR* result_string)
{
	bool success = _i64tot_s(value, result_string, MAX_INT64_CHARS, INT_FORMAT_RADIX) == 0;
	if(!success) *result_string = TEXT('\0');
	return success;
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
	bool success = false;

	if(TEXT('0') <= hex_char && hex_char <= TEXT('9'))
	{
		*result_integer = (u8) (hex_char - TEXT('0'));
		success = true;
	}
	else if(TEXT('a') <= hex_char && hex_char <= TEXT('f'))
	{
		*result_integer = (u8) (hex_char - TEXT('a') + 0x0A);
		success = true;
	}
	else if(TEXT('A') <= hex_char && hex_char <= TEXT('F'))
	{
		*result_integer = (u8) (hex_char - TEXT('A') + 0x0A);
		success = true;
	}
	else
	{
		log_print(LOG_ERROR, "Convert Hexadecimal Character To Integer: The character '%c' (%hhd) cannot be converted into an integer.", hex_char, (char) hex_char);
	}

	return success;
}

// Converts the first two hexadecimal characters in a string to a byte. Any third and remaining characters are ignored.
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
	TCHAR char_2 = (char_1 != TEXT('\0')) ? (*(byte_string+1)) : (TEXT('\0'));
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

// Converts an ANSI string to a TCHAR one and copies it to an arena.
// On the Windows 98 and ME builds, this function simply copies the ANSI string.
// On the Windows 2000 through 10 builds, this function converts the ANSI string to a wide one.
//
// @Parameters:
// 1. arena - The Arena structure that will receive the converted TCHAR string.
// 2. ansi_string - The ANSI string to convert and copy to the arena.
//
// @Returns: The pointer to the TCHAR string on success. Otherwise, it returns NULL.
TCHAR* copy_ansi_string_to_tchar(Arena* arena, const char* ansi_string)
{
	#ifdef BUILD_9X
		// In Windows 98 and ME, the resulting TCHAR string is also an ANSI string.
		return push_string_to_arena(arena, ansi_string);
	#else
		// In Windows 2000 onwards, we'll have to find how much space we need and then convert
		// the ANSI string to a Wide one.
		int num_chars_required = MultiByteToWideChar(CP_ACP, 0, ansi_string, -1, NULL, 0);

		if(num_chars_required == 0)
		{
			log_print(LOG_ERROR, "Copy Ansi String To Tchar: Failed to find the number of characters necessary to represent '%hs' as a Wide string with the error code %lu.", ansi_string, GetLastError());
			_ASSERT(false);
			return NULL;
		}

		int size_required = num_chars_required * sizeof(wchar_t);
		wchar_t* wide_string = push_arena(arena, size_required, wchar_t);

		if(MultiByteToWideChar(CP_ACP, 0, ansi_string, -1, wide_string, num_chars_required) == 0)
		{
			log_print(LOG_ERROR, "Copy Ansi String To Tchar: Failed to convert '%hs' to a Wide string with the error code %lu.", ansi_string, GetLastError());
			_ASSERT(false);
			return NULL;
		}

		return wide_string;
	#endif
}

// Skips to the null terminator character in a string.
//
// @Parameters:
// 1. str - The string.
//
// @Returns: The end of the string.
TCHAR* skip_to_end_of_string(TCHAR* str)
{
	while(*str != TEXT('\0')) ++str;
	return str;
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
		first_string = skip_to_end_of_string(first_string);
		++first_string;
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
// This syntax follows RFC 3986, although this function will accept empty path and host components in order to accommodate
// certain URL structures that may show up in cache databases. For example:
// - No path: "http://www.example.com/", where the server would serve, for example, "http://www.example.com/index.html".
// - No Host: "file:///C:\Users\<Username>\Desktop", where the host is localhost.
// Note that even an empty path requires a separator after the authority. For example, "http://www.example.com/" is valid
// but "http://www.example.com" isn't.
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
// @Returns: True if the function succeeds. Otherwise, false.
bool partition_url(Arena* arena, const TCHAR* original_url, Url_Parts* url_parts)
{
	ZeroMemory(url_parts, sizeof(Url_Parts));

	if(original_url == NULL) return false;

	TCHAR* url = push_string_to_arena(arena, original_url);
	TCHAR* remaining_url = NULL;
	TCHAR* scheme = _tcstok_s(url, TEXT(":"), &remaining_url);

	if(scheme == NULL || string_is_empty(scheme))
	{
		log_print(LOG_WARNING, "Partition Url: Missing the scheme in '%s'.", original_url);
		_ASSERT(false);
		return false;
	}

	url_parts->scheme = push_string_to_arena(arena, scheme);

	// Check if the authority exists.
	if(*remaining_url == TEXT('/') && *(remaining_url+1) == TEXT('/'))
	{
		remaining_url += 2;

		// If the authority is empty (e.g. "file:///C:\Path\File.ext")
		if(*remaining_url == TEXT('/'))
		{
			url_parts->host = push_string_to_arena(arena, TEXT(""));
			remaining_url += 1;
		}
		else
		{
			// Check if the path has at least one separator (either a URL path or Windows directory separator).
			// Otherwise, we wouldn't know where the authority ends and the path begins. Here we already know that
			// the authority exists, but we need to know where the host/port end and the path begins.
			bool has_path_separator = (_tcschr(remaining_url, TEXT('/')) != NULL) || (_tcschr(remaining_url, TEXT('\\')) != NULL);
			TCHAR* authority = _tcstok_s(NULL, TEXT("/\\"), &remaining_url);

			if(has_path_separator && authority != NULL)
			{
				TCHAR* remaining_authority = NULL;
				
				// Split the userinfo from the rest of the authority (host and port) if it exists.
				// Otherwise, the userinfo would be set to the remaining authority when it didn't exist.
				bool has_userinfo = _tcschr(authority, TEXT('@')) != NULL;
				TCHAR* userinfo = (has_userinfo) ? (_tcstok_s(authority, TEXT("@"), &remaining_authority)) : (NULL);

				// If the userinfo exists, we'll split the remaining authority into the host and port.
				// E.g. "userinfo@www.example.com:80" -> "www.example.com:80" -> "www.example.com" + "80"
				// If it doesn't, we'll split starting at the beginning of the authority.
				// E.g. "www.example.com:80" -> "www.example.com" + "80"
				TCHAR* string_to_split = (has_userinfo) ? (NULL) : (authority);
				TCHAR* host = _tcstok_s(string_to_split, TEXT(":"), &remaining_authority);
				// If the remaining authority now points to the end of the string, then there's no port.
				// Otherwise, whatever remains of the authority is the port. We can do this because of that
				// initial split with the path separator.
				TCHAR* port = (!string_is_empty(remaining_authority)) ? (remaining_authority) : (NULL);

				if(host == NULL) host = TEXT("");
				url_parts->host = push_string_to_arena(arena, host);

				// Leave the userinfo and port set to NULL or copy their actual value if they exist.
				if(userinfo != NULL) url_parts->userinfo = push_string_to_arena(arena, userinfo);
				if(port != NULL) url_parts->port = push_string_to_arena(arena, port);
			}
			else
			{
				// We don't have a path or can't distinguish between the authority and the path.
				// E.g. "http://www.example.com" or "http://www.example.compath".
				log_print(LOG_WARNING, "Partition Url: Found authority but missing the path in '%s'.", original_url);
				_ASSERT(false);
				return false;
			}
		}
	}

	// The path starts after the scheme or authority (if it exists) and ends at the query symbol or at the
	// end of the string (if there's no query).
	TCHAR* query_in_path = _tcschr(remaining_url, TEXT('?'));
	TCHAR* fragment_in_path = _tcschr(remaining_url, TEXT('#'));
	bool does_fragment_appear_before_query = (fragment_in_path != NULL && query_in_path == NULL)
							|| (fragment_in_path != NULL && query_in_path != NULL && fragment_in_path < query_in_path);

	TCHAR* path = _tcstok_s(NULL, TEXT("?#"), &remaining_url);
	// We'll allow empty paths (even though they're required by the RFC) because the cache's index/database
	// file might store some of them like this. E.g. the resource "http://www.example.com/index.html" might
	// have its URL stored as "http://www.example.com/" since the server would know to serve the index.html
	// file for that request.
	if(path == NULL) path = TEXT("");
	url_parts->path = push_string_to_arena(arena, path);

	TCHAR* query = (does_fragment_appear_before_query) ? (NULL) : (_tcstok_s(NULL, TEXT("#"), &remaining_url));
	TCHAR* fragment = (!string_is_empty(remaining_url)) ? (remaining_url) : (NULL);

	// Leave the query and fragment set to NULL or copy their actual value if they exist.
	if(query != NULL) url_parts->query = push_string_to_arena(arena, query);
	if(fragment != NULL) url_parts->fragment = push_string_to_arena(arena, fragment);

	return true;
}

// Decodes a percent-encoded URL. It does not validate or perform any other checks on the URL.
//
// @Parameters:
// 1. url - The URL string to decode.
//
// @Returns: True unless one of the percent-encoded characters can't be decoded correctly. If it fails on an encoded character,
// any remaining ones are unchanged. This function succeeds if the URL is NULL.
bool decode_url(TCHAR* url)
{
	bool success = true;

	if(url != NULL)
	{
		while(*url != TEXT('\0'))
		{
			// Decode percent-encoded characters.
			// @Docs: https://tools.ietf.org/html/rfc3986#section-2.1
			if(*url == TEXT('%'))
			{
				TCHAR* after_percent_sign = url + 1;
				u8 decoded_char = 0;

				if(convert_hexadecimal_string_to_byte(after_percent_sign, &decoded_char))
				{
					TCHAR* url_end_of_encoded_char = url + 2;
					MoveMemory(url, url_end_of_encoded_char, string_size(url_end_of_encoded_char));
					*url = decoded_char;
				}
				else
				{
					// If the percent sign isn't followed by two characters or if the characters
					// don't represent a valid hexadecimal value.
					success = false;
					break;
				}
			}
			else if(*url == TEXT('+'))
			{
				*url = TEXT(' ');
			}
			
			++url;
		}
	}

	if(!success)
	{
		log_print(LOG_ERROR, "Decode Url: Found an invalid percent-encoded character while decoding the URL. The remaining encoded URL is '%s'.", url);
		_ASSERT(false);
	}

	return success;
}

// Converts the host and path in a URL into a Windows directory path.
// For example: "http://www.example.com:80/path1/path2/file.ext?id=1#top" -> "www.example.com\path1\path2"
//
// @Parameters:
// 1. arena - The Arena structure where any intermediary strings are stored.
// 2. url - The URL to convert into a path.
// 3. path - The buffer which receives the converted path. This buffer must be able to hold MAX_PATH_CHARS characters.
//
// @Returns: True if it succeeds. Otherwise, false.
static void correct_url_path_characters(TCHAR* path);
static void truncate_path_components(TCHAR* path);
static void correct_reserved_path_components(TCHAR* path);

static bool convert_url_to_path(Arena* arena, const TCHAR* url, TCHAR* result_path)
{
	bool success = true;

	Url_Parts url_parts = {};
	if(partition_url(arena, url, &url_parts))
	{
		_ASSERT(url_parts.path != NULL);

		result_path[0] = TEXT('\0');

		if(url_parts.host != NULL)
		{
			success = success && PathAppend(result_path, url_parts.host);
		}

		correct_url_path_characters(url_parts.path);
		success = success && PathAppend(result_path, url_parts.path);

		// Remove the resource's filename so it's not part of the final path.
		// Because of the replacement above, we know that the path separator is a backslash.
		TCHAR* last_separator = _tcsrchr(result_path, TEXT('\\'));
		if(last_separator != NULL) *last_separator = TEXT('\0');

		truncate_path_components(result_path);
		correct_reserved_path_components(result_path);
	}
	else
	{
		success = false;
	}

	return success;
}

/*
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>> PATH MANIPULATION
	>>>>>>>>>>>>>>>>>>>>
	>>>>>>>>>>>>>>>>>>>>

	@NextSection
*/

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
//
// @Returns: The beginning of the file extension in the path. If the path is NULL, this function returns NULL.
// If the filename doesn't contain a file extension, the end of the string is returned.
TCHAR* skip_to_file_extension(TCHAR* path, bool optional_include_period, bool optional_get_first_extension)
{
	TCHAR* file_extension = NULL;

	if(path != NULL)
	{
		while(*path != TEXT('\0'))
		{
			if( *path == TEXT('.') && ( optional_include_period || *(path+1) != TEXT('\0') ) )
			{
				file_extension = (optional_include_period) ? (path) : (path + 1);
				if(optional_get_first_extension) break;
			}
			else if(*path == TEXT('\\'))
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

// Retrieves the absolute version of a specified path. The path may be relative or absolute. This function has two overloads: 
// - get_full_path_name(2 or 3), which copies the output to another buffer.
// - get_full_path_name(1), which copies the output to the same buffer.
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
	TCHAR full_path[MAX_PATH_CHARS] = TEXT("");
	bool success = get_full_path_name(result_full_path, full_path);
	if(success) StringCchCopy(result_full_path, MAX_PATH_CHARS, full_path);
	return success;
}

// Retrieves the absolute path of a special folder, identified by its CSIDL (constant special item ID list).
// See this page for a list of possible CSIDLs: https://docs.microsoft.com/en-us/windows/win32/shell/csidl
// Note that each different target build has a version requirement for these CSIDLs:
// - Windows 98/ME: version 4.72 or older.
// - Windows 2000 through 10: version 5.0 or older.
// E.g. This function fails if you use CSIDL_LOCAL_APPDATA (available starting with version 5.0) in the Windows 98/ME builds.
//
// @Parameters:
// 1. csidl - The CSIDL that identifies the special folder.
// 2. result_path - The buffer which receives the absolute path to the special folder. This buffer must be at least MAX_PATH
// characters in size.
//
// @Returns: True if it succeeds. Otherwise, false.
bool get_special_folder_path(int csidl, TCHAR* result_path)
{
	#ifdef BUILD_9X
		return SHGetSpecialFolderPathA(NULL, result_path, csidl, FALSE) == TRUE;
	#else
		// @Note:
		// Third parameter (hToken): "Microsoft Windows 2000 and earlier: Always set this parameter to NULL."
		return SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, result_path));
	#endif
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
static void correct_url_path_characters(TCHAR* path)
{
	if(path == NULL) return;

	bool is_first_path_segment = true;
	TCHAR* last_char = NULL;

	while(*path != TEXT('\0'))
	{
		last_char = path;

		switch(*path)
		{
			case(TEXT('/')):
			{
				*path = TEXT('\\');
			} // Intentional fallthrough.

			case(TEXT('\\')):
			{
				is_first_path_segment = false;

				// Remove double backslashes, leaving only one of them.
				// Otherwise, we'd run into ERROR_INVALID_NAME errors in copy_file_using_url_directory_structure().
				TCHAR* next_char = path + 1;
				if( *next_char == TEXT('\\') || *next_char == TEXT('/') )
				{
					MoveMemory(path, next_char, string_size(next_char));
					// Make sure to also replace the forward slash in the next character.
					*path = TEXT('\\');
				}

			} break;

			case(TEXT(':')):
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
				if( is_first_path_segment && (*next_char == TEXT('\\') || *next_char == TEXT('/')) )
				{
					MoveMemory(path, next_char, string_size(next_char));
				}
				else
				{
					*path = TEXT('_');
				}

			} break;

			case(TEXT('<')):
			case(TEXT('>')):
			case(TEXT('\"')):
			case(TEXT('|')):
			case(TEXT('?')):
			case(TEXT('*')):
			{
				// Replace reserved characters with underscores.
				// Otherwise, we'd run into ERROR_INVALID_NAME errors in copy_file_using_url_directory_structure().
				*path = TEXT('_');
			} break;

			default:
			{
				// Replace characters whose integer representations are in the range from 1 through 31 with underscores.
				if(1 <= *path && *path <= 31)
				{
					*path = TEXT('_');
				}

			} break;
		}

		++path;
	}

	// Replace a trailing period or space with an underscore.
	// Otherwise, we'd run into problems when trying to delete these files/directories.
	if(last_char != NULL && (*last_char == TEXT('.') || *last_char == TEXT(' ')) )
	{
		*last_char = TEXT('_');
	}
}

// Truncates each component in a path to the maximum component length supported by the current file system.
// For example, assuming that this limit is 255 characters:
// "C:\Path\<255 Characters>ABC\RemainingPath" -> "C:\Path\<255 Characters>\RemainingPath"
//
// @Parameters:
// 1. path - The path to modify.
//
// @Returns: True if any components were truncated. Otherwise, false.
static void truncate_path_components(TCHAR* path)
{
	TCHAR* component_begin = path;
	bool is_first_char = true;

	DWORD maximum_component_length = 0;
	if(!GetVolumeInformationA(NULL, NULL, 0, NULL, &maximum_component_length, NULL, NULL, 0))
	{
		maximum_component_length = 255;
		log_print(LOG_WARNING, "Truncate Path Segments: Failed to get the maximum component length with the error code %lu. This value will default to %lu.", GetLastError(), maximum_component_length);
		_ASSERT(false);
	}

	WHILE_TRUE()
	{
		bool is_end_of_string = (*path == TEXT('\0'));

		if( (*path == TEXT('\\') || is_end_of_string) && !is_first_char )
		{
			TCHAR* component_end = path;

			TCHAR previous_char = *component_end;
			*component_end = TEXT('\0');
			size_t num_component_chars = string_length(component_begin);
			*component_end = previous_char;

			component_begin = component_end + 1;

			if(num_component_chars > maximum_component_length)
			{
				// "C:\Path\<255 chars>123\ABC" -> "C:\Path\<255 chars>\ABC"
				//          ^ begin       ^ end
				// "C:\Path\ABC\<255 chars>123" -> "C:\Path\ABC\<255 chars>"
				//              ^ begin       ^ end
				size_t num_chars_over_limit = num_component_chars - maximum_component_length;
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
	*file_extension = TEXT('\0');

	int result = _tcsicmp(name, reserved_name);

	*file_extension = previous_char;

	return result;
}

// Corrects any component in a path that uses a reserved name (or a reserved name followed immediately  by a file extension) so it may
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
static void correct_reserved_path_components(TCHAR* path)
{
	const TCHAR* const SORTED_RESERVED_NAMES[] =
	{
		TEXT("AUX"),
		TEXT("COM1"), TEXT("COM2"), TEXT("COM3"), TEXT("COM4"), TEXT("COM5"), TEXT("COM6"), TEXT("COM7"), TEXT("COM8"), TEXT("COM9"),
		TEXT("CON"),
		TEXT("LPT1"), TEXT("LPT2"), TEXT("LPT3"), TEXT("LPT4"), TEXT("LPT5"), TEXT("LPT6"), TEXT("LPT7"), TEXT("LPT8"), TEXT("LPT9"),
		TEXT("NUL"),
		TEXT("PRN")
	};
	const size_t NUM_RESERVED_NAMES = _countof(SORTED_RESERVED_NAMES);

	TCHAR* component_begin = path;
	WHILE_TRUE()
	{
		bool is_end_of_string = (*path == TEXT('\0'));

		if((*path == TEXT('\\') || is_end_of_string))
		{
			TCHAR previous_char = *path;
			*path = TEXT('\0');

			void* search_result = bsearch(	&component_begin, SORTED_RESERVED_NAMES,
											NUM_RESERVED_NAMES, sizeof(TCHAR*),
											compare_reserved_names);
			if(search_result != NULL)
			{
				log_print(LOG_WARNING, "Correct Reserved Path Components: Found a path component that uses a reserved name: '%s'.", component_begin);
				*component_begin = TEXT('_');
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

// Determines whether or not a file exists given its path.
//
// @Parameters:
// 1. file_path - The path to the file.
//
// @Returns: True if the function succeeds. Otherwise, false. This function returns false if the path points to a directory.
// This function fails if the path is empty or if the file's attributes cannot be determined.
bool does_file_exist(const TCHAR* file_path)
{
	if(string_is_empty(file_path)) return false;

	DWORD attributes = GetFileAttributes(file_path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

// Determines the size in bytes of a file from its handle.
//
// @Parameters:
// 1. file_handle - The handle of the file whose size is of interest.
// 2. result_file_size - The resulting file size in bytes.
//
// @Returns: True if the function succeeds. Otherwise, it returns false and the resulting file size is undefined.
bool get_file_size(HANDLE file_handle, u64* result_file_size)
{
	#ifdef BUILD_9X
		DWORD file_size_high;
		DWORD file_size_low = GetFileSize(file_handle, &file_size_high);
		bool success = !( (file_size_low == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR) );
		if(success) *result_file_size = combine_high_and_low_u32s_into_u64(file_size_high, file_size_low);
		return success;
	#else
		LARGE_INTEGER file_size;
		bool success = GetFileSizeEx(file_handle, &file_size) == TRUE;
		if(success) *result_file_size = file_size.QuadPart;
		return success;
	#endif
}

// Closes a handle and sets its value to INVALID_HANDLE_VALUE.
//
// @Parameters:
// 1. handle - The address of the handle to close.
//
// @Returns: Nothing.
void safe_close_handle(HANDLE* handle)
{
	if(*handle != INVALID_HANDLE_VALUE && *handle != NULL)
	{
		CloseHandle(*handle);
		*handle = INVALID_HANDLE_VALUE;
	}
}

// Closes a file search handle and sets its value to INVALID_HANDLE_VALUE.
//
// @Parameters:
// 1. search_handle - The address of the handle to close.
//
// @Returns: Nothing.
void safe_find_close(HANDLE* search_handle)
{
	if(*search_handle != INVALID_HANDLE_VALUE && *search_handle != NULL)
	{
		FindClose(*search_handle);
		*search_handle = INVALID_HANDLE_VALUE;
	}
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
		UnmapViewOfFile(*base_address);
		*base_address = NULL;
		#ifdef DEBUG
			DEBUG_MEMORY_MAPPING_BASE_ADDRESS = retreat_bytes(DEBUG_MEMORY_MAPPING_BASE_ADDRESS, DEBUG_BASE_ADDRESS_INCREMENT);
		#endif
	}
	else
	{
		log_print(LOG_WARNING, "Safe Unmap View Of File: Attempted to unmap an invalid address.");
		_ASSERT(false);
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
// 1. directory_path - The path to the directory that contains the current object.
// 2. find_data - A pointer to the WIN32_FIND_DATA structure of the current object.
// 3. user_data - A pointer to any additional data that was passed to traverse_directory_objects().
//
// @CallbackReturns: Nothing.
//
// @Returns: Nothing.
void traverse_directory_objects(const TCHAR* path, const TCHAR* search_query,
								u32 traversal_flags, bool should_traverse_subdirectories,
								Traverse_Directory_Callback* callback_function, void* user_data)
{
	/*
		>>>> Traverse the files and directories that match the search query.
	*/

	TCHAR search_path[MAX_PATH_CHARS] = TEXT("");
	PathCombine(search_path, path, search_query);

	WIN32_FIND_DATA find_data = {};
	HANDLE search_handle = FindFirstFile(search_path, &find_data);
	
	bool found_object = search_handle != INVALID_HANDLE_VALUE;
	while(found_object)
	{
		TCHAR* filename = find_data.cFileName;
		if(!strings_are_equal(filename, TEXT(".")) && !strings_are_equal(filename, TEXT("..")))
		{
			bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			bool should_process_object = 	( (traversal_flags & TRAVERSE_FILES) && !is_directory )
										|| 	( (traversal_flags & TRAVERSE_DIRECTORIES) && is_directory );

			if(should_process_object)
			{
				callback_function(path, &find_data, user_data);
			}
		}

		found_object = FindNextFile(search_handle, &find_data) == TRUE;
	}

	safe_find_close(&search_handle);

	/*
		>>>> Traverse every subdirectory in this directory. We can't use the same search query here, otherwise we'd exclude directories.
	*/

	if(should_traverse_subdirectories)
	{
		PathCombine(search_path, path, TEXT("*"));
		search_handle = FindFirstFile(search_path, &find_data);
		
		found_object = search_handle != INVALID_HANDLE_VALUE;
		while(found_object)
		{
			TCHAR* filename = find_data.cFileName;
			if(!strings_are_equal(filename, TEXT(".")) && !strings_are_equal(filename, TEXT("..")))
			{
				bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				if(is_directory)
				{
					TCHAR subdirectory_path[MAX_PATH_CHARS] = TEXT("");
					PathCombine(subdirectory_path, path, filename);
					_ASSERT(!string_is_empty(subdirectory_path));

					traverse_directory_objects(	subdirectory_path, search_query,
												traversal_flags, should_traverse_subdirectories,
												callback_function, user_data);
				}			
			}

			found_object = FindNextFile(search_handle, &find_data) == TRUE;
		}

		safe_find_close(&search_handle);		
	}
}

// Creates a directory given its path, and any intermediate directories that don't exist.
//
// This function was created to replace SHCreateDirectoryEx() from the Shell API since it was only available from version 5.0
// onwards (Windows 2000 or later).
//
// @Parameters:
// 1. path_to_create - The path of the directory to create.
//
// @Returns: Nothing.
void create_directories(const TCHAR* path_to_create)
{
	// Default string length limit for the ANSI version of CreateDirectory().
	const size_t MAX_SHORT_FILENAME_CHARS = 12;
	const size_t MAX_CREATE_DIRECTORY_PATH_CHARS = MAX_PATH_CHARS - MAX_SHORT_FILENAME_CHARS;

	TCHAR path[MAX_CREATE_DIRECTORY_PATH_CHARS] = TEXT("");

	if(!get_full_path_name(path_to_create, path, MAX_CREATE_DIRECTORY_PATH_CHARS))
	{
		log_print(LOG_ERROR, "Create Directory: Failed to create the directory in '%s' because its fully qualified path could not be determined with the error code %lu.", path_to_create, GetLastError());
		_ASSERT(false);
		return;
	}

	for(size_t i = 0; i < MAX_CREATE_DIRECTORY_PATH_CHARS; ++i)
	{
		if(path[i] == TEXT('\0'))
		{
			// Create the last directory in the path.
			CreateDirectory(path, NULL);
			break;
		}
		else if(path[i] == TEXT('\\'))
		{
			// Make sure we create any intermediate directories by truncating the string at each path separator.
			// We do it this way since CreateDirectory() fails if a single intermediate directory doesn't exist.
			path[i] = TEXT('\0');
			CreateDirectory(path, NULL);
			path[i] = TEXT('\\');
		}
	}
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
		log_print(LOG_ERROR, "Delete Directory: Failed to delete the directory since its path was empty.");
		_ASSERT(false);
		return false;
	}

	// Ensure that we have the fully qualified path since its required by SHFileOperation().
	TCHAR path_to_delete[MAX_PATH_CHARS + 1] = TEXT("");
	if(!get_full_path_name(directory_path, path_to_delete))
	{
		log_print(LOG_ERROR, "Delete Directory: Failed to delete the directory '%s' since its fully qualified path couldn't be determined.", directory_path);
		_ASSERT(false);
		return false;
	}

	// Ensure that the path has two null terminators since its required by SHFileOperation().
	// Remember that MAX_PATH_CHARS already includes the first null terminator.
	size_t num_path_chars = string_length(path_to_delete);
	path_to_delete[num_path_chars + 1] = TEXT('\0');

	SHFILEOPSTRUCT file_operation = {};
	file_operation.wFunc = FO_DELETE;
	file_operation.pFrom = path_to_delete;
	// Perform the operation silently, presenting no UI to the user.
	file_operation.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;

	int error_code = SHFileOperation(&file_operation);
	if(error_code != 0)
	{
		log_print(LOG_ERROR, "Delete Directory: Failed to delete the directory '%s' and its contents with error code %d.", directory_path, error_code);
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
	*result_directory_path = TEXT('\0');

	bool create_success = false;
	// We'll use this value to generate the directory's name since GetTempFileName() creates a file if we let it
	// generate the name. This isn't a robust way to generate unique names, but it works for our use case.
	u32 unique_id = GetTickCount();
	do
	{
		create_success = GetTempFileName(base_temporary_path, TEXT("WCE"), unique_id, result_directory_path) != 0
						&& CreateDirectory(result_directory_path, NULL) == TRUE;
		++unique_id;
	} while(!create_success && GetLastError() == ERROR_ALREADY_EXISTS);

	return create_success;
}


// Copies a file to a temporary location with a unique name, and returns the destination path and file handle.
// The file is deleted when this handle is closed. This ensures that, even if the program crashes, Windows closes the handle
// and the file is deleted.
//
// @Parameters:
// 1. file_source_path - The source path of the file to copy.
// 2. base_temporary_path - The base path where the file will be copied to.
// 3. result_file_destination_path - The resulting destination path for the temporary file.
// 4. result_handle - The resulting handle for the temporary file. This handle has the FILE_FLAG_DELETE_ON_CLOSE flag set.
//
// @Returns: True if the file was copied and its handle was retrieved successfully. Otherwise, false.
// If it fails to retrieve the handle, this function will delete the copied file and return false.
bool copy_to_temporary_file(const TCHAR* file_source_path, const TCHAR* base_temporary_path,
							TCHAR* result_file_destination_path, HANDLE* result_handle)
{
	*result_file_destination_path = TEXT('\0');
	*result_handle = INVALID_HANDLE_VALUE;

	bool copy_success = false;
	bool get_handle_success = false;

	copy_success = GetTempFileName(base_temporary_path, TEXT("WCE"), 0, result_file_destination_path) != 0
					&& CopyFile(file_source_path, result_file_destination_path, FALSE) == TRUE;

	if(copy_success)
	{
		*result_handle = CreateFile(result_file_destination_path,
									GENERIC_READ,
									0,
									NULL,
									OPEN_EXISTING,
									FILE_FLAG_DELETE_ON_CLOSE,
									NULL);

		get_handle_success = *result_handle != INVALID_HANDLE_VALUE;

		// If we couldn't get the handle, the function will fail, so we'll make sure to delete the file.
		if(!get_handle_success) DeleteFile(result_file_destination_path);
	}

	return copy_success && get_handle_success;
}

// Creates an empty file with normal attributes.
//
// @Parameters:
// 1. file_path - The path where the file will be created.
//
// @Returns: True if the file was created successfully. Otherwise, false. This function fails if the file in the specified
// path already exists. In this case, GetLastError() returns ERROR_FILE_EXISTS.
#if defined(DEBUG) && defined(EXPORT_EMPTY_FILES)
	static bool create_empty_file(const TCHAR* file_path)
	{
		HANDLE empty_file = CreateFile(file_path, 0, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		DWORD error_code = GetLastError();
		
		bool success = empty_file != INVALID_HANDLE_VALUE;
		safe_close_handle(&empty_file);

		// We'll set the error code to be the one returned by CreateFile since CloseHandle would overwrite it.
		// This way, if the file already exists, calling GetLastError() after using this function returns ERROR_FILE_EXISTS.
		SetLastError(error_code);
		return success;
	}
#endif

// Copies a file using a given URL's directory structure. If the generated file path already exists, this function will resolve any
// naming collisions by adding a number to the filename. This function is a core part of the cache exporters.
//
// The final path is built by joining the following paths:
// 1. The base destination directory.
// 2. The host and path components of the URL (if a URL was passed to this function).
// 3. The filename.
//
// For example: "C:\Path" + "http://www.example.com:80/path/file.php?query#fragment" + "file.ext"
// results in: "C:\Path\www.example.com\path\file.ext"
//
// If this file already exists, a tilde followed by a number will be added before the file extension. This number will be incremented
// until there's no longer a naming collision. For example: "C:\Path\www.example.com\path\file~1.ext".
//
// Since the URL and filename can be invalid Windows paths, these may be modified accordingly (e.g. replacing invalid characters).
//
// Note that all of these paths are limited to MAX_PATH characters. This limit used to be extended by using the "\\?\" prefix
// on the Windows 2000 through 10 builds. However, in practice that would result in paths that would be too long for the File Explorer
// to delete. This is a problem for this application since the whole point is to get the average user to check their cache for lost
// web media files.
//
// Instead of failing in the cases where the final path length exceeds this limit, this function will attempt to copy the file to the base
// destination directory. Using the example above, this would be "C:\Path\file.ext". This limit may be exceed by either the URL structure
// or the filename.
//
// @Parameters:
// 1. arena - The Arena structure where any intermediary strings are stored.
// 2. full_file_path - The fully qualified path to the source file to copy.
// 3. full_base_directory_path - The fully qualified path to base destination directory.
// 4. url - The URL whose host and path components are converted into a Windows path. If this string is NULL, only the base directory
// and filename will be used.
// 5. filename - The filename to add to the end of the path.
//
// @Returns: True if the file was copied successfully. Otherwise, false. This function fails if the source file path is empty.
bool copy_file_using_url_directory_structure(	Arena* arena, const TCHAR* full_file_path, 
												const TCHAR* full_base_directory_path, const TCHAR* url, const TCHAR* filename)
{
	if(string_is_empty(full_file_path)) return false;

	// Leave space for the tilde character and the naming collision number (32-bit unsigned integer).
	// Note that both of these already take into account the null terminator, which cancels out the extra tilde character.

	// Copy Target = Base Destination Path
	TCHAR full_copy_target_path[MAX_PATH_CHARS] = TEXT("");
	StringCchCopy(full_copy_target_path, MAX_PATH_CHARS, full_base_directory_path);
	
	// Copy Target = Base Destination Path + Url Converted To Path (if it exists)
	if(url != NULL)
	{
		TCHAR url_path[MAX_PATH_CHARS] = TEXT("");
		bool build_target_success = convert_url_to_path(arena, url, url_path) && PathAppend(full_copy_target_path, url_path);
		if(!build_target_success)
		{
			console_print("The website directory structure for the file '%s' could not be created. This file will be copied to the base export directory instead.", filename);
			log_print(LOG_WARNING, "Copy File Using Url Structure: The website directory structure for the file '%s' could not be created. This file will be copied to the base export directory instead.", filename);
			StringCchCopy(full_copy_target_path, MAX_PATH_CHARS, full_base_directory_path);
		}
	}

	_ASSERT(!string_is_empty(full_copy_target_path));
	create_directories(full_copy_target_path);

	TCHAR corrected_filename[MAX_PATH_CHARS] = TEXT("");
	StringCchCopy(corrected_filename, MAX_PATH_CHARS, filename);
	correct_url_path_characters(corrected_filename);
	truncate_path_components(corrected_filename);
	correct_reserved_path_components(corrected_filename);

	// Copy Target = Base Destination Path + Url Converted To Path (if it exists) + Filename
	bool build_target_success = PathAppend(full_copy_target_path, corrected_filename) == TRUE;
	if(!build_target_success)
	{
		console_print("Could not add the filename '%s' to the website directory structure. This file will be copied to the base export directory instead.", filename);
		log_print(LOG_WARNING, "Copy File Using Url Structure: Could not add the filename '%s' to the website directory structure. This file will be copied to the base export directory instead.", filename);
		
		StringCchCopy(full_copy_target_path, MAX_PATH_CHARS, full_base_directory_path);
		if(!PathAppend(full_copy_target_path, corrected_filename))
		{
			console_print("Failed to build any valid path for the file '%s'. This file will not be copied.", filename);
			log_print(LOG_ERROR, "Copy File Using Url Structure: Failed to build any valid path for the file '%s'. This file will not be copied.", filename);
			return false;
		}
	}

	_ASSERT(!string_is_empty(full_copy_target_path));

	bool copy_success = false;
	#if defined(DEBUG) && defined(EXPORT_EMPTY_FILES)
		copy_success = create_empty_file(full_copy_target_path);
	#else
		copy_success = CopyFile(full_file_path, full_copy_target_path, TRUE) == TRUE;
	#endif

	u32 num_naming_collisions = 0;
	TCHAR unique_id[MAX_INT32_CHARS + 1] = TEXT("~");
	TCHAR full_unique_copy_target_path[MAX_PATH_CHARS] = TEXT("");

	TCHAR file_extension[MAX_PATH_CHARS] = TEXT("");
	StringCchCopy(file_extension, MAX_PATH_CHARS, skip_to_file_extension(corrected_filename, true));

	while(!copy_success && GetLastError() == ERROR_FILE_EXISTS)
	{
		++num_naming_collisions;
		if(num_naming_collisions == 0)
		{
			log_print(LOG_ERROR, "Copy File Using Url Structure: Wrapped around the number of naming collisions for the file '%s'. This file will not be copied.", filename);
			_ASSERT(false);
			break;
		}

		bool naming_success = SUCCEEDED(StringCchCopy(full_unique_copy_target_path, MAX_PATH_CHARS, full_copy_target_path));
		if(naming_success)
		{
			TCHAR* file_extension_in_target = skip_to_file_extension(full_unique_copy_target_path, true);
			*file_extension_in_target = TEXT('\0');
		}

		naming_success = naming_success && convert_u32_to_string(num_naming_collisions, unique_id + 1)
										&& SUCCEEDED(StringCchCat(full_unique_copy_target_path, MAX_PATH_CHARS, unique_id))
										&& SUCCEEDED(StringCchCat(full_unique_copy_target_path, MAX_PATH_CHARS, file_extension));

		if(!naming_success)
		{
			log_print(LOG_ERROR, "Copy File Using Url Structure: Failed to resolve the naming collision %I32u for the file '%s'. This file will not be copied.", num_naming_collisions, filename);
			_ASSERT(false);
			break;
		}
		
		#if defined(DEBUG) && defined(EXPORT_EMPTY_FILES)
			copy_success = create_empty_file(full_unique_copy_target_path);
		#else
			copy_success = CopyFile(full_file_path, full_unique_copy_target_path, TRUE) == TRUE;
		#endif
	}

	if(!copy_success)
	{
		log_print(LOG_ERROR, "Copy File Using Url Structure: Failed to copy '%s' to '%s' with the error code %lu.", filename, full_copy_target_path, GetLastError());
	}
	
	return copy_success;
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

					#ifdef DEBUG
						mapped_memory = MapViewOfFileEx(mapping_handle, desired_access, 0, 0, 0, DEBUG_MEMORY_MAPPING_BASE_ADDRESS);
						DEBUG_MEMORY_MAPPING_BASE_ADDRESS = advance_bytes(DEBUG_MEMORY_MAPPING_BASE_ADDRESS, DEBUG_BASE_ADDRESS_INCREMENT);
					#else
						mapped_memory = MapViewOfFile(mapping_handle, desired_access, 0, 0, 0);
					#endif

					if(mapped_memory == NULL)
					{
						log_print(LOG_ERROR, "Memory Mapping: Failed to map a view of the file with the error code %lu.", GetLastError());
					}
				}
				else
				{
					log_print(LOG_ERROR, "Memory Mapping: Failed to create the file mapping with the error code %lu.", GetLastError());
				}

				// About CloseHandle() and UnmapViewOfFile():
				// @Docs: "The order in which these functions are called does not matter." - CreateFileMapping - Win32 API Reference.
				safe_close_handle(&mapping_handle);
			}
			else
			{
				log_print(LOG_ERROR, "Memory Mapping: Failed to create a file mapping since the file is empty.");
			}
		}
		else
		{
			log_print(LOG_ERROR, "Memory Mapping: Failed to get the file's size with the error code %lu.", GetLastError());
		}
	}
	else
	{
		log_print(LOG_ERROR, "Memory Mapping: Failed to map the entire file into memory since the file handle is invalid.");
	}

	return mapped_memory;
}

// Behaves like memory_map_entire_file(2) but takes the file's path instead of its handle. This handle is instead returned
// along with the memory mapping's address and file size, and is created with exclusive access.
//
// @Parameters: In addition to the previous ones, this function also takes the following parameters:
// 1. file_path - The path to the file to map into memory.
// 2. result_file_handle - The address of a variable that receives the handle of the memory mapped file.
// 
// @Returns: See memory_map_entire_file(2). In addition to using the safe_unmap_view_of_file() function to unmap this memory,
// the resulting file handle should also be closed with safe_close_handle().
void* memory_map_entire_file(const TCHAR* file_path, HANDLE* result_file_handle, u64* result_file_size, bool optional_read_only)
{
	// @Docs:
	// PAGE_READONLY - "The file specified must have been created with the GENERIC_READ access right."
	// PAGE_WRITECOPY - "The files specified must have been created with the GENERIC_READ and GENERIC_WRITE access rights."
	// - CreateFileMapping - Win32 API Reference.
	DWORD desired_access = (optional_read_only) ? (GENERIC_READ) : (GENERIC_READ | GENERIC_WRITE);
	HANDLE file_handle = CreateFile(file_path,
									desired_access,
									0,
									NULL,
									OPEN_EXISTING,
									0,
									NULL);

	*result_file_handle = file_handle;

	return memory_map_entire_file(file_handle, result_file_size, optional_read_only);
}

// Reads a given number of bytes from the beginning of a file.
//
// @Parameters:
// 1. file_path - The path to the file to read.
// 2. file_buffer - The buffer that will receive the read bytes.
// 3. num_bytes_to_read - The size of the buffer.
// 4. optional_allow_reading_fewer_bytes - An optional parameter that specifies if this function is allowed to read fewer bytes than
// the ones requested. This value defaults to false.
// 5. optional_result_num_bytes_read - An optional parameter that receives number of bytes read. This value defaults to NULL.
// 
// @Returns: True if the file's contents were read successfully. Otherwise, false.
// This function fails under the following conditions:
// 1. The requested number of bytes is zero.
// 2. The path to the file is empty or NULL.
// 3. It read fewer bytes than the specified value and optional_allow_reading_fewer_bytes is false or not specified.
bool read_first_file_bytes(	const TCHAR* file_path, void* file_buffer, u32 num_bytes_to_read,
							bool optional_allow_reading_fewer_bytes, u32* optional_result_num_bytes_read)
{
	if(num_bytes_to_read == 0 || file_path == NULL || string_is_empty(file_path)) return false;

	bool success = false;
	HANDLE file_handle = CreateFile( file_path,
								     GENERIC_READ,
								     FILE_SHARE_READ,
								     NULL,
								     OPEN_EXISTING,
								     0,
								     NULL);

	if(file_handle != INVALID_HANDLE_VALUE)
	{
		DWORD num_bytes_read = 0;
		success = ReadFile(file_handle, file_buffer, num_bytes_to_read, &num_bytes_read, NULL)
				&& ( (num_bytes_read == num_bytes_to_read) || optional_allow_reading_fewer_bytes );
		safe_close_handle(&file_handle);

		if(optional_result_num_bytes_read != NULL) *optional_result_num_bytes_read = num_bytes_read;
	}

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
// Since this function returns true on success, you can short-circuit various calls if the data you're interested in is located
// in multiple registry keys. For example:
// 		query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "svcVersion", ..., ...)
// || 	query_registry(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", "Version", ..., ...);
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
// 5. value_data_size - The size of the buffer in bytes. Be sure that this includes the null terminator and is able to hold
// strings of the desired type (ANSI or Wide).
// 
// @Returns: True if the value was retrieved successfully. Otherwise, false.
bool tchar_query_registry(HKEY hkey, const TCHAR* key_name, const TCHAR* value_name, TCHAR* value_data, u32 value_data_size)
{
	LONG error_code = ERROR_SUCCESS;
	bool success = true;

	HKEY opened_key = NULL;
	error_code = RegOpenKeyEx(hkey, key_name, 0, KEY_QUERY_VALUE, &opened_key);
	success = (error_code == ERROR_SUCCESS);

	if(!success)
	{
		log_print(LOG_ERROR, "Query Registry: Failed to open the registry key '%s' with the error code.", key_name, error_code);
		// These registry functions only return this value, they don't set the last error code.
		// We'll do it ourselves for consistent error handling when calling our function.
		SetLastError(error_code);
		return success;
	}

	DWORD value_data_type = REG_NONE;
	DWORD actual_value_data_size = value_data_size;
	// When RegQueryValueEx() returns, actual_value_data_size contains the size of the data that was copied to the buffer.
	// For strings, this size includes any terminating null characters unless the data was stored without them.
	error_code = RegQueryValueEx(opened_key, value_name, NULL, &value_data_type, (LPBYTE) value_data, &actual_value_data_size);
	success = (error_code == ERROR_SUCCESS);
	RegCloseKey(opened_key);

	// For now, we'll only handle simple string. Strings with expandable environment variables are skipped (REG_EXPAND_SZ).
	if(success && (value_data_type != REG_SZ))
	{
		log_print(LOG_ERROR, "Query Registry: Unsupported data type %lu in registry value '%s\\%s'.", value_data_type, key_name, value_name);
		error_code = ERROR_NOT_SUPPORTED;
		success = false;
	}

	// According to the documentation, we need to ensure that string values null terminated. Whoever calls this function should
	// always guarantee that the buffer is able to hold whatever the possible string values are plus a null terminator.
	size_t num_value_data_chars = actual_value_data_size / sizeof(TCHAR);
	if( success && (value_data[num_value_data_chars - 1] != TEXT('\0')) )
	{	
		value_data[num_value_data_chars] = TEXT('\0');
	}

	SetLastError(error_code);
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
		log_print(LOG_ERROR, "Create Log File: Attempted to create a second log file in '%s' when the current one is still open.", log_file_path);
		_ASSERT(false);
		return false;
	}

	// Create any missing intermediate directories. 
	TCHAR full_log_directory_path[MAX_PATH_CHARS] = TEXT("");
	get_full_path_name(log_file_path, full_log_directory_path);
	PathAppend(full_log_directory_path, TEXT(".."));
	
	create_directories(full_log_directory_path);

	GLOBAL_LOG_FILE_HANDLE = CreateFile(log_file_path,
										GENERIC_WRITE,
										FILE_SHARE_READ,
										NULL,
										CREATE_ALWAYS,
										FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
										NULL);

	return GLOBAL_LOG_FILE_HANDLE != INVALID_HANDLE_VALUE;
}

// Closes the global log file. After being closed, all future log_print() calls will do nothing.
//
// @Parameters: None.
// 
// @Returns: Nothing.
void close_log_file(void)
{
	safe_close_handle(&GLOBAL_LOG_FILE_HANDLE);
}

// Appends a formatted TCHAR string to the global log file. This file must have been previously created using create_log_file().
// This ANSI or Wide UTF-16 string is converted to UTF-8 before being written to the log file.
//
// This function is usually called by using the macro log_print(log_type, string_format, ...), which takes the same arguments but
// where string_format is an ANSI string (for convenience).
//
// Use log_print_newline() to add an empty line, and debug_log_print() to add a line of type LOG_DEBUG. This last function is only
// called if the DEBUG macro is defined.
//
// @Parameters:
// 1. log_type - The type of log line to write. This is used to add a small string identifier to the beginning of the line.
// Use one of the following values:
// - LOG_INFO
// - LOG_WARNING
// - LOG_ERROR
// For LOG_DEBUG, use debug_log_print() instead. LOG_NONE is usually used when calling log_print_newline(), though it may be used
// directly with log_print() in certain cases.
// 2. string_format - The format string. Note that %hs is used for narrow ANSI strings, %ls for wide Unicode strings, and %s for TCHAR
// strings (ANSI or Wide depending on the build target).
// 3. ... - Zero or more arguments to be inserted in the format string.
//
// @Returns: Nothing.

static const size_t MAX_CHARS_PER_LOG_TYPE = 20;
static const size_t MAX_CHARS_PER_LOG_MESSAGE = 4000;
static const size_t MAX_CHARS_PER_LOG_WRITE = MAX_CHARS_PER_LOG_TYPE + MAX_CHARS_PER_LOG_MESSAGE + 2;

void tchar_log_print(Log_Type log_type, const TCHAR* string_format, ...)
{
	TCHAR log_buffer[MAX_CHARS_PER_LOG_WRITE] = TEXT("");
	
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
	StringCchCat(log_buffer, MAX_CHARS_PER_LOG_WRITE, TEXT("\r\n"));

	// Convert the log line to UTF-8.
	#ifdef BUILD_9X
		// For Windows 98 and ME, we'll first convert the ANSI string to UTF-16.
		wchar_t utf_16_log_buffer[MAX_CHARS_PER_LOG_WRITE] = L"";
		MultiByteToWideChar(CP_ACP, 0, log_buffer, -1, utf_16_log_buffer, MAX_CHARS_PER_LOG_WRITE);

		char utf_8_log_buffer[MAX_CHARS_PER_LOG_WRITE] = "";
		WideCharToMultiByte(CP_UTF8, 0, utf_16_log_buffer, -1, utf_8_log_buffer, sizeof(utf_8_log_buffer), NULL, NULL);
	#else
		char utf_8_log_buffer[MAX_CHARS_PER_LOG_WRITE] = "";
		WideCharToMultiByte(CP_UTF8, 0, log_buffer, -1, utf_8_log_buffer, sizeof(utf_8_log_buffer), NULL, NULL);
	#endif

	size_t num_bytes_to_write = 0;
	StringCbLengthA(utf_8_log_buffer, sizeof(utf_8_log_buffer), &num_bytes_to_write);

	DWORD num_bytes_written = 0;
	WriteFile(GLOBAL_LOG_FILE_HANDLE, utf_8_log_buffer, (DWORD) num_bytes_to_write, &num_bytes_written, NULL);
}

// Checks if a CSV string needs to be escaped, and returns the number of bytes that are necessary to store the escaped string
// (including the null terminator). A CSV string is escaped by using the following rules:
// 1. If the string contains a comma, double quotation mark, or newline character, then it's wrapped in double quotes.
// 2. Every double quotation mark is escaped by adding another double quotes character after it.
//
// For example:
// abc1234 doesn't need to be escaped.
// abc"12,34 is escaped as "abc""12,34" and requires 13 or 26 bytes to store it as an ANSI or Wide string, respectively.
//
// The worst case scenario is a string composed solely of quotes. For example: "" would need to be escaped as """""" (add a second
// quote for each character and surround the whole string with two quotes). In this case, we have 6 characters and 1 null terminator,
// meaning we'd need 7 or 14 bytes to store it as an ANSI or Wide string, respectively.
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

	while(*str != TEXT('\0'))
	{
		total_num_chars += 1;

		if(*str == TEXT(',') || *str == TEXT('\n'))
		{
			needs_escaping = true;
		}
		else if(*str == TEXT('\"'))
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

	while(*str != TEXT('\0'))
	{
		if(*str == TEXT(',') || *str == TEXT('\n'))
		{
			needs_escaping = true;
		}
		else if(*str == TEXT('\"'))
		{
			needs_escaping = true;

			TCHAR* next_char_1 = str + 1;
			TCHAR* next_char_2 = str + 2;
			// Add an extra double quotation mark.
			// E.g: ab"cd -> ab"ccd -> ab""cd
			// Where next_char_1 and next_char_2 point to 'c' and 'd', respectively.
			MoveMemory(next_char_2, next_char_1, string_size(next_char_1));
			*next_char_1 = TEXT('\"');

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
		string_start[0] = TEXT('\"');
		string_start[num_escaped_string_chars] = TEXT('\"');
		string_start[num_escaped_string_chars + 1] = TEXT('\0');
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
	TCHAR full_csv_directory_path[MAX_PATH_CHARS];
	get_full_path_name(csv_file_path, full_csv_directory_path);
	PathAppend(full_csv_directory_path, TEXT(".."));
	
	create_directories(full_csv_directory_path);

	*result_file_handle = CreateFile(	csv_file_path,
										GENERIC_WRITE,
										0,
										NULL,
										CREATE_ALWAYS,
										FILE_ATTRIBUTE_NORMAL,
										NULL);

	return *result_file_handle != INVALID_HANDLE_VALUE;
}

// Writes the header to a CSV file using UTF-8 as the character encoding. This header string is built using the Csv_Type enumeration
// values that correspond to each column. These values will be separated by commas.
// For example: the array {CSV_FILENAME, CSV_URL, CSV_RESPONSE} would write the string "Filename, URL, Server Response".
// This header is only added to empty CSV files. If the file already contains text, this function does nothing.
//
// @Parameters:
// 1. arena - The Arena structure where any intermediary strings are stored.
// 2. csv_file_handle - The handle to the CSV file.
// 3. column_types - The array of column types used to determine the column names.
// 4. num_columns - The number of elements in this array.
// 
// @Returns: Nothing.
void csv_print_header(Arena* arena, HANDLE csv_file_handle, const Csv_Type column_types[], size_t num_columns)
{
	if(csv_file_handle == INVALID_HANDLE_VALUE)
	{
		log_print(LOG_ERROR, "Csv Print Header: Attempted to add the header to a CSV file that wasn't been opened yet.");
		_ASSERT(false);
		return;
	}

	// Build the final CSV line where the strings are contiguous in memory.
	char* csv_header = push_arena(arena, 0, char);
	for(size_t i = 0; i < num_columns; ++i)
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

	// @Note: This assumes that sizeof(char) is one, meaning no alignment took place in the previous push_arena() calls.
	// Otherwise, we'd write some garbage (NUL bytes) into the file.
	ptrdiff_t csv_header_size = pointer_difference(arena->available_memory, csv_header);
	_ASSERT(csv_header_size > 0);

	DWORD num_bytes_written = 0;
	WriteFile(csv_file_handle, csv_header, (DWORD) csv_header_size, &num_bytes_written, NULL);
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
void csv_print_row(Arena* arena, HANDLE csv_file_handle, Csv_Entry column_values[], size_t num_columns)
{
	if(csv_file_handle == INVALID_HANDLE_VALUE)
	{
		log_print(LOG_ERROR, "Csv Print Row: Attempted to add the header to a CSV file that wasn't been opened yet.");
		_ASSERT(false);
		return;
	}

	// First pass: escape values that require it and convert every value to UTF-16. This is only necessary for ANSI strings
	// in Windows 98 and ME.
	for(size_t i = 0; i < num_columns; ++i)
	{
		TCHAR* value = column_values[i].value;
		// If there's no value, use an empty string never needs escaping. This string will still be copied.
		if(value == NULL) value = TEXT("");

		// Escape the value if it requires it.
		size_t size_required_for_escaping = 0;
		if(does_csv_string_require_escaping(value, &size_required_for_escaping))
		{
			value = push_and_copy_to_arena(arena, size_required_for_escaping, TCHAR, value, string_size(value));
			escape_csv_string(value);
		}

		// Convert the values to UTF-16.
		#ifdef BUILD_9X
			int num_chars_required_utf_16 = MultiByteToWideChar(CP_ACP, 0, value, -1, NULL, 0);
			int size_required_utf_16 = num_chars_required_utf_16 * sizeof(wchar_t);
			
			wchar_t* utf_16_value = push_arena(arena, size_required_utf_16, wchar_t);
			MultiByteToWideChar(CP_ACP, 0, value, -1, utf_16_value, num_chars_required_utf_16);

			column_values[i].utf_16_value = utf_16_value;
			column_values[i].value = NULL;
		#else
			column_values[i].utf_16_value = value;
			column_values[i].value = NULL;
		#endif
	}

	// Second pass: convert every value to UTF-8 and build the final CSV line where the strings are contiguous in memory.
	char* csv_row = push_arena(arena, 0, char);
	for(size_t i = 0; i < num_columns; ++i)
	{
		wchar_t* value = column_values[i].utf_16_value;

		int size_required_utf_8 = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);

		// Add a newline to the last value.
		if(i == num_columns - 1)
		{
			char* csv_row_value = push_arena(arena, size_required_utf_8 + 1, char);
			WideCharToMultiByte(CP_UTF8, 0, value, -1, csv_row_value, size_required_utf_8, NULL, NULL);
			csv_row_value[size_required_utf_8 - 1] = '\r';
			csv_row_value[size_required_utf_8] = '\n';
		}
		// Separate each value before the last one with a comma.
		else
		{
			char* csv_row_value = push_arena(arena, size_required_utf_8, char);
			WideCharToMultiByte(CP_UTF8, 0, value, -1, csv_row_value, size_required_utf_8, NULL, NULL);
			csv_row_value[size_required_utf_8 - 1] = ',';
		}
	}

	// @Note: This assumes that sizeof(char) is one, meaning no alignment took place in the previous push_arena() calls.
	// Otherwise, we'd write some garbage (NUL bytes) into the file.
	ptrdiff_t csv_row_size = pointer_difference(arena->available_memory, csv_row);
	_ASSERT(csv_row_size > 0);

	DWORD num_bytes_written = 0;
	WriteFile(csv_file_handle, csv_row, (DWORD) csv_row_size, &num_bytes_written, NULL);
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
	#pragma warning(push)
	#pragma warning(disable : 4100)
	static NT_QUERY_SYSTEM_INFORMATION(stub_nt_query_system_information)
	{
		log_print(LOG_WARNING, "NtQuerySystemInformation: Calling the stub version of this function.");
		_ASSERT(false);
		return STATUS_NOT_IMPLEMENTED;
	}
	#pragma warning(pop)
	typedef NT_QUERY_SYSTEM_INFORMATION(Nt_Query_System_Information);
	static Nt_Query_System_Information* dll_nt_query_system_information = stub_nt_query_system_information;
	#define NtQuerySystemInformation dll_nt_query_system_information

	// Dynamically load any necessary functions from Ntdll.dll. After being called, the following functions may used:
	//
	// - NtQuerySystemInformation()
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	static HMODULE ntdll_library = NULL;
	void windows_nt_load_ntdll_functions(void)
	{
		if(ntdll_library != NULL)
		{
			log_print(LOG_WARNING, "Load Ntdll Functions: The library was already loaded.");
			_ASSERT(false);
			return;
		}

		ntdll_library = LoadLibraryA("Ntdll.dll");
		if(ntdll_library != NULL)
		{
			GET_FUNCTION_ADDRESS(ntdll_library, "NtQuerySystemInformation", Nt_Query_System_Information*, NtQuerySystemInformation);
		}
		else
		{
			log_print(LOG_ERROR, "Load Ntdll Functions: Failed to load the library with error code %lu.", GetLastError());
			_ASSERT(false);
		}
	}

	// Free any functions that were previously dynamically loaded from Ntdll.dll. After being called, these functions should
	// no longer be called.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	void windows_nt_free_ntdll_functions(void)
	{
		if(ntdll_library == NULL)
		{
			log_print(LOG_ERROR, "Free Ntdll: Failed to free the library since it wasn't previously loaded.");
			return;
		}

		if(FreeLibrary(ntdll_library))
		{
			ntdll_library = NULL;
			NtQuerySystemInformation = stub_nt_query_system_information;
		}
		else
		{
			log_print(LOG_ERROR, "Free Ntdll: Failed to free the library with the error code %lu.", GetLastError());
			_ASSERT(false);
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
		log_print(LOG_WARNING, "GetOverlappedResultEx: Calling the stub version of this function. The timeout and alertable arguments will be ignored. These were set to %lu and %d.", dwMilliseconds, bAlertable);
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
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	static HMODULE kernel32_library = NULL;
	void windows_nt_load_kernel32_functions(void)
	{
		if(kernel32_library != NULL)
		{
			log_print(LOG_WARNING, "Load Kernel32 Functions: The library was already loaded.");
			_ASSERT(false);
			return;
		}

		kernel32_library = LoadLibraryA("Kernel32.dll");
		if(kernel32_library != NULL)
		{
			GET_FUNCTION_ADDRESS(kernel32_library, "GetOverlappedResultEx", Get_Overlapped_Result_Ex*, GetOverlappedResultEx);
		}
		else
		{
			log_print(LOG_ERROR, "Load Kernel32 Functions: Failed to load the library with error code %lu.", GetLastError());
			_ASSERT(false);
		}
	}

	// Free any functions that were previously dynamically loaded from Kernel32.dll. After being called, these functions should
	// no longer be called.
	//
	// @Parameters: None.
	// 
	// @Returns: Nothing.
	void windows_nt_free_kernel32_functions(void)
	{
		if(kernel32_library == NULL)
		{
			log_print(LOG_ERROR, "Free Kernel32: Failed to free the library since it wasn't previously loaded.");
			return;
		}

		if(FreeLibrary(kernel32_library))
		{
			kernel32_library = NULL;
			GetOverlappedResultEx = stub_get_overlapped_result_ex;
		}
		else
		{
			log_print(LOG_ERROR, "Free Kernel32: Failed to free the library with the error code %lu.", GetLastError());
			_ASSERT(false);
		}
	}

	// Checks if two file handles refer to the same file
	//
	// @Parameters:
	// 1. file_handle_1 - The first file handle.
	// 2. file_handle_2 - The second file handle.
	// 
	// @Returns: True if the file handles refer to the same file. Otherwise, false. This function also returns false if it's unable
	// to retrieve the handles' information.
	static bool do_handles_refer_to_the_same_file(HANDLE file_handle_1, HANDLE file_handle_2)
	{
		BY_HANDLE_FILE_INFORMATION handle_1_info = {};
		BY_HANDLE_FILE_INFORMATION handle_2_info = {};

		// @Docs: "The identifier (low and high parts) and the volume serial number uniquely identify a file on a single computer.
		// To determine whether two open handles represent the same file, combine the identifier and the volume serial number for
		// each file and compare them." - Win32 API reference.
		if(		GetFileInformationByHandle(file_handle_1, &handle_1_info) != 0
			&& 	GetFileInformationByHandle(file_handle_2, &handle_2_info) != 0)
		{
			return 		handle_1_info.nFileIndexHigh == handle_2_info.nFileIndexHigh
					&&	handle_1_info.nFileIndexLow == handle_2_info.nFileIndexLow
					&&	handle_1_info.dwVolumeSerialNumber == handle_2_info.dwVolumeSerialNumber;
		}

		return false;
	}

	// Finds and creates a duplicated handle for a file that was opened by another process given its path on disk.
	//
	// @Parameters:
	// 1. arena - The Arena structure where any intermediary information about the currently opened handles is stored.
	// Note that this function does not clear this arena. Use clear_arena() after calling this function.
	// 2. full_file_path - The full path to the file of interest.
	// 3. result_file_handle - The address to the variable that receives the resulting duplicated handle.
	// 
	// @Returns: True if it succeeds. Otherwise, false. This function fails under the following scenarios:
	// 1. If the file of interest is not opened by another process.
	// 2. If the file's handle cannot be duplicated.
	// 3. If it's not possible to get a handle with read attributes only access for the file. 
	// 4. If it couldn't query the system for information on all opened handles.
	static bool windows_nt_query_file_handle_from_file_path(Arena* arena, const wchar_t* full_file_path, HANDLE* result_file_handle)
	{
		*result_file_handle = INVALID_HANDLE_VALUE;

		// List all opened handles. We may have to increase the size of the Arena structure to receive all the necessary handle
		// information.
		// Note that, even if it succeeds, this information may already be stale...
		ULONG handle_info_size = (ULONG) megabytes_to_bytes(1);
		SYSTEM_HANDLE_INFORMATION* handle_info = push_arena(arena, handle_info_size, SYSTEM_HANDLE_INFORMATION);
		ULONG actual_handle_info_size = 0;
		
		NTSTATUS error_code = NtQuerySystemInformation(SystemHandleInformation, handle_info, handle_info_size, &actual_handle_info_size);
		while(error_code == STATUS_INFO_LENGTH_MISMATCH)
		{
			// On the next attempt, the overall size of this information may have changed (new handles could have been created),
			// so we'll go the extra mile and add the current size plus an extra bit.
			ULONG extra_size = actual_handle_info_size + (ULONG) kilobytes_to_bytes(5);
			push_arena(arena, extra_size, u8);
			handle_info_size += extra_size;

			log_print(LOG_WARNING, "Query File Handle: Insufficient buffer size while trying to query system information. Expanding the buffer to %lu bytes.", handle_info_size);
			error_code = NtQuerySystemInformation(SystemHandleInformation, handle_info, handle_info_size, &actual_handle_info_size);
		}

		if(!NT_SUCCESS(error_code))
		{
			log_print(LOG_ERROR, "Query File Handle: Failed to query system information with error code %ld.", error_code);
			return false;
		}

		// When we iterate over these handles, we'll want to check which one corresponds to the desired file path.
		// To do this robustly, we'll need another handle for this file. Since the whole point of this function is
		// getting a handle for a file that's being used by another process, we'll want to avoid asking for any
		// access rights  that result in a sharing violation. Because of this, we'll ask for the right to read the
		// file's attributes.
		HANDLE read_attributes_file_handle = CreateFileW(	full_file_path,
															FILE_READ_ATTRIBUTES,
															FILE_SHARE_READ,
															NULL,
															OPEN_EXISTING,
															0,
															NULL);
		if(read_attributes_file_handle == INVALID_HANDLE_VALUE)
		{
			log_print(LOG_ERROR, "Query File Handle: Failed to get the read attributes files handle for '%ls' with error code %lu.", full_file_path, GetLastError());
			return false;
		}

		DWORD current_process_id = GetCurrentProcessId();
		HANDLE current_process_handle = GetCurrentProcess();
		// This pseudo handle doesn't have to be closed.

		bool success = false;

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
					&& do_handles_refer_to_the_same_file(read_attributes_file_handle, duplicated_file_handle))
				{
					success = true;
					*result_file_handle = duplicated_file_handle;
					safe_close_handle(&process_handle);
					log_print(LOG_INFO, "Query File Handle: Found handle with attributes 0x%02X and granted access 0x%08X.", handle_entry.HandleAttributes, handle_entry.GrantedAccess);
					break;
				}

				safe_close_handle(&duplicated_file_handle);
			}

			safe_close_handle(&process_handle);
		}

		safe_close_handle(&read_attributes_file_handle);

		return success;
	}

	// Forcibly copies a file that was opened by another process. This function is used to bypass sharing violation errors when
	// trying to read a file that is being used by another process.
	//
	// You should use the CopyFile() function from the Windows API first, and only use this one if CopyFile() fails with the error
	// code ERROR_SHARING_VIOLATION.
	//
	// @Parameters:
	// 1. arena - The Arena structure where the file buffer and any intermediary information about the currently opened handles
	// is stored.
	// 2. copy_source_path - The full path to the source file.
	// 3. copy_destination_path - The destination path where the file will be copied to.
	// 
	// @Returns: True if the file was copied sucessfully. Otherwise, false. The copy operation is considered successfully if all
	// of the following conditions are true:
	// 1. The file specified in copy_source_path exists and is currently open in another process.
	// 2. The number of bytes written to disk matches the number of bytes read using the duplicated file handle.
	// 3. The number of bytes written to disk matches the source file's real size.
	bool windows_nt_force_copy_open_file(Arena* arena, const wchar_t* copy_source_path, const wchar_t* copy_destination_path)
	{
		bool copy_success = false;

		HANDLE source_file_handle = INVALID_HANDLE_VALUE;
		bool was_source_handle_found = windows_nt_query_file_handle_from_file_path(arena, copy_source_path, &source_file_handle);
		clear_arena(arena);

		if(was_source_handle_found)
		{
			HANDLE destination_file_handle = CreateFileW(	copy_destination_path,
															GENERIC_WRITE,
															0,
															NULL,
															CREATE_ALWAYS,
															FILE_ATTRIBUTE_NORMAL,
															NULL);

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

				const u32 DESIRED_FILE_BUFFER_SIZE = 32768;
				_STATIC_ASSERT(IS_POWER_OF_TWO(DESIRED_FILE_BUFFER_SIZE));

				u32 file_buffer_size = ALIGN_UP(DESIRED_FILE_BUFFER_SIZE, system_info.dwPageSize);
				_ASSERT(file_buffer_size % system_info.dwPageSize == 0);

				void* file_buffer = aligned_push_arena(arena, file_buffer_size, system_info.dwPageSize);
				
				u64 total_bytes_read = 0;
				u64 total_bytes_written = 0;
				OVERLAPPED overlapped = {};
				
				bool reached_end_of_file = false;
				u32 num_read_retry_attempts = 0;

				do
				{
					DWORD num_bytes_read = 0;
					bool read_success = ReadFile(source_file_handle, file_buffer, file_buffer_size, &num_bytes_read, &overlapped) == TRUE;
					
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
									log_print(LOG_WARNING, "Force Copy Open File: Failed to get the overlapped result because the function timed out after %lu seconds. Read %I64u and wrote %I64u bytes so far. Retrying read operation.", TIMEOUT_IN_SECONDS, total_bytes_read, total_bytes_written);
									++num_read_retry_attempts;
									continue;
								}
								else
								{
									log_print(LOG_ERROR, "Force Copy Open File: Failed to get the overlapped result while reading the file '%ls' with the unhandled error code %lu. Read %I64u and wrote %I64u bytes so far.", copy_source_path, overlapped_result_error_code, total_bytes_read, total_bytes_written);
									_ASSERT(false);
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
							log_print(LOG_ERROR, "Force Copy Open File: Failed to read the file '%ls' with the unhandled error code %lu. Read %I64u and wrote %I64u bytes so far.", copy_source_path, read_error_code, total_bytes_read, total_bytes_written);
							_ASSERT(false);
						}
					}

					if(!reached_end_of_file)
					{
						total_bytes_read += num_bytes_read;
					
						DWORD num_bytes_written = 0;
						if(WriteFile(destination_file_handle, file_buffer, num_bytes_read, &num_bytes_written, NULL))
						{
							total_bytes_written += num_bytes_written;
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

				} while(!reached_end_of_file);

				clear_arena(arena);

				if(num_read_retry_attempts > 0)
				{
					log_print(LOG_INFO, "Force Copy Open File: Retried read operations %I32u time(s) before reaching the end of file.", num_read_retry_attempts);
				}

				// This isn't a great way to verify if we copied everything successfully but it works for now...
				copy_success = (total_bytes_read == total_bytes_written);

				u64 file_size = 0;
				if(copy_success && get_file_size(source_file_handle, &file_size))
				{
					copy_success = copy_success && (total_bytes_read == file_size);
				}
			}

			safe_close_handle(&destination_file_handle);
		}
		
		safe_close_handle(&source_file_handle);
		return copy_success;
	}

#endif
