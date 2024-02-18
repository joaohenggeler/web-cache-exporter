#ifndef COMMON_ARRAY_H
#define COMMON_ARRAY_H

#include "common_core.h"
#include "common_context.h"
#include "common_arena.h"

struct String;
struct String_View;
struct Walk_Info;

template<typename Type>
struct Array
{
	int count;
	int capacity;
	Type data[ANYSIZE_ARRAY];
};

template<typename Type>
struct Array_View
{
	int count;
	Type* data;
};

#define ARRAY_VIEW_FROM_C(c_array) {_countof(c_array), c_array}

template<typename Type>
Array<Type>* array_create(int capacity)
{
	ASSERT(capacity >= 0, "Negative capacity");
	capacity = MAX(capacity, 1);
	size_t size = sizeof(Array<Type>) + (capacity - 1) * sizeof(Type);
	Array<Type>* array = arena_push(context.current_arena, size, Array<Type>);
	array->count = 0;
	array->capacity = capacity;
	return array;
}

template<typename Type>
Array<Type>* array_from_view(Array_View<Type> view)
{
	Array<Type>* array = array_create<Type>(view.count);
	array->count = view.count;
	size_t size = view.count * sizeof(Type);
	CopyMemory(array->data, view.data, size);
	return array;
}

template<typename Type>
void array_expand(Array<Type>** array_ptr)
{
	Arena* arena = context.current_arena;

	Array<Type>* old_array = *array_ptr;
	int new_capacity = old_array->capacity * 2;

	void* saved_marker = advance(arena->base_memory, arena->saved_size);
	bool was_saved = saved_marker > old_array;

	if(arena->last_memory == old_array)
	{
		size_t size = (new_capacity - old_array->capacity) * sizeof(Type);
		arena_extend(arena, size);
		old_array->capacity = new_capacity;
	}
	else
	{
		size_t old_size = sizeof(Array<Type>) + (old_array->capacity - 1) * sizeof(Type);
		size_t new_size = sizeof(Array<Type>) + (new_capacity - 1) * sizeof(Type);

		Array<Type>* new_array = arena_push(arena, new_size, Array<Type>);
		CopyMemory(new_array, old_array, old_size);
		new_array->capacity = new_capacity;

		*array_ptr = new_array;
	}

	if(was_saved) arena_save(arena);
}

template<typename Type>
void array_add(Array<Type>** array_ptr, Type value)
{
	Array<Type>* array = *array_ptr;

	if(array->count + 1 > array->capacity)
	{
		array_expand(array_ptr);
		array = *array_ptr;
	}

	ASSERT(array->capacity > array->count, "Array was not expanded");

	int index = array->count;
	array->count += 1;
	array->data[index] = value;
}

template<typename Type>
void array_insert(Array<Type>** array_ptr, int index, Type value)
{
	Array<Type>* array = *array_ptr;

	if(array->count + 1 > array->capacity)
	{
		array_expand(array_ptr);
		array = *array_ptr;
	}

	ASSERT(array->capacity > array->count, "Array was not expanded");

	index = MAX(index, 0);
	index = MIN(index, array->count);

	if(index == array->count)
	{
		array->data[index] = value;
	}
	else
	{
		Type* from = array->data + index;
		Type* to = from + 1;
		size_t size = (array->count - index) * sizeof(Type);
		MoveMemory(to, from, size);
		array->data[index] = value;
	}

	array->count += 1;
}

template<typename Type>
bool array_pop(Array<Type>* array, int index, Type* value = NULL)
{
	int last_index = array->count - 1;

	if(array->count == 0) return false;
	if(index < 0 || index > last_index) return false;

	if(value != NULL) *value = array->data[index];
	if(index != last_index) array->data[index] = array->data[last_index];
	array->count -= 1;

	return true;
}

template<typename Type>
bool array_pop_end(Array<Type>* array, int index, Type* value = NULL)
{
	int last_index = array->count - 1;
	return array_pop(array, last_index - index, value);
}

template<typename Type>
void array_truncate(Array<Type>* array, int count)
{
	array->count = MIN(count, array->count);
}

template<typename Type>
void array_clear(Array<Type>* array)
{
	array->count = 0;
}

template<typename Type>
struct Compare_Params
{
	bool reverse;
	int (*comparator)(Type a, Type b);
};

template<typename Type>
int array_comparator(Type a, Type b)
{
	if(a < b) return -1;
	else if(a == b) return 0;
	else return 1;
}

int array_comparator(const TCHAR* a, const TCHAR* b);
int array_comparator(String* a, String* b);
int array_comparator(String_View a, String_View b);
int array_comparator(Walk_Info a, Walk_Info b);

template<typename Type>
bool array_has(Type* array, int count, Type value, Compare_Params<Type> params)
{
	if(params.comparator == NULL) params.comparator = array_comparator;

	bool found = false;

	for(int i = 0; i < count; i += 1)
	{
		if(params.comparator(array[i], value) == 0)
		{
			found = true;
			break;
		}
	}

	return found;
}

template<typename Type>
bool array_has(Array<Type>* array, Type value, Compare_Params<Type> params)
{
	return array_has(array->data, array->count, value, params);
}

template<typename Type>
bool array_has(Array_View<Type> array, Type value, Compare_Params<Type> params)
{
	return array_has(array.data, array.count, value, params);
}

