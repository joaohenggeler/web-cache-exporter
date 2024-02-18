#ifndef COMMON_DECOMPRESS_H
#define COMMON_DECOMPRESS_H

#include "common_core.h"
#include "common_string.h"
#include "common_io.h"

bool zlib_file_decompress(String* path, File_Writer* writer);
bool brotli_file_decompress(String* path, File_Writer* writer);
bool compress_file_decompress(String* path, File_Writer* writer);

void decompress_tests(void);

#endif