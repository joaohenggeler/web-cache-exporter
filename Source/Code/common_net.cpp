#include "common.h"

const bool DECODE_PLUS = true;

String* url_decode(String_View component, bool decode_plus)
{
	String_Builder* builder = builder_create(component.code_count);

	const int MAX_UTF_8_BYTES = 4;
	Array<char>* decoder = array_create<char>(MAX_UTF_8_BYTES * 3 + 1);
	bool decoding = false;

	String_View chr = {};
	for(int i = 0; string_next_char(component, &chr); i += 1)
	{
		if(string_is_equal(chr, T("%")))
		{
			int byte_idx = i + 1;
			String_View slice = string_slice(component, byte_idx, byte_idx + 2);

			u32 bytes = 0;
			if(string_hex_to_num(slice, &bytes))
			{
				array_add(&decoder, (char) bytes);
				decoding = true;

				REPEAT(2)
				{
					string_next_char(component, &chr);
					i += 1;
				}
			}
			else
			{
				log_warning("Cannot decode '%.*s'", slice.code_count, slice.data);
				builder_append(&builder, chr);
			}
		}
		else
		{
			if(decoding)
			{
				ARENA_SAVEPOINT()
				{
					array_add(&decoder, '\0');
					String* str = string_from_utf_8(decoder->data);
					builder_append(&builder, str);
					array_clear(decoder);
				}

				decoding = false;
			}

			if(decode_plus && string_is_equal(chr, T("+")))
			{
				builder_append(&builder, T(" "));
			}
			else
			{
				builder_append(&builder, chr);
			}
		}
	}

	if(decoding)
	{
		ARENA_SAVEPOINT()
		{
			array_add(&decoder, '\0');
			String* str = string_from_utf_8(decoder->data);
			builder_append(&builder, str);
		}
	}

	return builder_terminate(&builder);
}

struct Url_View
{
	String_View scheme;

	String_View userinfo;
	String_View host;
	String_View port;

	String_View path;
	String_View query;
	String_View fragment;
};

static void url_path_split(String_View after_authority, Url_View* parts)
{
	Split_State state = {};
	state.view = after_authority;
	state.delimiters = T("?#");
	state.keep_empty = true;

	String_View remaining = {};
	String_View delimiter = {};

	if(string_partition(&state, &parts->path, &delimiter, &remaining))
	{
		if(string_is_equal(delimiter, T("?")))
		{
			Split_State fragment_state = {};
			fragment_state.view = remaining;
			fragment_state.delimiters = T("#");
			fragment_state.keep_empty = true;

			string_partition(&fragment_state, &parts->query, &parts->fragment);
		}
		else
		{
			parts->fragment = remaining;
		}
	}
}

static Url_View url_split(String* url)
{
	Url_View parts = {};

	Split_State state = {};
	state.str = url;
	state.delimiters = T(":");
	state.keep_empty = true;

	String_View scheme = {};
	String_View remaining = {};

	if(string_partition(&state, &scheme, &remaining))
	{
		parts.scheme = scheme;

		if(string_begins_with(remaining, T("//")))
		{
			REPEAT(2)
			{
				string_split_move_iter(&state);
			}

			state.delimiters = T("/?#");

			String_View authority = {};
			String_View delimiter = {};

			if(string_partition(&state, &authority, &delimiter, &remaining))
			{
				if(string_is_equal(delimiter, T("/")))
				{
					// Include the slash delimiter.
					remaining = view_extend(delimiter);
					url_path_split(remaining, &parts);
				}
				else if(string_is_equal(delimiter, T("#")))
				{
					parts.fragment = remaining;
				}
				else
				{
					Split_State fragment_state = {};
					fragment_state.view = remaining;
					fragment_state.delimiters = T("#");
					fragment_state.keep_empty = true;
					string_partition(&fragment_state, &parts.query, &parts.fragment);
				}
			}

			Split_State userinfo_state = {};
			userinfo_state.view = authority;
			userinfo_state.delimiters = T("@");
			userinfo_state.keep_empty = true;

			String_View userinfo = {};
			String_View host_and_port = {};

			if(string_partition(&userinfo_state, &userinfo, &host_and_port))
			{
				parts.userinfo = userinfo;
			}
			else
			{
				// No userinfo.
				host_and_port = authority;
			}

			Split_State port_state = {};
			port_state.view = host_and_port;
			port_state.delimiters = T(":");
			port_state.keep_empty = true;

			string_partition(&port_state, &parts.host, &parts.port);
		}
		else
		{
			url_path_split(remaining, &parts);
		}
	}
	else
	{
		// No scheme.
		parts.path = scheme;
	}

	return parts;
}

