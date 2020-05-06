#include "web_cache_exporter.h"
#include "file_io.h"

bool create_arena(Arena* arena, size_t total_size)
{
	arena->available_memory = VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	arena->used_size = 0;
	arena->total_size = (arena->available_memory != NULL) ? total_size : 0;
	return arena->available_memory != NULL;
}

ptrdiff_t pointer_difference(void* a, void* b)
{
	return ((u8*) a) - ((u8*) b);
}

void* aligned_push_arena(Arena* arena, size_t push_size, size_t alignment_size)
{
	void* misaligned_address = arena->available_memory;
	void* aligned_address = misaligned_address;
	if(alignment_size > 1)
	{
		_ASSERT((alignment_size & (alignment_size - 1)) == 0); // Check if it's a power of two.
		aligned_address = (void*) ( (((uintptr_t) misaligned_address) + (alignment_size - 1)) & ~(alignment_size - 1) );
	}
	
	size_t aligned_push_size = push_size + pointer_difference(aligned_address, misaligned_address);
	_ASSERT(push_size <= aligned_push_size);
	_ASSERT(((uintptr_t) aligned_address) % alignment_size == 0);
	_ASSERT(arena->used_size + aligned_push_size <= arena->total_size);

	arena->available_memory = advance_bytes(arena->available_memory, aligned_push_size);
	arena->used_size += aligned_push_size;
	return aligned_address;
}

void* aligned_push_and_copy_to_arena(Arena* arena, const void* data, size_t data_size, size_t alignment_size)
{
	void* copy_address = aligned_push_arena(arena, data_size, alignment_size);
	CopyMemory(copy_address, data, data_size);
	return copy_address;
}

void clear_arena(Arena* arena)
{
	arena->available_memory = retreat_bytes(arena->available_memory, arena->used_size);
	arena->used_size = 0;
}

bool destroy_arena(Arena* arena)
{
	void* base_memory = retreat_bytes(arena->available_memory, arena->used_size);
	BOOL success = VirtualFree(base_memory, 0, MEM_RELEASE);
	arena->available_memory = NULL;
	arena->used_size = 0;
	arena->total_size = 0;

	return success == TRUE;
}

size_t string_size(const char* str)
{
	return strlen(str) + sizeof(char);
}

char* skip_leading_whitespace(char* str)
{
	if(str != NULL)
	{
		while(*str == ' ' || *str == '\t')
		{
			++str;
		}
	}

	return str;
}

// filename = NULL -> NULL
// filename = "a.gif" -> gif
// filename = "abc." -> ""
// filename = "abc" -> ""
char* skip_to_file_extension(char* str)
{
	char* file_extension = NULL;

	if(str != NULL)
	{
		while(*str != '\0' && *str != '.')
		{
			++str;
		}

		file_extension = (str != '\0') ? (++str) : (str);
	}

	return file_extension;
}

static s8 hexadecimal_char_to_numeric(char hex_char)
{
	if('0' <= hex_char && hex_char <= '9') return hex_char - '0';
	else if('a' <= hex_char && hex_char <= 'f') return hex_char - 'a' + 0x0A;
	else if('A' <= hex_char && hex_char <= 'F') return hex_char - 'A' + 0x0A;
	else return -1;
}

bool decode_url(const char* url, char* decoded_url)
{
	bool success = true;

	if(url != NULL)
	{
		while(*url != '\0')
		{
			if(*url == '%')
			{
				char next_char_1 = *(url+1);
				char next_char_2 = (next_char_1 != '\0') ? (*(url+2)) : ('\0');
				s8 next_value_1 = hexadecimal_char_to_numeric(next_char_1);
				s8 next_value_2 = hexadecimal_char_to_numeric(next_char_2);

				if(next_value_1 > -1 && next_value_2 > -1)
				{
					*decoded_url = (char) (next_value_1 * 16 + next_value_2);
					url += 3; // Skip "%xx".
				}
				else
				{
					success = false;
					break;
				}
			}
			else if(*url == '+')
			{
				*decoded_url = ' ';
				url += 1;
			}
			else
			{
				*decoded_url = *url;
				url += 1;
			}
			
			++decoded_url;
		}

		*decoded_url = '\0';
	}

	return success;
}

