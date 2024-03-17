#include "common.h"

#include "zlib/zlib.h"
#include "brotli/decode.h"

static const size_t MIN_OUTPUT_SIZE = 500;

static voidpf zlib_alloc(voidpf opaque, uInt item_count, uInt item_size)
{
	Arena* arena = (Arena*) opaque;
	void* result = arena_push_any(arena, item_count * item_size);
	return (result != NULL) ? (result) : (Z_NULL);
}

#pragma warning(push)
#pragma warning(disable : 4100)
static void zlib_free(voidpf opaque, voidpf address)
{
	// Leak memory since the arena is cleared when we finish decompressing the file.
}
#pragma warning(pop)

static bool zlib_file_decompress(String* path, File_Writer* writer, bool temporary = false)
{
	bool success = false;

	Arena* arena = context.current_arena;

	ARENA_SAVEPOINT()
	{
		// See: https://zlib.net/manual.html
		z_stream stream = {};
		stream.zalloc = zlib_alloc;
		stream.zfree = zlib_free;
		stream.opaque = arena;
		stream.next_in = Z_NULL;

		const size_t MAGIC_COUNT = 2;
		u8 magic[MAGIC_COUNT] = {};
		file_read_first_chunk(path, magic, MAGIC_COUNT, temporary);

		// This assumes 0x78 is the first byte of the Zlib format (DEFLATE with a 32K window).
		bool is_gzip_or_zlib = memory_is_equal(magic, "\x1F\x8B", MAGIC_COUNT)
							|| memory_is_equal(magic, "\x78\x01", MAGIC_COUNT)
							|| memory_is_equal(magic, "\x78\x5E", MAGIC_COUNT)
							|| memory_is_equal(magic, "\x78\x9C", MAGIC_COUNT)
							|| memory_is_equal(magic, "\x78\xDA", MAGIC_COUNT);

		int window_bits = (is_gzip_or_zlib) ? (15 + 32) : (-15);
		int error = inflateInit2(&stream, window_bits);

		#define ERROR_MESSAGE ( (stream.msg != NULL) ? (stream.msg) : ("") )

		if(error == Z_OK)
		{
			File_Reader reader = {};
			reader.temporary = temporary;
			FILE_READ_DEFER(&reader, path)
			{
				// Clamp the capacity so we can cast the read and write sizes to Zlib's u32 type.
				reader.capacity = u32_clamp(reader.capacity);
				size_t buffer_size = MAX(reader.capacity, MIN_OUTPUT_SIZE);
				void* buffer = arena_push(arena, buffer_size, char);

				while(file_read_next(&reader) && error != Z_STREAM_END)
				{
					stream.avail_in = (uInt) reader.size;
					stream.next_in = (Bytef*) reader.data;

					do
					{
						stream.avail_out = (uInt) buffer_size;
						stream.next_out = (Bytef*) buffer;

						error = inflate(&stream, Z_NO_FLUSH);

						if(error != Z_OK && error != Z_STREAM_END)
						{
							log_error("Failed to decompress a chunk from '%s' with the error: %hs (%d)", path->data, ERROR_MESSAGE, error);
							goto break_outer;
						}

						size_t write_size = buffer_size - stream.avail_out;
						if(!file_write_next(writer, buffer, write_size))
						{
							log_error("Failed to write a decompressed chunk to '%s'", writer->path->data);
							goto break_outer;
						}

					} while(stream.avail_out == 0);
				}

				break_outer:;
				success = (error == Z_STREAM_END);
			}

			inflateEnd(&stream);
		}
		else
		{
			log_error("Failed to initialize the stream to decompress '%s' with the error: %hs (%d)", path->data, ERROR_MESSAGE, error);
		}

		#undef ERROR_MESSAGE
	}

	return success;
}

static void* brotli_alloc(void* opaque, size_t size)
{
	Arena* arena = (Arena*) opaque;
	return arena_push_any(arena, size);
}

#pragma warning(push)
#pragma warning(disable : 4100)
static void brotli_free(void* opaque, void* address)
{
	// Leak memory since the arena is cleared when we finish decompressing the file.
}
#pragma warning(pop)