Url url_parse(String* url)
{
	Url_View view = url_split(url);

	Url parts = {};
	parts.full = url;
	parts.scheme = view.scheme;
	parts.userinfo = url_decode(view.userinfo);
	parts.host = url_decode(view.host);
	parts.port = url_decode(view.port);
	parts.path = url_decode(view.path);
	parts.query = url_decode(view.query, DECODE_PLUS);
	parts.fragment = url_decode(view.fragment);

	Map<const TCHAR*, String*>* map = map_create<const TCHAR*, String*>(8);

	Split_State query_state = {};
	query_state.view = view.query;
	query_state.delimiters = T("&");

	String_View item = {};
	while(string_split(&query_state, &item))
	{
		Split_State item_state = {};
		item_state.view = item;
		item_state.delimiters = T("=");

		String_View key = {};
		String_View value = {};
		string_partition(&item_state, &key, &value);

		const TCHAR* decoded_key = url_decode(key, DECODE_PLUS)->data;
		String* decoded_value = url_decode(value, DECODE_PLUS);
		map_put(&map, decoded_key, decoded_value);
	}

	parts.query_params = map;

	return parts;
}

Map<const TCHAR*, String_View>* http_headers_parse(String* headers)
{
	Map<const TCHAR*, String_View>* map = map_create<const TCHAR*, String_View>(32);

	Split_State line_state = {};
	line_state.str = headers;
	line_state.delimiters = LINE_DELIMITERS;

	String_View line = {};

	for(int i = 0; string_split(&line_state, &line); i += 1)
	{
		if(i == 0)
		{
			// E.g. "HTTP/1.1 200 OK"
			map_put(&map, T(""), line);
		}
		else
		{
			Split_State key_state = {};
			key_state.view = line;
			key_state.delimiters = T(":");

			String_View key = {};
			String_View value = {};
			if(string_partition(&key_state, &key, &value))
			{
				// E.g. "Content-Type: text/html"
				const TCHAR* lower_key = string_lower(key)->data;
				value = string_trim(value);
				map_put(&map, lower_key, value);
			}
		}
	}

	return map;
}

