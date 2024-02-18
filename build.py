#!/usr/bin/env python3

import shutil
import subprocess
from argparse import ArgumentParser
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from subprocess import CalledProcessError, CompletedProcess
from sys import exit

DEFAULT_VCVARSALL_PATH = r'C:\Program Files (x86)\Microsoft Visual Studio 8\VC\vcvarsall.bat'
DEFAULT_7ZIP_PATH = r'7za920\7za.exe'

parser = ArgumentParser(description='Builds the Web Cache Exporter application using Visual Studio.')
parser.add_argument('-mode', choices=['debug', 'release'], help='Build mode.')
parser.add_argument('-vcvarsall_path', type=Path, default=DEFAULT_VCVARSALL_PATH, help='Path to the vcvarsall.bat file.')
parser.add_argument('-package', action='store_true', help='Whether or not to package the built executables.')
parser.add_argument('-7zip_path', dest='_7zip_path', type=Path, default=DEFAULT_7ZIP_PATH, help='Path to the 7-Zip utility used to package the built executables.')
cmd_args = parser.parse_args()

if not cmd_args.vcvarsall_path.exists():
	parser.error(f'The vcvarsall.bat path does not exist: {cmd_args.vcvarsall_path}')

if cmd_args.package and not cmd_args._7zip_path.exists():
	parser.error(f'The 7-Zip executable path does not exist: {cmd_args._7zip_path}')

def run(*args, **kwargs) -> CompletedProcess:
	args = [str(arg) for arg in args]
	print(*args)
	return subprocess.run(args, check=True, **kwargs)

def run_for_arch(arch: str, *args) -> None:
	run(cmd_args.vcvarsall_path, arch, '&&', *args)

try:
	process = run(cmd_args.vcvarsall_path, capture_output=True, text=True)
	is_vs_2005 = 'Visual Studio 2005' in process.stdout
except CalledProcessError:
	exit(1)

try:
	with open('version.txt', encoding='utf-8') as file:
		version = file.read()
	major, minor, patch = version.split('.', 2)
except FileNotFoundError:
	print('Could not find the version file.')
	exit(1)

date = datetime.now(timezone.utc).strftime('%Y-%m-%d')

source_path = Path('Source')
code_path = source_path / 'Code'
labels_path = source_path / 'Labels'
resources_path = source_path / 'Resources'
third_party_path = source_path / 'ThirdParty'

code_files_path = code_path / '*.cpp'
input_resources_path = resources_path / 'resources.rc'

# Disabled warnings:
# - C4127 - constant conditional expression.
# - C4201 - nameless structs and unions.
compiler_args = [
	'/nologo',
	'/W4',
	'/WX', '/wd4127',
	'/WX', '/wd4201',
	'/Oi', '/GR-', '/EHa-',
	'/J',
	'/D', f'WCE_VERSION="{version}"',
	'/D', f'WCE_DATE="{date}"',
	'/D', f'WCE_MODE="{cmd_args.mode}"',
]

if cmd_args.mode == 'debug':
	compiler_args.extend([
		'/Od', '/MTd',
		'/RTC1', '/RTCc',
		'/Zi', '/FC',
		'/D', 'WCE_DEBUG',
	])
else:
	compiler_args.extend(['/O2', '/GL', '/MT'])

library_args = [
	'Kernel32.lib', 'User32.lib', 'Ole32.lib',
	'Shell32.lib', 'Shlwapi.lib', 'Version.lib',
]

# Starting in Visual Studio 2015, the C Run-time Library was refactored into new binaries.
if cmd_args.mode == 'debug':
	library_args.extend(['LIBCMTD.lib', 'LIBCPMTD.lib'])
	if not is_vs_2005:
		library_args.extend(['LIBUCRTD.lib', 'LIBVCRUNTIMED.lib'])
else:
	library_args.extend(['LIBCMT.lib', 'LIBCPMT.lib'])
	if not is_vs_2005:
		library_args.extend(['LIBUCRT.lib', 'LIBVCRUNTIME.lib'])

linker_args = ['/WX', '/NODEFAULTLIB']

