#include "common.h"

HANDLE handle_create(const TCHAR* path, u32 desired_access, u32 share_mode, u32 creation_disposition, u32 flags_and_attributes)
{
	// In Windows 9x:
	// - The FILE_SHARE_DELETE and FILE_FLAG_BACKUP_SEMANTICS flags are not supported.
	// - Directories, physical disks, and volumes can't be opened using CreateFile.
	// - The hTemplateFile parameter must be NULL.

	// We'll check the version at runtime since that allows us to run the
	// 9x build in newer Windows versions without removing any parameters.
	if(windows_is_9x())
	{
		share_mode &= ~FILE_SHARE_DELETE;
		flags_and_attributes &= ~FILE_FLAG_BACKUP_SEMANTICS;
	}

	return CreateFile(path, desired_access, share_mode, NULL, creation_disposition, flags_and_attributes, NULL);
}

HANDLE handle_create(String* path, u32 desired_access, u32 share_mode, u32 creation_disposition, u32 flags_and_attributes)
{
	return handle_create(path->data, desired_access, share_mode, creation_disposition, flags_and_attributes);
}

void handle_close(HANDLE* handle)
{
	if(*handle != INVALID_HANDLE_VALUE && *handle != NULL) CloseHandle(*handle);
	*handle = INVALID_HANDLE_VALUE;
}

HANDLE metadata_handle_create(String* path)
{
	// In Windows 9x: sharing everything is required when querying metadata on already open files (e.g. temporary files).
	return handle_create(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0);
}

