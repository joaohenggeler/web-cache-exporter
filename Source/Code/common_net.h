#ifndef COMMON_NET_H
#define COMMON_NET_H

#include "common_core.h"
#include "common_string.h"
#include "common_map.h"

struct Url
{
	String* full;

	String_View scheme;

	String* userinfo;
	String* host;
	String* port;

	String* path;
	String* query;
	String* fragment;

	Map<const TCHAR*, String*>* query_params;
};

extern const bool DECODE_PLUS;

String* url_decode(String_View component, bool decode_plus = false);
Url url_parse(String* url);

Map<const TCHAR*, String_View>* http_headers_parse(String* headers);

void net_tests(void);

#endif