if cmd_args.mode == 'debug':
	linker_args.extend(['/DEBUG'])
else:
	linker_args.extend(['/LTCG', '/OPT:REF,ICF'])

resource_args = [
	'/D', f'WCE_MAJOR={major}',
	'/D', f'WCE_MINOR={minor}',
	'/D', f'WCE_PATCH={patch}',
	'/D', 'WCE_BUILD=0',
]

third_party_compiler_args = [
	'/nologo',
	'/w',
	'/Oi', '/GR-', '/EHa-',
]

if cmd_args.mode == 'debug':
	third_party_compiler_args.extend([
		'/Od', '/MTd',
		'/Zi', '/FC',
	])
else:
	third_party_compiler_args.extend(['/O2', '/GL', '/MT'])

third_party_include_args = [
	'/I', third_party_path,
	'/I', third_party_path / 'brotli' / 'include',
	'/I', third_party_path / 'msinttypes',
]

third_party_lib_args = ['/WX', '/NODEFAULTLIB']

if cmd_args.mode == 'debug':
	third_party_lib_args.extend([])
else:
	third_party_lib_args.extend(['/LTCG'])

@dataclass
class Lib:
	name: str
	compiler_args: list[str] = field(default_factory=list, repr=False)
	code_paths: list[Path] = field(default_factory=list, repr=False)

	def __post_init__(self):

		base_path = third_party_path / self.name

		code_paths = set()
		for pattern in ['*.c', '*.cpp']:
			for path in base_path.rglob(pattern):
				code_paths.add(path.parent / pattern)

		self.code_paths = list(code_paths)

libs = [
	Lib(name='brotli', compiler_args=['/TP']),
	Lib(name='sha256', compiler_args=['/TP', '/wd4530']),
	Lib(name='zlib'),
]

@dataclass
class Build:
	name: str
	family: str
	arch: str
	compiler_args: list[str] = field(default_factory=list, repr=False)
	linker_args: list[str] = field(default_factory=list, repr=False)
	resource_args: list[str] = field(default_factory=list, repr=False)

	def __post_init__(self):

		self.compiler_args.extend([
			'/D', f'WCE_FAMILY="{self.family}"',
			'/D', f'WCE_ARCH="{self.arch}"',
		])

		# Remove the /OPT:WIN98 and /OPT:NOWIN98 options since
		# they don't exist in modern Visual Studio versions.
		if self.family == '9x':
			self.compiler_args.extend(['/D', 'WCE_9X'])
			self.resource_args.extend(['/D', 'WCE_9X'])
			if is_vs_2005:
				self.linker_args.extend(['/OPT:WIN98'])
		else:
			if is_vs_2005:
				self.linker_args.extend(['/OPT:NOWIN98'])

		if self.arch == 'x86':
			self.compiler_args.extend(['/D', 'WCE_32_BIT'])
			self.resource_args.extend(['/D', 'WCE_32_BIT'])

builds = [
	Build(name='WCE32', family='NT', arch='x86'),
	Build(name='WCE64', family='NT', arch='x64'),
	Build(name='WCE9x', family='9x', arch='x86'),
]

mode_path = Path('Builds') / cmd_args.mode.title()
libs_path = mode_path / 'Libs'

try:
	shutil.rmtree(mode_path)
except FileNotFoundError:
	pass
except OSError as error:
	print(f'Failed to delete the previous build directory with the error: {repr(error)}')
	exit(1)

mode_path.mkdir(parents=True, exist_ok=True)
libs_path.mkdir(parents=True, exist_ok=True)

archs = [build.arch for build in builds if build.family == 'NT']

