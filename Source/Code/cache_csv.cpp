#include "cache.h"
#include "common.h"

static const TCHAR* CSV_COLUMNS[] =
{
	T("Filename"),
	T("Extension"),

	T("URL"),
	T("Origin"),

	T("Last Modified Time"),
	T("Creation Time"),
	T("Last Write Time"),
	T("Last Access Time"),
	T("Expiry Time"),

	T("Access Count"),

	T("Response"),
	T("Server"),
	T("Cache Control"),
	T("Pragma"),
	T("Content Type"),
	T("Content Length"),
	T("Content Range"),
	T("Content Encoding"),

	T("Browser"),
	T("Profile"),
	T("Version"),

	T("Found"),
	T("Input Path"),
	T("Input Size"),

	T("Decompressed"),
	T("Exported"),
	T("Output Path"),
	T("Output Size"),

	T("Major File Label"),
	T("Minor File Label"),
	T("Major URL Label"),
	T("Minor URL Label"),
	T("Major Origin Label"),
	T("Minor Origin Label"),

	T("SHA-256"),

	// Report
	T("Format"),
	T("Mode"),
	T("Excluded"),

	// Shockwave
	T("Director Format"),
	T("Xtra Description"),
	T("Xtra Version"),
	T("Xtra Copyright"),
};

_STATIC_ASSERT(_countof(CSV_COLUMNS) == NUM_CSV_COLUMNS);

bool csv_begin(Csv* csv, String* path, Array_View<Csv_Column> columns)
{
	csv->path = path;
	csv->columns = columns;

	// Note that we always delete the previous output in main before running.
	csv->_writer.append = true;
	csv->_writer.create_parents = true;
	csv->_add_header = !path_is_file(path);

	csv->created = file_write_begin(&csv->_writer, path);
	if(!csv->created) log_error("Failed to create '%s'", path->data);

	return csv->created;
}

void csv_end(Csv* csv)
{
	file_write_end(&csv->_writer);
}

static String* csv_escape(String* str)
{
	String* result = str;

	bool escape = false;

	{
		String_View chr = {};
		while(string_next_char(str, &chr))
		{
			if(string_is_equal(chr, T(",")) || string_is_equal(chr, T("\n")) || string_is_equal(chr, T("\"")))
			{
				escape = true;
				break;
			}
		}
	}

	if(escape)
	{
		String_Builder* builder = builder_create(str->code_count + 2);
		builder_append(&builder, T("\""));

		String_View chr = {};
		while(string_next_char(str, &chr))
		{
			builder_append(&builder, chr);
			if(string_is_equal(chr, T("\""))) builder_append(&builder, chr);
		}

		builder_append(&builder, T("\""));
		result = builder_terminate(&builder);
	}

	return result;
}

void csv_next(Csv* csv, Map<Csv_Column, String*>* row)
{
	ARENA_SAVEPOINT()
	{
		String_Builder* builder = builder_create(row->count * 50);

		if(csv->_add_header)
		{
			csv->_add_header = false;

			for(int i = 0; i < csv->columns.count; i += 1)
			{
				Csv_Column column = csv->columns.data[i];
				builder_append(&builder, CSV_COLUMNS[column]);

				if(i != csv->columns.count - 1)
				{
					builder_append(&builder, T(","));
				}
			}

			builder_append(&builder, NEW_LINE);

			size_t utf_8_row_size = 0;
			const char* utf_8_row = string_to_utf_8(builder->data, &utf_8_row_size);

			if(!file_write_next(&csv->_writer, utf_8_row, utf_8_row_size))
			{
				log_error("Failed to write the header to '%s'", csv->path->data);
			}
		}

		builder_clear(builder);

		for(int i = 0; i < csv->columns.count; i += 1)
		{
			Csv_Column column = csv->columns.data[i];
			String* value = NULL;

			if(map_get(row, column, &value) && value != NULL)
			{
				value = csv_escape(value);
				builder_append(&builder, value);
			}

			if(i != csv->columns.count - 1)
			{
				builder_append(&builder, T(","));
			}
		}

		builder_append(&builder, NEW_LINE);

		size_t utf_8_row_size = 0;
		const char* utf_8_row = string_to_utf_8(builder->data, &utf_8_row_size);

		if(!file_write_next(&csv->_writer, utf_8_row, utf_8_row_size))
		{
			log_error("Failed to write a row to '%s'", csv->path->data);
		}
	}
}

void csv_tests(void)
{
	console_info("Running CSV tests");
	log_info("Running CSV tests");

	{
		TEST(csv_escape(CSTR("abc")), T("abc"));
		TEST(csv_escape(CSTR("abc,def")), T("\"abc,def\""));
		TEST(csv_escape(CSTR("abc\ndef")), T("\"abc\ndef\""));
		TEST(csv_escape(CSTR("abc\"def")), T("\"abc\"\"def\""));
		TEST(csv_escape(CSTR("")), T(""));
	}
}