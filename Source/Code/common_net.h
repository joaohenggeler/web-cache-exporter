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

Url url_parse(String* url);

void net_tests(void);

#endif