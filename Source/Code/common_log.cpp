#include "common.h"

void log_create(void)
{
	context.log_handle = handle_create(T("WCE.log"), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
	if(context.log_handle == INVALID_HANDLE_VALUE) console_error("Failed to create the log file with the error: %s", last_error_message());
}

void log_close(void)
{
	handle_close(&context.log_handle);
}

void internal_log_print(const TCHAR* format, ...)
{
	// @NoArena

	if(context.log_handle == INVALID_HANDLE_VALUE) return;

	const size_t MAX_LINE_COUNT = 5000;
	TCHAR line[MAX_LINE_COUNT] = T("");

	SYSTEMTIME time = {};
	GetSystemTime(&time);
	StringCchPrintf(line, MAX_LINE_COUNT, T("[%hu-%02hu-%02hu %02hu:%02hu:%02hu] "), time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

	size_t timestamp_count = 0;
	StringCchLength(line, MAX_LINE_COUNT, &timestamp_count);

	va_list args;
	va_start(args, format);
	StringCchVPrintf(line + timestamp_count, MAX_LINE_COUNT - timestamp_count, format, args);
	va_end(args);

	ASSERT(line[0] != T('\0'), "Empty line");
	StringCchCat(line, MAX_LINE_COUNT, T("\r\n"));
	line[MAX_LINE_COUNT - 3] = '\r';
	line[MAX_LINE_COUNT - 2] = '\n';
	line[MAX_LINE_COUNT - 1] = '\0';

	#ifdef WCE_9X
		wchar_t utf_16_line[MAX_LINE_COUNT] = L"";
		MultiByteToWideChar(CP_ACP, 0, line, -1, utf_16_line, MAX_LINE_COUNT);
		wchar_t* line_ptr = utf_16_line;
	#else
		wchar_t* line_ptr = line;
	#endif

	// dwFlags must be 0 or MB_ERR_INVALID_CHARS for UTF-8.
	char utf_8_line[MAX_LINE_COUNT] = "";
	WideCharToMultiByte(CP_UTF8, 0, line_ptr, -1, utf_8_line, sizeof(utf_8_line), NULL, NULL);

	size_t utf_8_size = 0;
	StringCbLengthA(utf_8_line, sizeof(utf_8_line), &utf_8_size);

	// Since Windows 7: lpNumberOfBytesWritten cannot be NULL.
	DWORD bytes_written = 0;
	WriteFile(context.log_handle, utf_8_line, (u32) utf_8_size, &bytes_written, NULL);
}