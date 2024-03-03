#ifndef CACHE_MOZILLA_H
#define CACHE_MOZILLA_H

#include "cache_csv.h"
#include "cache_exporter.h"
#include "common_core.h"
#include "common_string.h"
#include "common_array.h"

extern Array_View<Csv_Column> MOZILLA_COLUMNS;

void mozilla_single_export(Exporter* exporter, String* path);
void mozilla_batch_export(Exporter* exporter, Key_Paths key_paths);

void mozilla_tests(void);

#endif