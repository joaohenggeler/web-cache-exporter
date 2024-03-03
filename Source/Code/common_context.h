#ifndef COMMON_CONTEXT_H
#define COMMON_CONTEXT_H

struct Exporter;

#include "common_core.h"
#include "common_arena.h"

struct String;

struct Context
{
	u32 major_os_version;
	u32 minor_os_version;
	size_t page_size;
	int max_component_count;
	s64 performance_counter_frequency;

	bool console_enabled;
	bool log_enabled;
	bool large_tests;
	bool tiny_file_buffers;

	int total_test_count;
	int failed_test_count;

	HANDLE log_handle;
	Arena* current_arena;

	String* executable_path;

	bool has_temporary;
	String* temporary_path;

	int previous_progress_count;
	int current_progress_count;

	#ifdef WCE_DEBUG
		int debug_walk_balance;
		int debug_file_read_balance;
		int debug_file_write_balance;
		int debug_file_temporary_balance;
		int debug_file_map_balance;
		int debug_timer_balance;
		int debug_exporter_balance;
		int debug_report_balance;
	#endif
};

extern Context context;

void context_initialize_1(void);
bool context_initialize_2(void);
void context_initialize_3(Exporter* exporter);
void context_terminate(void);

bool windows_is_9x(void);

Arena* context_temporary_arena(void);
Arena* context_permanent_arena(void);

#define ARENA_SAVEPOINT() \
	size_t LINE_VAR(_saved_size_); \
	DEFER \
	( \
		(LINE_VAR(_saved_size_) = arena_save(context.current_arena)), \
		(arena_clear(context.current_arena), arena_restore(context.current_arena, LINE_VAR(_saved_size_))) \
	)

#define TO_TEMPORARY_ARENA() \
	Arena* LINE_VAR(_saved_arena_) = context.current_arena; \
	DEFER \
	( \
		(context.current_arena = context_temporary_arena()), \
		(context.current_arena = LINE_VAR(_saved_arena_)) \
	)

#define TO_PERMANENT_ARENA() \
	Arena* LINE_VAR(_saved_arena_) = context.current_arena; \
	DEFER \
	( \
		(context.current_arena = context_permanent_arena()), \
		(context.current_arena = LINE_VAR(_saved_arena_)) \
	)

void context_tests(void);

#endif