template<typename Type>
bool array_has(Type* array, int count, Type value)
{
	Compare_Params<Type> params = {};
	return array_has(array, count, value, params);
}

template<typename Type>
bool array_has(Array<Type>* array, Type value)
{
	return array_has(array->data, array->count, value);
}

template<typename Type>
bool array_has(Array_View<Type> array, Type value)
{
	return array_has(array.data, array.count, value);
}

template<typename Type>
bool array_is_sorted(Type* array, int count, Compare_Params<Type> params)
{
	if(params.comparator == NULL) params.comparator = array_comparator;

	bool sorted = true;

	int comparator = (params.reverse) ? (-1) : (1);
	for(int i = 1; i < count; i += 1)
	{
		Type a = array[i-1];
		Type b = array[i];
		if(params.comparator(a, b) == comparator)
		{
			sorted = false;
			break;
		}
	}

	return sorted;
}

template<typename Type>
bool array_is_sorted(Array<Type>* array, Compare_Params<Type> params)
{
	return array_is_sorted(array->data, array->count, params);
}

template<typename Type>
bool array_is_sorted(Array_View<Type> array, Compare_Params<Type> params)
{
	return array_is_sorted(array.data, array.count, params);
}

template<typename Type>
bool array_is_sorted(Type* array, int count)
{
	Compare_Params<Type> params = {};
	return array_is_sorted(array, count, params);
}

template<typename Type>
bool array_is_sorted(Array<Type>* array)
{
	return array_is_sorted(array->data, array->count);
}

template<typename Type>
bool array_is_sorted(Array_View<Type> array)
{
	return array_is_sorted(array.data, array.count);
}

template<typename Type>
void array_insertion_sort(Type* array, int count, Compare_Params<Type> params)
{
	if(params.comparator == NULL) params.comparator = array_comparator;

	int comparator = (params.reverse) ? (-1) : (1);
	for(int i = 1; i < count; i += 1)
	{
		int j = i;
		while(j > 0 && params.comparator(array[j-1], array[j]) == comparator)
		{
			Type temp = array[j];
			array[j] = array[j-1];
			array[j-1] = temp;
			j -= 1;
		}
	}

	ASSERT(array_is_sorted(array, count, params), "Array was not sorted");
}

template<typename Type>
Array_View<Type> array_slice(Type* array, int count, int begin, int end)
{
	Array_View<Type> slice = {};

	int last = count - 1;
	if(count == 0 || begin > last || end < 0 || begin > end) return slice;

	// Inclusive begin and exclusive end.
	begin = MAX(begin, 0);
	end = MIN(end, last + 1);

	slice.count = end - begin;
	slice.data = array + begin;

	return slice;
}

template<typename Type>
void array_tim_sort(Type* array, int count, Compare_Params<Type> params)
{
	if(params.comparator == NULL) params.comparator = array_comparator;

	const int MIN_RUN_COUNT = 32;
	int comparator = (params.reverse) ? (-1) : (1);

	for(int i = 0; i < count; i += MIN_RUN_COUNT)
	{
		Array_View<Type> run = array_slice(array, count, i, i + MIN_RUN_COUNT);
		array_insertion_sort(run.data, run.count, params);
	}

	for(int run_count = MIN_RUN_COUNT; run_count < count; run_count *= 2)
	{
		ASSERT(is_power_of_two(run_count), "Run count is not a power of two");

		for(int left_idx = 0; left_idx < count; left_idx += run_count * 2)
		{
			ARENA_SAVEPOINT()
			{
				int middle_idx = left_idx + run_count;
				int right_idx = left_idx + run_count * 2;

				Array<Type>* left_run = array_from_view(array_slice(array, count, left_idx, middle_idx));
				Array<Type>* right_run = array_from_view(array_slice(array, count, middle_idx, right_idx));

				// Index in the runs.
				int l = 0;
				int r = 0;

				// Index in the array.
				int a = left_idx;

				// Merge the sorted runs.
				while(l < left_run->count && r < right_run->count)
				{
					Type left = left_run->data[l];
					Type right = right_run->data[r];

					if(params.comparator(left, right) == comparator)
					{
						array[a] = right;
						r += 1;
					}
					else
					{
						array[a] = left;
						l += 1;
					}

					a += 1;
				}

				// Append any remaining elements from the left or right runs.

				for(; l < left_run->count; l += 1)
				{
					array[a] = left_run->data[l];
					a += 1;
				}

				for(; r < right_run->count; r += 1)
				{
					array[a] = right_run->data[r];
					a += 1;
				}
			}
		}
	}

	ASSERT(array_is_sorted(array, count, params), "Array was not sorted");
}

template<typename Type>
void array_sort(Type* array, int count, Compare_Params<Type> params)
{
	array_tim_sort(array, count, params);
}

template<typename Type>
void array_sort(Array<Type>* array, Compare_Params<Type> params)
{
	array_sort(array->data, array->count, params);
}

template<typename Type>
void array_sort(Array_View<Type> array, Compare_Params<Type> params)
{
	array_sort(array.data, array.count, params);
}

template<typename Type>
void array_sort(Type* array, int count)
{
	Compare_Params<Type> params = {};
	array_tim_sort(array, count, params);
}

template<typename Type>
void array_sort(Array<Type>* array)
{
	array_sort(array->data, array->count);
}

template<typename Type>
void array_sort(Array_View<Type> array)
{
	array_sort(array.data, array.count);
}

void array_tests(void);

#endif