static bool brotli_file_decompress(String* path, File_Writer* writer, bool temporary = false)
{
	bool success = false;

	Arena* arena = context.current_arena;

	ARENA_SAVEPOINT()
	{
		// See: https://brotli.org/decode.html
		BrotliDecoderState* state = BrotliDecoderCreateInstance(brotli_alloc, brotli_free, arena);

		#define ERROR_CODE BrotliDecoderGetErrorCode(state)
		#define ERROR_MESSAGE BrotliDecoderErrorString(ERROR_CODE)

		if(state != NULL)
		{
			File_Reader reader = {};
			reader.temporary = temporary;
			FILE_READ_DEFER(&reader, path)
			{
				size_t buffer_size = MAX(reader.capacity, MIN_OUTPUT_SIZE);
				void* buffer = arena_push(arena, buffer_size, char);

				size_t available_out = buffer_size;
				u8* next_out = (u8*) buffer;

				BrotliDecoderResult result = BROTLI_DECODER_RESULT_ERROR;

				while(file_read_next(&reader) && result != BROTLI_DECODER_RESULT_SUCCESS)
				{
					size_t available_in = reader.size;
					const u8* next_in = (u8*) reader.data;

					do
					{
						result = BrotliDecoderDecompressStream(state, &available_in, &next_in, &available_out, &next_out, NULL);

						if(result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
						{
							// Continue reading.
							break;
						}
						else if(result == BROTLI_DECODER_RESULT_ERROR)
						{
							log_error("Failed to decompress a chunk from '%s' with the error: %hs (%d)", path->data, ERROR_MESSAGE, ERROR_CODE);
							goto break_outer;
						}

						ASSERT(result == BROTLI_DECODER_RESULT_SUCCESS || result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT, "Unexpected Brotli result");

						size_t write_size = buffer_size - available_out;
						if(!file_write_next(writer, buffer, write_size))
						{
							log_error("Failed to write a decompressed chunk to '%s'", writer->path->data);
							goto break_outer;
						}

						// Unlike with Zlib, we only update these values after successfully writing
						// the decompressed data to the file since we would otherwise clobber any
						// partially decompressed output data if the decoder required more input.
						available_out = buffer_size;
						next_out = (u8*) buffer;

					} while(BrotliDecoderHasMoreOutput(state) == BROTLI_TRUE);
				}

				break_outer:;
				success = (result == BROTLI_DECODER_RESULT_SUCCESS);
			}

			ASSERT(success || !BrotliDecoderIsFinished(state), "Mismatched decompression status");
		}
		else
		{
			log_error("Failed to initialize the decoder to decompress '%s' with the error: %hs (%d)", path->data, ERROR_MESSAGE, ERROR_CODE);
		}

		BrotliDecoderDestroyInstance(state);

		#undef ERROR_CODE
		#undef ERROR_MESSAGE
	}

	return success;
}

static bool compress_file_decompress(String* path, File_Writer* writer, bool temporary = false)
{
	// Below are the main sources used to learn how to parse the ncompress Unix utility's file format,
	// and how the Lempel–Ziv–Welch (LZW) compression algorithm works.
	//
	// - LZW algorithm:
	// - http://warp.povusers.org/EfficientLZW/part5.html
	// - https://web.archive.org/web/20120507095719if_/http://marknelson.us/2011/11/08/lzw-revisited
	//
	// - ncompress utility's file format:
	// - https://github.com/vapier/ncompress/blob/main/compress.c
	//
	// - How the ncompress utility adds padding bits when the number of compression bits changes:
	// - https://github.com/vapier/ncompress/issues/5
	// - https://github.com/andrew-aladev/lzws/blob/master/doc/output_compatibility.txt
	// - https://en.wikipedia.org/wiki/Compress#Special_output_format
	//
	// The ncompress utility's source code is one of the most important references since we want to
	// decompress files that were most likely created using this tool. Note also the padding bits
	// references where it's explained that this tool adds extra alignment bits to the compressed
	// bit groups when it generates the compressed data (even though it's not necessary when using
	// the LZW algorithm). Some references use the terms "code" and "string" but we'll refer to these
	// as "index" and "data", respectively.

	bool success = false;

	Arena* arena = context.current_arena;

	ARENA_SAVEPOINT()
	{
		File_Reader reader = {};
		reader.temporary = temporary;

		const size_t MAGIC_COUNT = 3;
		u8 magic[MAGIC_COUNT] = {};
		file_read_first_chunk(path, magic, MAGIC_COUNT, temporary);

		if(!memory_is_equal(magic, "\x1F\x9D", 2))
		{
			log_error("Invalid signature 0x%02X%02X in '%s'", magic[0], magic[1], path->data);
			goto error;
		}

		int max_compression_bits = magic[2] & 0x1F;
		// Whether or not to reset the dictionary when we see the clear index.
		bool block_mode = (magic[2] & 0x80) != 0;

		const int MIN_ALLOWED_COMPRESSION_BITS = 9;
		const int MAX_ALLOWED_COMPRESSION_BITS = 16;

		if(max_compression_bits < MIN_ALLOWED_COMPRESSION_BITS || max_compression_bits > MAX_ALLOWED_COMPRESSION_BITS)
		{
			log_error("The maximum compression bits (%d) in '%s' is out of bounds (%d to %d)", max_compression_bits, path->data, MIN_ALLOWED_COMPRESSION_BITS, MAX_ALLOWED_COMPRESSION_BITS);
			goto error;
		}

		#define MAX_ENTRIES(bit_count) (1 << (bit_count))

		int current_bit_count = MIN_ALLOWED_COMPRESSION_BITS;
		int current_max_entries = MAX_ENTRIES(current_bit_count);

		const int MIN_ALLOWED_DICTIONARY_ENTRIES = 256;
		const int MAX_ALLOWED_DICTIONARY_ENTRIES = MAX_ENTRIES(max_compression_bits);

		const int INITIAL_DICTIONARY_ENTRIES = (block_mode) ? (MIN_ALLOWED_DICTIONARY_ENTRIES + 1) : (MIN_ALLOWED_DICTIONARY_ENTRIES);
		const int NO_INDEX = -1;
		const int CLEAR_INDEX = (block_mode) ? (INITIAL_DICTIONARY_ENTRIES - 1) : (NO_INDEX);

		int current_index = NO_INDEX;
		int previous_index = NO_INDEX;
		int bit_offset = 0;
		int previous_bit_count = current_bit_count;
		int indexes_found_for_bit_count = 0;

		// Skip the file signature and allocate enough space to extract the entry index with BIT_SLICE.
		reader.offset = MAGIC_COUNT;
		reader.min_capacity = sizeof(u32);
		FILE_READ_DEFER(&reader, path)
		{
			size_t buffer_size = MAX(reader.capacity, MIN_OUTPUT_SIZE);
			void* buffer = arena_push(arena, buffer_size, char);

			size_t available_out = buffer_size;
			u8* next_out = (u8*) buffer;

			struct Entry
			{
				// Set to NO_INDEX if there's no prefix, meaning the data is only a single byte.
				int prefix_index;
				u8 value;
			};

			// Allocate the dictionary after the input and output buffers so it's easier to grow it.
			Array<Entry>* dictionary = array_create<Entry>(INITIAL_DICTIONARY_ENTRIES);

			for(int i = 0; i < INITIAL_DICTIONARY_ENTRIES; i += 1)
			{
				Entry entry = {};
				entry.prefix_index = NO_INDEX;
				entry.value = (block_mode && i == CLEAR_INDEX) ? (0) : ((u8) i);
				array_add(&dictionary, entry);
			}

			#define ENTRY_GET(index, data_count_var, first_value_var) \
			do \
			{ \
				ASSERT(!block_mode || index != CLEAR_INDEX, "Invalid entry index"); \
				\
				int entry_idx = index; \
				data_count_var = 0; \
				do \
				{ \
					data_count_var += 1; \
					Entry entry = dictionary->data[entry_idx]; \
					first_value_var = entry.value; \
					entry_idx = entry.prefix_index; \
				} while(entry_idx != NO_INDEX); \
			} while(false)

			#define ENTRY_ADD(new_prefix_index, new_value) \
			do \
			{ \
				/* Do nothing if we reached the maximum. */ \
				if(dictionary->count >= MAX_ALLOWED_DICTIONARY_ENTRIES) break; \
				\
				ASSERT(0 <= new_prefix_index && new_prefix_index < MAX_ALLOWED_DICTIONARY_ENTRIES - 1, "New prefix index is out of bounds"); \
				ASSERT(new_prefix_index != NO_INDEX && new_prefix_index != CLEAR_INDEX, "New prefix index is invalid"); \
				\
				Entry entry = {}; \
				entry.prefix_index = new_prefix_index; \
				entry.value = new_value; \
				array_add(&dictionary, entry); \
				\
				if(dictionary->count >= current_max_entries) \
				{ \
					current_bit_count = MIN(current_bit_count + 1, max_compression_bits); \
					current_max_entries = MAX_ENTRIES(current_bit_count); \
				} \
			} while(false)

			#define OUTPUT_FLUSH() \
			do \
			{ \
				size_t write_size = buffer_size - available_out; \
				if(!file_write_next(writer, buffer, write_size)) \
				{ \
					log_error("Failed to write a decompressed chunk to '%s'", writer->path->data); \
					goto break_outer; \
				} \
				available_out = buffer_size; \
				next_out = (u8*) buffer; \
			} while(false)

			#define ENTRY_OUTPUT(index, data_count) \
			do \
			{ \
				ASSERT(data_count > 0, "Data count is zero"); \
				\
				if(data_count > buffer_size) \
				{ \
					log_error("The entry at %d of length %Iu in '%s' cannot fit in the output buffer of size %Iu", index, data_count, path->data, buffer_size); \
					goto break_outer; \
				} \
				\
				if(data_count > available_out) OUTPUT_FLUSH(); \
				\
				/* The decompressed data must be written backwards. */ \
				int entry_idx = index; \
				u8* reverse_out = next_out + (data_count - 1); \
				do \
				{ \
					Entry entry = dictionary->data[entry_idx]; \
					*reverse_out = entry.value; \
					reverse_out -= 1; \
					entry_idx = entry.prefix_index; \
				} while(entry_idx != NO_INDEX); \
				\
				next_out += data_count; \
				available_out -= data_count; \
			} while(false)

			while(file_read_next(&reader))
			{
				size_t available_in = reader.size;
				const u8* next_in = (u8*) reader.data;

				s64 remaining_bit_count = available_in * CHAR_BIT;
				ASSERT(remaining_bit_count >= current_bit_count, "Not enough data to decompress");

				bool is_last_chunk = (available_in < reader.capacity);
				const u8* after_next_in = (u8*) advance(next_in, available_in);

				while(true)
				{
					ASSERT(CHAR_BIT < current_bit_count && current_bit_count <= max_compression_bits, "Current bit count is out of bounds");
					ASSERT(0 <= bit_offset && bit_offset < CHAR_BIT, "Bit offset is out of bounds");

					// Check if we need more input or if we reached the end.
					if(!is_last_chunk && (remaining_bit_count < sizeof(u32) * CHAR_BIT))
					{
						// Continue reading the input if we don't have enough information
						// to extract the next index. We need to make sure that the input
						// buffer starts at the correct byte. The bit offset is already
						// correct.
						reader.offset -= ptr_diff(after_next_in, next_in);
						break;
					}
					else if(is_last_chunk && remaining_bit_count < current_bit_count)
					{
						// Terminate the decoder.
						break;
					}

					// Skip the extra alignment bits introduced by the ncompress tool as mentioned above.
					if(previous_bit_count != current_bit_count)
					{
						// All the indexes of a given bit size N are aligned to N * CHAR_BIT.
						int padding_bits = ROUND_UP_OFFSET(indexes_found_for_bit_count * previous_bit_count, CHAR_BIT * previous_bit_count);
						next_in += (bit_offset + padding_bits) / CHAR_BIT;
						bit_offset = (bit_offset + padding_bits) % CHAR_BIT;
						remaining_bit_count -= padding_bits;
						indexes_found_for_bit_count = 0;

						// Due to the alignment, this should start at a byte boundary.
						ASSERT(bit_offset == 0, "Misaligned bit offset");

						previous_bit_count = current_bit_count;
						continue;
					}

					previous_bit_count = current_bit_count;

					// See: https://stackoverflow.com/a/4415180
					#define BIT_SLICE(num, lsb_idx, msb_idx) ( ((num) >> (lsb_idx)) & ~(~0 << ((msb_idx) - (lsb_idx) + 1)) )

					// Use 32 bits as a window that is always able to hold an index of the maximum possible size.
					// The worst case is when the bit offset is 7 and the current number of bits is 16. In this case,
					// the index is located between bits 8 and 23.
					u32 next_bits = 0;
					CopyMemory(&next_bits, next_in, sizeof(next_bits));
					current_index = BIT_SLICE(next_bits, bit_offset, bit_offset + current_bit_count - 1);

					#undef BIT_SLICE

					// Locate the index for the next iteration.
					next_in += (bit_offset + current_bit_count) / CHAR_BIT;
					bit_offset = (bit_offset + current_bit_count) % CHAR_BIT;
					remaining_bit_count -= current_bit_count;
					indexes_found_for_bit_count += 1;

					// One index over the limit is allowed by the LZW decoding algorithm.
					if(current_index < 0 || current_index > dictionary->count)
					{
						log_error("The current index %d is out of bounds (0 to %d) in '%s'", current_index, dictionary->count, path->data);
						goto break_outer;
					}

					// Initialization step on the first iteration or after we clear the dictionary.
					if(previous_index == NO_INDEX)
					{
						if(current_index > MIN_ALLOWED_DICTIONARY_ENTRIES - 1)
						{
							log_error("The current index %d is out of bounds (0 to %d) when initializing the previous one in '%s'", current_index, MIN_ALLOWED_DICTIONARY_ENTRIES - 1, path->data);
							goto break_outer;
						}

						ENTRY_OUTPUT(current_index, 1);
						previous_index = current_index;
						continue;
					}

					// Clear the dictionary.
					if(block_mode && current_index == CLEAR_INDEX)
					{
						array_truncate(dictionary, INITIAL_DICTIONARY_ENTRIES);
						current_bit_count = MIN_ALLOWED_COMPRESSION_BITS;
						current_max_entries = MAX_ENTRIES(current_bit_count);
						current_index = NO_INDEX;
						previous_index = NO_INDEX;
						continue;
					}

					ASSERT(current_index != NO_INDEX && current_index != CLEAR_INDEX, "Current index is invalid");
					ASSERT(previous_index != NO_INDEX && previous_index != CLEAR_INDEX, "Previous index is invalid");

					// The LZW decoding algorithm.
					if(current_index < dictionary->count)
					{
						// If the index exists in the dictionary.
						size_t current_data_count = 0;
						u8 current_first_value = 0;
						ENTRY_GET(current_index, current_data_count, current_first_value);
						ENTRY_OUTPUT(current_index, current_data_count);
						ENTRY_ADD(previous_index, current_first_value);
					}
					else
					{
						// If the index does not exist in the dictionary (the KwKwK special case).
						size_t previous_data_count = 0;
						u8 previous_first_value = 0;
						ENTRY_GET(previous_index, previous_data_count, previous_first_value);
						ENTRY_ADD(previous_index, previous_first_value);
						size_t current_data_count = previous_data_count + 1;
						ENTRY_OUTPUT(current_index, current_data_count);
					}

					previous_index = current_index;
				}
			}

			break_outer:;

			if(reader.eof)
			{
				success = true;
				OUTPUT_FLUSH();
			}

			#undef ENTRY_GET
			#undef ENTRY_ADD
			#undef OUTPUT_FLUSH
			#undef ENTRY_OUTPUT
		}

		error:;

		#undef MAX_ENTRIES
	}

	return success;
}

bool decompress_from_content_encoding(String* path, String* content_encoding, File_Writer* writer, bool temporary)
{
	// The Content-Encoding HTTP header contains a list of comma-separated encodings in the order they were applied.
	//
	// We only support the main encodings listed here: https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Encoding
	// To avoid any confusion, here's a brief description of each one:
	//
	// 1. gzip - Gzip file format (RFC 1952) that uses the DEFLATE compression method. Alias: x-gzip.
	//
	// 2. deflate - Zlib data format (RFC 1950) that uses the DEFLATE compression method. Despite the name, this is
	// not supposed to be a raw DEFLATE stream (RFC 1951). Although HTTP 1.1 defines this encoding as the Zlib format,
	// some servers (e.g. Microsoft) transmitted raw DEFLATE data. As such, we'll try to decompress the data using both
	// methods when we see this encoding. See: https://zlib.net/zlib_faq.html#faq39
	//
	// 3. br - Brotli data format (RFC 7932).
	//
	// 4. compress - data compressed using the compress/ncompress Unix utility. Although uncommon nowdays, this format
	// should be handled since we support older browsers. Alias: x-compress.
	//
	// See also:
	// - "Hypertext Transfer Protocol (HTTP) Parameters": https://www.iana.org/assignments/http-parameters/http-parameters.xml#http-parameters-1
	// - "Hypertext Transfer Protocol -- HTTP/1.0": https://datatracker.ietf.org/doc/html/rfc1945#section-3.5
	// - "Hypertext Transfer Protocol -- HTTP/1.1": https://datatracker.ietf.org/doc/html/rfc2616#section-3.5

	bool success = true;

	ARENA_SAVEPOINT()
	{
		Split_State state = {};
		state.str = content_encoding;
		state.delimiters = T(", ");
		state.reverse = true;

		Array<String_View>* encodings = string_split_all(&state);

		if(encodings->count == 0)
		{
			log_warning("Got empty content encoding");

			File_Reader reader = {};
			reader.temporary = temporary;
			FILE_READ_DEFER(&reader, path)
			{
				while(file_read_next(&reader))
				{
					success = file_write_next(writer, reader.data, reader.size);
					if(!success) break;
				}
			}

			success = success && reader.eof;
		}
		else if(encodings->count == 1)
		{
			String_View encoding = encodings->data[0];

			if(string_is_equal(encoding, T("gzip"))
			|| string_is_equal(encoding, T("x-gzip"))
			|| string_is_equal(encoding, T("deflate")))
			{
				success = zlib_file_decompress(path, writer, temporary);
			}
			else if(string_is_equal(encoding, T("br")))
			{
				success = brotli_file_decompress(path, writer, temporary);
			}
			else if(string_is_equal(encoding, T("compress"))
				 || string_is_equal(encoding, T("x-compress")))
			{
				success = compress_file_decompress(path, writer, temporary);
			}
			else
			{
				log_error("Unsupported encoding '%.*s'", encoding.code_count, encoding.data);
				success = false;
			}
		}
		else
		{
			File_Writer even = {};
			File_Writer odd = {};

			TEMPORARY_FILE_DEFER(&even)
			{
				TEMPORARY_FILE_DEFER(&odd)
				{
					String* previous_path = path;

					for(int i = 0; i < encodings->count; i += 1)
					{
						File_Writer* current_writer = NULL;

						if(i == encodings->count - 1) current_writer = writer;
						else if(i % 2 == 0) current_writer = &even;
						else current_writer = &odd;

						ASSERT(!path_is_equal(previous_path, current_writer->path), "Same input and output paths");

						String_View encoding = encodings->data[i];

						if(string_is_equal(encoding, T("gzip"))
						|| string_is_equal(encoding, T("x-gzip"))
						|| string_is_equal(encoding, T("deflate")))
						{
							success = zlib_file_decompress(previous_path, current_writer, TEMPORARY);
						}
						else if(string_is_equal(encoding, T("br")))
						{
							success = brotli_file_decompress(previous_path, current_writer, TEMPORARY);
						}
						else if(string_is_equal(encoding, T("compress"))
							 || string_is_equal(encoding, T("x-compress")))
						{
							success = compress_file_decompress(previous_path, current_writer, TEMPORARY);
						}
						else
						{
							log_error("Unsupported encoding '%.*s' in '%s'", encoding.code_count, encoding.data, content_encoding->data);
							success = false;
						}

						previous_path = current_writer->path;

						if(!success) break;
					}
				}
			}

			success = success && even.opened && odd.opened;
		}
	}

	return success;
}

void decompress_tests(void)
{
	console_info("Running decompress tests");
	log_info("Running decompress tests");

	{
		#define TEST_DECOMPRESS(in_name, encoding, expected_name) \
			do \
			{ \
				ARENA_SAVEPOINT() \
				{ \
					String* in_path = string_from_c(T("Tests\\Decompress\\") T(in_name)); \
					String* expected_path = string_from_c(T("Tests\\Decompress\\") T(expected_name)); \
					\
					File_Writer writer = {}; \
					TEMPORARY_FILE_DEFER(&writer) \
					{ \
						bool success = false; \
						\
						success = decompress_from_content_encoding(in_path, CSTR(encoding), &writer); \
						TEST(success, true); \
						\
						File out_file = {}; \
						success = file_read_all(writer.path, &out_file, writer.temporary); \
						TEST(success, true); \
						\
						File expected_file = {}; \
						success = file_read_all(expected_path, &expected_file); \
						TEST(success, true); \
						TEST(out_file.size, expected_file.size); \
						TEST(memory_is_equal(out_file.data, expected_file.data, expected_file.size), true); \
					} \
					TEST(writer.opened, true); \
				} \
			} while(false)

		TEST_DECOMPRESS("File\\file.txt.gz", "gzip", "File\\file.txt");
		TEST_DECOMPRESS("File\\file.txt.zz", "deflate", "File\\file.txt");
		TEST_DECOMPRESS("File\\file.txt.deflate", "deflate", "File\\file.txt");
		TEST_DECOMPRESS("File\\file.txt.br", "br", "File\\file.txt");
		TEST_DECOMPRESS("File\\file.txt.Z", "compress", "File\\file.txt");
		TEST_DECOMPRESS("File\\file.txt.zz.gz", "deflate, gzip", "File\\file.txt");
		TEST_DECOMPRESS("File\\file.txt", "", "File\\file.txt");

		TEST_DECOMPRESS("Empty\\empty.txt.gz", "gzip", "Empty\\empty.txt");
		TEST_DECOMPRESS("Empty\\empty.txt.zz", "deflate", "Empty\\empty.txt");
		TEST_DECOMPRESS("Empty\\empty.txt.deflate", "deflate", "Empty\\empty.txt");
		TEST_DECOMPRESS("Empty\\empty.txt.br", "br", "Empty\\empty.txt");
		TEST_DECOMPRESS("Empty\\empty.txt.Z", "compress", "Empty\\empty.txt");
		TEST_DECOMPRESS("Empty\\empty.txt.zz.gz", "deflate, gzip", "Empty\\empty.txt");
		TEST_DECOMPRESS("Empty\\empty.txt", "", "Empty\\empty.txt");

		if(context.large_tests)
		{
			TEST_DECOMPRESS("Large\\large.jpg.gz", "gzip", "Large\\large.jpg");
			TEST_DECOMPRESS("Large\\large.jpg.zz", "deflate", "Large\\large.jpg");
			TEST_DECOMPRESS("Large\\large.jpg.deflate", "deflate", "Large\\large.jpg");
			TEST_DECOMPRESS("Large\\large.jpg.br", "br", "Large\\large.jpg");
			TEST_DECOMPRESS("Large\\large.jpg.Z", "compress", "Large\\large.jpg");
			TEST_DECOMPRESS("Large\\large.jpg.zz.gz", "deflate, gzip", "Large\\large.jpg");
			TEST_DECOMPRESS("Large\\large.jpg", "", "Large\\large.jpg");
		}

		#undef TEST_DECOMPRESS
	}
}