#ifndef CUSTOM_GROUPS_H
#define CUSTOM_GROUPS_H

enum Group_Type
{
	GROUP_NONE = 0,
	GROUP_FILE = 1,
	GROUP_URL = 2,
	NUM_GROUP_TYPES = 3
};

enum List_Type
{
	LIST_NONE = 0,

	LIST_FILE_SIGNATURES = 1,
	LIST_MIME_TYPES = 2,
	LIST_FILE_EXTENSIONS = 3,

	LIST_DOMAINS = 4,

	NUM_LIST_TYPES = 5
};

const TCHAR* const GROUP_TYPE_TO_STRING[NUM_GROUP_TYPES] = {TEXT(""), TEXT("File"), TEXT("URL")};
const TCHAR* const LIST_TYPE_TO_STRING[NUM_LIST_TYPES] =
{
	TEXT(""),
	TEXT("File Signatures"), TEXT("MIME Types"), TEXT("File Extensions"),
	TEXT("Domains")
};

struct File_Signature
{
	u32 num_bytes;
	u8* bytes;
	bool* is_wildcard;
};

struct Domain
{
	TCHAR* host;
	TCHAR* path;
};

struct Group
{
	Group_Type type;
	TCHAR* name;

	union
	{
		struct
		{
			u32 num_file_signatures;
			File_Signature** file_signatures;

			u32 num_mime_types;
			TCHAR** mime_types;

			u32 num_file_extensions;
			TCHAR** file_extensions;
		} file_info;

		struct
		{
			u32 num_domains;
			Domain** domains;
		} url_info;
	};
};

struct Custom_Groups
{
	u8* file_signature_buffer;
	u32 file_signature_buffer_size;

	u32 num_groups;
	Group groups[ANYSIZE_ARRAY];
};

struct Matchable_Cache_Entry
{
	TCHAR* full_file_path;
	TCHAR* mime_type_to_match;
	TCHAR* file_extension_to_match;
	TCHAR* url_to_match;

	bool should_match_file_group;
	bool should_match_url_group;

	TCHAR* matched_file_group_name;
	TCHAR* matched_url_group_name;
};

size_t get_total_group_files_size(Exporter* exporter, u32* num_groups);
void load_all_group_files(Exporter* exporter, u32 num_groups);
bool match_cache_entry_to_groups(Arena* temporary_arena, Custom_Groups* custom_groups, Matchable_Cache_Entry* entry_to_match);

#endif
