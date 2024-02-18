#ifndef CACHE_REPORT_H
#define CACHE_REPORT_H

#include "cache_exporter.h"
#include "common_core.h"
#include "common_string.h"

struct Report_Params
{
	String* path;
	int found;
	int exported;
	int excluded;
};

void report_begin(Exporter* exporter);
void report_end(Exporter* exporter);
void report_next(Exporter* exporter, Report_Params params);

#endif