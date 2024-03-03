#include "cache.h"
#include "common.h"

static Csv_Column _REPORT_COLUMNS[] =
{
	CSV_FORMAT, CSV_MODE, CSV_PROFILE,
	CSV_INPUT_PATH, CSV_OUTPUT_PATH,
	CSV_FOUND, CSV_EXPORTED, CSV_EXCLUDED,
};

static Array_View<Csv_Column> REPORT_COLUMNS = ARRAY_VIEW_FROM_C(_REPORT_COLUMNS);

void report_begin(Exporter* exporter)
{
	if(exporter->create_csvs)
	{
		builder_clear(exporter->builder);
		builder_append_path(&exporter->builder, exporter->output_path);
		builder_append_path(&exporter->builder, T("Report.csv"));

		String* path = builder_to_string(exporter->builder);
		csv_begin(&exporter->report_csv, path, REPORT_COLUMNS);
	}

	#ifdef WCE_DEBUG
		context.debug_report_balance += 1;
	#endif

	ASSERT(exporter->builder != NULL, "Terminated builder");
}

void report_end(Exporter* exporter)
{
	if(exporter->create_csvs && exporter->report_csv.created)
	{
		csv_end(&exporter->report_csv);
		if(file_is_empty(exporter->report_csv.path)) file_delete(exporter->report_csv.path);
	}

	#ifdef WCE_DEBUG
		context.debug_report_balance -= 1;
	#endif
}

Report_Params report_save(Exporter* exporter)
{
	Report_Params save = {};
	save.found = exporter->total_found;
	save.exported = exporter->total_exported;
	save.excluded = exporter->total_excluded;
	return save;
}

Report_Params report_update(Exporter* exporter, Report_Params save)
{
	Report_Params params = {};
	params.found = exporter->total_found - save.found;
	params.exported = exporter->total_exported - save.exported;
	params.excluded = exporter->total_excluded - save.excluded;
	return params;
}

void report_next(Exporter* exporter, Report_Params params)
{
	ASSERT(exporter->current_long != NULL, "Missing current long");
	ASSERT(params.path != NULL, "Missing path");

	ARENA_SAVEPOINT()
	{
		Map<Csv_Column, String*>* row = map_create<Csv_Column, String*>(REPORT_COLUMNS.count);

		map_put(&row, CSV_FORMAT, exporter->current_long);
		map_put(&row, CSV_MODE, (exporter->current_batch) ? (CSTR("Batch")) : (CSTR("Single")));
		map_put(&row, CSV_PROFILE, exporter->current_key_paths.name);
		map_put(&row, CSV_INPUT_PATH, path_absolute(params.path));
		map_put(&row, CSV_OUTPUT_PATH, path_absolute(exporter->current_output));
		map_put(&row, CSV_FOUND, string_from_num(params.found));
		map_put(&row, CSV_EXPORTED, string_from_num(params.exported));
		map_put(&row, CSV_EXCLUDED, string_from_num(params.excluded));

		if(exporter->create_csvs && exporter->report_csv.created)
		{
			csv_next(&exporter->report_csv, row);
		}
	}
}