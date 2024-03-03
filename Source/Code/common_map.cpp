#include "common.h"

const f32 MAP_LOAD_FACTOR = 0.75f;

u32 fnv1a_hash(const void* data, size_t size)
{
	const u32 FNV_OFFSET = 0x811C9DC5U;
	const u32 FNV_PRIME = 0x01000193U;

	u32 hash = FNV_OFFSET;

	for(size_t i = 0; i < size; i += 1)
	{
		hash ^= *((char*) data + i);
		hash *= FNV_PRIME;
	}

	return hash;
}

u32 map_hash(const TCHAR* key)
{
	return fnv1a_hash(key, string_size(key));
}

bool map_equals(const TCHAR* a, const TCHAR* b)
{
	return string_is_equal(a, b);
}

u32 map_hash(String* key)
{
	return fnv1a_hash(key->data, string_size(key));
}

bool map_equals(String* a, String* b)
{
	return string_is_equal(a, b);
}

u32 map_hash(String_View key)
{
	return fnv1a_hash(key.data, string_size(key));
}

bool map_equals(String_View a, String_View b)
{
	return string_is_equal(a, b);
}

void map_tests(void)
{
	console_info("Running map tests");
	log_info("Running map tests");

	{
		Map<int, char>* map = map_create<int, char>(10);

		TEST(map->count, 0);
		TEST(map->capacity, 14);

		map_put(&map, 123, 'A');
		map_put(&map, 123, 'A');

		TEST(map->count, 1);
		TEST(map->capacity, 14);

		char value = '\0';
		bool found = map_get(map, 123, &value);
		TEST(found, true);
		TEST(value, 'A');

		value = map_get_or(map, 123, 'B');
		TEST(value, 'A');

		value = map_get_or(map, 456, 'B');
		TEST(value, 'B');

		TEST(map_has(map, 123), true);
		TEST(map_has(map, 456), false);
	}

	{
		Map<String*, int>* map = map_create<String*, int>(10);

		TEST(map->count, 0);
		TEST(map->capacity, 14);

		map_put(&map, CSTR("key"), 999);
		map_put(&map, CSTR("key"), 999);

		TEST(map->count, 1);
		TEST(map->capacity, 14);

		int value = 0;
		bool found = map_get(map, CSTR("key"), &value);
		TEST(found, true);
		TEST(value, 999);

		value = map_get_or(map, CSTR("key"), -1);
		TEST(value, 999);

		value = map_get_or(map, CSTR("wrong"), -1);
		TEST(value, -1);

		TEST(map_has(map, CSTR("key")), true);
		TEST(map_has(map, CSTR("wrong")), false);
	}

	{
		Map<String_View, int>* map = map_create<String_View, int>(10);

		TEST(map->count, 0);
		TEST(map->capacity, 14);

		map_put(&map, CVIEW("key"), 999);
		map_put(&map, CVIEW("key"), 999);

		TEST(map->count, 1);
		TEST(map->capacity, 14);

		int value = 0;
		bool found = map_get(map, CVIEW("key"), &value);
		TEST(found, true);
		TEST(value, 999);

		value = map_get_or(map, CVIEW("key"), -1);
		TEST(value, 999);

		value = map_get_or(map, CVIEW("wrong"), -1);
		TEST(value, -1);

		TEST(map_has(map, CVIEW("key")), true);
		TEST(map_has(map, CVIEW("wrong")), false);
	}

	{
		Map<int, int>* map = map_create<int, int>(100);

		TEST(map->count, 0);
		TEST(map->capacity, 134);

		for(int i = 0; i < 100; i += 1) map_put(&map, i, i * i);

		TEST(map->count, 100);
		TEST(map->capacity, 134);

		for(int i = 0; i < 100; i += 1)
		{
			int value = 0;
			bool found = map_get(map, i, &value);
			TEST(found, true);
			TEST(value, i * i);
		}

		map_put(&map, 999, 999);

		TEST(map->count, 101);
		TEST(map->capacity, 268);

		map_clear(map);
		TEST(map->count, 0);
		TEST(map->capacity, 268);
	}

	{
		Map<int, int>* map = map_create<int, int>(0);

		TEST(map->count, 0);
		TEST(map->capacity, 2);

		for(int i = 0; i < 100; i += 1) map_put(&map, i, i * i);

		TEST(map->count, 100);
		TEST(map->capacity, 256);

		for(int i = 0; i < 100; i += 1)
		{
			int value = 0;
			bool found = map_get(map, i, &value);
			TEST(found, true);
			TEST(value, i * i);
		}
	}

	{
		const TCHAR* c_str = T("key");
		String* str = CSTR("key");
		String_View view = CVIEW("key");

		u32 c_str_hash = map_hash(c_str);
		u32 str_hash = map_hash(str);
		u32 view_hash = map_hash(view);

		TEST(c_str_hash, str_hash);
		TEST(str_hash, view_hash);
	}
}