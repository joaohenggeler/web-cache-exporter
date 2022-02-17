#!/usr/bin/env python3

"""
	Compresses a given file using various methods that may show up in the Content-Encoding HTTP header
	when exporting the web cache. Used to test the Web Cache Exporter's decompression functions.

	Usage:
		compress.py <File Path>

	Example:
		compress.py "./test.png"

	Dependencies:
		Brotli: https://github.com/python-hyper/brotlipy/
		Compress: https://github.com/valgur/ncompress
"""

import bz2
import gzip
import sys
import zlib

import brotlicffi
import ncompress

####################################################################################################

script_name = sys.argv[0]
num_args = len(sys.argv) - 1

if num_args != 1:
	print(f'Wrong number of arguments. Usage: {script_name} <File Path>')
	sys.exit(1)

def gzip_compress(input_path: str) -> str:
	print(f'Gzip compressing "{input_path}..."')
	output_path = input_path + '.gz'
	with open(input_path, 'rb') as input_file:
		with gzip.open(output_path, 'wb') as output_file:
			output_file.writelines(input_file)
	return output_path

def zlib_compress(input_path: str) -> str:
	print(f'Zlib compressing "{input_path}..."')
	output_path = input_path + '.zz'
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = zlib.compress(input_file.read())
			output_file.write(compressed_data)
	return output_path

def raw_deflate_compress(input_path: str) -> str:
	print(f'DEFLATE compressing "{input_path}..."')
	output_path = input_path + '.deflate'
	stream = zlib.compressobj(wbits=-zlib.MAX_WBITS)
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = stream.compress(input_file.read())
			compressed_data += stream.flush()
			output_file.write(compressed_data)
	return output_path

def brotli_compress(input_path: str) -> str:
	print(f'Brotli compressing "{input_path}..."')
	output_path = input_path + '.br'
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = brotlicffi.compress(input_file.read())
			output_file.write(compressed_data)
	return output_path

def compress_compress(input_path: str) -> str:
	print(f'Compress compressing "{input_path}..."')
	output_path = input_path + '.Z'
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = ncompress.compress(input_file.read())
			output_file.write(compressed_data)
	return output_path

def bzip2_compress(input_path: str) -> str:
	print(f'Bzip2 compressing "{input_path}..."')
	output_path = input_path + '.bz2'
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = bz2.compress(input_file.read())
			output_file.write(compressed_data)
	return output_path

file_path = sys.argv[1]
gzip_compress(file_path)
zlib_compress(file_path)
raw_deflate_compress(file_path)
brotli_compress(file_path)
compress_compress(file_path)
bzip2_compress(file_path)

print()

all_formats_path = file_path
all_formats_path = gzip_compress(all_formats_path)
all_formats_path = zlib_compress(all_formats_path)
all_formats_path = raw_deflate_compress(all_formats_path)
all_formats_path = brotli_compress(all_formats_path)
all_formats_path = compress_compress(all_formats_path)
all_formats_path = bzip2_compress(all_formats_path)

print()

print('Finished running.')
