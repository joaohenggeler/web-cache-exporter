#ifndef COMMON_PATH_H
#define COMMON_PATH_H

#include "common_core.h"
#include "common_string.h"
#include "common_array.h"

extern const int MAX_PATH_COUNT;
extern String* NO_PATH;

bool path_is_file(String* path);
bool path_is_directory(String* path);
bool path_refers_to_same_object(String* a, String* b);
Array<String*>* path_unique_directories(Array<String*>* paths);

struct Path_Parts
{
	String_View parent;
	String_View name;
	String_View stem;
	String_View extension;
};

Path_Parts path_parse(String* path);
String_View path_parent(String* path);
String_View path_name(String* path);
String_View path_stem(String* path);
String_View path_extension(String* path);
bool path_has_extension(String* path, const TCHAR* extension);

String* path_absolute(String* path);
String* path_safe(String* path);

extern const GUID KFID_LOCAL_LOW_APPDATA;

bool path_from_csidl(int csidl, String** path);
bool path_from_kfid(const GUID & kfid, String** path);

struct Walk_Node
{
	String* path;
	int depth;
};

struct Walk_State
{
	String* base_path;
	const TCHAR* query;
	bool files;
	bool directories;
	int max_depth;
	bool copy;

	HANDLE _handle;
	String_Builder* _builder;
	Walk_Node _current;
	Array<Walk_Node>* _next_nodes;

	size_t _saved_size;
};

struct Walk_Info
{
	union
	{
		const TCHAR* iter_path;
		String* path;
	};

	u64 size;
	bool is_directory;
	int depth;

	FILETIME creation_time;
	FILETIME last_access_time;
	FILETIME last_write_time;

	Walk_State* _state;
};

extern const bool SORT_PATHS;

void walk_begin(Walk_State* state);
void walk_end(Walk_State* state);
bool walk_next(Walk_State* state, Walk_Info* info);
Array<Walk_Info>* walk_all(Walk_State* state, bool sort_paths = false);

#define WALK_DEFER(state) DEFER(walk_begin(state), walk_end(state))

void path_tests(void);

#endif