bool format_date_time(FILETIME* date_time, char* formatted_string)
{
	if(date_time->dwLowDateTime == 0 && date_time->dwHighDateTime == 0)
	{
		*formatted_string = '\0';
		return true;
	}

	SYSTEMTIME dt;
	if(!FileTimeToSystemTime(date_time, &dt)) return false;
	return SUCCEEDED(StringCchPrintfA(formatted_string, MAX_FORMATTED_DATE_TIME_CHARS, "%4d-%02d-%02d %02d:%02d:%02d", dt.wYear, dt.wMonth, dt.wDay, dt.wHour, dt.wMinute, dt.wSecond));
}

void* memory_map_entire_file(char* path)
{
	void* mapped_memory = NULL;

	HANDLE file_handle = CreateFileA(path,
								     GENERIC_READ,
								     FILE_SHARE_READ, // @Note: MSDN recommends exclusive access (though not required).
								     NULL,
								     OPEN_EXISTING,
								     0,
								     NULL);

	if(file_handle != INVALID_HANDLE_VALUE)
	{
		// TODO: GetFileSize vs GetFileSizeEx for Windows 98 and ME
		// Reject empty files.
		LARGE_INTEGER file_size;
		if(GetFileSizeEx(file_handle, &file_size))
		{
			if(((u32) file_size.QuadPart) > 0)
			{
				HANDLE mapping_handle = CreateFileMappingA(file_handle,
													       NULL,
													       PAGE_READONLY,
													       0,
													       0,
													       NULL);

				if(mapping_handle != NULL)
				{
					mapped_memory = MapViewOfFile(mapping_handle, FILE_MAP_READ, 0, 0, 0);
				}
				else
				{
					log_print("Error %d while trying to create the file mapping for '%s'.\n", GetLastError(), path);
				}

				CloseHandle(mapping_handle); // @Note: According to MSDN, CloseHandle() and UnmapViewOfFile() can be called in any order.
			}
			else
			{
				log_print("Skipping file mapping for empty file '%s'.\n", path);
			}

		}
		else
		{
			log_print("Error %d while trying to get the file size for '%s'.\n", GetLastError(), path);
		}

		CloseHandle(file_handle); // @Note: "Files for which the last view has not yet been unmapped are held open with no sharing restrictions."
	}
	else
	{
		log_print("Error %d while trying to get the file handle for '%s'.\n", GetLastError(), path);
	}

	return mapped_memory;
}

void* advance_bytes(void* pointer, size_t num_bytes)
{
	return (void*) ( ((u8*) pointer) + num_bytes );
}

void* retreat_bytes(void* pointer, size_t num_bytes)
{
	return (void*) ( ((u8*) pointer) - num_bytes );
}

/*
_byteswap_ushort
_byteswap_ulong
_byteswap_uint64
*/

void copy_byte_range(void* source, void* destination, /*size_t destination_size,*/ size_t offset, size_t num_bytes_to_copy)
{
	source = advance_bytes(source, offset);
	CopyMemory(destination, source, num_bytes_to_copy);
	//memcpy_s(destination, destination_size, source, num_bytes_to_copy);
}

bool query_registry(HKEY base_key, const char* key_name, const char* value_name, char* value_data)
{
	DWORD value_data_type, value_data_size;
	LONG error_code = RegGetValueA(base_key,
									key_name,
									value_name,
									RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
									&value_data_type,
									value_data,
									&value_data_size);

	// RegGetValue() only returns this value, it doesn't set the last error code.
	// We'll do it ourselves for consistent error handling when calling this function.
	SetLastError(error_code);
	return error_code == ERROR_SUCCESS;
}

HANDLE LOG_FILE_HANDLE = NULL;
const size_t MAX_CHARS_PER_LOG_WRITE = 4096;
// @TODO: Would the log_printW idea work with WideCharToMultibyte?
void log_print(const char* string_format, ...)
{
	if(LOG_FILE_HANDLE == NULL || LOG_FILE_HANDLE == INVALID_HANDLE_VALUE) return;

	char log_buffer[MAX_CHARS_PER_LOG_WRITE];

	va_list args;
	va_start(args, string_format);
	StringCchVPrintfA(log_buffer, MAX_CHARS_PER_LOG_WRITE, string_format, args);
	va_end(args);

	size_t num_chars_to_write;
	StringCchLengthA(log_buffer, MAX_CHARS_PER_LOG_WRITE, &num_chars_to_write);

	DWORD num_bytes_written;
	WriteFile(LOG_FILE_HANDLE, log_buffer, (DWORD) (num_chars_to_write * sizeof(char)), &num_bytes_written, NULL);
}
