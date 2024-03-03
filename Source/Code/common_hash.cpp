#include "common.h"

#pragma warning(push)
#pragma warning(disable : 4530 4995)
	#include "sha256/sha256.h"
#pragma warning(pop)

String* sha256_file(String* path, bool temporary)
{
	String* result = EMPTY_STRING;

	sha256_buff state = {};
	sha256_init(&state);

	File_Reader reader = {};
	reader.temporary = temporary;

	FILE_READ_DEFER(&reader, path)
	{
		while(file_read_next(&reader))
		{
			sha256_update(&state, reader.data, reader.size);
		}
	}

	sha256_finalize(&state);

	if(reader.eof)
	{
		const int HASH_COUNT = 64;
		char* hash = arena_push_buffer(context.current_arena, HASH_COUNT + 1, char);
		sha256_read_hex(&state, hash);
		hash[HASH_COUNT] = '\0';
		result = string_from_utf_8(hash);
	}

	return result;
}

void hash_tests(void)
{
	console_info("Running hash tests");
	log_info("Running hash tests");

	{
		String* file_path = CSTR("Tests\\IO\\file.txt");
		String* empty_path = CSTR("Tests\\IO\\empty.txt");

		TEST(sha256_file(file_path), T("ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c"));
		TEST(sha256_file(empty_path), T("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	}
}