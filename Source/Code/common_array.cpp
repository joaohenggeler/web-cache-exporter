#include "common.h"

int array_comparator(const TCHAR* a, const TCHAR* b)
{
	return string_comparator(a, b);
}

int array_comparator(String* a, String* b)
{
	return string_comparator(a, b);
}

int array_comparator(String_View a, String_View b)
{
	return string_comparator(a, b);
}

int array_comparator(Walk_Info a, Walk_Info b)
{
	return string_comparator(a.path, b.path);
}

void array_tests(void)
{
	console_info("Running array tests");
	log_info("Running array tests");

	{
		int c_array[] = {123, 456, 789};
		Array_View<int> view = ARRAY_VIEW_FROM_C(c_array);
		TEST(view.count, 3);
		TEST(view.data[0], 123);
		TEST(view.data[1], 456);
		TEST(view.data[2], 789);
	}

	{
		Array<int>* array = array_create<int>(3);

		array_add(&array, 11);
		array_add(&array, 22);
		array_add(&array, 33);

		TEST(array->count, 3);
		TEST(array->capacity, 3);
		TEST(array->data[0], 11);
		TEST(array->data[1], 22);
		TEST(array->data[2], 33);

		array_add(&array, 44);

		TEST(array->count, 4);
		TEST(array->capacity, 6);
		TEST(array->data[0], 11);
		TEST(array->data[3], 44);

		ARENA_SAVEPOINT()
		{
			CSTR("foo");

			array_add(&array, 55);
			array_add(&array, 66);
		}

		TEST(array->count, 6);
		TEST(array->capacity, 6);
		TEST(array->data[0], 11);
		TEST(array->data[4], 55);
		TEST(array->data[5], 66);

		ARENA_SAVEPOINT()
		{
			CSTR("bar");

			array_add(&array, 77);
		}

		TEST(array->count, 7);
		TEST(array->capacity, 12);
		TEST(array->data[0], 11);
		TEST(array->data[6], 77);

		bool found = false;
		int value = -1;

		found = array_pop(array, 0, &value);
		TEST(array->count, 6);
		TEST(found, true);
		TEST(value, 11);
		TEST_NOT(array->data[0], 11);

		found = array_pop(array, 5, &value);
		TEST(array->count, 5);
		TEST(found, true);
		TEST(value, 66);

		found = array_pop(array, 5, &value);
		TEST(array->count, 5);
		TEST(found, false);

		found = array_pop_end(array, 0, &value);
		TEST(array->count, 4);
		TEST(found, true);
		TEST(value, 55);

		Array<int>* other = array_create<int>(0);
		for(int i = 0; i < 5; i += 1) array_add(&other, i);
		array_merge(&array, other);
		TEST(array->count, 9);
		TEST(other->count, 5);

		array_truncate(array, 2);
		TEST(array->count, 2);

		array_truncate(array, 99);
		TEST(array->count, 2);

		array_clear(array);
		TEST(array->count, 0);
		TEST(array->capacity, 12);

		array_insert(&array, 0, 1);
		array_insert(&array, 0, 2);
		array_insert(&array, 0, 3);

		array_insert(&array, 3, 4);
		array_insert(&array, 3, 5);
		array_insert(&array, 3, 6);

		array_insert(&array, -1, 7);
		array_insert(&array, 99, 8);

		TEST(array->count, 8);
		TEST(array->capacity, 12);
		TEST(array->data[0], 7);
		TEST(array->data[1], 3);
		TEST(array->data[2], 2);
		TEST(array->data[3], 1);
		TEST(array->data[4], 6);
		TEST(array->data[5], 5);
		TEST(array->data[6], 4);
		TEST(array->data[7], 8);
	}

	{
		#define TEST_HAS(c_array, Type, value, expected) \
			do \
			{ \
				Array_View<Type> array = ARRAY_VIEW_FROM_C(c_array); \
				TEST(array_has(array, value), expected); \
			} while(false)

		#define TEST_SORT(c_array, Type) \
			do \
			{ \
				Array_View<Type> array = ARRAY_VIEW_FROM_C(c_array); \
				Compare_Params<Type> params = {}; \
				params.reverse = true; \
				\
				TEST(array_is_sorted(array), false); \
				TEST(array_is_sorted(array, params), false); \
				\
				array_sort(array); \
				TEST(array_is_sorted(array), true); \
				TEST(array_is_sorted(array, params), false); \
				\
				array_sort(array, params); \
				TEST(array_is_sorted(array), false); \
				TEST(array_is_sorted(array, params), true); \
			} while(false)

		int array_num[] =
		{
			40, 10, 20, 42, 27, 25, 1, 19, 30,
			30, 19, 1, 25, 27, 42, 20, 10, 40,
			40, 10, 20, 42, 27, 25, 1, 19, 30,
			30, 19, 1, 25, 27, 42, 20, 10, 40,
			40, 10, 20, 42, 27, 25, 1, 19, 30,
			30, 19, 1, 25, 27, 42, 20, 10, 40,
			40, 10, 20, 42, 27, 25, 1, 19, 30,
			30, 19, 1, 25, 27, 42, 20, 10, 40,
			40, 10, 20, 42, 27, 25, 1, 19, 30,
		};

		const TCHAR* array_c_str[] =
		{
			T("40"), T("10"), T("20"), T("42"), T("27"), T("25"), T("1"), T("19"), T("30"),
			T("30"), T("19"), T("1"), T("25"), T("27"), T("42"), T("20"), T("10"), T("40"),
			T("40"), T("10"), T("20"), T("42"), T("27"), T("25"), T("1"), T("19"), T("30"),
			T("30"), T("19"), T("1"), T("25"), T("27"), T("42"), T("20"), T("10"), T("40"),
			T("40"), T("10"), T("20"), T("42"), T("27"), T("25"), T("1"), T("19"), T("30"),
			T("30"), T("19"), T("1"), T("25"), T("27"), T("42"), T("20"), T("10"), T("40"),
			T("40"), T("10"), T("20"), T("42"), T("27"), T("25"), T("1"), T("19"), T("30"),
			T("30"), T("19"), T("1"), T("25"), T("27"), T("42"), T("20"), T("10"), T("40"),
			T("40"), T("10"), T("20"), T("42"), T("27"), T("25"), T("1"), T("19"), T("30"),
		};

		String* array_str[] =
		{
			CSTR("40"), CSTR("10"), CSTR("20"), CSTR("42"), CSTR("27"), CSTR("25"), CSTR("1"), CSTR("19"), CSTR("30"),
			CSTR("30"), CSTR("19"), CSTR("1"), CSTR("25"), CSTR("27"), CSTR("42"), CSTR("20"), CSTR("10"), CSTR("40"),
			CSTR("40"), CSTR("10"), CSTR("20"), CSTR("42"), CSTR("27"), CSTR("25"), CSTR("1"), CSTR("19"), CSTR("30"),
			CSTR("30"), CSTR("19"), CSTR("1"), CSTR("25"), CSTR("27"), CSTR("42"), CSTR("20"), CSTR("10"), CSTR("40"),
			CSTR("40"), CSTR("10"), CSTR("20"), CSTR("42"), CSTR("27"), CSTR("25"), CSTR("1"), CSTR("19"), CSTR("30"),
			CSTR("30"), CSTR("19"), CSTR("1"), CSTR("25"), CSTR("27"), CSTR("42"), CSTR("20"), CSTR("10"), CSTR("40"),
			CSTR("40"), CSTR("10"), CSTR("20"), CSTR("42"), CSTR("27"), CSTR("25"), CSTR("1"), CSTR("19"), CSTR("30"),
			CSTR("30"), CSTR("19"), CSTR("1"), CSTR("25"), CSTR("27"), CSTR("42"), CSTR("20"), CSTR("10"), CSTR("40"),
			CSTR("40"), CSTR("10"), CSTR("20"), CSTR("42"), CSTR("27"), CSTR("25"), CSTR("1"), CSTR("19"), CSTR("30"),
		};

		String_View array_view[] =
		{
			CVIEW("40"), CVIEW("10"), CVIEW("20"), CVIEW("42"), CVIEW("27"), CVIEW("25"), CVIEW("1"), CVIEW("19"), CVIEW("30"),
			CVIEW("30"), CVIEW("19"), CVIEW("1"), CVIEW("25"), CVIEW("27"), CVIEW("42"), CVIEW("20"), CVIEW("10"), CVIEW("40"),
			CVIEW("40"), CVIEW("10"), CVIEW("20"), CVIEW("42"), CVIEW("27"), CVIEW("25"), CVIEW("1"), CVIEW("19"), CVIEW("30"),
			CVIEW("30"), CVIEW("19"), CVIEW("1"), CVIEW("25"), CVIEW("27"), CVIEW("42"), CVIEW("20"), CVIEW("10"), CVIEW("40"),
			CVIEW("40"), CVIEW("10"), CVIEW("20"), CVIEW("42"), CVIEW("27"), CVIEW("25"), CVIEW("1"), CVIEW("19"), CVIEW("30"),
			CVIEW("30"), CVIEW("19"), CVIEW("1"), CVIEW("25"), CVIEW("27"), CVIEW("42"), CVIEW("20"), CVIEW("10"), CVIEW("40"),
			CVIEW("40"), CVIEW("10"), CVIEW("20"), CVIEW("42"), CVIEW("27"), CVIEW("25"), CVIEW("1"), CVIEW("19"), CVIEW("30"),
			CVIEW("30"), CVIEW("19"), CVIEW("1"), CVIEW("25"), CVIEW("27"), CVIEW("42"), CVIEW("20"), CVIEW("10"), CVIEW("40"),
			CVIEW("40"), CVIEW("10"), CVIEW("20"), CVIEW("42"), CVIEW("27"), CVIEW("25"), CVIEW("1"), CVIEW("19"), CVIEW("30"),
		};

		TEST_HAS(array_num, int, 30, true);
		TEST_HAS(array_num, int, 99, false);

		TEST_HAS(array_c_str, const TCHAR*, T("30"), true);
		TEST_HAS(array_c_str, const TCHAR*, T("99"), false);

		TEST_HAS(array_str, String*, CSTR("30"), true);
		TEST_HAS(array_str, String*, CSTR("99"), false);

		TEST_HAS(array_view, String_View, CVIEW("30"), true);
		TEST_HAS(array_view, String_View, CVIEW("99"), false);

		TEST_SORT(array_num, int);
		TEST_SORT(array_c_str, const TCHAR*);
		TEST_SORT(array_str, String*);
		TEST_SORT(array_view, String_View);

		#undef TEST_HAS
		#undef TEST_SORT
	}
}