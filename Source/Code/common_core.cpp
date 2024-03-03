#include "common.h"

const u32 MAX_U32 = ULONG_MAX;

u64 u32s_to_u64(u32 low, u32 high)
{
	return ((u64) high << 32) | low;
}

void u32_to_u16s(u32 num, u16* low, u16* high)
{
	*low = (u16) (num & 0xFFFF);
	*high = (u16) (num >> 16);
}

void u64_to_u32s(u64 num, u32* low, u32* high)
{
	*low = (u32) (num & 0xFFFFFFFF);
	*high = (u32) (num >> 32);
}

bool flag_has_one(u32 flags)
{
	return is_power_of_two(flags);
}

#pragma warning(push)
#pragma warning(disable : 4189)
u32 flag_to_index(u32 flag)
{
	ASSERT(is_power_of_two(flag), "Flag is not a power of two");
	DWORD index = 0;
	bool success = _BitScanReverse(&index, flag) != FALSE;
	ASSERT(success, "Empty flag");
	return index;
}
#pragma warning(pop)

size_t from_kilobytes(size_t kilobytes)
{
	return kilobytes * 1000;
}

size_t from_megabytes(size_t megabytes)
{
	return megabytes * 1000 * 1000;
}

size_t from_gigabytes(size_t gigabytes)
{
	return gigabytes * 1000 * 1000 * 1000;
}

bool memory_is_equal(const void* a, const void* b, size_t size)
{
	return memcmp(a, b, size) == 0;
}

ptrdiff_t ptr_diff(const void* a, const void* b)
{
	return (char*) a - (char*) b;
}

static const int ERROR_MESSAGE_COUNT = 100;
static TCHAR error_message[ERROR_MESSAGE_COUNT] = T("");

const TCHAR* last_error_message(void)
{
	// @NoArena
	// @NoLog

	u32 error = GetLastError();
	u32 flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK;
	u32 language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

	if(FormatMessage(flags, NULL, error, language, error_message, ERROR_MESSAGE_COUNT, NULL) != 0)
	{
		ASSERT(error_message[0] != T('\0'), "Empty error message");
		int count = c_string_code_count(error_message);
		StringCchPrintf(error_message + count, ERROR_MESSAGE_COUNT - count, T("(%I32u)"), error);
	}
	else
	{
		StringCchPrintf(error_message, ERROR_MESSAGE_COUNT, T("%I32u"), error);
	}

	return error_message;
}

const TCHAR* errno_string(void)
{
	// @NoArena
	// @NoLog

	if(_tcserror_s(error_message, ERROR_MESSAGE_COUNT, errno) == 0)
	{
		ASSERT(error_message[0] != T('\0'), "Empty error message");
		int count = c_string_code_count(error_message);
		StringCchPrintf(error_message + count, ERROR_MESSAGE_COUNT - count, T(" (%d)"), errno);
	}
	else
	{
		StringCchPrintf(error_message, ERROR_MESSAGE_COUNT, T("%d"), errno);
	}

	return error_message;
}

u8 byte_order_swap(u8 num)
{
	return num;
}

u16 byte_order_swap(u16 num)
{
	return _byteswap_ushort(num);
}

u32 byte_order_swap(u32 num)
{
	return _byteswap_ulong(num);
}

u64 byte_order_swap(u64 num)
{
	return _byteswap_uint64(num);
}