for lib in libs:

	filename = lib.name + '.lib'
	library_args.append(filename)

	for arch in archs:

		output_path = libs_path / lib.name / arch
		output_path.mkdir(parents=True, exist_ok=True)

		obj_path = output_path / '*.obj'
		pdb_path = output_path / f'VC_{lib.name}.pdb'
		lib_path = output_path / filename

		try:
			print(f'Compiling {lib.name} ({arch})...')

			run_for_arch(
				arch,

				'cl',
				'/c',
				*third_party_compiler_args,
				*lib.compiler_args,
				*third_party_include_args,
				f'/Fo{output_path}\\',
				f'/Fd{pdb_path}',
				*lib.code_paths,
			)

			print()

			print(f'Creating the {lib.name} ({arch}) static library...')

			run_for_arch(
				arch,

				'lib',
				*third_party_lib_args,
				f'/OUT:{lib_path}',
				obj_path,
			)

			print()

		except CalledProcessError:
			exit(1)

for build in builds:

	if not is_vs_2005 and build.family == '9x':
		continue

	output_path = mode_path / build.family / build.arch
	output_path.mkdir(parents=True, exist_ok=True)

	shutil.copytree(labels_path, output_path / labels_path.name)

	exe_path = output_path / f'{build.name}.exe'
	pdb_path = output_path / f'VC_{build.name}.pdb'
	output_resources_path = output_path / 'resources.res'

	libpath_args = []
	for path in libs_path.rglob(fr'{build.arch}\*.lib'):
		libpath_args.append(f'/LIBPATH:{path.parent}')

	try:
		print(f'Compiling resources for {exe_path.name}...')

		run_for_arch(
			build.arch,

			'rc',
			*resource_args, *build.resource_args,
			'/D', f'WCE_FILENAME="{exe_path.name}"',
			'/FO', output_resources_path,
			input_resources_path,
		)

		print()

		print(f'Compiling {exe_path.name}...')

		run_for_arch(
			build.arch,

			'cl',
      		*compiler_args, *build.compiler_args,
		    *third_party_include_args,
			f'/Fe{exe_path}',
			f'/Fo{output_path}\\',
			f'/Fd{pdb_path}',
      		code_files_path,
			output_resources_path,
			*library_args,
			'/link', *linker_args, *build.linker_args,
			*libpath_args,
		)

		print()

	except CalledProcessError:
		exit(1)

if cmd_args.package:

	print('Packaging the built executables...')

	mode_id = 'debug' if cmd_args.mode == 'debug' else None
	package_ids = ['web', 'cache', 'exporter', version, mode_id]
	package_name = '-'.join(filter(None, package_ids))

	package_path = mode_path / 'Package' / package_name
	package_path.mkdir(parents=True, exist_ok=True)

	copy_paths = []

	for exe_path in mode_path.rglob('*.exe'):
		copy_paths.append(exe_path)
		if cmd_args.mode == 'debug':
			for pdb_path in exe_path.parent.glob('*.pdb'):
				copy_paths.append(pdb_path)

	for path in copy_paths:
		shutil.copy(path, package_path)

	shutil.copytree(labels_path, package_path / labels_path.name)

	if cmd_args.mode == 'debug':
		shutil.copytree('Tests', package_path / 'Tests')

	licenses_path = package_path / 'Licenses'
	licenses_path.mkdir(exist_ok=True)

	for path in third_party_path.rglob('LICENSE'):
		filename = path.parent.name + '.txt'
		shutil.copy(path, licenses_path / filename)

	try:
		readme_template_path = source_path / 'readme.template'
		with open(readme_template_path, encoding='utf-8') as file:
			readme = file.read()
	except FileNotFoundError:
		print('Could not find the readme template.')
		exit(1)

	readme_path = package_path / 'readme.txt'
	with open(readme_path, 'w', encoding='utf-8') as file:
		readme = readme.replace('{version}', version)
		readme = readme.replace('{date}', date)
		file.write(readme)

	zip_path = package_path.parent / (package_path.name + '.zip')
	run(cmd_args._7zip_path, 'a', zip_path.absolute(), '*', '-xr!Large', cwd=package_path)

	sfx_module = '7z.sfx'
	sfx_path = package_path.parent / (package_path.name + '.exe')
	run(cmd_args._7zip_path, 'a', sfx_path.absolute(), package_path.name, f'-sfx{sfx_module}', '-xr!Large', cwd=package_path.parent)

	try:
		shutil.rmtree(package_path)
	except OSError:
		pass