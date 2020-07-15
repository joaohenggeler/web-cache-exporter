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

static s8 hexadecimal_char_to_integer(TCHAR hex_char)
{
	if(TEXT('0') <= hex_char && hex_char <= TEXT('9')) return (s8) (hex_char - TEXT('0'));
	else if(TEXT('a') <= hex_char && hex_char <= TEXT('f')) return (s8) (hex_char - TEXT('a') + 0x0A);
	else if(TEXT('A') <= hex_char && hex_char <= TEXT('F')) return (s8) (hex_char - TEXT('A') + 0x0A);
	else return -1;
}

bool decode_url(TCHAR* url)
{
	bool success = true;

	if(url != NULL)
	{
		while(*url != TEXT('\0'))
		{
			// Decode percent-encoded characters.
			// @Documentation: https://tools.ietf.org/html/rfc3986#section-2.1
			if(*url == TEXT('%'))
			{
				TCHAR next_char_1 = *(url+1);
				TCHAR next_char_2 = (next_char_1 != TEXT('\0')) ? (*(url+2)) : (TEXT('\0'));
				s8 next_value_1 = hexadecimal_char_to_integer(next_char_1);
				s8 next_value_2 = hexadecimal_char_to_integer(next_char_2);

				if(next_value_1 > -1 && next_value_2 > -1)
				{
					TCHAR* url_end_of_encoded_char = url + 2;
					MoveMemory(url, url_end_of_encoded_char, string_size(url_end_of_encoded_char));
					*url = (TCHAR) (next_value_1 * 16 + next_value_2);
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
		_ASSERT(false);
	}

	return success;
}

// scheme:[//authority]path[?query][#fragment]
// authority = [userinfo@]host[:port]

bool partition_url(Arena* arena, const TCHAR* original_url, Url_Parts* url_parts)
{
	if(original_url == NULL) return false;

	TCHAR* url = push_and_copy_to_arena(arena, string_size(original_url), TCHAR, original_url, string_size(original_url));
	SecureZeroMemory(url_parts, sizeof(Url_Parts));

	TCHAR* remaining_url = NULL;
	TCHAR* scheme = _tcstok_s(url, TEXT(":"), &remaining_url);

	if(scheme == NULL || is_string_empty(scheme))
	{
		return false;
	}

	url_parts->scheme = push_and_copy_to_arena(arena, string_size(scheme), TCHAR, scheme, string_size(scheme));

	if(*remaining_url == TEXT('/') && *(remaining_url+1) == TEXT('/'))
	{
		remaining_url += 2;

		bool has_path_delimiter = (_tcschr(remaining_url, TEXT('/')) != NULL) || (_tcschr(remaining_url, TEXT('\\')) != NULL);
		TCHAR* authority = _tcstok_s(NULL, TEXT("/\\"), &remaining_url);

		if(has_path_delimiter && authority != NULL)
		{
			TCHAR* remaining_authority = NULL;
			
			bool has_userinfo = _tcschr(authority, TEXT('@')) != NULL;
			TCHAR* userinfo = (has_userinfo) ? (_tcstok_s(authority, TEXT("@"), &remaining_authority)) : (NULL);

			TCHAR* string_to_split = (has_userinfo) ? (NULL) : (authority);
			TCHAR* host = _tcstok_s(string_to_split, TEXT(":"), &remaining_authority);
			TCHAR* port = (!is_string_empty(remaining_authority)) ? (remaining_authority) : (NULL);

			if(host != NULL && !is_string_empty(host))
			{
				url_parts->host = push_and_copy_to_arena(arena, string_size(host), TCHAR, host, string_size(host));
			}
			else
			{
				return false;
			}

			if(userinfo != NULL) url_parts->userinfo = push_and_copy_to_arena(arena, string_size(userinfo), TCHAR, userinfo, string_size(userinfo));
			if(port != NULL) url_parts->port = push_and_copy_to_arena(arena, string_size(port), TCHAR, port, string_size(port));
		}
		else
		{
			return false;
		}
	}

	TCHAR* path = _tcstok_s(NULL, TEXT("?"), &remaining_url);
	if(path != NULL && !is_string_empty(path))
	{
		url_parts->path = push_and_copy_to_arena(arena, string_size(path), TCHAR, path, string_size(path));
	}
	else
	{
		return false;
	}

	TCHAR* query = _tcstok_s(NULL, TEXT("#"), &remaining_url);
	TCHAR* fragment = (!is_string_empty(remaining_url)) ? (remaining_url) : (NULL);
	if(query != NULL) url_parts->query = push_and_copy_to_arena(arena, string_size(query), TCHAR, query, string_size(query));
	if(fragment != NULL) url_parts->fragment = push_and_copy_to_arena(arena, string_size(fragment), TCHAR, fragment, string_size(fragment));

	return true;

	/*TCHAR* scheme_begin = url;
	bool seen_scheme = false;
	while(*url != TEXT('\0'))
	{
		if(*url == TEXT(':'))
		{
			size_t scheme_size = pointer_difference(url, scheme_begin) + sizeof(TCHAR);
			size_t num_scheme_chars = scheme_size / sizeof(TCHAR);
			url_parts->scheme = push_and_copy_to_arena(arena, scheme_size, TCHAR, scheme_begin, scheme_size);
			url_parts->scheme[num_scheme_chars - 1] = TEXT('\0');

			seen_scheme = true;
			break;
		}

		++url;
	}

	if(!seen_scheme) return false;
	++url;

	// authority = [username:password@]host[:port]
	// authority = [username@]host[:port]
	if(*url == TEXT('/') && *(url+1) == TEXT('/'))
	{
		url += 2;
		TCHAR* authority_begin = url;

		TCHAR* username_begin = NULL;
		TCHAR* password_begin = NULL;
		TCHAR* host_begin = NULL;
		TCHAR* port_begin = NULL;

		TCHAR* username_end = NULL;
		TCHAR* password_end = NULL;
		TCHAR* host_end = NULL;
		TCHAR* port_end = NULL;

		size_t username_size = 0;
		size_t password_size = 0;
		size_t host_size = 0;
		size_t port_size = 0;

		bool seen_username = false;
		bool seen_end_of_userinfo = false;

		while(*url != TEXT('\0'))
		{
			if(*url == TEXT(':'))
			{ 
				if(!seen_end_of_userinfo)
				{
					username_begin = authority_begin;
					username_end = url - 1;
					password_begin = url + 1;
				}
				else
				{
					if(host_begin != NULL) host_end = url - 1;
					port_begin = url + 1;
				}
			}
			else if(*url == TEXT('@'))
			{
				seen_end_of_userinfo = true;

				if(password_begin != NULL)
				{
					password_end = url - 1;
				}
				else
				{
					username_end = url - 1;
				}

				host_begin = url + 1;
			}

			++url;
		}

		if(port_begin != NULL) port_end = url - 1;
		if(host_begin == NULL) return false;

		if(username_begin != NULL)
		{
			size_t username_size = pointer_difference(url, username_begin) + sizeof(TCHAR);
			size_t num_username_chars = username_size / sizeof(TCHAR);
			url_parts->scheme = push_and_copy_to_arena(arena, username_size, TCHAR, username_begin, username_size);
			url_parts->scheme[num_scheme_chars - 1] = TEXT('\0');
		}
	}


	return true;*/

	/*
	bool success = true;

	while(*url != TEXT('\0'))
	{
		size_t part_size = pointer_difference(url, part_begin) + sizeof(TCHAR);
		size_t num_part_chars = part_size / sizeof(TCHAR);

		if(!seen_scheme && *url == TEXT(':'))
		{
			seen_scheme = true;
			url_parts->scheme = push_and_copy_to_arena(arena, part_size, TCHAR, part_begin, part_size);
			url_parts->scheme[num_part_chars - 1] = TEXT('\0');
			part_begin = url + 1;
		}
		else if(XXX && *url == TEXT('/') && *(url+1) == TEXT('/'))
		{


			seen_authority = true;
		}

		++url;
	}

	return success;*/
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

static void correct_url_path_separators(TCHAR* path)
{
	if(path != NULL)
	{
		while(*path != TEXT('\0'))
		{
			// Path = "www.example.com/../file.ext" -> "www.example.com/__/file.ext"
			/*if( 	(*path == TEXT('\\') || *path == TEXT('/'))
				&& 	*(path+1) == TEXT('.')
				&& 	*(path+2) == TEXT('.')
				&& 	(*(path+3) == TEXT('\\') || *(path+3) == TEXT('/')))
			{
				*(path+1) = TEXT('_');
				*(path+2) = TEXT('_');
			}*/

			// Path = "www.example.com/path/file.ext" -> "www.example.com\path\file.ext"
			if(*path == TEXT('/'))
			{
				*path = TEXT('\\');
			}

			++path;
		}		
	}
}

static void convert_url_to_path(Arena* arena, const TCHAR* url, TCHAR* path)
{
	Url_Parts url_parts;
	if(partition_url(arena, url, &url_parts))
	{
		path[0] = TEXT('\0');
		if(url_parts.host != NULL) PathAppend(path, url_parts.host);
		
		_ASSERT(url_parts.path != NULL);

		correct_url_path_separators(url_parts.path);
		// Remove the resource's filename so it's not part of the final path.
		TCHAR* last_separator = _tcsrchr(url_parts.path, TEXT('\\'));
		if(last_separator != NULL) *last_separator = TEXT('\0');
		
		PathAppend(path, url_parts.path);
	}
}

/*void path_append_and_truncate(TCHAR* base_path, const TCHAR* path_to_append)
{
	const size_t MAX_CHARS = MAX_PATH_CHARS - 1;
	size_t num_base_path_chars = _tcslen(base_path);

	if(num_base_path_chars > MAX_CHARS)
	{
		base_path[MAX_CHARS] = TEXT('\0');
		return;
	}

	size_t num_path_to_append_chars = _tcslen(path_to_append);
	TCHAR foo[MAX_PATH_CHARS];
	StringCchCopy(foo, MAX_PATH_CHARS, path_to_append);

	MIN();
}*/

bool copy_file_using_url_directory_structure(Arena* arena, const TCHAR* full_file_path, const TCHAR* base_destination_path, const TCHAR* url, const TCHAR* filename)
{
	TCHAR url_path[MAX_PATH_CHARS] = TEXT("");
	if(url != NULL) convert_url_to_path(arena, url, url_path);

	TCHAR full_copy_target_path[MAX_PATH_CHARS] = TEXT("");
	get_full_path_name((TCHAR*) base_destination_path, full_copy_target_path);
	bool path_append_success = PathAppend(full_copy_target_path, url_path) == TRUE;
	if(is_string_empty(url_path) || !path_append_success)
	{
		console_print("The website directory structure for the file '%s' could not be created because it's too long. This file will be copied to the base export destination instead: '%s'.\n", filename, base_destination_path);
		log_print(LOG_WARNING, "Copy File Using URL Structure: Failed to build the website directory structure for the file '%s' because its URL is too long. This file will be copied to the base export destination instead: '%s'.", filename, base_destination_path);
		get_full_path_name((TCHAR*) base_destination_path, full_copy_target_path);
	}

	_ASSERT(!is_string_empty(full_copy_target_path));
	
	create_directories(full_copy_target_path);
	path_append_success = PathAppend(full_copy_target_path, filename) == TRUE;
	if(!path_append_success)
	{
		console_print("An error occurred while building the final output copy path for the file '%s'. This file will not be copied.\n", filename);
		log_print(LOG_ERROR, "Copy File Using URL Structure: Failed to append the filename '%s' to the output copy path '%s'. This file will not be copied.", filename, full_copy_target_path);
		_ASSERT(false);
		return false;
	}

	_ASSERT(!is_string_empty(full_copy_target_path));

	bool copy_success = false;

	#if defined(DEBUG) && defined(EXPORT_EMPTY_FILES)
		copy_success = create_empty_file(full_copy_target_path);
		full_file_path;
	#else
		copy_success = CopyFile(full_file_path, full_copy_target_path, TRUE) != 0;
	#endif

	u32 num_naming_collisions = 0;
	while(!copy_success && GetLastError() == ERROR_FILE_EXISTS)
	{
		++num_naming_collisions;
		TCHAR full_unique_copy_target_path[MAX_PATH_CHARS] = TEXT("");
		StringCchPrintf(full_unique_copy_target_path, MAX_PATH_CHARS, TEXT("%s.%I32u"), full_copy_target_path, num_naming_collisions);
		
		#if defined(DEBUG) && defined(EXPORT_EMPTY_FILES)
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

void safe_close_handle(HANDLE* handle)
{
	if(*handle != INVALID_HANDLE_VALUE && *handle != NULL)
	{
		CloseHandle(*handle);
		*handle = INVALID_HANDLE_VALUE;
	}
	else
	{
		_ASSERT(false);
	}
}

void* memory_map_entire_file(HANDLE file_handle, u64* file_size_result)
{
	void* mapped_memory = NULL;
	*file_size_result = 0;

	if(file_handle != INVALID_HANDLE_VALUE && file_handle != NULL)
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
						#ifndef BUILD_9X
							DEBUG_MEMORY_MAPPING_BASE_ADDRESS = advance_bytes(DEBUG_MEMORY_MAPPING_BASE_ADDRESS, 0x01000000);
						#endif
					#else
						mapped_memory = MapViewOfFile(mapping_handle, FILE_MAP_READ, 0, 0, 0);
					#endif

					if(mapped_memory == NULL)
					{
						log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to map a view of the file '%p'.", GetLastError(), file_handle);
					}
				}
				else
				{
					log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to create the file mapping for '%p'.", GetLastError(), file_handle);
				}

				CloseHandle(mapping_handle); // @Note: According to MSDN, CloseHandle() and UnmapViewOfFile() can be called in any order.
			}
			else
			{
				log_print(LOG_WARNING, "Memory Mapping: Skipping file mapping for empty file '%p'.", file_handle);
			}

		}
		else
		{
			log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to get the file size for '%p'.", GetLastError(), file_handle);
		}

		// CloseHandle(file_handle); // @Note: "Files for which the last view has not yet been unmapped are held open with no sharing restrictions."
	}
	else
	{
		log_print(LOG_ERROR, "Memory Mapping: Error %lu while trying to get the file handle for '%p'.", GetLastError(), file_handle);
	}

	return mapped_memory;
}

void* memory_map_entire_file(TCHAR* file_path, HANDLE* result_file_handle, u64* file_size_result)
{
	HANDLE file_handle = CreateFile(file_path,
									GENERIC_READ,
									FILE_SHARE_READ, // @Note: MSDN recommends exclusive access (though not required).
									NULL,
									OPEN_EXISTING,
									0,
									NULL);

	*result_file_handle = file_handle;

	return memory_map_entire_file(file_handle, file_size_result);
}

bool copy_to_temporary_file(const TCHAR* file_source_path, const TCHAR* base_temporary_path,
							TCHAR* result_file_destination_path, HANDLE* result_handle)
{
	bool copy_success = false;
	bool get_handle_success = false;

	copy_success = GetTempFileName(base_temporary_path, TEXT("WCE"), 0, result_file_destination_path) != 0
					&& CopyFile(file_source_path, result_file_destination_path, FALSE) == TRUE;

	if(copy_success)
	{
		HANDLE file_handle = CreateFile(result_file_destination_path,
										GENERIC_READ,
										0,
										NULL,
										OPEN_EXISTING,
										FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
										NULL);

		*result_handle = file_handle;
		get_handle_success = file_handle != INVALID_HANDLE_VALUE;
		if(!get_handle_success) DeleteFile(result_file_destination_path);
	}

	return copy_success && get_handle_success;

	/*bool success = false;
	TCHAR temporary_path[MAX_TEMPORARY_PATH_CHARS];

	if(GetTempPath(MAX_TEMPORARY_PATH_CHARS, temporary_path) != 0)
	{
		if(GetTempFileName(temporary_path, TEXT("WCE"), 0, full_temporary_file_path_result) != 0)
		{
			if(CopyFile(file_source_path, full_temporary_file_path_result, FALSE) == TRUE)
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

	return success;*/
}

bool create_temporary_directory(const TCHAR* base_temporary_path, TCHAR* result_directory_path, HANDLE* result_handle)
{	
	bool create_success = false;
	bool get_handle_success = false;

	u32 unique_id = 1;
	do
	{
		create_success = GetTempFileName(base_temporary_path, TEXT("WCE"), unique_id, result_directory_path) != 0
						&& CreateDirectory(result_directory_path, NULL) == TRUE;
		++unique_id;
	} while(!create_success && GetLastError() == ERROR_ALREADY_EXISTS);

	if(create_success)
	{
		HANDLE directory_handle = CreateFile(result_directory_path,
										     GENERIC_READ,
										     0,
										     NULL,
										     OPEN_EXISTING,
										     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_DELETE_ON_CLOSE,
										     NULL);

		*result_handle = directory_handle;
		get_handle_success = directory_handle != INVALID_HANDLE_VALUE;
		if(!get_handle_success) RemoveDirectory(result_directory_path);
	}

	return create_success && get_handle_success;
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

void close_csv_file(HANDLE* csv_file)
{
	safe_close_handle(csv_file);
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
