#ifndef CACHE_BATCH_H
#define CACHE_BATCH_H

#include "cache_exporter.h"
#include "common_core.h"

bool batch_load(Exporter* exporter);
bool batch_check(Exporter* exporter);

void batch_tests(void);

#endif