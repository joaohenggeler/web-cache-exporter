#include "web_cache_exporter.h"
#include "memory_and_file_io.h"

const int foo = 123;

#ifdef DEBUG
	#ifdef BUILD_9X
		void* DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = NULL;
		void* DEBUG_MEMORY_MAPPING_BASE_ADDRESS = NULL;
	#else
		void* DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = (void*) 0x10000000;
		void* DEBUG_MEMORY_MAPPING_BASE_ADDRESS = (void*) 0x50000000;
	#endif
#endif

bool create_arena(Arena* arena, size_t total_size)
{
	#ifdef DEBUG
		arena->available_memory = VirtualAlloc(DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	#else
		arena->available_memory = VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	#endif
	_ASSERT(arena->available_memory != NULL);
	arena->used_size = 0;
	arena->total_size = (arena->available_memory != NULL) ? total_size : 0;
	return arena->available_memory != NULL;
}

void* aligned_push_arena(Arena* arena, size_t push_size, size_t alignment_size)
{
	if(arena->available_memory == NULL)
	{
		_ASSERT(false);
		return NULL;
	}

	void* misaligned_address = arena->available_memory;
	void* aligned_address = misaligned_address;
	if(alignment_size > 1)
	{
		_ASSERT((alignment_size & (alignment_size - 1)) == 0); // Guarantee that it's a power of two.
		aligned_address = (void*) ( (((uintptr_t) misaligned_address) + (alignment_size - 1)) & ~(alignment_size - 1) );
	}
	
	size_t aligned_push_size = push_size + pointer_difference(aligned_address, misaligned_address);
	_ASSERT(push_size <= aligned_push_size);
	_ASSERT(((uintptr_t) aligned_address) % alignment_size == 0);
	_ASSERT(arena->used_size + aligned_push_size <= arena->total_size);

	#ifdef DEBUG
		FillMemory(aligned_address, aligned_push_size, 0xFF);
	#endif

	arena->available_memory = advance_bytes(arena->available_memory, aligned_push_size);
	arena->used_size += aligned_push_size;
	return aligned_address;
}

void* aligned_push_and_copy_to_arena(Arena* arena, size_t push_size, size_t alignment_size, const void* data, size_t data_size)
{
	_ASSERT(data_size <= push_size);
	void* copy_address = aligned_push_arena(arena, push_size, alignment_size);
	CopyMemory(copy_address, data, data_size);
	return copy_address;
}

void clear_arena(Arena* arena)
{
	if(arena->available_memory == NULL)
	{
		_ASSERT(false);
		return;
	}

	arena->available_memory = retreat_bytes(arena->available_memory, arena->used_size);
	#ifdef DEBUG
		SecureZeroMemory(arena->available_memory, arena->used_size);
	#endif
	arena->used_size = 0;
}

bool destroy_arena(Arena* arena)
{
	if(arena->available_memory == NULL)
	{
		return true;
	}

	void* base_memory = retreat_bytes(arena->available_memory, arena->used_size);
	BOOL success = VirtualFree(base_memory, 0, MEM_RELEASE);
	*arena = NULL_ARENA;

	return success == TRUE;
}

bool format_filetime_date_time(FILETIME date_time, TCHAR* formatted_string)
{
	if(date_time.dwLowDateTime == 0 && date_time.dwHighDateTime == 0)
	{
		*formatted_string = TEXT('\0');
		return true;
	}

	SYSTEMTIME dt;
	bool success = FileTimeToSystemTime(&date_time, &dt) != 0;
	success = success && SUCCEEDED(StringCchPrintf(formatted_string, MAX_FORMATTED_DATE_TIME_CHARS, TEXT("%4hu-%02hu-%02hu %02hu:%02hu:%02hu"), dt.wYear, dt.wMonth, dt.wDay, dt.wHour, dt.wMinute, dt.wSecond));

	if(!success)
	{
		debug_log_print("Format Filetime DateTime: Error %lu while trying to convert FILETIME datetime (high datetime = %lu, low datetime = %lu).", GetLastError(), date_time.dwHighDateTime, date_time.dwLowDateTime);
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

	FILETIME filetime;
	bool success = DosDateTimeToFileTime(date_time.date, date_time.time, &filetime) != 0;
	success = success && format_filetime_date_time(filetime, formatted_string);

	if(!success)
	{
		debug_log_print("Format Dos DateTime: Error %lu while trying to convert DOS datetime (date = %I32u, time = %I32u).", GetLastError(), date_time.date, date_time.time);
		*formatted_string = TEXT('\0');
	}

	return success;
}

const size_t INT_FORMAT_RADIX = 10;
bool convert_u32_to_string(u32 value, TCHAR* result_string)
{
	return _ultot_s(value, result_string, MAX_INT32_CHARS, INT_FORMAT_RADIX) != 0;
}
bool convert_u64_to_string(u64 value, TCHAR* result_string)
{
	return _ui64tot_s(value, result_string, MAX_INT64_CHARS, INT_FORMAT_RADIX) != 0;
}
bool convert_s64_to_string(s64 value, TCHAR* result_string)
{
	return _i64tot_s(value, result_string, MAX_INT64_CHARS, INT_FORMAT_RADIX) != 0;
}

TCHAR* copy_ansi_string_to_tchar(Arena* arena, const char* ansi_string)
{
	#ifdef BUILD_9X
		return push_and_copy_to_arena(arena, string_size(ansi_string), char, ansi_string, string_size(ansi_string));
	#else
		int num_chars_required = MultiByteToWideChar(CP_ACP, 0, ansi_string, -1, NULL, 0);
		int size_required = num_chars_required * sizeof(wchar_t);
			
		wchar_t* wide_string = push_arena(arena, size_required, wchar_t);
		MultiByteToWideChar(CP_ACP, 0, ansi_string, -1, wide_string, num_chars_required);
		return wide_string;
	#endif
	//size_t total_num_chars = string_size(ansi_string) / sizeof(char);
	//return copy_ansi_string_to_tchar(arena, ansi_string, total_num_chars);
}

/*
TCHAR* copy_ansi_string_to_tchar(Arena* arena, const char* ansi_string, size_t num_chars)
{
	#ifdef BUILD_9X
		size_t size_to_copy_ansi = num_chars * sizeof(char);
		return push_and_copy_to_arena(arena, size_to_copy_ansi, char, ansi_string, size_to_copy_ansi);
	#else
		int size_to_copy_ansi = (int) (num_chars * sizeof(char));

		int num_chars_required_utf_16 = MultiByteToWideChar(CP_ACP, 0, ansi_string, size_to_copy_ansi, NULL, 0);
		size_t size_required_utf_16 = num_chars_required_utf_16 * sizeof(wchar_t);
		wchar_t* wide_string = push_arena(arena, size_required_utf_16, wchar_t);

		MultiByteToWideChar(CP_ACP, 0, ansi_string, size_to_copy_ansi, wide_string, num_chars_required_utf_16);
		return wide_string;
	#endif
}*/

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

wchar_t* skip_leading_whitespace(wchar_t* str)
{
	if(str != NULL)
	{
		while(*str == L' ' || *str == L'\t')
		{
			++str;
		}
	}

	return str;
}

// filename = NULL -> NULL
// filename = "a.gif" -> gif
// filename = "a.gif.gz" -> gif.gz
// filename = "abc." -> ""
// filename = "abc" -> ""
TCHAR* skip_to_file_extension(TCHAR* str)
{
	TCHAR* file_extension = NULL;

	if(str != NULL)
	{
		while(*str != TEXT('\0') && *str != TEXT('.'))
		{
			++str;
		}

		file_extension = (*str != TEXT('\0')) ? (++str) : (str);
	}

	return file_extension;

	/*char* file_extension = NULL;

	if(str != NULL)
	{
		while(*str != '\0')
		{
			if(*str == '.')
			{
				file_extension = str + 1;
			}

			++str;
		}

		file_extension = (file_extension != NULL) ? (file_extension) : (str);
	}

	return file_extension;*/
}

static s8 hexadecimal_char_to_numeric(TCHAR hex_char)
{
	if(TEXT('0') <= hex_char && hex_char <= TEXT('9')) return (s8) (hex_char - TEXT('0'));
	else if(TEXT('a') <= hex_char && hex_char <= TEXT('f')) return (s8) (hex_char - TEXT('a') + 0x0A);
	else if(TEXT('A') <= hex_char && hex_char <= TEXT('F')) return (s8) (hex_char - TEXT('A') + 0x0A);
	else return -1;
}

bool decode_url(const TCHAR* url, TCHAR* decoded_url)
{
	bool success = true;

	if(url != NULL)
	{
		while(*url != TEXT('\0'))
		{
			if(*url == TEXT('%'))
			{
				TCHAR next_char_1 = *(url+1);
				TCHAR next_char_2 = (next_char_1 != TEXT('\0')) ? (*(url+2)) : (TEXT('\0'));
				s8 next_value_1 = hexadecimal_char_to_numeric(next_char_1);
				s8 next_value_2 = hexadecimal_char_to_numeric(next_char_2);

				if(next_value_1 > -1 && next_value_2 > -1)
				{
					*decoded_url = (TCHAR) (next_value_1 * 16 + next_value_2);
					url += 3; // Skip "%xx".
				}
				else
				{
					success = false;
					break;
				}
			}
			else if(*url == TEXT('+'))
			{
				*decoded_url = TEXT(' ');
				url += 1;
			}
			else
			{
				*decoded_url = *url;
				url += 1;
			}
			
			++decoded_url;
		}

		*decoded_url = TEXT('\0');
	}

	return success;
}

static void convert_url_to_path(TCHAR* url)
{
	if(url == NULL) return;

	TCHAR* url_start = url;
	TCHAR* last_path_separator = NULL;
	TCHAR* url_path_start = NULL;

	while(*url != TEXT('\0'))
	{
		if(*url == TEXT('/'))
		{
			*url = TEXT('\\');
			last_path_separator = url;
		}
		else if(*url == TEXT('?') || *url == TEXT('#'))
		{
			break;
		}
		else if(*url == TEXT(':'))
		{
			url_path_start = url + 1;

			TCHAR* next_char_1 = url + 1;
			if(*next_char_1 == TEXT('/'))
			{
				url_path_start = next_char_1 + 1;
				++url;
			}

			TCHAR* next_char_2 = next_char_1 + 1;
			if(*next_char_1 == TEXT('/') && *next_char_2 == TEXT('/'))
			{
				url_path_start = next_char_2 + 1;
				++url;
			}
			
		}

		++url;
	}

	if(last_path_separator != NULL)
	{
		*last_path_separator = TEXT('\0');
	}

	if(url_path_start != NULL)
	{
		MoveMemory(url_start, url_path_start, string_size(url_path_start));
	}
}

bool get_full_path_name(TCHAR* path, TCHAR* optional_full_path_result)
{
	if(optional_full_path_result != NULL)
	{
		GetFullPathName(path, MAX_PATH_CHARS, optional_full_path_result, NULL);
	}
	else
	{
		TCHAR full_path[MAX_PATH_CHARS];
		GetFullPathName(path, MAX_PATH_CHARS, full_path, NULL);
		StringCchCopy(path, MAX_PATH_CHARS, full_path);
	}
	
	return GetLastError() != ERROR_SUCCESS;
}

bool create_empty_file(const TCHAR* file_path)
{
	DWORD error_code;
	bool success;

	HANDLE empty_file = CreateFile(file_path, 0, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	error_code = GetLastError();
	success = empty_file != INVALID_HANDLE_VALUE;
	CloseHandle(empty_file);

	SetLastError(error_code);
	return success;
}

void create_directories(const TCHAR* path_to_create)
{
	const size_t MAX_CREATE_DIRECTORY_PATH_CHARS = 248;
	TCHAR path[MAX_CREATE_DIRECTORY_PATH_CHARS];
	// @TODO: replace with GetFullPathNameA(path_to_create, MAX_CREATE_DIRECTORY_PATH_CHARS, path, NULL);
	// _STATIC_ASSERT(MAX_CREATE_DIRECTORY_PATH_CHARS <= MAX_PATH_CHARS);
	StringCchCopy(path, MAX_CREATE_DIRECTORY_PATH_CHARS, path_to_create);

	for(size_t i = 0; i < MAX_CREATE_DIRECTORY_PATH_CHARS; ++i)
	{
		if(path[i] == TEXT('\0'))
		{
			CreateDirectory(path, NULL);
			break;
		}
		else if(path[i] == TEXT('\\'))
		{
			path[i] = TEXT('\0');
			CreateDirectory(path, NULL);
			path[i] = TEXT('\\');
		}
	}
}

bool copy_file_using_url_directory_structure(Arena* arena, const TCHAR* full_file_path, const TCHAR* base_destination_path, const TCHAR* url, const TCHAR* filename)
{
	TCHAR* url_path;
	if(url != NULL)
	{
		url_path = push_and_copy_to_arena(arena, string_size(url), TCHAR, url, string_size(url));
		convert_url_to_path(url_path);
	}
	else
	{
		url_path = NULL;
	}

	TCHAR full_copy_target_path[MAX_PATH_CHARS];
	GetFullPathName(base_destination_path, MAX_PATH_CHARS, full_copy_target_path, NULL);
	if(url_path != NULL) PathAppend(full_copy_target_path, url_path);
	
	create_directories(full_copy_target_path);
	PathAppend(full_copy_target_path, filename);

	bool copy_success;

	#if defined(DEBUG) && defined(EXPORT_DUMMY_FILES)
		copy_success = create_empty_file(full_copy_target_path);
		full_file_path;
	#else
		copy_success = CopyFile(full_file_path, full_copy_target_path, TRUE) != 0;
	#endif

	u32 num_naming_collisions = 0;
	while(!copy_success && GetLastError() == ERROR_FILE_EXISTS)
	{
		++num_naming_collisions;
		TCHAR full_unique_copy_target_path[MAX_PATH_CHARS];
		StringCchPrintf(full_unique_copy_target_path, MAX_PATH_CHARS, TEXT("%s.%I32u"), full_copy_target_path, num_naming_collisions);
		
		#if defined(DEBUG) && defined(EXPORT_DUMMY_FILES)
			copy_success = create_empty_file(full_unique_copy_target_path);
			full_file_path;
		#else
			copy_success = CopyFile(full_file_path, full_unique_copy_target_path, TRUE) != 0;
		#endif
	}
	
	return copy_success;
}

u64 combine_high_and_low_u32s(u32 high, u32 low)
{
	return (high << sizeof(u32)) | low;
}

bool get_file_size(HANDLE file_handle, u64* file_size_result)
{
	#ifdef BUILD_9X
		DWORD file_size_high;
		DWORD file_size_low = GetFileSize(file_handle, &file_size_high);
		bool success = GetLastError() == NO_ERROR;
		if(success) *file_size_result = combine_high_and_low_u32s(file_size_high, file_size_low);
		return success;
	#else
		LARGE_INTEGER file_size;
		bool success = GetFileSizeEx(file_handle, &file_size) == TRUE;
		if(success) *file_size_result = file_size.QuadPart;
		return success;
	#endif
}

bool does_file_exist(TCHAR* file_path)
{
	DWORD attributes = GetFileAttributes(file_path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && ( (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

void* memory_map_entire_file(TCHAR* file_path, u64* file_size_result)
{
	void* mapped_memory = NULL;
	*file_size_result = 0;

	HANDLE file_handle = CreateFile(file_path,
								     GENERIC_READ,
								     FILE_SHARE_READ, // @Note: MSDN recommends exclusive access (though not required).
								     NULL,
								     OPEN_EXISTING,
								     0,
								     NULL);

	if(file_handle != INVALID_HANDLE_VALUE)
	{
		// Reject empty files.
		u64 file_size;
		if(get_file_size(file_handle, &file_size))
		{
			*file_size_result = file_size;
			if(file_size > 0)
			{
				HANDLE mapping_handle = CreateFileMapping(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);

				if(mapping_handle != NULL)
				{
					#ifdef DEBUG
						mapped_memory = MapViewOfFileEx(mapping_handle, FILE_MAP_READ, 0, 0, 0, DEBUG_MEMORY_MAPPING_BASE_ADDRESS);
					#else
						mapped_memory = MapViewOfFile(mapping_handle, FILE_MAP_READ, 0, 0, 0);
					#endif

					if(mapped_memory == NULL)
					{
						log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to map a view of the file '%s'.", GetLastError(), file_path);
					}
				}
				else
				{
					log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to create the file mapping for '%s'.", GetLastError(), file_path);
				}

				CloseHandle(mapping_handle); // @Note: According to MSDN, CloseHandle() and UnmapViewOfFile() can be called in any order.
			}
			else
			{
				log_print(LOG_WARNING, "Memory Mapping: Skipping file mapping for empty file '%s'.", file_path);
			}

		}
		else
		{
			log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to get the file size for '%s'.", GetLastError(), file_path);
		}

		CloseHandle(file_handle); // @Note: "Files for which the last view has not yet been unmapped are held open with no sharing restrictions."
	}
	else
	{
		log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to get the file handle for '%s'.", GetLastError(), file_path);
	}

	return mapped_memory;
}

bool copy_to_temporary_file(TCHAR* full_source_file_path, TCHAR* full_temporary_file_path_result)
{
	bool success = false;

	const size_t NUM_TEMPORARY_PATH_CHARS = MAX_PATH_CHARS - 14;
	TCHAR temporary_path[NUM_TEMPORARY_PATH_CHARS];

	if(GetTempPath(NUM_TEMPORARY_PATH_CHARS, temporary_path) != 0)
	{
		if(GetTempFileName(temporary_path, TEXT("WCE"), 0, full_temporary_file_path_result) != 0)
		{
			if(CopyFile(full_source_file_path, full_temporary_file_path_result, FALSE) == TRUE)
			{
				success = true;
			}
			else
			{
				log_print(LOG_ERROR, "Copy To Temporary File: Error %lu while trying to copy to the temporary file '%s'.", GetLastError(), full_temporary_file_path_result);
			}
		}
		else
		{
			log_print(LOG_ERROR, "Copy To Temporary File: Error %lu while trying to get the temporary file path in '%s'.", GetLastError(), temporary_path);
		}
	}
	else
	{
		log_print(LOG_ERROR, "Copy To Temporary File: Error %lu while trying to get the temporary directory path.", GetLastError());
	}

	return success;
}

bool read_first_file_bytes(TCHAR* path, void* file_buffer, DWORD num_bytes_to_read)
{
	bool success = true;
	HANDLE file_handle = CreateFile(path,
								     GENERIC_READ,
								     FILE_SHARE_READ,
								     NULL,
								     OPEN_EXISTING,
								     0,
								     NULL);

	if(file_handle != INVALID_HANDLE_VALUE)
	{
		DWORD num_bytes_read;
		success = (ReadFile(file_handle, file_buffer, num_bytes_to_read, &num_bytes_read, NULL) == TRUE)
					&& (num_bytes_read == num_bytes_to_read);
		CloseHandle(file_handle);
	}
	else
	{
		log_print(LOG_ERROR, "Read First File Bytes: Error %lu while trying to get the file handle for '%s'.", GetLastError(), path);
		success = false;
	}

	return success;
}

bool tchar_query_registry(HKEY hkey, const TCHAR* key_name, const TCHAR* value_name, TCHAR* value_data, DWORD value_data_size)
{
	LONG error_code;
	bool success;

	HKEY opened_key;
	error_code = RegOpenKeyEx(hkey, key_name, 0, KEY_QUERY_VALUE, &opened_key);
	success = (error_code == ERROR_SUCCESS);

	if(!success)
	{
		log_print(LOG_ERROR, "Query Registry: Error %ld while trying to open registry key '%s'.", error_code, key_name);
		// These registry functions only return this value, they don't set the last error code.
		// We'll do it ourselves for consistent error handling when calling our function.
		SetLastError(error_code);
		return success;
	}

	DWORD value_data_type;
	// If RegQueryValueExA succeeds, value_data_size is set to the actual value's size instead of the buffer's size.
	error_code = RegQueryValueEx(opened_key, value_name, NULL, &value_data_type, (unsigned char*) value_data, &value_data_size);
	success = (error_code == ERROR_SUCCESS);
	RegCloseKey(opened_key);

	// For now, we'll only handle simple string. Strings with expandable environment variables are skipped (REG_EXPAND_SZ).
	if(success && (value_data_type != REG_SZ))
	{
		log_print(LOG_ERROR, "Query Registry: Unsupported data type %lu in registry value '%s\\%s'.", value_data_type, key_name, value_name);
		error_code = ERROR_NOT_SUPPORTED;
		success = false;
	}

	// According to MSDN, we need to ensure that the string we read is null terminated. Whoever calls this function should
	// always guarantee that the buffer is able to hold whatever the possible string values are plus a null terminator.
	size_t num_value_data_chars = value_data_size / sizeof(TCHAR);
	if(success && value_data[num_value_data_chars - 1] != TEXT('\0'))
	{
		value_data[num_value_data_chars] = TEXT('\0');
	}

	SetLastError(error_code);
	return success;
}

static HANDLE LOG_FILE_HANDLE = INVALID_HANDLE_VALUE;
bool create_log_file(const TCHAR* file_path)
{
	if(file_path == NULL)
	{
		_ASSERT(false);
		return false;
	}

	TCHAR full_log_path[MAX_PATH_CHARS];
	GetFullPathName(file_path, MAX_PATH_CHARS, full_log_path, NULL);
	PathAppend(full_log_path, TEXT(".."));
	create_directories(full_log_path);

	LOG_FILE_HANDLE = CreateFile(file_path,
								GENERIC_WRITE,
								FILE_SHARE_READ,
								NULL,
								CREATE_ALWAYS,
								FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
								NULL);

	return LOG_FILE_HANDLE != INVALID_HANDLE_VALUE;
}

void close_log_file(void)
{
	if(LOG_FILE_HANDLE != NULL && LOG_FILE_HANDLE != INVALID_HANDLE_VALUE)
	{
		CloseHandle(LOG_FILE_HANDLE);
		LOG_FILE_HANDLE = INVALID_HANDLE_VALUE;
	}
	else
	{
		_ASSERT(false);
	}
}

const size_t MAX_CHARS_PER_LOG_TYPE = 16;
const size_t MAX_CHARS_PER_LOG_MESSAGE = 4096;
const size_t MAX_CHARS_PER_LOG_WRITE = MAX_CHARS_PER_LOG_TYPE + MAX_CHARS_PER_LOG_MESSAGE + 2;

void tchar_log_print(Log_Type log_type, const TCHAR* string_format, ...)
{
	TCHAR log_buffer[MAX_CHARS_PER_LOG_WRITE];
	
	StringCchCopy(log_buffer, MAX_CHARS_PER_LOG_TYPE, LOG_TYPE_TO_STRING[log_type]);
	size_t num_log_type_chars;
	StringCchLength(log_buffer, MAX_CHARS_PER_LOG_TYPE, &num_log_type_chars);

	va_list arguments;
	va_start(arguments, string_format);
	StringCchVPrintf(&log_buffer[num_log_type_chars], MAX_CHARS_PER_LOG_MESSAGE, string_format, arguments);
	va_end(arguments);

	StringCchCat(log_buffer, MAX_CHARS_PER_LOG_WRITE, TEXT("\r\n"));

	#ifdef BUILD_9X
		wchar_t utf_16_log_buffer[MAX_CHARS_PER_LOG_WRITE];
		MultiByteToWideChar(CP_ACP, 0, log_buffer, -1, utf_16_log_buffer, MAX_CHARS_PER_LOG_WRITE);

		char utf_8_log_buffer[MAX_CHARS_PER_LOG_WRITE];
		WideCharToMultiByte(CP_UTF8, 0, utf_16_log_buffer, -1, utf_8_log_buffer, sizeof(utf_8_log_buffer), NULL, NULL);
	#else
		char utf_8_log_buffer[MAX_CHARS_PER_LOG_WRITE];
		WideCharToMultiByte(CP_UTF8, 0, log_buffer, -1, utf_8_log_buffer, sizeof(utf_8_log_buffer), NULL, NULL);
	#endif

	size_t num_bytes_to_write;
	StringCbLengthA(utf_8_log_buffer, sizeof(utf_8_log_buffer), &num_bytes_to_write);

	DWORD num_bytes_written;
	WriteFile(LOG_FILE_HANDLE, utf_8_log_buffer, (DWORD) num_bytes_to_write, &num_bytes_written, NULL);
}

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
		if(*str == TEXT(',') || *str == TEXT('\n'))
		{
			needs_escaping = true;
		}
		else if(*str == TEXT('\"'))
		{
			needs_escaping = true;
			++total_num_chars; // Extra quotation mark.
		}

		++total_num_chars;
		++str;
	}

	++total_num_chars; // Null terminator.

	if(needs_escaping)
	{
		total_num_chars += 2; // Two quotation marks to wrap the value.
	}

	*size_required = total_num_chars * sizeof(TCHAR);
	return needs_escaping;
}

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
			MoveMemory(next_char_2, next_char_1, string_size(next_char_1));
			*next_char_1 = TEXT('\"');
			++str; // Skip this new quotation mark.
		}

		++str;
	}

	if(needs_escaping)
	{
		size_t escaped_string_size = string_size(string_start);
		size_t num_escaped_string_chars = escaped_string_size / sizeof(TCHAR);
		MoveMemory(string_start + 1, string_start, escaped_string_size);
		string_start[0] = TEXT('\"');
		string_start[num_escaped_string_chars] = TEXT('\"');
		string_start[num_escaped_string_chars+1] = TEXT('\0');
	}
}

HANDLE create_csv_file(const TCHAR* file_path)
{
	if(file_path == NULL)
	{
		_ASSERT(false);
		return NULL;
	}

	TCHAR full_csv_path[MAX_PATH_CHARS];
	GetFullPathName(file_path, MAX_PATH_CHARS, full_csv_path, NULL);
	PathAppend(full_csv_path, TEXT(".."));
	create_directories(full_csv_path);

	return CreateFile(file_path,
					GENERIC_WRITE,
					0,
					NULL,
					CREATE_ALWAYS,
					FILE_ATTRIBUTE_NORMAL,
					NULL);
}

void close_csv_file(HANDLE csv_file)
{
	if(csv_file != NULL && csv_file != INVALID_HANDLE_VALUE)
	{
		CloseHandle(csv_file);
	}
	else
	{
		_ASSERT(false);
	}
}

void csv_print_header(Arena* arena, HANDLE csv_file, const Csv_Type row_types[], size_t num_columns)
{
	if(csv_file == NULL || csv_file == INVALID_HANDLE_VALUE)
	{
		_ASSERT(false);
		return;
	}

	char* csv_header = push_arena(arena, 0, char);
	for(size_t i = 0; i < num_columns; ++i)
	{
		Csv_Type type = row_types[i];
		const char* column_name = CSV_TYPE_TO_ASCII_STRING[type];
		size_t column_name_size = string_size(column_name);

		if(i == num_columns - 1)
		{
			char* csv_column = push_and_copy_to_arena(arena, column_name_size + 1, char, column_name, column_name_size);
			csv_column[column_name_size - 1] = '\r';
			csv_column[column_name_size] = '\n';
		}
		else
		{
			char* csv_column = push_and_copy_to_arena(arena, column_name_size, char, column_name, column_name_size);
			csv_column[column_name_size - 1] = ',';
		}
	}

	DWORD csv_header_size = (DWORD) pointer_difference(arena->available_memory, csv_header);
	DWORD num_bytes_written;
	WriteFile(csv_file, csv_header, csv_header_size, &num_bytes_written, NULL);
}

void csv_print_row(Arena* arena, HANDLE csv_file, const Csv_Type row_types[], Csv_Entry row[], size_t num_columns)
{
	row_types; // @TODO: Use this in the future for file filters/groups.

	if(csv_file == NULL || csv_file == INVALID_HANDLE_VALUE)
	{
		_ASSERT(false);
		return;
	}

	for(size_t i = 0; i < num_columns; ++i)
	{
		TCHAR* value = row[i].value;
		if(value == NULL) value = TEXT(""); // An empty string never needs escaping.
		size_t size_required_for_escaping; // String size or extra size to properly escape the CSV string.

		if(does_csv_string_require_escaping(value, &size_required_for_escaping))
		{
			value = push_and_copy_to_arena(arena, size_required_for_escaping, TCHAR, value, string_size(value));
			escape_csv_string(value);
		}

		#ifdef BUILD_9X
			int num_chars_required_utf_16 = MultiByteToWideChar(CP_ACP, 0, value, -1, NULL, 0);
			int size_required_utf_16 = num_chars_required_utf_16 * sizeof(wchar_t);
			
			wchar_t* utf_16_value = push_arena(arena, size_required_utf_16, wchar_t);
			MultiByteToWideChar(CP_ACP, 0, value, -1, utf_16_value, num_chars_required_utf_16);

			row[i].utf_16_value = utf_16_value;
			row[i].value = NULL;
		#else
			row[i].utf_16_value = value;
			row[i].value = NULL;
		#endif
	}

	char* csv_row = push_arena(arena, 0, char);
	for(size_t i = 0; i < num_columns; ++i)
	{
		wchar_t* value = row[i].utf_16_value;

		int size_required_utf_8 = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);

		if(i == num_columns - 1)
		{
			char* csv_row_value = push_arena(arena, size_required_utf_8 + 1, char);
			WideCharToMultiByte(CP_UTF8, 0, value, -1, csv_row_value, size_required_utf_8, NULL, NULL);
			csv_row_value[size_required_utf_8 - 1] = '\r';
			csv_row_value[size_required_utf_8] = '\n';
		}
		else
		{
			char* csv_row_value = push_arena(arena, size_required_utf_8, char);
			WideCharToMultiByte(CP_UTF8, 0, value, -1, csv_row_value, size_required_utf_8, NULL, NULL);
			csv_row_value[size_required_utf_8 - 1] = ',';
		}
	}

	DWORD csv_row_size = (DWORD) pointer_difference(arena->available_memory, csv_row);
	DWORD num_bytes_written;
	WriteFile(csv_file, csv_row, csv_row_size, &num_bytes_written, NULL);
}








#ifndef BUILD_9X

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

	enum OBJECT_INFORMATION_CLASS
	{
    	ObjectBasicInformation = 0,
		ObjectNameInformation = 1,
		ObjectTypeInformation = 2,
		ObjectAllTypesInformation = 3,
		ObjectHandleInformation = 4
	};

	struct OBJECT_NAME_INFORMATION
	{
		UNICODE_STRING Name;
		WCHAR NameBuffer[ANYSIZE_ARRAY];
	};

	struct OBJECT_TYPE_INFORMATION
	{
		UNICODE_STRING TypeName;
		ULONG Reserved[22]; // reserved for internal use
	};

	#define NT_QUERY_SYSTEM_INFORMATION(function_name) NTSTATUS function_name(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength)
	#pragma warning(push)
	#pragma warning(disable : 4100) // Disable warnings for the unused stub function parameters.
	NT_QUERY_SYSTEM_INFORMATION(stub_nt_query_system_information)
	{
		log_print(LOG_WARNING, "NtQuerySystemInformation: Calling the stub version of this function.");
		_ASSERT(false);
		return STATUS_NOT_IMPLEMENTED;
	}
	#pragma warning(pop)
	typedef NT_QUERY_SYSTEM_INFORMATION(Nt_Query_System_Information);
	Nt_Query_System_Information* dll_nt_query_system_information = stub_nt_query_system_information;
	#define NtQuerySystemInformation dll_nt_query_system_information

	#define NT_QUERY_OBJECT(function_name) NTSTATUS function_name(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass, PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
	#pragma warning(push)
	#pragma warning(disable : 4100) // Disable warnings for the unused stub function parameters.
	NT_QUERY_OBJECT(stub_nt_query_object)
	{
		log_print(LOG_WARNING, "NtQueryObject: Calling the stub version of this function.");
		_ASSERT(false);
		return STATUS_NOT_IMPLEMENTED;
	}
	#pragma warning(pop)
	typedef NT_QUERY_OBJECT(Nt_Query_Object);
	Nt_Query_Object* dll_nt_query_object = stub_nt_query_object;
	#define NtQueryObject dll_nt_query_object

	HANDLE windows_nt_query_file_handle_from_file_path(Arena* arena, char* file_path)
	{
		HANDLE file_handle_result = INVALID_HANDLE_VALUE;

		HMODULE ntdll_library = LoadLibraryA("Ntdll.dll");

		if(ntdll_library == NULL)
		{
			log_print(LOG_ERROR, "Error %lu while loading library.", GetLastError());
			return file_handle_result;
		}

		Nt_Query_System_Information* nt_query_system_information_address = (Nt_Query_System_Information*) GetProcAddress(ntdll_library, "NtQuerySystemInformation");

		if(nt_query_system_information_address == NULL)
		{
			log_print(LOG_ERROR, "Error %lu while retrieving the function's address.", GetLastError());
			return file_handle_result;
		}
		else
		{
			NtQuerySystemInformation = nt_query_system_information_address;
		}

		Nt_Query_Object* nt_query_object_function_address = (Nt_Query_Object*) GetProcAddress(ntdll_library, "NtQueryObject");

		if(nt_query_object_function_address == NULL)
		{
			log_print(LOG_ERROR, "Error %lu while retrieving the function's address.", GetLastError());
			return file_handle_result;
		}
		else
		{
			NtQueryObject = nt_query_object_function_address;
		}

		ULONG handle_info_size = (ULONG) (arena->total_size - arena->used_size) / 2;
		SYSTEM_HANDLE_INFORMATION* handle_info = push_arena(arena, handle_info_size, SYSTEM_HANDLE_INFORMATION);
		ULONG actual_handle_info_size;
		NTSTATUS error_code = NtQuerySystemInformation(SystemHandleInformation, handle_info, handle_info_size, &actual_handle_info_size);
		
		if(!NT_SUCCESS(error_code))
		{
			log_print(LOG_ERROR, "Error %ld while running NtQuerySystemInformation.", error_code);
			return file_handle_result;
		}

		ULONG object_info_size = (ULONG) (arena->total_size - arena->used_size);
		OBJECT_NAME_INFORMATION* object_info = push_arena(arena, object_info_size, OBJECT_NAME_INFORMATION);
		object_info;

		HANDLE current_process_handle = GetCurrentProcess();
		wchar_t wide_file_path[MAX_PATH_CHARS];
		MultiByteToWideChar(CP_ACP, 0, file_path, -1, wide_file_path, MAX_PATH_CHARS);

		for(ULONG i = 0; i < handle_info->NumberOfHandles; ++i)
		{
			SYSTEM_HANDLE_TABLE_ENTRY_INFO handle_entry = handle_info->Handles[i];
			bool foo = true;

			if(foo)
			{
				HANDLE process_handle = OpenProcess(PROCESS_DUP_HANDLE, FALSE, handle_entry.UniqueProcessId);
				HANDLE file_handle = (HANDLE) handle_entry.HandleValue;

				HANDLE duplicated_file_handle;
				if(	process_handle != NULL &&
					DuplicateHandle(process_handle, file_handle,
									current_process_handle, &duplicated_file_handle,
									0, FALSE, DUPLICATE_SAME_ACCESS))
				{
					//DWORD bar = GetFileType(duplicated_file_handle); bar;

					SecureZeroMemory(object_info, object_info_size);
					ULONG actual_object_info_size;
					NTSTATUS error_code_2 = NtQueryObject(duplicated_file_handle, ObjectNameInformation, object_info, object_info_size, &actual_object_info_size);
					
					if(NT_SUCCESS(error_code_2))
					{
						//OBJECT_NAME_INFORMATION local_object_info = *object_info; local_object_info;
						UNICODE_STRING filename = object_info->Name;

						/*log_print(LOG_INFO, "Process %hu has handle %hu of type %hhu with access rights 0x%08X. Name is '%S'.",
						handle_entry.UniqueProcessId, handle_entry.HandleValue,
						handle_entry.ObjectTypeIndex, handle_entry.GrantedAccess,
						filename.Buffer);*/

						_ASSERT(NT_SUCCESS(error_code_2));

						size_t num_filename_chars = filename.Length / sizeof(wchar_t);
						if(wcsncmp(wide_file_path, filename.Buffer, num_filename_chars) == 0)
						{
							file_handle_result = duplicated_file_handle;
							break;
						}
					}
					else
					{
						_ASSERT(!NT_SUCCESS(error_code_2));
					}

					//CloseHandle(duplicated_file_handle);
					duplicated_file_handle = INVALID_HANDLE_VALUE;
				}

				if(process_handle != NULL)
				{
					//CloseHandle(process_handle);
					process_handle = INVALID_HANDLE_VALUE;
				}
			}
		}

		file_path;

		FreeLibrary(ntdll_library);
		clear_arena(arena);

		return file_handle_result;
	}

#endif