void net_tests(void)
{
	console_info("Running net tests");
	log_info("Running net tests");

	{
		String* decoded = EMPTY_STRING;

		decoded = url_decode(CVIEW("%7E %C3%A3 %E2%88%80 %F0%9F%87%A6"));
		TEST(decoded, CUTF8("~ \xC3\xA3 \xE2\x88\x80 \xF0\x9F\x87\xA6"));

		decoded = url_decode(CVIEW("scotland = %F0%9F%8F%B4%F3%A0%81%A7%F3%A0%81%A2%F3%A0%81%B3%F3%A0%81%A3%F3%A0%81%B4%F3%A0%81%BF"));
		TEST(decoded, CUTF8("scotland = \xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xB3\xF3\xA0\x81\xA3\xF3\xA0\x81\xB4\xF3\xA0\x81\xBF"));

		decoded = url_decode(CVIEW("foo+bar"));
		TEST(decoded, T("foo+bar"));

		decoded = url_decode(CVIEW("foo+bar"), DECODE_PLUS);
		TEST(decoded, T("foo bar"));

		decoded = url_decode(CVIEW("foo%??"));
		TEST(decoded, T("foo%??"));

		decoded = url_decode(CVIEW("foo%"));
		TEST(decoded, T("foo%"));

		decoded = url_decode(CVIEW(""));
		TEST(decoded, T(""));
	}

	{
		#define TEST_PARSE(parts, _scheme, _userinfo, _host, _port, _path, _query, _fragment) \
		do \
		{ \
			TEST(parts.scheme, T(_scheme)); \
			TEST(parts.userinfo, T(_userinfo)); \
			TEST(parts.host, T(_host)); \
			TEST(parts.port, T(_port)); \
			TEST(parts.path, T(_path)); \
			TEST(parts.query, T(_query)); \
			TEST(parts.fragment, T(_fragment)); \
		} while(false)

		Url parts = {};

		parts = url_parse(CSTR("http://user:pass@example.com:80/path/file.ext?key1=value1&key2=value2#id"));
		TEST_PARSE(parts, "http", "user:pass", "example.com", "80", "/path/file.ext", "key1=value1&key2=value2", "id");

		parts = url_parse(CSTR("http://example.com"));
		TEST_PARSE(parts, "http", "", "example.com", "", "", "", "");

		parts = url_parse(CSTR("http://example.com/"));
		TEST_PARSE(parts, "http", "", "example.com", "", "/", "", "");

		parts = url_parse(CSTR("http://example.com:80"));
		TEST_PARSE(parts, "http", "", "example.com", "80", "", "", "");

		parts = url_parse(CSTR("http://example.com?key=value#id"));
		TEST_PARSE(parts, "http", "", "example.com", "", "", "key=value", "id");

		parts = url_parse(CSTR("http://example.com#id"));
		TEST_PARSE(parts, "http", "", "example.com", "", "", "", "id");

		parts = url_parse(CSTR("http://example.com#id?key=value"));
		TEST_PARSE(parts, "http", "", "example.com", "", "", "", "id?key=value");

		parts = url_parse(CSTR("example.com/path/file.ext"));
		TEST_PARSE(parts, "", "", "", "", "example.com/path/file.ext", "", "");

		parts = url_parse(CSTR("view-source:http://example.com"));
		TEST_PARSE(parts, "view-source", "", "", "", "http://example.com", "", "");

		parts = url_parse(CSTR("file:path/file.ext"));
		TEST_PARSE(parts, "file", "", "", "", "path/file.ext", "", "");

		parts = url_parse(CSTR("file:/path/file.ext"));
		TEST_PARSE(parts, "file", "", "", "", "/path/file.ext", "", "");

		parts = url_parse(CSTR("file://path/file.ext"));
		TEST_PARSE(parts, "file", "", "path", "", "/file.ext", "", "");

		parts = url_parse(CSTR("file:///path/file.ext"));
		TEST_PARSE(parts, "file", "", "", "", "/path/file.ext", "", "");

		parts = url_parse(CSTR("file:///C:\\Path\\File.ext"));
		TEST_PARSE(parts, "file", "", "", "", "/C:\\Path\\File.ext", "", "");

		parts = url_parse(CSTR("http://%65%78%61%6D%70%6C%65%2E%63%6F%6D/%3F+%23/file.ext?_%23_=_%3F_&_%26_=_%3D_&%2B+%2B+#id"));
		TEST_PARSE(parts, "http", "", "example.com", "", "/?+#/file.ext", "_#_=_?_&_&_=_=_&+ + ", "id");

		parts = url_parse(CSTR("http://example.com?key1=value1&key2=value+%26+2&key3"));
		TEST_PARSE(parts, "http", "", "example.com", "", "", "key1=value1&key2=value & 2&key3", "");

		String* value = NULL;
		bool found = false;
		TEST(parts.query_params->count, 3);

		found = map_get(parts.query_params, T("key1"), &value);
		TEST(found, true);
		TEST(value, T("value1"));

		found = map_get(parts.query_params, T("key2"), &value);
		TEST(found, true);
		TEST(value, T("value & 2"));

		found = map_get(parts.query_params, T("key3"), &value);
		TEST(found, true);
		TEST(value, T(""));

		#undef TEST_PARSE
	}

	{
		const TCHAR* headers =  T("HTTP/1.1 200 OK\r\n") \
								T("Content-Type: text/html\r\n") \
								T("Content-Length: 5000\r\n") \
								T("\r\n") \
								T("Content-Encoding: gzip\r\n") \
								T("Cache-Control: public; max-age=3600");

		Map<const TCHAR*, String_View>* map = http_headers_parse(string_from_c(headers));
		TEST(map->count, 5);

		bool found = false;
		String_View value = {};

		found = map_get(map, T(""), &value);
		TEST(found, true);
		TEST(value, T("HTTP/1.1 200 OK"));

		found = map_get(map, T("content-type"), &value);
		TEST(found, true);
		TEST(value, T("text/html"));

		found = map_get(map, T("content-length"), &value);
		TEST(found, true);
		TEST(value, T("5000"));

		found = map_get(map, T("content-encoding"), &value);
		TEST(found, true);
		TEST(value, T("gzip"));

		found = map_get(map, T("cache-control"), &value);
		TEST(found, true);
		TEST(value, T("public; max-age=3600"));
	}
}