#include "web_cache_exporter.h"
#include "memory_and_file_io.h"

#ifdef DEBUG
	void* DEBUG_VIRTUAL_MEMORY_BASE_ADDRESS = (void*) 0x10000000;
	void* DEBUG_MEMORY_MAPPING_BASE_ADDRESS = (void*) 0x50000000;
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
	arena->available_memory = retreat_bytes(arena->available_memory, arena->used_size);
	#ifdef DEBUG
	SecureZeroMemory(arena->available_memory, arena->used_size);
	#endif
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

bool format_filetime_date_time(FILETIME date_time, char* formatted_string)
{
	if(date_time.dwLowDateTime == 0 && date_time.dwHighDateTime == 0)
	{
		*formatted_string = '\0';
		return true;
	}

	SYSTEMTIME dt;
	bool success = FileTimeToSystemTime(&date_time, &dt) != 0;
	return success && SUCCEEDED(StringCchPrintfA(formatted_string, MAX_FORMATTED_DATE_TIME_CHARS, "%4d-%02d-%02d %02d:%02d:%02d", dt.wYear, dt.wMonth, dt.wDay, dt.wHour, dt.wMinute, dt.wSecond));
}

bool format_dos_date_time(Dos_Date_Time date_time, char* formatted_string)
{
	if(date_time.date == 0 && date_time.time == 0)
	{
		*formatted_string = '\0';
		return true;
	}

	FILETIME filetime;
	bool success = DosDateTimeToFileTime(date_time.date, date_time.time, &filetime) != 0;
	return success && format_filetime_date_time(filetime, formatted_string);
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
// filename = "a.gif.gz" -> gif.gz
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

		file_extension = (*str != '\0') ? (++str) : (str);
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

static void url_to_path(char* url)
{
	if(url == NULL) return;

	char* url_start = url;
	char* last_path_separator = NULL;
	char* url_path_start = NULL;
	while(*url != '\0')
	{
		if(*url == '/')
		{
			*url = '\\';
			last_path_separator = url;
		}
		else if(*url == '?' || *url == '#')
		{
			break;
		}
		else if(*url == ':')
		{
			url_path_start = url + 1;

			char* next_char_1 = url + 1;
			if(*next_char_1 == '/')
			{
				url_path_start = next_char_1 + 1;
				++url;
			}

			char* next_char_2 = next_char_1 + 1;
			if(*next_char_1 == '/' && *next_char_2 == '/')
			{
				url_path_start = next_char_2 + 1;
				++url;
			}
			
		}

		++url;
	}

	if(last_path_separator != NULL)
	{
		*last_path_separator = '\0';
	}

	if(url_path_start != NULL)
	{
		MoveMemory(url_start, url_path_start, string_size(url_path_start));
	}
}

bool create_empty_file(const char* file_path)
{
	DWORD error_code;
	bool success;

	HANDLE empty_file = CreateFileA(file_path, 0, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	error_code = GetLastError();
	success = empty_file != INVALID_HANDLE_VALUE;
	CloseHandle(empty_file);

	SetLastError(error_code);
	return success;
}

void create_directories(const char* path_to_create)
{
	const size_t MAX_CREATE_DIRECTORY_PATH_CHARS = 248;
	char path[MAX_CREATE_DIRECTORY_PATH_CHARS];
	// @TODO: replace with GetFullPathNameA(path_to_create, MAX_CREATE_DIRECTORY_PATH_CHARS, path, NULL);
	// _STATIC_ASSERT(MAX_CREATE_DIRECTORY_PATH_CHARS <= MAX_PATH_CHARS);
	StringCchCopyA(path, MAX_CREATE_DIRECTORY_PATH_CHARS, path_to_create);

	for(size_t i = 0; i < MAX_CREATE_DIRECTORY_PATH_CHARS; ++i)
	{
		if(path[i] == '\0')
		{
			CreateDirectoryA(path, NULL);
			break;
		}
		else if(path[i] == '\\')
		{
			path[i] = '\0';
			CreateDirectoryA(path, NULL);
			path[i] = '\\';
		}
	}
}

bool copy_file_using_url_directory_structure(Arena* arena, const char* full_file_path, const char* base_destination_path, const char* url, const char* filename)
{
	char* url_path;
	if(url != NULL)
	{
		url_path = push_and_copy_to_arena(arena, string_size(url), char, url, string_size(url));
		url_to_path(url_path);
	}
	else
	{
		url_path = NULL;
	}

	char full_copy_target_path[MAX_PATH_CHARS];
	GetFullPathNameA(base_destination_path, MAX_PATH_CHARS, full_copy_target_path, NULL);
	if(url_path != NULL) PathAppendA(full_copy_target_path, url_path);
	
	create_directories(full_copy_target_path);
	PathAppendA(full_copy_target_path, filename);

	bool copy_success;

	#if defined(DEBUG) && defined(EXPORT_DUMMY_FILES)
		copy_success = create_empty_file(full_copy_target_path);
		full_file_path;
	#else
		copy_success = CopyFileA(full_file_path, full_copy_target_path, TRUE) != 0;
	#endif

	u32 num_naming_collisions = 0;
	while(!copy_success && GetLastError() == ERROR_FILE_EXISTS)
	{
		++num_naming_collisions;
		char full_unique_copy_target_path[MAX_PATH_CHARS];
		StringCchPrintfA(full_unique_copy_target_path, MAX_PATH_CHARS, "%s.%I32u", full_copy_target_path, num_naming_collisions);
		
		#if defined(DEBUG) && defined(EXPORT_DUMMY_FILES)
			copy_success = create_empty_file(full_unique_copy_target_path);
			full_file_path;
		#else
			copy_success = CopyFileA(full_file_path, full_unique_copy_target_path, TRUE) != 0;
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
		bool success = (file_size_low == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR);
		if(success) *file_size_result = combine_high_and_low_u32s(file_size_high, file_size_low);
		return success;
	#else
		LARGE_INTEGER file_size;
		bool success = GetFileSizeEx(file_handle, &file_size) == TRUE;
		if(success) *file_size_result = file_size.QuadPart;
		return success;
	#endif
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
		// @TODO: GetFileSize vs GetFileSizeEx for Windows 98 and ME
		// Reject empty files.
		u64 file_size;
		if(get_file_size(file_handle, &file_size))
		{
			if(file_size > 0)
			{
				HANDLE mapping_handle = CreateFileMappingA(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);

				if(mapping_handle != NULL)
				{
					#ifdef DEBUG
						mapped_memory = MapViewOfFileEx(mapping_handle, FILE_MAP_READ, 0, 0, 0, DEBUG_MEMORY_MAPPING_BASE_ADDRESS);
					#else
						mapped_memory = MapViewOfFile(mapping_handle, FILE_MAP_READ, 0, 0, 0);
					#endif
					_ASSERT(mapped_memory != NULL);
				}
				else
				{
					log_print(LOG_ERROR, "Error %d while trying to create the file mapping for '%s'.", GetLastError(), path);
				}

				CloseHandle(mapping_handle); // @Note: According to MSDN, CloseHandle() and UnmapViewOfFile() can be called in any order.
			}
			else
			{
				log_print(LOG_WARNING, "Skipping file mapping for empty file '%s'.", path);
			}

		}
		else
		{
			log_print(LOG_ERROR, "Error %d while trying to get the file size for '%s'.", GetLastError(), path);
		}

		CloseHandle(file_handle); // @Note: "Files for which the last view has not yet been unmapped are held open with no sharing restrictions."
	}
	else
	{
		log_print(LOG_ERROR, "Error %d while trying to get the file handle for '%s'.", GetLastError(), path);
	}

	return mapped_memory;
}

bool read_first_file_bytes(char* path, void* file_buffer, DWORD num_bytes_to_read)
{
	bool success = true;
	HANDLE file_handle = CreateFileA(path,
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
		log_print(LOG_ERROR, "Error %d while trying to get the file handle for '%s'.", GetLastError(), path);
		success = false;
	}

	return success;
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

/*bool query_registry(HKEY base_key, const char* key_name, const char* value_name, char* value_data)
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
}*/

static HANDLE LOG_FILE_HANDLE = NULL;
bool create_log_file(const char* file_path)
{
	if(file_path == NULL)
	{
		_ASSERT(false);
		return false;
	}

	char full_log_path[MAX_PATH_CHARS];
	GetFullPathNameA(file_path, MAX_PATH_CHARS, full_log_path, NULL);
	PathAppendA(full_log_path, "..");
	create_directories(full_log_path);

	LOG_FILE_HANDLE = CreateFileA(file_path,
								GENERIC_WRITE,
								FILE_SHARE_READ,
								NULL,
								CREATE_ALWAYS,
								FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
								NULL);
	console_print("Error: %d\n", GetLastError());

	return LOG_FILE_HANDLE != INVALID_HANDLE_VALUE;
}

void close_log_file(void)
{
	if(LOG_FILE_HANDLE != NULL && LOG_FILE_HANDLE != INVALID_HANDLE_VALUE)
	{
		CloseHandle(LOG_FILE_HANDLE);
		LOG_FILE_HANDLE = NULL;
	}
	else
	{
		_ASSERT(false);
	}
}

const size_t MAX_CHARS_PER_LOG_TYPE = 16;
const size_t MAX_CHARS_PER_LOG_MESSAGE = 4096;
const size_t MAX_CHARS_PER_LOG_WRITE = MAX_CHARS_PER_LOG_TYPE + MAX_CHARS_PER_LOG_MESSAGE + 2;
// @TODO: Would the log_printW idea work with WideCharToMultibyte?
void log_print(Log_Type log_type, const char* string_format, ...)
{
	if(LOG_FILE_HANDLE == NULL || LOG_FILE_HANDLE == INVALID_HANDLE_VALUE)
	{
		_ASSERT(false);
		return;
	}

	char log_buffer[MAX_CHARS_PER_LOG_WRITE];

	StringCchCopyA(log_buffer, MAX_CHARS_PER_LOG_TYPE, LOG_TYPE_TO_STRING[log_type]);
	size_t num_log_type_chars;
	StringCchLengthA(log_buffer, MAX_CHARS_PER_LOG_TYPE, &num_log_type_chars);

	va_list arguments;
	va_start(arguments, string_format);
	StringCchVPrintfA(&log_buffer[num_log_type_chars], MAX_CHARS_PER_LOG_MESSAGE, string_format, arguments);
	va_end(arguments);

	StringCchCatA(log_buffer, MAX_CHARS_PER_LOG_WRITE, "\r\n");

	size_t num_chars_to_write;
	StringCchLengthA(log_buffer, MAX_CHARS_PER_LOG_WRITE, &num_chars_to_write);

	DWORD num_bytes_written;
	WriteFile(LOG_FILE_HANDLE, log_buffer, (DWORD) (num_chars_to_write * sizeof(char)), &num_bytes_written, NULL);
}

void log_print(Log_Type log_type, const wchar_t* string_format, ...)
{
	char utf_8_string_format[MAX_CHARS_PER_LOG_WRITE];
	int utf_8_string_format_size = MAX_CHARS_PER_LOG_WRITE * sizeof(char);
	//StringCbLengthW(utf_8_string_format, MAX_CHARS_PER_LOG_WRITE * sizeof(char), &utf_8_string_format_size);
	WideCharToMultiByte(CP_UTF8, 0, string_format, -1, utf_8_string_format, utf_8_string_format_size, NULL, NULL);

	size_t num_chars_to_write;
	StringCchLengthA(utf_8_string_format, MAX_CHARS_PER_LOG_WRITE, &num_chars_to_write);

	log_type;
	DWORD num_bytes_written;
	WriteFile(LOG_FILE_HANDLE, utf_8_string_format, (DWORD) (num_chars_to_write * sizeof(char)), &num_bytes_written, NULL);
	//va_list arguments;
	//va_start(arguments, string_format);
	//log_print(log_type, utf_8_string_format, arguments);
	//va_end(arguments);
}

static bool does_csv_string_require_escaping(const char* str, size_t* size_required)
{
	if(str == NULL)
	{
		*size_required = 0;
		return false;
	}

	bool needs_escaping = false;
	size_t total_size = 0;

	while(*str != '\0')
	{
		if(*str == ',' || *str == '\n')
		{
			needs_escaping = true;
		}
		else if(*str == '\"')
		{
			needs_escaping = true;
			++total_size; // Extra quotation mark.
		}

		++total_size;
		++str;
	}

	++total_size; // Null terminator.

	if(needs_escaping)
	{
		total_size += 2; // Two quotation marks to wrap the value.
	}

	*size_required = total_size;
	return needs_escaping;
}

static void escape_csv_string(char* str)
{
	if(str == NULL) return;

	char* string_start = str;
	bool needs_escaping = false;

	while(*str != '\0')
	{
		if(*str == ',' || *str == '\n')
		{
			needs_escaping = true;
		}
		else if(*str == '\"')
		{
			needs_escaping = true;

			char* next_char_1 = str + 1;
			char* next_char_2 = str + 2;
			MoveMemory(next_char_2, next_char_1, string_size(next_char_1));
			*next_char_1 = '\"';
			++str; // Skip this new quotation mark.
		}

		++str;
	}

	if(needs_escaping)
	{
		size_t escaped_string_size = string_size(string_start);
		MoveMemory(string_start + 1, string_start, escaped_string_size);
		string_start[0] = '\"';
		string_start[escaped_string_size] = '\"';
		string_start[escaped_string_size+1] = '\0';
	}
}

HANDLE create_csv_file(const char* file_path)
{
	if(file_path == NULL)
	{
		_ASSERT(false);
		return NULL;
	}

	char full_csv_path[MAX_PATH_CHARS];
	GetFullPathNameA(file_path, MAX_PATH_CHARS, full_csv_path, NULL);
	PathAppendA(full_csv_path, "..");
	create_directories(full_csv_path);

	return CreateFileA(file_path,
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

void csv_print_header(HANDLE csv_file, const char* header)
{
	if(csv_file == NULL || csv_file == INVALID_HANDLE_VALUE)
	{
		return;
	}

	DWORD num_bytes_written;
	WriteFile(csv_file, header, (DWORD) (strlen(header) * sizeof(char)), &num_bytes_written, NULL);
}

void csv_print_row(Arena* arena, HANDLE csv_file, char* rows[], size_t num_columns)
{
	if(csv_file == NULL || csv_file == INVALID_HANDLE_VALUE)
	{
		return;
	}

	for(size_t i = 0; i < num_columns; ++i)
	{
		char* value = rows[i];
		size_t size_required; // String size or extra size to properly escape the CSV string.
		if(does_csv_string_require_escaping(value, &size_required))
		{
			value = push_and_copy_to_arena(arena, size_required, char, value, string_size(value));
			escape_csv_string(value);
			rows[i] = value;
		}
	}
	
	char* csv_row = push_arena(arena, 0, char);
	for(size_t i = 0; i < num_columns; ++i)
	{
		char* value = rows[i];
		size_t value_size = string_size(value);
		if(i == num_columns - 1)
		{
			char* csv_row_value = push_and_copy_to_arena(arena, value_size + 1, char, value, value_size);
			csv_row_value[value_size - 1] = '\r';
			csv_row_value[value_size] = '\n';
		}
		else
		{
			char* csv_row_value = push_and_copy_to_arena(arena, value_size, char, value, value_size);
			csv_row_value[value_size - 1] = ',';
		}
	}

	size_t csv_row_size = pointer_difference(arena->available_memory, csv_row);
	DWORD num_bytes_written;
	WriteFile(csv_file, csv_row, (DWORD) csv_row_size, &num_bytes_written, NULL);
}