HANDLE directory_metadata_handle_create(String* path)
{
	// See above. Even though this won't work in Windows 9x, we'll leave it for older versions of NT like Windows 2000.
	return handle_create(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
}

bool handle_refers_to_same_object(HANDLE a, HANDLE b)
{
	bool result = false;

	BY_HANDLE_FILE_INFORMATION a_info = {};
	BY_HANDLE_FILE_INFORMATION b_info = {};

	if(GetFileInformationByHandle(a, &a_info) != FALSE && GetFileInformationByHandle(b, &b_info) != FALSE)
	{
		result = a_info.dwVolumeSerialNumber == b_info.dwVolumeSerialNumber
			  && a_info.nFileIndexLow == b_info.nFileIndexLow
			  && a_info.nFileIndexHigh == b_info.nFileIndexHigh;
	}

	return result;
}

bool file_size_get(HANDLE handle, u64* size)
{
	bool success = false;

	#ifdef WCE_9X
		DWORD high = 0;
		DWORD low = GetFileSize(handle, &high);
		success = !( (low == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR) );

		if(success) *size = u32s_to_u64(low, high);
		else log_error("Failed with the error: %s", last_error_message());
	#else
		LARGE_INTEGER _size = {};
		success = (GetFileSizeEx(handle, &_size) != FALSE);

		if(success) *size = _size.QuadPart;
		else log_error("Failed with the error: %s", last_error_message());
	#endif

	return success;
}

bool file_size_get(String* path, u64* size)
{
	HANDLE handle = metadata_handle_create(path);
	bool success = file_size_get(handle, size);
	handle_close(&handle);
	return success;
}

const bool TEMPORARY = true;

bool file_read_begin(File_Reader* reader, String* path)
{
	reader->path = path;

	u32 share_mode = (reader->temporary) ? (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) : (FILE_SHARE_READ);
	reader->_handle = handle_create(path, GENERIC_READ, share_mode, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
	reader->opened = (reader->_handle != INVALID_HANDLE_VALUE);

	if(reader->opened)
	{
		if(reader->capacity == 0) reader->capacity = arena_file_buffer_size(context.current_arena, reader->_handle);
		reader->capacity = MAX(reader->capacity, reader->min_capacity);

		if(reader->data == NULL) reader->data = arena_push(context.current_arena, reader->capacity, char);

		reader->size = 0;
		reader->eof = false;

		reader->_saved_size = arena_save(context.current_arena);
	}
	else
	{
		log_error("Failed to open '%s' for reading with the error: %s", path->data, last_error_message());
	}

	#ifdef WCE_DEBUG
		if(reader->opened) context.debug_file_read_balance += 1;
	#endif

	return reader->opened;
}

void file_read_end(File_Reader* reader)
{
	handle_close(&reader->_handle);
	reader->data = NULL;
	arena_restore(context.current_arena, reader->_saved_size);

	#ifdef WCE_DEBUG
		context.debug_file_read_balance -= 1;
	#endif
}

bool file_read_next(File_Reader* reader)
{
	if(reader->_handle == INVALID_HANDLE_VALUE) return false;

	bool success = false;

	reader->size = 0;
	u32 max_read_size = u32_clamp(reader->capacity);

	size_t read_count = CEIL_DIV(reader->capacity, max_read_size);
	ASSERT(read_count >= 1, "Read count is zero");

	for(size_t i = 0; i < read_count; i += 1)
	{
		void* buffer = advance(reader->data, reader->size);
		u32 buffer_size = (u32) MIN(reader->capacity - reader->size, max_read_size);

		u32 low = 0;
		u32 high = 0;
		u64_to_u32s(reader->offset, &low, &high);

		DWORD bytes_read = 0;

		if(windows_is_9x())
		{
			// In Windows 9x: using overlapped IO results in ERROR_INVALID_PARAMETER.
			LONG long_high = high;
			low = SetFilePointer(reader->_handle, low, &long_high, FILE_BEGIN);
			success = !( (low == 0xFFFFFFFF) && (GetLastError() != NO_ERROR) );

			// Since Windows 7: lpNumberOfBytesRead cannot be NULL.
			success = success && ReadFile(reader->_handle, buffer, buffer_size, &bytes_read, NULL) != FALSE;

			// When lpOverlapped is NULL, reaching the EOF returns true and zero bytes read.
			// We'll change this so it's consistent with the Windows NT branch.
			if(success && bytes_read == 0)
			{
				success = false;
				SetLastError(ERROR_HANDLE_EOF);
			}
		}
		else
		{
			OVERLAPPED overlapped = {};
			overlapped.Offset = low;
			overlapped.OffsetHigh = high;

			// Since Windows 7: lpNumberOfBytesRead cannot be NULL.
			success = ReadFile(reader->_handle, buffer, buffer_size, &bytes_read, &overlapped) != FALSE;
		}

		if(success)
		{
			reader->offset += bytes_read;
			reader->size += bytes_read;
			ASSERT(reader->size <= reader->capacity, "Read more than expected");
		}
		else
		{
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				// Note that, even though we may read zero bytes, we
				// could have read something in a previous iteration.
				success = (reader->size > 0);
				reader->eof = true;
				handle_close(&reader->_handle);
			}
			else
			{
				log_error("Failed to read %I32u bytes at %I64u from '%s' with the error: %s", buffer_size, reader->offset, reader->path->data, last_error_message());
			}

			break;
		}
	}

	return success;
}

bool file_read_all(String* path, File* file, bool temporary)
{
	bool success = false;

	u64 true_size = 0;
	if(file_size_get(path, &true_size))
	{
		// Include a null terminator to make it easier to read UTF-8 text.
		file->size = size_clamp(true_size);
		file->data = arena_push(context.current_arena, file->size + 1, char);

		// @OutOfMemory
		if(file->data != NULL)
		{
			ARENA_SAVEPOINT()
			{
				File_Reader reader = {};
				reader.temporary = temporary;
				FILE_READ_DEFER(&reader, path)
				{
					size_t total = 0;

					while(file_read_next(&reader))
					{
						// Don't read more than expected.
						if(total + reader.size > file->size) break;
						void* buffer = advance(file->data, total);
						CopyMemory(buffer, reader.data, reader.size);
						total += reader.size;
					}

					char* end = (char*) advance(file->data, total);
					*end = '\0';

					success = (total == file->size);
					ASSERT(success == reader.eof, "Mismatched reader status");
					if(!success) log_error("Failed to read '%s'", path->data);
				}
			}
		}
		else
		{
			log_error("Failed to allocate %Iu bytes to read '%s'", file->size, path->data);
		}
	}
	else
	{
		log_error("Failed to get the size of '%s'", path->data);
	}

	return success;
}

bool file_read_chunk(String* path, void* buffer, size_t size, u64 offset, bool temporary)
{
	bool success = false;

	File_Reader reader = {};
	reader.temporary = temporary;
	reader.offset = offset;
	reader.capacity = size;
	reader.data = buffer;

	FILE_READ_DEFER(&reader, path)
	{
		success = file_read_next(&reader) && reader.size == size;
		if(!success) log_error("Failed to read %Iu bytes from '%s'", size, path->data);
	}

	return success;
}

bool file_read_first_chunk(String* path, void* buffer, size_t size, bool temporary)
{
	return file_read_chunk(path, buffer, size, 0ULL, temporary);
}

bool file_read_at_most(String* path, void* buffer, size_t size, u64 offset, size_t* bytes_read, bool temporary)
{
	bool success = false;

	File_Reader reader = {};
	reader.temporary = temporary;
	reader.offset = offset;
	reader.capacity = size;
	reader.data = buffer;

	FILE_READ_DEFER(&reader, path)
	{
		// Reading an empty file shouldn't be an error.
		success = file_read_next(&reader) || reader.eof;
		if(!success) log_error("Failed to read %Iu bytes from '%s'", size, path->data);
		*bytes_read = reader.size;
	}

	return success;
}

bool file_read_first_at_most(String* path, void* buffer, size_t size, size_t* bytes_read, bool temporary)
{
	return file_read_at_most(path, buffer, size, 0ULL, bytes_read, temporary);
}

bool file_write_begin(File_Writer* writer, String* path)
{
	writer->path = path;

	if(writer->create_parents) directory_create(path, PARENTS_ONLY);

	u32 creation_disposition = (writer->append) ? (OPEN_ALWAYS) : (CREATE_ALWAYS);
	u32 flags_and_attributes = (writer->temporary) ? (FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY) : (FILE_ATTRIBUTE_NORMAL);

	writer->_handle = handle_create(path, GENERIC_WRITE, FILE_SHARE_READ, creation_disposition, flags_and_attributes);
	writer->opened = (writer->_handle != INVALID_HANDLE_VALUE);

	if(writer->opened)
	{
		if(writer->append)
		{
			// Normally we could just replace GENERIC_WRITE with FILE_APPEND_DATA, but that doesn't seem to work in Windows 9x
			// since it causes CreateFile to fail with ERROR_INVALID_PARAMETER. Instead of using that parameter, we'll set the
			// file pointer to the end of the file so we don't overwrite the previous data.
			writer->opened = SetFilePointer(writer->_handle, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER;
			if(!writer->opened) log_error("Failed to move the file pointer to the end of '%s' with the error: %s", path->data, last_error_message());
		}
	}
	else
	{
		log_error("Failed to open '%s' for writing with the error: %s", path->data, last_error_message());
	}

	#ifdef WCE_DEBUG
		if(writer->opened) context.debug_file_write_balance += 1;
	#endif

	return writer->opened;
}

void file_write_end(File_Writer* writer)
{
	handle_close(&writer->_handle);

	#ifdef WCE_DEBUG
		context.debug_file_write_balance -= 1;
	#endif
}

bool file_write_next(File_Writer* writer, const void* data, size_t size)
{
	if(writer->_handle == INVALID_HANDLE_VALUE) return false;
	if(size == 0) return true;

	bool success = false;

	size_t total_written = 0;
	u32 max_write_size = u32_clamp(size);

	size_t write_count = CEIL_DIV(size, max_write_size);
	ASSERT(write_count >= 1, "Write count is zero");

	for(size_t i = 0; i < write_count; i += 1)
	{
		const void* buffer = advance(data, total_written);
		u32 buffer_size = (u32) MIN(size - total_written, max_write_size);

		// Since Windows 7: lpNumberOfBytesWritten cannot be NULL.
		DWORD bytes_written = 0;
		success =  (WriteFile(writer->_handle, buffer, buffer_size, &bytes_written, NULL) != FALSE)
				&& (bytes_written == buffer_size);

		if(success)
		{
			total_written += bytes_written;
			ASSERT(total_written <= size, "Wrote more than expected");
		}
		else
		{
			log_error("Failed to write %I32u bytes to '%s' with the error: %s", buffer_size, writer->path->data, last_error_message());
			break;
		}
	}

	return success;
}

bool file_write_all(String* path, const void* data, size_t size)
{
	bool success = false;

	File_Writer writer = {};
	FILE_WRITE_DEFER(&writer, path)
	{
		success = file_write_next(&writer, data, size);
		if(!success) log_error("Failed to write %Iu bytes to '%s'", size, path->data);
	}

	return success;
}

bool file_write_truncate(File_Writer* writer, u64 size)
{
	u32 low = 0;
	u32 high = 0;
	u64_to_u32s(size, &low, &high);
	LONG long_high = high;
	return (SetFilePointer(writer->_handle, low, &long_high, FILE_BEGIN) != INVALID_SET_FILE_POINTER) && (SetEndOfFile(writer->_handle) != FALSE);
}

bool temporary_file_begin(File_Writer* writer)
{
	if(!context.has_temporary) return false;

	writer->temporary = true;

	String_Builder* builder = builder_create(MAX_PATH_COUNT);

	do
	{
		builder_clear(builder);
		builder_append_path(&builder, context.temporary_path);
		builder_append_path(&builder, T("WCE~"));
		builder_append_format(&builder, T("%04X"), u16_truncate(GetTickCount()));
		writer->_handle = handle_create(builder->data, GENERIC_WRITE, FILE_SHARE_READ, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY);
		writer->opened = (writer->_handle != INVALID_HANDLE_VALUE);
	} while(!writer->opened && GetLastError() == ERROR_FILE_EXISTS);

	if(writer->opened) writer->path = builder_terminate(&builder);
	else log_error("Failed to open '%s' for temporary writing with the error: %s", builder->data, last_error_message());

	#ifdef WCE_DEBUG
		if(writer->opened) context.debug_file_temporary_balance += 1;
	#endif

	return writer->opened;
}

void temporary_file_end(File_Writer* writer)
{
	handle_close(&writer->_handle);

	#ifdef WCE_DEBUG
		context.debug_file_temporary_balance -= 1;
	#endif
}

bool file_map_begin(File_Mapping* file, String* path)
{
	file->path = path;

	u32 share_mode = (file->temporary) ? (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) : (FILE_SHARE_READ);
	file->_handle = handle_create(path, GENERIC_READ, share_mode, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);

	if(file->_handle != INVALID_HANDLE_VALUE)
	{
		u64 true_size = 0;
		if(file_size_get(file->_handle, &true_size))
		{
			// CreateFileMapping requires a non-empty file.
			if(true_size > 0)
			{
				file->size = size_clamp(true_size);
				HANDLE mapping_handle = CreateFileMapping(file->_handle, NULL, PAGE_READONLY, 0, 0, NULL);

				if(mapping_handle != NULL)
				{
					file->data = MapViewOfFile(mapping_handle, FILE_MAP_READ, 0, 0, 0);

					if(file->data != NULL)
					{
						file->opened = true;
						ASSERT(POINTER_IS_ALIGNED_TO_SIZE(file->data, context.page_size), "Misaligned mapping");
					}
					else
					{
						log_error("Failed to map a view of '%s' with the error: %s", path->data, last_error_message());
					}

					handle_close(&mapping_handle);
				}
				else
				{
					log_error("Failed to create the mapping of '%s' with the error: %s", path->data, last_error_message());
				}
			}
			else
			{
				log_warning("Cannot create the mapping since '%s' is empty", path->data);
			}
		}
		else
		{
			log_error("Failed to get the size of '%s'", path->data);
		}
	}
	else
	{
		log_error("Failed to create the handle of '%s' with the error: %s", path->data, last_error_message());
	}

	#ifdef WCE_DEBUG
		if(file->opened) context.debug_file_map_balance += 1;
	#endif

	return file->opened;
}

void file_map_end(File_Mapping* file)
{
	handle_close(&file->_handle);
	if(file->data != NULL) UnmapViewOfFile(file->data);
	file->data = NULL;

	#ifdef WCE_DEBUG
		context.debug_file_map_balance -= 1;
	#endif
}

bool file_is_empty(String* path)
{
	u64 size = 0;
	return file_size_get(path, &size) && size == 0;
}

bool file_empty_create(String* path)
{
	// @GetLastError: see exporter_copy.
	HANDLE handle = handle_create(path, 0, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL);
	u32 error = GetLastError();
	bool success = (handle != INVALID_HANDLE_VALUE);
	handle_close(&handle);
	if(!success) SetLastError(error);
	return success;
}

bool file_copy_try(String* from_path, String* to_path)
{
	// @GetLastError: see exporter_copy.
	return CopyFile(from_path->data, to_path->data, TRUE) != FALSE;
}

bool file_delete(String* path)
{
	bool success = DeleteFile(path->data) != FALSE;
	if(!success) log_error("Failed to delete '%s' with the error: %s", path->data, last_error_message());
	return success;
}

const bool PARENTS_ONLY = true;

bool directory_create(String* path, bool parents_only)
{
	bool success = false;

	ARENA_SAVEPOINT()
	{
		String_View parent = path_parent(path);

		Split_State state = {};
		state.delimiters = PATH_DELIMITERS;

		if(parents_only) state.view = parent;
		else state.str = path;

		String_View component = {};
		String_Builder* builder = builder_create(path->code_count);

		while(string_split(&state, &component))
		{
			builder_append_path(&builder, component);

			if(CreateDirectory(builder->data, NULL) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
			{
				if(parents_only) log_error("Failed to create '%s' of '%.*s' with the error: %s", builder->data, parent.code_count, parent.data, last_error_message());
				else log_error("Failed to create '%s' of '%s' with the error: %s", builder->data, path->data, last_error_message());
				break;
			}
		}

		if(parents_only) success = path_is_directory(string_from_view(parent));
		else success = path_is_directory(path);
	}

	return success;
}

bool directory_delete(String* path)
{
	bool success = false;

	ARENA_SAVEPOINT()
	{
		// SHFileOperation requires an absolute path.
		path = path_absolute(path);

		// SHFileOperation requires two null terminators.
		// This is already guaranteed by the builder since
		// we're asking for an extra element.
		String_Builder* builder = builder_create(path->code_count + 1);
		builder_append(&builder, path);

		SHFILEOPSTRUCT operation = {};
		operation.wFunc = FO_DELETE;
		operation.pFrom = builder->data;
		operation.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;

		int error = SHFileOperation(&operation);
		success = (error == 0);
		if(!success) log_error("Failed to delete '%s' with the error %d", path->data, error);
	}

	return success;
}

File_Info file_info_get(String* path)
{
	// Missing info values are NULL if they don't exist in the executable.
	File_Info info = {};

	DWORD handle = 0;
	u32 info_size = GetFileVersionInfoSize(path->data, &handle);

	if(info_size > 0)
	{
		void* info_block = arena_push(context.current_arena, info_size, char);

		if(GetFileVersionInfo(path->data, handle, info_size, info_block) != FALSE)
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
				int language_count = (int) (queried_info_size / sizeof(Language_Code_Page_Info));

				if(language_count > 0)
				{
					if(language_count > 1)
					{
						log_warning("Ignoring %d languages in '%s'", language_count - 1, path->data);
					}

					u16 language = language_code_page_info[0].wLanguage;
					u16 code_page = language_code_page_info[0].wCodePage;

					const TCHAR* info_keys[] =
					{
						T("Comments"),
						T("CompanyName"),
						T("FileDescription"),
						T("FileVersion"),
						T("InternalName"),
						T("LegalCopyright"),
						T("LegalTrademarks"),
						T("OriginalFilename"),
						T("PrivateBuild"),
						T("ProductName"),
						T("ProductVersion"),
						T("SpecialBuild"),
					};

					String_Builder* builder = builder_create(50);

					for(int i = 0; i < _countof(info_keys); i += 1)
					{
						const TCHAR* key = info_keys[i];

						builder_clear(builder);
						builder_append_format(&builder, T("\\StringFileInfo\\%04x%04x\\%s"), language, code_page, key);

						TCHAR* value = NULL;
						UINT value_size = 0;

						if(VerQueryValue(info_block, builder->data, (LPVOID*) &value, &value_size) != FALSE)
						{
							if(value_size > 0)
							{
								#define IF_KEY(str, member) \
									if(string_is_equal(key, T(str))) \
									{ \
										info.member = string_from_c(value); \
									}

								IF_KEY("Comments", comments)
								else IF_KEY("CompanyName", company_name)
								else IF_KEY("FileDescription", file_description)
								else IF_KEY("FileVersion", file_version)
								else IF_KEY("InternalName", internal_name)
								else IF_KEY("LegalCopyright", legal_copyright)
								else IF_KEY("LegalTrademarks", legal_trademarks)
								else IF_KEY("OriginalFilename", original_filename)
								else IF_KEY("PrivateBuild", private_build)
								else IF_KEY("ProductName", product_name)
								else IF_KEY("ProductVersion", product_version)
								else IF_KEY("SpecialBuild", special_build)
								else ASSERT(false, "Unhandled info key");

								#undef IF_KEY
							}
						}
					}
				}
				else
				{
					log_warning("No translation info found in '%s'", path->data);
				}
			}
			else
			{
				log_error("Failed to query the translation info in '%s'", path->data);
			}
		}
		else
		{
			log_error("Failed to get the version info in '%s' with the error: %s", path->data, last_error_message());
		}
	}
	else if(GetLastError() != ERROR_RESOURCE_DATA_NOT_FOUND && GetLastError() != ERROR_RESOURCE_TYPE_NOT_FOUND)
	{
		log_error("Failed to get the version info size in '%s' with the error: %s", path->data, last_error_message());
	}

	return info;
}

void io_tests(void)
{
	console_info("Running IO tests");
	log_info("Running IO tests");

	String* file_path = CSTR("Tests\\IO\\file.txt");
	String* empty_path = CSTR("Tests\\IO\\empty.txt");

	u64 file_size = 0;
	{
		bool success = file_size_get(file_path, &file_size);
		TEST(success, true);
		TEST(file_size, 44ULL);
	}

	{
		File_Reader reader = {};
		FILE_READ_DEFER(&reader, file_path)
		{
			for(int i = 0; file_read_next(&reader); i += 1)
			{
				TEST(i, 0);
				TEST((u64) reader.size, file_size);
				TEST(memory_is_equal("The quick brown fox jumps over the lazy dog.", reader.data, reader.size), true);
			}

			TEST(reader.eof, true);
		}

		TEST(reader.opened, true);
	}

	{
		File file = {};
		bool success = file_read_all(file_path, &file);
		TEST(success, true);
		TEST((u64) file.size, file_size);
		TEST(memory_is_equal("The quick brown fox jumps over the lazy dog.", file.data, file.size), true);
	}

	{
		File file = {};
		bool success = file_read_all(empty_path, &file);
		TEST(success, true);
		TEST(file.size, (size_t) 0);
	}

	{
		char buffer[3] = "";
		bool success = file_read_chunk(file_path, buffer, sizeof(buffer), 16ULL);
		TEST(success, true);
		TEST(memory_is_equal("fox", buffer, sizeof(buffer)), true);
	}

	{
		char buffer[3] = "";
		bool success = file_read_first_chunk(file_path, buffer, sizeof(buffer));
		TEST(success, true);
		TEST(memory_is_equal("The", buffer, sizeof(buffer)), true);
	}

	{
		char buffer[999] = "";
		size_t bytes_read = 0;
		bool success = file_read_at_most(file_path, buffer, sizeof(buffer), 16ULL, &bytes_read);
		TEST(success, true);
		TEST(bytes_read, (size_t) 28);
		TEST(memory_is_equal("fox jumps over the lazy dog.", buffer, bytes_read), true);
	}

	{
		char buffer[999] = "";
		size_t bytes_read = 0;
		bool success = file_read_first_at_most(file_path, buffer, sizeof(buffer), &bytes_read);
		TEST(success, true);
		TEST(bytes_read, (size_t) 44);
		TEST(memory_is_equal("The quick brown fox jumps over the lazy dog.", buffer, bytes_read), true);
	}

	{
		File_Mapping file = {};
		FILE_MAP_DEFER(&file, file_path)
		{
			TEST((u64) file.size, file_size);
			TEST(memory_is_equal("The quick brown fox jumps over the lazy dog.", file.data, file.size), true);
		}

		TEST(file.opened, true);
	}

	{
		File_Mapping file = {};
		FILE_MAP_DEFER(&file, empty_path)
		{
			TEST_UNREACHABLE();
		}

		TEST(file.opened, false);
	}

	{
		File_Writer writer = {};
		TEMPORARY_FILE_DEFER(&writer)
		{
			{
				bool success = false;

				File file = {};
				success = file_read_all(file_path, &file);
				TEST(success, true);

				success = file_write_next(&writer, file.data, file.size);
				TEST(success, true);
			}

			{
				File file = {};
				bool success = file_read_all(writer.path, &file, writer.temporary);
				TEST(success, true);
				TEST((u64) file.size, file_size);
				TEST(memory_is_equal("The quick brown fox jumps over the lazy dog.", file.data, file.size), true);
			}

			{
				char buffer[3] = "";
				bool success = file_read_chunk(writer.path, buffer, sizeof(buffer), 16ULL, writer.temporary);
				TEST(success, true);
				TEST(memory_is_equal("fox", buffer, sizeof(buffer)), true);
			}

			{
				char buffer[3] = "";
				bool success = file_read_first_chunk(writer.path, buffer, sizeof(buffer), writer.temporary);
				TEST(success, true);
				TEST(memory_is_equal("The", buffer, sizeof(buffer)), true);
			}

			{
				char buffer[999] = "";
				size_t bytes_read = 0;
				bool success = file_read_at_most(writer.path, buffer, sizeof(buffer), 16ULL, &bytes_read, writer.temporary);
				TEST(success, true);
				TEST(bytes_read, (size_t) 28);
				TEST(memory_is_equal("fox jumps over the lazy dog.", buffer, bytes_read), true);
			}

			{
				char buffer[999] = "";
				size_t bytes_read = 0;
				bool success = file_read_first_at_most(writer.path, buffer, sizeof(buffer), &bytes_read, writer.temporary);
				TEST(success, true);
				TEST(bytes_read, (size_t) 44);
				TEST(memory_is_equal("The quick brown fox jumps over the lazy dog.", buffer, bytes_read), true);
			}

			{
				File_Mapping file = {};
				file.temporary = true;
				FILE_MAP_DEFER(&file, writer.path)
				{
					TEST((u64) file.size, file_size);
					TEST(memory_is_equal("The quick brown fox jumps over the lazy dog.", file.data, file.size), true);
				}

				TEST(file.opened, true);
			}

			{
				bool success = false;

				success = file_write_truncate(&writer, 3);
				TEST(success, true);

				File file = {};
				success = file_read_all(writer.path, &file, writer.temporary);
				TEST(success, true);
				TEST(file.size, (size_t) 3);
				TEST(memory_is_equal("The", file.data, file.size), true);
			}
		}

		TEST(writer.opened, true);
		TEST(path_is_file(writer.path), false);
	}

	{
		File_Info info = file_info_get(CSTR("Tests\\IO\\hello_world.exe"));
		TEST(info.comments, T("Comments"));
		TEST(info.company_name, T("CompanyName"));
		TEST(info.file_description, T("FileDescription"));
		TEST(info.file_version, T("1.0.0.0"));
		TEST(info.internal_name, T("InternalName"));
		TEST((void*) info.legal_copyright, NULL);
		TEST((void*) info.legal_trademarks, NULL);
		TEST(info.original_filename, T("OriginalFilename"));
		TEST((void*) info.private_build, NULL);
		TEST(info.product_name, T("ProductName"));
		TEST(info.product_version, T("1.0.0.0"));
		TEST((void*) info.special_build, NULL);
	}
}