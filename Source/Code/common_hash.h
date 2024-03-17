#ifndef COMMON_HASH_H
#define COMMON_HASH_H

#include "common_core.h"
#include "common_string.h"

struct Sha256
{
	u8 data[32];
};

String* sha256_string_from_file(String* path, bool temporary = false);
Sha256 sha256_bytes_from_file(String* path, bool temporary = false);

void hash_tests(void);

#endif