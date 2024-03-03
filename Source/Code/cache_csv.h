#ifndef CACHE_CSV_H
#define CACHE_CSV_H

#include "common_core.h"
#include "common_string.h"
#include "common_array.h"
#include "common_map.h"
#include "common_io.h"

enum Csv_Column
{
	CSV_FILENAME,
	CSV_EXTENSION,

	CSV_URL,
	CSV_ORIGIN,

	CSV_LAST_MODIFIED_TIME,
	CSV_CREATION_TIME,
	CSV_LAST_WRITE_TIME,
	CSV_LAST_ACCESS_TIME,
	CSV_EXPIRY_TIME,

	CSV_ACCESS_COUNT,

	CSV_RESPONSE,
	CSV_SERVER,
	CSV_CACHE_CONTROL,
	CSV_PRAGMA,
	CSV_CONTENT_TYPE,
	CSV_CONTENT_LENGTH,
	CSV_CONTENT_RANGE,
	CSV_CONTENT_ENCODING,

	CSV_BROWSER,
	CSV_PROFILE,
	CSV_VERSION,

	CSV_FOUND,
	CSV_INPUT_PATH,
	CSV_INPUT_SIZE,

	CSV_DECOMPRESSED,
	CSV_EXPORTED,
	CSV_OUTPUT_PATH,
	CSV_OUTPUT_SIZE,

	CSV_MAJOR_FILE_LABEL,
	CSV_MINOR_FILE_LABEL,
	CSV_MAJOR_URL_LABEL,
	CSV_MINOR_URL_LABEL,
	CSV_MAJOR_ORIGIN_LABEL,
	CSV_MINOR_ORIGIN_LABEL,

	CSV_SHA_256,

	// Report
	CSV_FORMAT,
	CSV_MODE,
	CSV_EXCLUDED,

	// Shockwave
	CSV_DIRECTOR_FORMAT,
	CSV_XTRA_DESCRIPTION,
	CSV_XTRA_VERSION,
	CSV_XTRA_COPYRIGHT,

	NUM_CSV_COLUMNS,
};

struct Csv
{
	String* path;
	Array_View<Csv_Column> columns;

	bool created;

	File_Writer _writer;
	bool _add_header;
};

bool csv_begin(Csv* csv, String* path, Array_View<Csv_Column> columns);
void csv_end(Csv* csv);
void csv_next(Csv* csv, Map<Csv_Column, String*>* row);

#define CSV_DEFER(csv, path, columns) DEFER_IF(csv_begin(csv, path, columns), csv_end(csv))

void csv_tests(void);

#endif