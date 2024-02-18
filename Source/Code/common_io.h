#ifndef COMMON_IO_H
#define COMMON_IO_H

#include "common_core.h"
#include "common_string.h"

HANDLE handle_create(const TCHAR* path, u32 desired_access, u32 share_mode, u32 creation_disposition, u32 flags_and_attributes);
HANDLE handle_create(String* path, u32 desired_access, u32 share_mode, u32 creation_disposition, u32 flags_and_attributes);
void handle_close(HANDLE* handle);

HANDLE metadata_handle_create(String* path);
HANDLE directory_metadata_handle_create(String* path);

bool handle_refers_to_same_object(HANDLE a, HANDLE b);

bool file_size_get(HANDLE handle, u64* size);
bool file_size_get(String* path, u64* size);

struct File_Reader
{
	bool temporary;
	u64 offset;
	size_t capacity;
	size_t min_capacity;
	void* data;

	String* path;
	bool opened;
	size_t size;
	bool eof;

	HANDLE _handle;
	size_t _saved_size;
};

struct File
{
	size_t size;
	void* data;
};

bool file_read_begin(File_Reader* reader, String* path);
void file_read_end(File_Reader* reader);
bool file_read_next(File_Reader* reader);
bool file_read_all(String* path, File* file, bool temporary = false);
bool file_read_first(String* path, void* buffer, size_t size, bool temporary = false);
bool file_read_at_most(String* path, void* buffer, size_t size, size_t* bytes_read, bool temporary = false);

#define FILE_READ_DEFER(reader, path) DEFER_IF(file_read_begin(reader, path), file_read_end(reader))

struct File_Writer
{
	bool temporary;
	bool append;
	bool create_parents;

	String* path;
	bool opened;

	HANDLE _handle;
};

bool file_write_begin(File_Writer* writer, String* path);
void file_write_end(File_Writer* writer);
bool file_write_next(File_Writer* writer, const void* data, size_t size);
bool file_write_all(String* path, const void* data, size_t size);

#define FILE_WRITE_DEFER(writer, path) DEFER_IF(file_write_begin(writer, path), file_write_end(writer))

bool temporary_file_begin(File_Writer* writer);
void temporary_file_end(File_Writer* writer);

#define TEMPORARY_FILE_DEFER(writer) DEFER_IF(temporary_file_begin(writer), temporary_file_end(writer))

struct File_Mapping
{
	bool temporary;

	size_t size;
	const void* data;

	String* path;
	bool opened;

	HANDLE _handle;
};

bool file_map_begin(File_Mapping* file, String* path);
void file_map_end(File_Mapping* file);

#define FILE_MAP_DEFER(file, path) DEFER_IF(file_map_begin(file, path), file_map_end(file))

bool file_is_empty(String* path);
bool empty_file_create(String* path);

bool file_copy_try(String* from_path, String* to_path);
bool file_delete(String* path);

extern const bool PARENTS_ONLY;

bool directory_create(String* path, bool parents_only = false);
bool directory_delete(String* path);

struct File_Info
{
	String* comments;
	String* company_name;
	String* file_description;
	String* file_version;
	String* internal_name;
	String* legal_copyright;
	String* legal_trademarks;
	String* original_filename;
	String* private_build;
	String* product_name;
	String* product_version;
	String* special_build;
};

File_Info file_info_get(String* path);

void io_tests(void);

#endif