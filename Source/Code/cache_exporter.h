#ifndef CACHE_EXPORTER_H
#define CACHE_EXPORTER_H

#include "cache_csv.h"
#include "cache_label.h"
#include "common_core.h"
#include "common_string.h"
#include "common_array.h"
#include "common_map.h"
#include "common_path.h"

enum
{
	CACHE_WALK = 1 << 0,
	CACHE_WININET = 1 << 1,
	CACHE_MOZILLA = 1 << 2,
	CACHE_FLASH = 1 << 3,
	CACHE_SHOCKWAVE = 1 << 4,
	CACHE_JAVA = 1 << 5,
	CACHE_UNITY = 1 << 6,

	CACHE_COUNT = 7,

	CACHE_BROWSERS = CACHE_WININET | CACHE_MOZILLA,
	CACHE_PLUGINS = CACHE_FLASH | CACHE_SHOCKWAVE | CACHE_JAVA | CACHE_UNITY,
	CACHE_ALL = CACHE_BROWSERS | CACHE_PLUGINS,
};

struct Single_Path
{
	u32 flags;
	String* path;
};

struct Key_Paths
{
	String* name;

	String* drive;
	String* windows;
	String* temporary;
	String* user;
	String* appdata;
	String* local_appdata;
	String* local_low_appdata;
	String* wininet;
};

struct Exporter
{
	u32 cache_flags;

	String* input_path;
	String* batch_path;
	String* output_path;
	String* temporary_directory;

	Array<String*>* positive_filter;
	Array<String*>* negative_filter;
	u32 ignore_filter;

	bool copy_files;
	bool create_csvs;
	bool auto_confirm;
	bool run_tests;

	Array<Single_Path>* single_paths;
	Array<Key_Paths>* key_paths;

	Array<Label>* labels;
	size_t max_signature_size;

	String_Builder* builder;

	u32 current_flag;
	String* current_short;
	String* current_long;
	String* current_output;
	bool current_batch;
	String* current_profile;

	Csv current_csv;
	Csv report_csv;

	int current_found;
	int current_exported;
	int current_excluded;

	int total_found;
	int total_exported;
	int total_excluded;

	int filename_count;
};

struct Export_Params
{
	String* data_path;
	String* url;
	Walk_Info* info;

	String* subdirectory;
	Map<Csv_Column, String*>* row;
};

bool cache_flags_from_names(const TCHAR* ids, u32* flags);
Key_Paths default_key_paths(void);

void exporter_next(Exporter* exporter, Export_Params params);
void exporter_main(Exporter* exporter);

void exporter_tests(void);

#endif