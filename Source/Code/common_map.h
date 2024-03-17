#ifndef COMMON_MAP_H
#define COMMON_MAP_H

#include "common_core.h"
#include "common_arena.h"
#include "common_string.h"

// See:
// - https://craftinginterpreters.com/hash-tables.html
// - https://benhoyt.com/writings/hash-table-in-c/

template<typename Key_Type, typename Value_Type>
struct Map
{
	int count;
	int capacity;

	struct
	{
		Key_Type key;
		Value_Type value;
		bool filled;
	} buckets[ANYSIZE_ARRAY];
};

extern const f32 MAP_LOAD_FACTOR;

u32 fnv1a_hash(const void* data, size_t size);

template<typename Key_Type, typename Value_Type>
Map<Key_Type, Value_Type>* internal_map_create(int capacity)
{
	ASSERT(capacity >= 0, "Negative capacity");

	capacity = MAX(capacity, 1);
	size_t size = sizeof(Map<Key_Type, Value_Type>) + (capacity - 1) * sizeof( ((Map<Key_Type, Value_Type>*) 0)->buckets[0] );
	Map<Key_Type, Value_Type>* map = arena_push(context.current_arena, size, PROTECT(Map<Key_Type, Value_Type>));

	map->count = 0;
	map->capacity = capacity;
	ZeroMemory(map->buckets, capacity * sizeof(map->buckets[0]));

	return map;
}

template<typename Key_Type, typename Value_Type>
Map<Key_Type, Value_Type>* map_create(int capacity)
{
	// Ensure the map can hold the initial capacity without expanding.
	capacity = (int) ceil(MAX(capacity, 1) / MAP_LOAD_FACTOR);
	return internal_map_create<Key_Type, Value_Type>(capacity);
}

template<typename Key_Type>
u32 map_hash(Key_Type key)
{
	return fnv1a_hash(&key, sizeof(Key_Type));
}

template<typename Key_Type>
bool map_equals(Key_Type a, Key_Type b)
{
	return memory_is_equal(&a, &b, sizeof(Key_Type));
}

u32 map_hash(const TCHAR* key);
bool map_equals(const TCHAR* a, const TCHAR* b);

u32 map_hash(String* key);
bool map_equals(String* a, String* b);

u32 map_hash(String_View key);
bool map_equals(String_View a, String_View b);

template<typename Key_Type, typename Value_Type>
bool map_get(Map<Key_Type, Value_Type>* map, Key_Type key, Value_Type* value)
{
	bool found = false;

	int index = map_hash(key) % map->capacity;

	while(map->buckets[index].filled)
	{
		if(map_equals(key, map->buckets[index].key))
		{
			*value = map->buckets[index].value;
			found = true;
			break;
		}
		else
		{
			index = (index + 1) % map->capacity;
		}
	}

	return found;
}

template<typename Key_Type, typename Value_Type>
Value_Type map_get_or(Map<Key_Type, Value_Type>* map, Key_Type key, Value_Type default)
{
	Value_Type value;
	bool found = map_get(map, key, &value);
	return (found) ? (value) : (default);
}

template<typename Key_Type, typename Value_Type>
bool map_has(Map<Key_Type, Value_Type>* map, Key_Type key)
{
	Value_Type value;
	return map_get(map, key, &value);
}

template<typename Key_Type, typename Value_Type>
void internal_map_put(Map<Key_Type, Value_Type>* map, Key_Type key, Value_Type value)
{
	int index = map_hash(key) % map->capacity;
	bool is_new = true;

	while(map->buckets[index].filled)
	{
		if(map_equals(key, map->buckets[index].key))
		{
			is_new = false;
			break;
		}
		else
		{
			index = (index + 1) % map->capacity;
		}
	}

	if(is_new) map->count += 1;
	map->buckets[index].key = key;
	map->buckets[index].value = value;
	map->buckets[index].filled = true;
}

template<typename Key_Type, typename Value_Type>
void map_expand(Map<Key_Type, Value_Type>** map_ptr)
{
	Arena* arena = context.current_arena;

	Map<Key_Type, Value_Type>* old_map = *map_ptr;
	int new_capacity = old_map->capacity * 2;

	void* saved_marker = advance(arena->base_memory, arena->saved_size);
	bool was_saved = saved_marker > old_map;

	Map<Key_Type, Value_Type>* new_map = internal_map_create<Key_Type, Value_Type>(new_capacity);

	for(int i = 0; i < old_map->capacity; i += 1)
	{
		Key_Type key = old_map->buckets[i].key;
		Value_Type value = old_map->buckets[i].value;
		bool filled = old_map->buckets[i].filled;
		if(filled) internal_map_put(new_map, key, value);
	}

	*map_ptr = new_map;

	if(was_saved) arena_save(arena);
}

template<typename Key_Type, typename Value_Type>
void map_put(Map<Key_Type, Value_Type>** map_ptr, Key_Type key, Value_Type value)
{
	Map<Key_Type, Value_Type>* map = *map_ptr;
	internal_map_put(map, key, value);

	// Let's expand after inserting since this also impacts the retrieval.
	if((f32) map->count / map->capacity > MAP_LOAD_FACTOR)
	{
		map_expand(map_ptr);
		map = *map_ptr;
	}

	ASSERT((f32) map->count / map->capacity <= MAP_LOAD_FACTOR, "Map was not expanded.");
}

template<typename Key_Type, typename Value_Type>
void map_clear(Map<Key_Type, Value_Type>* map)
{
	map->count = 0;
	ZeroMemory(map->buckets, map->capacity * sizeof(map->buckets[0]));
}

void map_tests(void);

#endif