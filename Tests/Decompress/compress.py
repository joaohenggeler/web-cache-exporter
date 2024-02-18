#!/usr/bin/env python3

import gzip
import zlib
from argparse import ArgumentParser
from pathlib import Path

import brotlicffi # type: ignore
import ncompress # type: ignore

def gzip_compress(input_path: Path) -> Path:
	output_path = input_path.with_suffix('.gz')
	with open(input_path, 'rb') as input_file:
		with gzip.open(output_path, 'wb') as output_file:
			output_file.writelines(input_file)
	return output_path

def zlib_compress(input_path: Path) -> Path:
	output_path = input_path.with_suffix('.zz')
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = zlib.compress(input_file.read())
			output_file.write(compressed_data)
	return output_path

def raw_deflate_compress(input_path: Path) -> Path:
	output_path = input_path.with_suffix('.deflate')
	stream = zlib.compressobj(wbits=-zlib.MAX_WBITS)
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = stream.compress(input_file.read())
			compressed_data += stream.flush()
			output_file.write(compressed_data)
	return output_path

def brotli_compress(input_path: Path) -> Path:
	output_path = input_path.with_suffix('.br')
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = brotlicffi.compress(input_file.read())
			output_file.write(compressed_data)
	return output_path

def compress_compress(input_path: Path) -> Path:
	output_path = input_path.with_suffix('.Z')
	with open(input_path, 'rb') as input_file:
		with open(output_path, 'wb') as output_file:
			compressed_data = ncompress.compress(input_file.read())
			output_file.write(compressed_data)
	return output_path

if __name__ == '__main__':

	parser = ArgumentParser(description='Compresses a file using various methods that may show up in the Content-Encoding HTTP header when exporting the web cache.')
	parser.add_argument('path', type=Path, help='The file to compress.')
	args = parser.parse_args()

	gzip_compress(args.path)
	zlib_compress(args.path)
	raw_deflate_compress(args.path)
	brotli_compress(args.path)
	compress_compress(args.path)