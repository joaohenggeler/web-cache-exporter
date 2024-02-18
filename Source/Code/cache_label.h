#ifndef CACHE_LABEL_H
#define CACHE_LABEL_H

struct Exporter;

#include "common_core.h"
#include "common_string.h"
#include "common_array.h"
#include "common_net.h"

enum Label_Type
{
	LABEL_NONE,
	LABEL_FILE,
	LABEL_URL,
};

struct Signature
{
	Array<u8>* bytes;
	Array<bool>* wildcards;
};

struct Domain
{
	String* host;
	String* path;
};

struct Label
{
	String* major_name;

	Label_Type type;
	String* minor_name;

	union
	{
		struct
		{
			Array<Signature>* signatures;
			Array<String*>* mime_types;
			Array<String*>* extensions;
			String* default_extension;
		};

		struct
		{
			Array<Domain>* domains;
		};
	};
};

struct Match_Params
{
	String* path;
	String* mime_type;
	String* extension;
	Url url;
};

void label_load_all(Exporter* exporter);
bool label_file_match(Exporter* exporter, Match_Params params, Label* result);
bool label_url_match(Exporter* exporter, Match_Params params, Label* result);

void label_tests(void);

#endif