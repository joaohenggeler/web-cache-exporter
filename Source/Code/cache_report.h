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
Report_Params report_save(Exporter* exporter);
Report_Params report_update(Exporter* exporter, Report_Params save);
void report_next(Exporter* exporter, Report_Params params);

#define REPORT_DEFER(exporter, path) \
	Report_Params LINE_VAR(_params_) = {}; \
	DEFER \
	( \
		(LINE_VAR(_params_) = report_save(exporter)), \
		(LINE_VAR(_params_) = report_update(exporter, LINE_VAR(_params_)), LINE_VAR(_params_).path = path, report_next(exporter, LINE_VAR(_params_))) \
	)

#endif