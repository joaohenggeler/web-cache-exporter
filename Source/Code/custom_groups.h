#ifndef CUSTOM_GROUPS_H
#define CUSTOM_GROUPS_H

// The types of the various groups.
// See: load_group_file().
enum Group_Type
{
	GROUP_NONE = 0,
	GROUP_INVALID = 1,

	GROUP_FILE = 2,
	GROUP_URL = 3,

	NUM_GROUP_TYPES = 4
};

// The types of the various lists in a file or URL group.
// See: load_group_file().
enum List_Type
{
	LIST_NONE = 0,
	LIST_INVALID = 1,

	LIST_FILE_SIGNATURES = 2,
	LIST_MIME_TYPES = 3,
	LIST_FILE_EXTENSIONS = 4,

	LIST_DOMAINS = 5,

	NUM_LIST_TYPES = 6
};

// Two arrays that map the previous values to full names.
const TCHAR* const GROUP_TYPE_TO_STRING[] = {T(""), T("Invalid"), T("File"), T("URL")};
const TCHAR* const LIST_TYPE_TO_STRING[] =
{
	T(""), T("Invalid"),
	T("File Signatures"), T("MIME Types"), T("File Extensions"),
	T("Domains")
};

_STATIC_ASSERT(_countof(GROUP_TYPE_TO_STRING) == NUM_GROUP_TYPES);
_STATIC_ASSERT(_countof(LIST_TYPE_TO_STRING) == NUM_LIST_TYPES);

// A structure that represents a file signature. Wildcards may be used to match any byte when comparing file signatures.
struct File_Signature
{
	int num_bytes;
	u8* bytes;
	bool* is_wildcard;
};

// A structure that represents a domain. We only compare the host and path components of two URLs.
struct Domain
{
	bool match_any_top_or_second_level_domain;
	TCHAR* host;
	TCHAR* path;
};

// A structure that represents a file or URL group. Each group type contains arrays with different information that is used to
// match cached files to a given group name.
// See: load_group_file().
struct Group
{
	Group_Type type;
	TCHAR* name;
	bool enabled_for_filtering;

	union
	{
		struct
		{
			int num_file_signatures;
			File_Signature** file_signatures;

			int num_mime_types;
			TCHAR** mime_types;

			int num_file_extensions;
			TCHAR** file_extensions;

			TCHAR* default_file_extension;
		} file_info;

		struct
		{
			int num_domains;
			Domain** domains;
		} url_info;
	};
};

// A structure that contains every loaded group and a sufficiently large buffer for all file signatures.
// See: load_all_group_files().
struct Custom_Groups
{
	u8* file_signature_buffer;
	int file_signature_buffer_size;

	int num_groups;
	Group groups[ANYSIZE_ARRAY];
};

// A structure that is used to both pass parameters and receive the results of a cache entry match.
// See: match_cache_entry_to_groups().
struct Matchable_Cache_Entry
{
	// Input
	
	TCHAR* full_file_path;
	TCHAR* mime_type_to_match;
	TCHAR* file_extension_to_match;
	TCHAR* url_to_match;
	
	bool match_file_group;
	bool match_url_group;

	// Output

	TCHAR* matched_file_group_name;
	TCHAR* matched_url_group_name;
	const TCHAR* matched_default_file_extension;
	
	bool match_is_enabled_for_filtering;
};

size_t get_total_group_files_size(Exporter* exporter, int* num_groups);
void load_all_group_files(Exporter* exporter, int num_groups);
bool match_cache_entry_to_groups(Exporter* exporter, Matchable_Cache_Entry* entry_to_match);

#endif
