#ifndef CUSTOM_GROUPS_H
#define CUSTOM_GROUPS_H

enum Group_Type
{
	GROUP_NONE = 0,
	GROUP_FILE_GROUP = 1,
	GROUP_URL_GROUP = 2
};

enum List_Type
{
	LIST_NONE = 0,

	LIST_FILE_SIGNATURES = 1,
	LIST_MIME_TYPES = 2,
	LIST_FILE_EXTENSIONS = 3,

	LIST_DOMAINS = 4
};

struct Group
{
	Group_Type type;
	TCHAR* name;

	union
	{
		struct
		{
			u32 num_mime_types;
			TCHAR** mime_types;

			u32 num_file_extensions;
			TCHAR** file_extensions;
		} file_info;

		struct
		{
			u32 num_domains;
			TCHAR** domains;
		} url_info;
	};
};

struct Custom_Groups
{
	u32 num_groups;
	Group groups[ANYSIZE_ARRAY];
};

struct Matchable_Cache_Entry
{
	u32 num_file_signature_bytes;
	u8* file_signature_to_match;
	TCHAR* mime_type_to_match;
	TCHAR* file_extension_to_match;

	TCHAR* url_to_match;

	u32 matched_group_index;
};

size_t get_total_group_files_size(Exporter* exporter, u32* num_groups);
void load_all_group_files(Exporter* exporter, u32 num_groups);

#endif
