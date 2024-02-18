#ifndef COMMON_HASH_H
#define COMMON_HASH_H

#include "common_core.h"
#include "common_string.h"

String* sha256_file(String* path);

void hash_tests(void);

#endif