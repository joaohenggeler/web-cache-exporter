@ECHO OFF

REM This script is used to build the application on Windows using Visual Studio.
REM It defines any useful macros (like the version string and the Windows 98/ME build macro), and sets the
REM correct compiler and linker options. Extra compiler options may be added via the command line.
REM
REM Usage: Build.bat [Optional Compiler Arguments]
REM
REM For example:
REM - Build.bat
REM - Build.bat /D EXPORT_EMPTY_FILES
REM
REM Macros that can be used in the debug builds:
REM - EXPORT_EMPTY_FILES, which tells the application to create empty files instead of copying the real cached files.
REM This is useful for testing the program without having to wait for Windows to copy each file.
REM
REM Macros that are set by this batch file:
REM - BUILD_DEBUG, defined when building in debug mode, undefined when building in release mode.
REM - BUILD_9X, defined when building the Windows 98/ME version, undefined when building the Windows 2000 to 10 version. 
REM - BUILD_32_BIT, defined when building the 32-bit version, undefined when building the 64-bit version.
REM - BUILD_VERSION, defined as the application's version string in the form of "MAJOR.MINOR.PATCH". Note that this project
REM does *not* use semantic versioning. Defined as "0.0.0" if the version cannot be read from the "version.txt" file.
REM - BUILD_TARGET, defined as the application's build target string (9x vs NT, architecture). For example, "NT-x86".
REM - BUILD_BIG_ENDIAN, defined when targeting a big endian architecture, undefined when targeting a little endian architecture.
REM
REM If BUILD_DEBUG, BUILD_9X, BUILD_32_BIT, and BUILD_BIG_ENDIAN are not defined when compiling the source code, then the release
REM x64 version is built. If BUILD_VERSION and BUILD_TARGET are not defined, they'll default to "0.0.0" and "?", respectively.

