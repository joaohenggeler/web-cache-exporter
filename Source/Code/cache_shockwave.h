#ifndef CACHE_SHOCKWAVE_H
#define CACHE_SHOCKWAVE_H

#include "cache_csv.h"
#include "cache_exporter.h"
#include "common_core.h"
#include "common_string.h"
#include "common_array.h"

extern Array_View<Csv_Column> SHOCKWAVE_COLUMNS;

void shockwave_single_export(Exporter* exporter, String* path);
void shockwave_batch_export(Exporter* exporter, Key_Paths key_paths);

void shockwave_tests(void);

#endif