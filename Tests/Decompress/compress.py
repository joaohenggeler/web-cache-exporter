#!/usr/bin/env python3

import gzip
import zlib
from argparse import ArgumentParser
from pathlib import Path

import brotlicffi # type: ignore
import ncompress # type: ignore

def gzip_compress(in_path: Path) -> Path:
	out_path = in_path.with_suffix(in_path.suffix + '.gz')
	with open(in_path, 'rb') as in_file:
		with gzip.open(out_path, 'wb') as out_file:
			out_file.writelines(in_file)
	return out_path

def zlib_compress(in_path: Path) -> Path:
	out_path = in_path.with_suffix(in_path.suffix + '.zz')
	with open(in_path, 'rb') as in_file:
		with open(out_path, 'wb') as out_file:
			data = zlib.compress(in_file.read())
			out_file.write(data)
	return out_path

def deflate_compress(in_path: Path) -> Path:
	out_path = in_path.with_suffix(in_path.suffix + '.deflate')
	stream = zlib.compressobj(wbits=-zlib.MAX_WBITS)
	with open(in_path, 'rb') as in_file:
		with open(out_path, 'wb') as out_file:
			data = stream.compress(in_file.read())
			data += stream.flush()
			out_file.write(data)
	return out_path

def brotli_compress(in_path: Path) -> Path:
	out_path = in_path.with_suffix(in_path.suffix + '.br')
	with open(in_path, 'rb') as in_file:
		with open(out_path, 'wb') as out_file:
			data = brotlicffi.compress(in_file.read())
			out_file.write(data)
	return out_path

def compress_compress(in_path: Path) -> Path:
	out_path = in_path.with_suffix(in_path.suffix + '.Z')
	with open(in_path, 'rb') as in_file:
		with open(out_path, 'wb') as out_file:
			data = ncompress.compress(in_file.read())
			out_file.write(data)
	return out_path

if __name__ == '__main__':

	parser = ArgumentParser(description='Compresses a file using various methods that may show up in the Content-Encoding HTTP header when exporting the web cache.')
	parser.add_argument('path', type=Path, help='The file to compress.')
	parser.add_argument('method', choices=['gzip', 'zlib', 'deflate', 'brotli', 'compress', 'all'], help='The compression method.')
	args = parser.parse_args()

	if args.method in ['gzip', 'all']:
		gzip_compress(args.path)

	if args.method in ['zlib', 'all']:
		zlib_compress(args.path)

	if args.method in ['deflate', 'all']:
		deflate_compress(args.path)

	if args.method in ['brotli', 'all']:
		brotli_compress(args.path)

	if args.method in ['compress', 'all']:
		compress_compress(args.path)