SETLOCAL
PUSHD "%~dp0"
	
	CLS

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Basic Build Parameters
	REM ------------------------------------------------------------

	REM Any parameters where the value can be true or false are set to either "Yes" or "No". During development and when
	REM packaging a new release, these are set to "Yes" and VCVARSALL_PATH is set to Visual Studio 2005 Professional:
	REM - "C:\Program Files (x86)\Microsoft Visual Studio 8\VC\vcvarsall.bat"
	REM
	REM Visual Studio 2019, for example, uses the following:
	REM - "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"

	REM The build mode.
	REM - debug - turns off optimizations, enables run-time error checks, generates debug information, and defines
	REM certain macros (like BUILD_DEBUG).
	REM - release - turns on optimizations, disables any debug features and macros, and puts all the different executable
	REM versions in the same release directory.
	SET "BUILD_MODE=debug"

	REM Set to "Yes" to delete all the build directories before compiling.
	SET "CLEAN_BUILD=Yes"

	REM Set to "Yes" to also build the Windows 98/ME version.
	SET "WIN9X_BUILD=Yes"

	REM Set to "Yes" to use certain compiler and linker options that were removed after Visual Studio 2005.
	REM If this value is "No", it's assumed that a more modern of Visual Studio (2015 or later) is specified in VCVARSALL_PATH.
	REM In this case, WIN9X_BUILD is automatically set to "No" and the Windows 98/ME version is not built.
	SET "USE_VS_2005_OPTIONS=Yes"

	REM Set to "Yes" to compile and link the resource file. This will add an icon and version information to the executable.
	SET "COMPILE_RESOURCES=Yes"

	REM Set to "Yes" to use 7-Zip to compress the executables and source code and create two archives.
	REM Note that this requires that the _7ZIP_EXE_PATH variable (see below) points to the correct executable.
	REM In our case, we use 7-Zip 9.20 Command Line Version.
	SET "PACKAGE_BUILD=Yes"
	
	REM The absolute path to the vcvarsall.bat batch file that is installed with Visual Studio.
	REM You can use this to change the compiler version, although this application hasn't been thoroughly tested with more
	REM modern Visual Studio versions.
	SET "VCVARSALL_PATH=C:\Program Files (x86)\Microsoft Visual Studio 8\VC\vcvarsall.bat"

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ VCVARSALL.bat Parameters
	REM ------------------------------------------------------------

	REM Where to look for the source files.
	SET "SOURCE_PATH=%~dp0Source"
	SET "SOURCE_CODE_FILES="%SOURCE_PATH%\*.cpp""
	
	SET "GROUP_FILES_DIR=Groups"
	SET "SOURCE_GROUP_FILES_DIR=%SOURCE_PATH%\%GROUP_FILES_DIR%"
	
	SET "EXTERNAL_LOCATIONS_DIR=ExternalLocations"
	SET "SOURCE_EXTERNAL_LOCATIONS_DIR=%SOURCE_PATH%\%EXTERNAL_LOCATIONS_DIR%"
	
	SET "SCRIPTS_DIR=Scripts"
	SET "SOURCE_SCRIPTS_DIR=%SOURCE_PATH%\%SCRIPTS_DIR%"

	REM Where to look for third-party source files, headers, and libraries.
	REM - /I "<Path>" - third-party header files for the compiler.
	REM - /LIBPATH:"<Path>" - third-party static library files for the linker.
	SET "THIRD_PARTY_BASE_PATH=%SOURCE_PATH%\ThirdParty"
	SET "THIRD_PARTY_LIB_PATH=%THIRD_PARTY_BASE_PATH%\StaticLibraries"
	
	SET "ESENT_INCLUDE_PATH=%THIRD_PARTY_BASE_PATH%\ESENT"
	
	SET "ZLIB_PATH=%THIRD_PARTY_BASE_PATH%\Zlib"
	SET "ZLIB_CODE_FILES="%ZLIB_PATH%\*.c""
	SET "ZLIB_LIB_FILENAME_X86=zlib_x86.lib"
	SET "ZLIB_LIB_FILENAME_X64=zlib_x64.lib"

	SET "BROTLI_PATH=%THIRD_PARTY_BASE_PATH%\Brotli"
	SET "BROTLI_CODE_FILES="%BROTLI_PATH%\common\*.c" "%BROTLI_PATH%\dec\*.c""
	SET "BROTLI_INCLUDE_PATH=%BROTLI_PATH%\include"
	SET "BROTLI_LIB_FILENAME_X86=brotli_x86.lib"
	SET "BROTLI_LIB_FILENAME_X64=brotli_x64.lib"

	REM Common compiler options and any other ones that only apply to a specific build target or mode.
	REM Disabled warnings:
	REM - C4100 - unused function parameter.
	REM
	REM  _ALLOW_RTCc_IN_STL is defined to suppress the warning that shows up when you use /RTCc with later versions
	REM of Visual Studio (/RTCc rejects conformant code, so it is not supported by the C++ Standard Library).
	SET "COMPILER_OPTIONS=/W4 /WX /wd4100 /Oi /GR- /EHa- /nologo /I "%THIRD_PARTY_BASE_PATH%" /I "%ESENT_INCLUDE_PATH%" /I "%BROTLI_INCLUDE_PATH%""
	SET "COMPILER_OPTIONS_RELEASE=/O2 /MT /GL"
	SET "COMPILER_OPTIONS_DEBUG=/Od /MTd /RTC1 /RTCc /Zi /FC /D BUILD_DEBUG /D _ALLOW_RTCc_IN_STL"

	SET "THIRD_PARTY_LIB_COMPILER_OPTIONS=/c /w /Oi /GR- /EHa- /nologo /I "%BROTLI_INCLUDE_PATH%""
	SET "THIRD_PARTY_LIB_COMPILER_OPTIONS_RELEASE=/O2 /MT /GL"
	SET "THIRD_PARTY_LIB_COMPILER_OPTIONS_DEBUG=/Od /MTd /RTC1 /Zi /FC"

	REM The BUILD_BIG_ENDIAN option isn't currently used by any build target, but we'll mention it here if that changes
	REM in the future. By default, we'll assume that we're targeting a little endian architecture.
	SET "COMPILER_OPTIONS_9X_X86=/D BUILD_9X /D BUILD_32_BIT /D BUILD_TARGET=\"9x-x86\""
	SET "COMPILER_OPTIONS_NT_X86=/D BUILD_32_BIT /D BUILD_TARGET=\"NT-x86\""
	SET "COMPILER_OPTIONS_NT_X64=/D BUILD_TARGET=\"NT-x64\""
	
	REM Statically link with the required libraries. Note that we tell the linker not to use any default libraries.
	SET "LIBRARIES=Kernel32.lib Advapi32.lib Shell32.lib Shlwapi.lib Version.lib"
	SET "LIBRARIES_RELEASE=LIBCMT.lib LIBCPMT.lib"
	SET "LIBRARIES_DEBUG=LIBCMTD.lib LIBCPMTD.lib"
	SET "LIBRARIES_X86=%ZLIB_LIB_FILENAME_X86% %BROTLI_LIB_FILENAME_X86%"
	SET "LIBRARIES_X64=%ZLIB_LIB_FILENAME_X64% %BROTLI_LIB_FILENAME_X64%"

	REM Common linker options and any other ones that only apply to a specific build target or mode.
	REM We used to statically link to ESENT.lib in the Windows 2000 to 10 (NT) builds.
	SET "LINKER_OPTIONS=/link /WX /NODEFAULTLIB /LIBPATH:"%THIRD_PARTY_LIB_PATH%""
	SET "LINKER_OPTIONS_RELEASE=/LTCG /OPT:REF,ICF"
	SET "LINKER_OPTIONS_DEBUG=/DEBUG"

	SET "THIRD_PARTY_LIB_LINKER_OPTIONS=/WX /NODEFAULTLIB"
	SET "THIRD_PARTY_LIB_LINKER_OPTIONS_RELEASE=/LTCG"
	SET "THIRD_PARTY_LIB_LINKER_OPTIONS_DEBUG="	

	SET "LINKER_OPTIONS_9X=/OPT:WIN98"
	SET "LINKER_OPTIONS_NT=/OPT:NOWIN98"
	
	REM The names of the resulting executable files.
	SET "EXE_FILENAME_9X_X86=WCE9x32.exe"
	SET "EXE_FILENAME_NT_X86=WCE32.exe"
	SET "EXE_FILENAME_NT_X64=WCE64.exe"
	
	REM The paths and filenames to the uncompiled and compiled resource files.
	SET "RESOURCE_FILE_PATH=%SOURCE_PATH%\Resources\resources.rc"
	SET "COMPILED_RESOURCE_FILENAME=resources.res"

	REM Resource compiler options that only apply to a specific build target.
	SET "RESOURCE_OPTIONS_9X_X86=/D BUILD_9X /D BUILD_32_BIT"
	SET "RESOURCE_OPTIONS_NT_X86=/D BUILD_32_BIT"
	SET "RESOURCE_OPTIONS_NT_X64="
	
	REM Modify or add options that depend on the Visual Studio version.
	
	IF "%USE_VS_2005_OPTIONS%" NEQ "Yes" (
		ECHO [%~nx0] Using compiler and linker options for Visual Studio 2015 or later.
		ECHO.

		SET "WIN9X_BUILD=No"

		REM Remove the /OPT:WIN98 and /OPT:NOWIN98 options since they don't exist in modern Visual Studio versions.
		SET "LINKER_OPTIONS_9X=%LINKER_OPTIONS_9X:/OPT:WIN98=%"
		SET "LINKER_OPTIONS_NT=%LINKER_OPTIONS_NT:/OPT:NOWIN98=%"

		REM Starting in Visual Studio 2015, the C Run-time Library was refactored into new binaries
		SET "LIBRARIES_RELEASE=%LIBRARIES_RELEASE% LIBUCRT.lib LIBVCRUNTIME.lib"
		SET "LIBRARIES_DEBUG=%LIBRARIES_DEBUG% LIBUCRTD.lib LIBVCRUNTIMED.lib"
	)

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ File And Directory Locations
	REM ------------------------------------------------------------

	SET "MAIN_BUILD_PATH=%~dp0Builds"

	SET "RELEASE_BUILD_PATH=%MAIN_BUILD_PATH%\Release"

	SET "DEBUG_BUILD_PATH=%MAIN_BUILD_PATH%\Debug"
	SET "DEBUG_BUILD_PATH_9X_X86=%DEBUG_BUILD_PATH%\Build-9x-x86"
	SET "DEBUG_BUILD_PATH_NT_X86=%DEBUG_BUILD_PATH%\Build-NT-x86"
	SET "DEBUG_BUILD_PATH_NT_X64=%DEBUG_BUILD_PATH%\Build-NT-x64"
	
	REM The location of the compressed archives.
	SET "BUILD_ARCHIVE_DIR=Archives"
	SET "BUILD_ARCHIVE_PATH=%MAIN_BUILD_PATH%\%BUILD_ARCHIVE_DIR%"
	REM The path to the 7-Zip executable.
	SET "_7ZIP_EXE_PATH=_7za920\7za.exe"

	REM The location of any files that should be packaged in the source archive.
	SET "BATCH_FILE_PATH=%~dpnx0"
	SET "VERSION_FILE_PATH=%~dp0version.txt"
	SET "README_BODY_PATH=%~dp0readme_body.txt"
	SET "LICENSE_FILE_PATH=%~dp0LICENSE"
	SET "BUILDING_FILE_PATH=%~dp0Building.txt"

	REM The location of the final release README. This file is generated using the version, body template,
	REM and license files above. These files must only use ASCII characters and use CRLF for newlines.
	SET "RELEASE_README_PATH=%RELEASE_BUILD_PATH%\readme.txt"

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Set Build Mode Options And Delete Previous Directories
	REM ------------------------------------------------------------

	REM Get the build version from our file or default to an unknown one.
	SET BUILD_VERSION=
	IF EXIST "%VERSION_FILE_PATH%" (
		SET /P BUILD_VERSION=<"%VERSION_FILE_PATH%"
	) ELSE (
		ECHO [%~nx0] The version file was not found. A placeholder value will be used instead.
		ECHO.
		SET "BUILD_VERSION=0.0.0"
	)

	REM Extract the individual version numbers from the version string...
	SET "MAJOR_VERSION="
	SET "MINOR_VERSION="
	SET "PATCH_VERSION="
	SET "BUILD_NUMBER=0"
	FOR /F "tokens=1-3 delims=." %%I IN ('ECHO %BUILD_VERSION%') DO (
		SET "MAJOR_VERSION=%%I"
		SET "MINOR_VERSION=%%J"
		SET "PATCH_VERSION=%%K"
	)

	REM ...so we can pass them to the resource compiler.
	SET "RESOURCE_VERSION_OPTIONS=/D MAJOR_VERSION=%MAJOR_VERSION% /D MINOR_VERSION=%MINOR_VERSION% /D PATCH_VERSION=%PATCH_VERSION% /D BUILD_NUMBER=%BUILD_NUMBER%"

	REM Check if the build mode is valid and add the previously specified compiler and linkers options, and static libraries. 
	IF "%BUILD_MODE%"=="debug" (

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_DEBUG% /D BUILD_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_DEBUG%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_DEBUG%"

		SET "THIRD_PARTY_LIB_COMPILER_OPTIONS=%THIRD_PARTY_LIB_COMPILER_OPTIONS% %THIRD_PARTY_LIB_COMPILER_OPTIONS_DEBUG%"
		SET "THIRD_PARTY_LIB_LINKER_OPTIONS=%THIRD_PARTY_LIB_LINKER_OPTIONS% %THIRD_PARTY_LIB_LINKER_OPTIONS_DEBUG%"

		SET "BUILD_PATH_9X_X86=%DEBUG_BUILD_PATH_9X_X86%"
		SET "BUILD_PATH_NT_X86=%DEBUG_BUILD_PATH_NT_X86%"
		SET "BUILD_PATH_NT_X64=%DEBUG_BUILD_PATH_NT_X64%"

	) ELSE IF "%BUILD_MODE%"=="release" (

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_RELEASE% /D BUILD_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_RELEASE%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_RELEASE%"

		SET "THIRD_PARTY_LIB_COMPILER_OPTIONS=%THIRD_PARTY_LIB_COMPILER_OPTIONS% %THIRD_PARTY_LIB_COMPILER_OPTIONS_RELEASE%"
		SET "THIRD_PARTY_LIB_LINKER_OPTIONS=%THIRD_PARTY_LIB_LINKER_OPTIONS% %THIRD_PARTY_LIB_LINKER_OPTIONS_RELEASE%"

		SET "BUILD_PATH_9X_X86=%RELEASE_BUILD_PATH%"
		SET "BUILD_PATH_NT_X86=%RELEASE_BUILD_PATH%"
		SET "BUILD_PATH_NT_X64=%RELEASE_BUILD_PATH%"

	) ELSE (
		ECHO [%~nx0] Unknown build mode "%BUILD_MODE%".
		EXIT /B 1
	)

	REM Delete the previous builds.
	IF "%CLEAN_BUILD%"=="Yes" (

		ECHO [%~nx0] Deleting the previous builds in "%MAIN_BUILD_PATH%"...
		ECHO.

		IF EXIST "%MAIN_BUILD_PATH%" (
			REM Delete everything except .sln and .suo Visual Studio project files, and the compressed archives directory.
			MKDIR "TemporaryDirectory" >NUL
			ROBOCOPY "%MAIN_BUILD_PATH%" "TemporaryDirectory" /MOVE /S /XF "*.sln" "*.suo" /XD "%BUILD_ARCHIVE_DIR%" >NUL
			RMDIR /S /Q "TemporaryDirectory"
		)

		ECHO [%~nx0] Deleting the previous static libraries in "%THIRD_PARTY_LIB_PATH%"...
		ECHO.

		IF EXIST "%THIRD_PARTY_LIB_PATH%" (
			RMDIR /S /Q "%THIRD_PARTY_LIB_PATH%"
		)
	)

	IF NOT EXIST "%THIRD_PARTY_LIB_PATH%" (
		MKDIR "%THIRD_PARTY_LIB_PATH%"
	)

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Build Third-Party Static Libraries
	REM ------------------------------------------------------------

	PUSHD "%THIRD_PARTY_LIB_PATH%"

		DEL /Q "%THIRD_PARTY_LIB_PATH%\*.obj" >NUL 2>&1

		REM ------------------------------------------------------------ Zlib x86

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x86
			
			ECHO.
			ECHO [%~nx0] Building the static library "%ZLIB_LIB_FILENAME_X86%"...

			@ECHO ON
			cl %THIRD_PARTY_LIB_COMPILER_OPTIONS% /Fd"VC_%ZLIB_LIB_FILENAME_X86%.pdb" %ZLIB_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%ZLIB_LIB_FILENAME_X86%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%THIRD_PARTY_LIB_PATH%\*.obj" /OUT:"%ZLIB_LIB_FILENAME_X86%" %THIRD_PARTY_LIB_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%THIRD_PARTY_LIB_PATH%\*.obj"

		REM ------------------------------------------------------------ Zlib x64

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x64
			
			ECHO.
			ECHO [%~nx0] Building the static library "%ZLIB_LIB_FILENAME_X64%"...

			@ECHO ON
			cl %THIRD_PARTY_LIB_COMPILER_OPTIONS% /Fd"VC_%ZLIB_LIB_FILENAME_X64%.pdb" %ZLIB_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%ZLIB_LIB_FILENAME_X64%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%THIRD_PARTY_LIB_PATH%\*.obj" /OUT:"%ZLIB_LIB_FILENAME_X64%" %THIRD_PARTY_LIB_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%THIRD_PARTY_LIB_PATH%\*.obj"

		REM ------------------------------------------------------------ Brotli x86

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x86
			
			ECHO.
			ECHO [%~nx0] Building the static library "%BROTLI_LIB_FILENAME_X86%"...

			@ECHO ON
			cl %THIRD_PARTY_LIB_COMPILER_OPTIONS% /Fd"VC_%BROTLI_LIB_FILENAME_X86%.pdb" %BROTLI_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%BROTLI_LIB_FILENAME_X86%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%THIRD_PARTY_LIB_PATH%\*.obj" /OUT:"%BROTLI_LIB_FILENAME_X86%" %THIRD_PARTY_LIB_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%THIRD_PARTY_LIB_PATH%\*.obj"

		REM ------------------------------------------------------------ Brotli x64

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x64
			
			ECHO.
			ECHO [%~nx0] Building the static library "%BROTLI_LIB_FILENAME_X64%"...

			@ECHO ON
			cl %THIRD_PARTY_LIB_COMPILER_OPTIONS% /Fd"VC_%BROTLI_LIB_FILENAME_X64%.pdb" %BROTLI_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%BROTLI_LIB_FILENAME_X64%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%THIRD_PARTY_LIB_PATH%\*.obj" /OUT:"%BROTLI_LIB_FILENAME_X64%" %THIRD_PARTY_LIB_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%THIRD_PARTY_LIB_PATH%\*.obj"

	POPD

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Compile The Program
	REM ------------------------------------------------------------

	ECHO [%~nx0] Building in %BUILD_MODE% mode...
	ECHO.

	REM Add any remaining command line arguments to the compiler options.
	SET "COMMAND_LINE_COMPILER_OPTIONS=%*"
	IF "%COMMAND_LINE_COMPILER_OPTIONS%" NEQ "" (
		ECHO [%~nx0] Passing extra compiler options from the command line: "%COMMAND_LINE_COMPILER_OPTIONS%"...
		ECHO.
		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMMAND_LINE_COMPILER_OPTIONS%"
	)

	REM Build each target by calling the vcvarsall.bat batch file with the correct architecture.
	REM We'll stop the building process if a single target doesn't compile or link successfully.

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Windows NT x86 Build
	REM ------------------------------------------------------------

	ECHO [%~nx0] Compiling The Windows NT x86 %BUILD_MODE% build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_PATH_NT_X86%" (
		MKDIR "%BUILD_PATH_NT_X86%"
	)

	SETLOCAL
		PUSHD "%BUILD_PATH_NT_X86%"

			ECHO [%~nx0] Copying the source group files...
			ROBOCOPY "%SOURCE_GROUP_FILES_DIR%" ".\%GROUP_FILES_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%SOURCE_EXTERNAL_LOCATIONS_DIR%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source scripts...
			ROBOCOPY "%SOURCE_SCRIPTS_DIR%" ".\%SCRIPTS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_NT_X86% /Fe"%EXE_FILENAME_NT_X86%" /Fd"VC_%EXE_FILENAME_NT_X86%.pdb""
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_NT%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_X86%"

			CALL "%VCVARSALL_PATH%" x86

			SET "COMPILED_RESOURCE_PATH="
			IF "%COMPILE_RESOURCES%" NEQ "Yes" (
				GOTO SKIP_RESOURCES_NT_X86
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_NT_X86%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_NT_X86%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_NT_X86%\" %RESOURCE_OPTIONS_NT_X86%"
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCE_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_NT_X86
			
			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_NT_X86%"...

			@ECHO ON
			cl %COMPILER_OPTIONS% %SOURCE_CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF
			
		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_NT_X86%".
		EXIT /B 1
	)

	IF "%BUILD_MODE%"=="release" (
		ECHO.
		ECHO [%~nx0] Deleting any *.obj and *.res files...

		DEL /Q "%RELEASE_BUILD_PATH%\*.obj"
		DEL /Q "%RELEASE_BUILD_PATH%\*.res"
	)

	ECHO.
	ECHO.
	ECHO.

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Windows NT x64 Build
	REM ------------------------------------------------------------

	ECHO [%~nx0] Compiling The Windows NT x64 %BUILD_MODE% build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_PATH_NT_X64%" (
		MKDIR "%BUILD_PATH_NT_X64%"
	)

	SETLOCAL
		PUSHD "%BUILD_PATH_NT_X64%"

			ECHO [%~nx0] Copying the source group files...
			ROBOCOPY "%SOURCE_GROUP_FILES_DIR%" ".\%GROUP_FILES_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%SOURCE_EXTERNAL_LOCATIONS_DIR%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source scripts...
			ROBOCOPY "%SOURCE_SCRIPTS_DIR%" ".\%SCRIPTS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_NT_X64% /Fe"%EXE_FILENAME_NT_X64%" /Fd"VC_%EXE_FILENAME_NT_X64%.pdb""
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_NT%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_X64%"

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_NT_X64%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" /D WCE_EXE_FILENAME=\"%EXE_FILENAME_NT_X64%\" %RESOURCE_OPTIONS_NT_X64%"
			
			CALL "%VCVARSALL_PATH%" x64

			SET "COMPILED_RESOURCE_PATH="
			IF "%COMPILE_RESOURCES%" NEQ "Yes" (
				GOTO SKIP_RESOURCES_NT_X64
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_NT_X64%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_NT_X64%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_NT_X64%\""
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCE_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_NT_X64

			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_NT_X64%"...

			@ECHO ON
			cl %COMPILER_OPTIONS% %SOURCE_CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF

		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_NT_X64%".
		EXIT /B 1
	)

	IF "%BUILD_MODE%"=="release" (
		ECHO.
		ECHO [%~nx0] Deleting any *.obj and *.res files...

		DEL /Q "%RELEASE_BUILD_PATH%\*.obj"
		DEL /Q "%RELEASE_BUILD_PATH%\*.res"
	)

	ECHO.
	ECHO.
	ECHO.

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Windows 9x x86 Build
	REM ------------------------------------------------------------

	IF "%WIN9X_BUILD%" NEQ "Yes" (
		GOTO SKIP_BUILD_9X
	)

	ECHO [%~nx0] Compiling The Windows 9x x86 %BUILD_MODE% build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_PATH_9X_X86%" (
		MKDIR "%BUILD_PATH_9X_X86%"
	)

	SETLOCAL
		PUSHD "%BUILD_PATH_9X_X86%"

			ECHO [%~nx0] Copying the source group files...
			ROBOCOPY "%SOURCE_GROUP_FILES_DIR%" ".\%GROUP_FILES_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%SOURCE_EXTERNAL_LOCATIONS_DIR%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source scripts...
			ROBOCOPY "%SOURCE_SCRIPTS_DIR%" ".\%SCRIPTS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_9X_X86% /Fe"%EXE_FILENAME_9X_X86%" /Fd"VC_%EXE_FILENAME_9X_X86%.pdb""
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_9X%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_X86%"

			CALL "%VCVARSALL_PATH%" x86

			SET "COMPILED_RESOURCE_PATH="
			IF "%COMPILE_RESOURCES%" NEQ "Yes" (
				GOTO SKIP_RESOURCES_9X_X86
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_9X_X86%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_9X_X86%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_9X_X86%\" %RESOURCE_OPTIONS_9X_X86%"
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCE_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_9X_X86

			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_9X_X86%"...

			@ECHO ON		
			cl %COMPILER_OPTIONS% %SOURCE_CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF

		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_9X_X86%".
		EXIT /B 1
	)

	IF "%BUILD_MODE%"=="release" (
		ECHO.
		ECHO [%~nx0] Deleting any *.obj and *.res files...

		DEL /Q "%RELEASE_BUILD_PATH%\*.obj"
		DEL /Q "%RELEASE_BUILD_PATH%\*.res"
	)

	:SKIP_BUILD_9X

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Package All Builds
	REM ------------------------------------------------------------

	IF "%BUILD_MODE%"=="release" (
		ECHO.
		ECHO [%~nx0] Creating the release README file...
	
		ECHO Web Cache Exporter v%BUILD_VERSION%> "%RELEASE_README_PATH%"
		ECHO.>> "%RELEASE_README_PATH%"
		TYPE "%README_BODY_PATH%">> "%RELEASE_README_PATH%"
		ECHO.>> "%RELEASE_README_PATH%"
		TYPE "%LICENSE_FILE_PATH%">> "%RELEASE_README_PATH%"
	)

	ECHO.
	ECHO [%~nx0] The build process for %BUILD_MODE% version %BUILD_VERSION% completed successfully.
	ECHO.

	REM Compress the built executables and source code and create two archives.
	IF "%PACKAGE_BUILD%" NEQ "Yes" (
		GOTO SKIP_PACKAGE_BUILD
	)

	SET "BUILD_PATH_TO_ZIP="

	IF "%BUILD_MODE%"=="debug" (

		SET "BUILD_PATH_TO_ZIP=%DEBUG_BUILD_PATH%"

	) ELSE IF "%BUILD_MODE%"=="release" (

		SET "BUILD_PATH_TO_ZIP=%RELEASE_BUILD_PATH%"

	) ELSE (
		ECHO [%~nx0] Unknown build mode "%BUILD_MODE%".
		EXIT /B 1
	)

	REM Add an identifier to the archive names if we passed any extra command line options to the compiler.
	REM This will prevent accidentally sending someone a debug build that had the EXPORT_EMPTY_FILES macro defined.
	SET "EXTRA_ARGS_ID="
	IF "%~1" NEQ "" (
		SET "EXTRA_ARGS_ID=-extra-args"
	)

	SET "EXE_ARCHIVE_PATH=%BUILD_ARCHIVE_PATH%\web-cache-exporter-x86-%BUILD_MODE%%EXTRA_ARGS_ID%-%BUILD_VERSION%.zip"
	SET "SOURCE_ARCHIVE_PATH=%BUILD_ARCHIVE_PATH%\web-cache-exporter-x86-source%EXTRA_ARGS_ID%-%BUILD_VERSION%.zip"

	IF EXIST "%EXE_ARCHIVE_PATH%" (
		DEL /Q "%EXE_ARCHIVE_PATH%"
	)

	ECHO [%~nx0] Packaging the built executables...
	"%_7ZIP_EXE_PATH%" a "%EXE_ARCHIVE_PATH%" "%BUILD_PATH_TO_ZIP%\*" -r0 "-x!*.sln" "-x!*.suo" >NUL

	ECHO.

	IF EXIST "%SOURCE_ARCHIVE_PATH%" (
		DEL /Q "%SOURCE_ARCHIVE_PATH%"
	)

	ECHO [%~nx0] Packaging the source files...
	"%_7ZIP_EXE_PATH%" a "%SOURCE_ARCHIVE_PATH%" "%SOURCE_PATH%\" -r0 "-x!*.md" "-x!esent.*" "-x!*.class" "-x!*.idx" >NUL
	"%_7ZIP_EXE_PATH%" a "%SOURCE_ARCHIVE_PATH%" "%BATCH_FILE_PATH%" "%VERSION_FILE_PATH%" "%README_BODY_PATH%" "%LICENSE_FILE_PATH%" "%BUILDING_FILE_PATH%" >NUL

	ECHO.

	:SKIP_PACKAGE_BUILD

	ECHO [%~nx0] Finished running.

POPD
ENDLOCAL
