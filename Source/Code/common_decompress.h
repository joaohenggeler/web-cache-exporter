#ifndef COMMON_DECOMPRESS_H
#define COMMON_DECOMPRESS_H

#include "common_core.h"
#include "common_string.h"
#include "common_io.h"

bool decompress_from_content_encoding(String* path, String* content_encoding, File_Writer* writer, bool temporary = false);

void decompress_tests(void);

#endif