void core_tests(void)
{
	console_info("Running core tests");
	log_info("Running core tests");

	{
		{
			TEST(u32s_to_u64(0xAABBCCDDU, 0x11223344U), 0x11223344AABBCCDDULL);
		}

		{
			u16 low = 0;
			u16 high = 0;
			u32_to_u16s(0xAABBCCDDU, &low, &high);
			TEST(low, 0xCCDD);
			TEST(high, 0xAABB);
		}

		{
			u32 low = 0;
			u32 high = 0;
			u64_to_u32s(0xAABBCCDD11223344ULL, &low, &high);
			TEST(low, 0x11223344U);
			TEST(high, 0xAABBCCDDU);
		}
	}

	{
		TEST(flag_to_index(1U << 0), 0U);
		TEST(flag_to_index(1U << 31), 31U);
	}

	{
		TEST(from_kilobytes(1), (size_t) 1000);
		TEST(from_megabytes(1), (size_t) 1000000);
		TEST(from_gigabytes(1), (size_t) 1000000000);
	}

	{
		wchar_t a[] = L"test";
		wchar_t b[] = L"test";
		TEST(memory_is_equal(a, b, sizeof(a)), true);
		TEST((int) ptr_diff(a, a), 0);
		TEST((int) ptr_diff(a + 3, a), (int) (3 * sizeof(wchar_t)));
	}

	{
		SetLastError(ERROR_SUCCESS);
		TEST(last_error_message(), T("The operation completed successfully. (0)"));

		_set_errno(0);
		TEST(errno_string(), T("No error (0)"));
	}

	{
		TEST(MIN(0, 0), 0);
		TEST(MIN(0, 1), 0);
		TEST(MIN(1, 0), 0);

		TEST(MAX(0, 0), 0);
		TEST(MAX(0, 1), 1);
		TEST(MAX(1, 0), 1);
	}

	{
		TEST(CEIL_DIV(0, 3), 0);
		TEST(CEIL_DIV(1, 3), 1);
		TEST(CEIL_DIV(2, 3), 1);
		TEST(CEIL_DIV(3, 3), 1);
		TEST(CEIL_DIV(4, 3), 2);
	}

	{
		TEST(ROUND_UP(0, 4), 0);
		TEST(ROUND_UP(1, 4), 4);
		TEST(ROUND_UP(4, 4), 4);
		TEST(ROUND_UP(6, 4), 8);
		TEST(ROUND_UP(8, 4), 8);
		TEST(ROUND_UP(11, 4), 12);

		TEST(ROUND_UP_OFFSET(0, 4), 0);
		TEST(ROUND_UP_OFFSET(1, 4), 3);
		TEST(ROUND_UP_OFFSET(4, 4), 0);
		TEST(ROUND_UP_OFFSET(6, 4), 2);
		TEST(ROUND_UP_OFFSET(8, 4), 0);
		TEST(ROUND_UP_OFFSET(11, 4), 1);
	}

	{
		void* ptr = (void*) 44;

		TEST(POINTER_IS_ALIGNED_TO_SIZE(ptr, 1), true);
		TEST(POINTER_IS_ALIGNED_TO_SIZE(ptr, 2), true);
		TEST(POINTER_IS_ALIGNED_TO_SIZE(ptr, 4), true);
		TEST(POINTER_IS_ALIGNED_TO_SIZE(ptr, 8), false);

		TEST(POINTER_IS_ALIGNED_TO_TYPE(ptr, u8), true);
		TEST(POINTER_IS_ALIGNED_TO_TYPE(ptr, u16), true);
		TEST(POINTER_IS_ALIGNED_TO_TYPE(ptr, u32), true);
		TEST(POINTER_IS_ALIGNED_TO_TYPE(ptr, u64), false);
	}

	{
		TEST(byte_order_swap((u8) 0xAA), 0xAA);
		TEST(byte_order_swap((u16) 0xAABB), 0xBBAA);
		TEST(byte_order_swap(0xAABBCCDDU), 0xDDCCBBAAU);
		TEST(byte_order_swap(0xAABBCCDD11223344ULL), 0x44332211DDCCBBAAULL);
	}

	{
		TEST(u32_clamp(1000U), 1000U);
		TEST(u32_clamp(MAX_U32), MAX_U32);
		TEST(u32_clamp((u64) MAX_U32 * 2), MAX_U32);

		TEST(size_clamp(1000), (size_t) 1000);
		TEST(size_clamp(MAX_U32), (size_t) MAX_U32);

		#ifdef WCE_32_BIT
			TEST(size_clamp((u64) MAX_U32 * 2), (size_t) MAX_U32);
		#else
			TEST(size_clamp((u64) MAX_U32 * 2), (size_t) MAX_U32 * 2);
		#endif
	}

	{
		TEST(u16_truncate(0xAABB), 0xAABB);
		TEST(u16_truncate(0xAABBCCDDU), 0xCCDD);
		TEST(u16_truncate(0xAABBCCDD11223344ULL), 0x3344);
	}

	{
		TEST(is_power_of_two(0), false);
		TEST(is_power_of_two(1), true);
		TEST(is_power_of_two(2), true);
		TEST(is_power_of_two(3), false);

		TEST(is_power_of_two(32), true);
		TEST(is_power_of_two(33), false);
		TEST(is_power_of_two(64), true);
		TEST(is_power_of_two(65), false);
	}

	{
		int buffer[10] = {};
		TEST(advance(buffer, 0), buffer);
		TEST(advance(buffer, 5 * sizeof(int)), buffer + 5);
	}
}