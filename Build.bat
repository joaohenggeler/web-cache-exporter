@ECHO OFF

REM This script is used to build the application on Windows using Visual Studio.
REM It defines any useful macros (like the version string and the Windows 98/ME build macro), and sets the
REM correct compiler and linker options. Extra compiler options may be added via the command line.
REM
REM Usage: Build.bat <Build Mode> [Optional Compiler Arguments]
REM Where <Build Mode> is either "debug" or "release".
REM
REM For example:
REM - Build.bat release
REM - Build.bat debug
REM - Build.bat debug /D WCE_EMPTY_EXPORT
REM
REM Macros that can be used in the debug builds:
REM - WCE_EMPTY_EXPORT, forces the application to create empty files instead of exporting the cache. Useful for testing
REM the application without having to wait for Windows to copy each file.
REM - WCE_TINY_FILE_BUFFERS, forces the application to use very small buffers when processing files. Useful for ensuring
REM that the application can read large files in chunks correctly in the release builds.
REM
REM Macros that are set by this batch file:
REM - WCE_DEBUG, defined when building in debug mode, undefined when building in release mode.
REM - WCE_9X, defined when building the Windows 98/ME version, undefined when building the Windows 2000 to 10 version. 
REM - WCE_32_BIT, defined when building the 32-bit version, undefined when building the 64-bit version.
REM - WCE_VERSION, defined as the application's version string in the form of "MAJOR.MINOR.PATCH". Note that this project
REM does *not* use semantic versioning. Defined as "0.0.0" if the version cannot be read from the "version.txt" file.
REM - WCE_TARGET, defined as the application's build target string (9x vs NT, architecture). For example, "NT-x86".
REM - WCE_BIG_ENDIAN, defined when targeting a big endian architecture, undefined when targeting a little endian architecture.
REM
REM If WCE_DEBUG, WCE_9X, WCE_32_BIT, and WCE_BIG_ENDIAN are not defined when compiling the source code, then the release
REM x64 version is built. If WCE_VERSION and WCE_TARGET are not defined, they'll default to "0.0.0" and "?", respectively.

SETLOCAL
PUSHD "%~dp0"
	
	REM The build mode argument.
	REM - debug - turns off optimizations, enables run-time error checks, generates debug information, and defines
	REM certain macros (like WCE_DEBUG).
	REM - release - turns on optimizations, disables any debug features and macros, and puts all the different executable
	REM versions in the same release directory.
	SET "BUILD_MODE=%~1"

	IF "%BUILD_MODE%"=="" (
		ECHO Usage: %~nx0 ^<Build Mode^> [Optional Compiler Arguments]
		ECHO Where ^<Build Mode^> is either "debug" or "release".
		EXIT /B 1
	)

	IF "%BUILD_MODE%"=="debug" GOTO VALID_BUILD_MODE
	IF "%BUILD_MODE%"=="release" GOTO VALID_BUILD_MODE

	ECHO [%~nx0] Unknown build mode "%BUILD_MODE%".
	EXIT /B 1

	:VALID_BUILD_MODE

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

	REM Set to "Yes" to use 7-Zip to package the built executables and source code into compressed archives.
	REM If enabled, the _7ZIP_EXE_PATH variable below must point to the 7-Zip executable.
	SET "PACKAGE_BUILD=Yes"
	
	REM The absolute path to the vcvarsall.bat batch file that is installed with Visual Studio.
	REM You can use this to change the compiler version, although this application hasn't been thoroughly tested with more
	REM modern Visual Studio versions.
	SET "VCVARSALL_PATH=C:\Program Files (x86)\Microsoft Visual Studio 8\VC\vcvarsall.bat"

	REM The path to the 7-Zip executable that is used to package the built executables and source code.
	SET "_7ZIP_EXE_PATH=_7za920\7za.exe"

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ VCVARSALL.bat Parameters
	REM ------------------------------------------------------------

	REM Where to look for the source files.
	SET "SOURCE_PATH=%~dp0Source"
	SET "CODE_FILES="%SOURCE_PATH%\Code\*.cpp""
	
	SET "GROUPS_DIR=Groups"
	SET "GROUPS_PATH=%SOURCE_PATH%\%GROUPS_DIR%"
	
	SET "EXTERNAL_LOCATIONS_DIR=ExternalLocations"
	SET "EXTERNAL_LOCATIONS_PATH=%SOURCE_PATH%\%EXTERNAL_LOCATIONS_DIR%"
	
	SET "SCRIPTS_DIR=Scripts"
	SET "SCRIPTS_PATH=%SOURCE_PATH%\%SCRIPTS_DIR%"

	REM Where to look for third-party source files, headers, and libraries.
	REM - /I "<Path>" - third-party header files for the compiler.
	REM - /LIBPATH:"<Path>" - third-party static library files for the linker.
	SET "THIRD_PARTY_PATH=%SOURCE_PATH%\ThirdParty"
	SET "STATIC_LIBRARIES_PATH=%THIRD_PARTY_PATH%\StaticLibraries"
	
	SET "ESENT_INCLUDE_PATH=%THIRD_PARTY_PATH%\ESENT"
	
	SET "ZLIB_PATH=%THIRD_PARTY_PATH%\Zlib"
	SET "ZLIB_CODE_FILES="%ZLIB_PATH%\*.c""
	SET "ZLIB_LIB_FILENAME_X86=zlib_x86.lib"
	SET "ZLIB_LIB_FILENAME_X64=zlib_x64.lib"

	SET "BROTLI_PATH=%THIRD_PARTY_PATH%\Brotli"
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
	SET "COMPILER_OPTIONS=/nologo /J /W4 /WX /wd4100 /Oi /GR- /EHa- /I "%THIRD_PARTY_PATH%" /I "%ESENT_INCLUDE_PATH%" /I "%BROTLI_INCLUDE_PATH%""
	SET "COMPILER_OPTIONS_RELEASE=/O2 /MT /GL"
	SET "COMPILER_OPTIONS_DEBUG=/Od /MTd /RTC1 /RTCc /Zi /FC /D WCE_DEBUG /D _ALLOW_RTCc_IN_STL"

	SET "THIRD_PARTY_COMPILER_OPTIONS=/c /w /Oi /GR- /EHa- /nologo /I "%BROTLI_INCLUDE_PATH%""
	SET "THIRD_PARTY_COMPILER_OPTIONS_RELEASE=/O2 /MT /GL"
	SET "THIRD_PARTY_COMPILER_OPTIONS_DEBUG=/Od /MTd /RTC1 /Zi /FC"

	REM The WCE_BIG_ENDIAN macro isn't currently used by any build target, but we'll mention it here if that changes
	REM in the future. By default, we'll assume that we're targeting a little endian architecture.
	SET "COMPILER_OPTIONS_9X_X86=/D WCE_9X /D WCE_32_BIT /D WCE_TARGET=\"9x-x86\""
	SET "COMPILER_OPTIONS_NT_X86=/D WCE_32_BIT /D WCE_TARGET=\"NT-x86\""
	SET "COMPILER_OPTIONS_NT_X64=/D WCE_TARGET=\"NT-x64\""
	
	REM Statically link with the required libraries. Note that we tell the linker not to use any default libraries.
	SET "LIBRARIES=Kernel32.lib Advapi32.lib Shell32.lib Shlwapi.lib Version.lib"
	SET "LIBRARIES_RELEASE=LIBCMT.lib LIBCPMT.lib"
	SET "LIBRARIES_DEBUG=LIBCMTD.lib LIBCPMTD.lib"
	SET "LIBRARIES_X86=%ZLIB_LIB_FILENAME_X86% %BROTLI_LIB_FILENAME_X86%"
	SET "LIBRARIES_X64=%ZLIB_LIB_FILENAME_X64% %BROTLI_LIB_FILENAME_X64%"

	REM Common linker options and any other ones that only apply to a specific build target or mode.
	REM We used to statically link to ESENT.lib in the Windows 2000 to 10 (NT) builds.
	SET "LINKER_OPTIONS=/link /WX /NODEFAULTLIB /LIBPATH:"%STATIC_LIBRARIES_PATH%""
	SET "LINKER_OPTIONS_RELEASE=/LTCG /OPT:REF,ICF"
	SET "LINKER_OPTIONS_DEBUG=/DEBUG"

	SET "THIRD_PARTY_LINKER_OPTIONS=/WX /NODEFAULTLIB"
	SET "THIRD_PARTY_LINKER_OPTIONS_RELEASE=/LTCG"
	SET "THIRD_PARTY_LINKER_OPTIONS_DEBUG="	

	SET "LINKER_OPTIONS_9X=/OPT:WIN98"
	SET "LINKER_OPTIONS_NT=/OPT:NOWIN98"
	
	REM The names of the resulting executable files.
	SET "EXE_FILENAME_9X_X86=WCE9x32.exe"
	SET "EXE_FILENAME_NT_X86=WCE32.exe"
	SET "EXE_FILENAME_NT_X64=WCE64.exe"
	
	REM The paths and filenames to the uncompiled and compiled resource files.
	SET "RESOURCES_FILE_PATH=%SOURCE_PATH%\Resources\resources.rc"
	SET "COMPILED_RESOURCES_FILENAME=resources.res"

	REM Resource compiler options that only apply to a specific build target.
	SET "RESOURCE_COMPILER_OPTIONS_9X_X86=/D WCE_9X /D WCE_32_BIT"
	SET "RESOURCE_COMPILER_OPTIONS_NT_X86=/D WCE_32_BIT"
	SET "RESOURCE_COMPILER_OPTIONS_NT_X64="
	
	REM Modify or add options that depend on the Visual Studio version.
	
	IF /I "%USE_VS_2005_OPTIONS%"=="No" (
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

	SET "BUILD_PATH=%~dp0Builds"

	SET "RELEASE_DIR=Release"
	SET "RELEASE_PATH=%BUILD_PATH%\%RELEASE_DIR%"

	SET "DEBUG_PATH=%BUILD_PATH%\Debug"
	SET "DEBUG_PATH_9X_X86=%DEBUG_PATH%\9x-x86"
	SET "DEBUG_PATH_NT_X86=%DEBUG_PATH%\NT-x86"
	SET "DEBUG_PATH_NT_X64=%DEBUG_PATH%\NT-x64"
	
	REM The location of the compressed archives.
	SET "ARCHIVES_PATH_DIR=Archives"
	SET "ARCHIVES_PATH_PATH=%BUILD_PATH%\%ARCHIVES_PATH_DIR%"

	REM The location of any files and directories that should be packaged in the source archive.
	SET "BATCH_FILE_PATH=%~dpnx0"
	SET "BUILDING_FILE_PATH=%~dp0Building.txt"
	SET "LICENSE_FILE_PATH=%~dp0LICENSE"
	SET "README_BODY_PATH=%~dp0readme_body.txt"
	SET "VERSION_FILE_PATH=%~dp0version.txt"
	SET "VS_CODE_PATH=%~dp0.vscode"

	REM The location of the final release README. This file is generated using the version, body template,
	REM and license files above. These files must only use ASCII characters and use CRLF for newlines.
	SET "RELEASE_README_PATH=%RELEASE_PATH%\readme.txt"

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
	SET "RESOURCE_VERSION_OPTIONS=/D WCE_MAJOR_VERSION=%MAJOR_VERSION% /D WCE_MINOR_VERSION=%MINOR_VERSION% /D WCE_PATCH_VERSION=%PATCH_VERSION% /D WCE_BUILD_NUMBER=%BUILD_NUMBER%"

	REM Check if the build mode is valid and add the previously specified compiler and linkers options, and static libraries. 
	IF "%BUILD_MODE%"=="debug" (

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_DEBUG% /D WCE_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_DEBUG%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_DEBUG%"

		SET "THIRD_PARTY_COMPILER_OPTIONS=%THIRD_PARTY_COMPILER_OPTIONS% %THIRD_PARTY_COMPILER_OPTIONS_DEBUG%"
		SET "THIRD_PARTY_LINKER_OPTIONS=%THIRD_PARTY_LINKER_OPTIONS% %THIRD_PARTY_LINKER_OPTIONS_DEBUG%"

		SET "BUILD_PATH_9X_X86=%DEBUG_PATH_9X_X86%"
		SET "BUILD_PATH_NT_X86=%DEBUG_PATH_NT_X86%"
		SET "BUILD_PATH_NT_X64=%DEBUG_PATH_NT_X64%"

	) ELSE IF "%BUILD_MODE%"=="release" (

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_RELEASE% /D WCE_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_RELEASE%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_RELEASE%"

		SET "THIRD_PARTY_COMPILER_OPTIONS=%THIRD_PARTY_COMPILER_OPTIONS% %THIRD_PARTY_COMPILER_OPTIONS_RELEASE%"
		SET "THIRD_PARTY_LINKER_OPTIONS=%THIRD_PARTY_LINKER_OPTIONS% %THIRD_PARTY_LINKER_OPTIONS_RELEASE%"

		SET "BUILD_PATH_9X_X86=%RELEASE_PATH%"
		SET "BUILD_PATH_NT_X86=%RELEASE_PATH%"
		SET "BUILD_PATH_NT_X64=%RELEASE_PATH%"

	)

	REM Delete the previous builds.
	IF /I "%CLEAN_BUILD%"=="Yes" (

		ECHO [%~nx0] Deleting the previous builds in "%BUILD_PATH%"...
		ECHO.

		IF EXIST "%BUILD_PATH%" (
			REM Delete everything except .sln and .suo Visual Studio project files, and the compressed archives directory.
			MKDIR "TemporaryDirectory" >NUL
			ROBOCOPY "%BUILD_PATH%" "TemporaryDirectory" /MOVE /S /XF "*.sln" "*.suo" /XD "%ARCHIVES_PATH_DIR%" >NUL
			RMDIR /S /Q "TemporaryDirectory"
		)

		ECHO [%~nx0] Deleting the previous static libraries in "%STATIC_LIBRARIES_PATH%"...
		ECHO.

		IF EXIST "%STATIC_LIBRARIES_PATH%" (
			RMDIR /S /Q "%STATIC_LIBRARIES_PATH%"
		)
	)

	IF NOT EXIST "%STATIC_LIBRARIES_PATH%" (
		MKDIR "%STATIC_LIBRARIES_PATH%"
	)

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Build Third-Party Static Libraries
	REM ------------------------------------------------------------

	PUSHD "%STATIC_LIBRARIES_PATH%"

		DEL /Q "%STATIC_LIBRARIES_PATH%\*.obj" >NUL 2>&1

		REM ------------------------------------------------------------ Zlib x86

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x86
			
			ECHO.
			ECHO [%~nx0] Building the static library "%ZLIB_LIB_FILENAME_X86%"...

			@ECHO ON
			cl %THIRD_PARTY_COMPILER_OPTIONS% /Fd"VC_%ZLIB_LIB_FILENAME_X86%.pdb" %ZLIB_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%ZLIB_LIB_FILENAME_X86%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%STATIC_LIBRARIES_PATH%\*.obj" /OUT:"%ZLIB_LIB_FILENAME_X86%" %THIRD_PARTY_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%STATIC_LIBRARIES_PATH%\*.obj"

		REM ------------------------------------------------------------ Zlib x64

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x64
			
			ECHO.
			ECHO [%~nx0] Building the static library "%ZLIB_LIB_FILENAME_X64%"...

			@ECHO ON
			cl %THIRD_PARTY_COMPILER_OPTIONS% /Fd"VC_%ZLIB_LIB_FILENAME_X64%.pdb" %ZLIB_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%ZLIB_LIB_FILENAME_X64%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%STATIC_LIBRARIES_PATH%\*.obj" /OUT:"%ZLIB_LIB_FILENAME_X64%" %THIRD_PARTY_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%STATIC_LIBRARIES_PATH%\*.obj"

		REM ------------------------------------------------------------ Brotli x86

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x86
			
			ECHO.
			ECHO [%~nx0] Building the static library "%BROTLI_LIB_FILENAME_X86%"...

			@ECHO ON
			cl %THIRD_PARTY_COMPILER_OPTIONS% /Fd"VC_%BROTLI_LIB_FILENAME_X86%.pdb" %BROTLI_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%BROTLI_LIB_FILENAME_X86%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%STATIC_LIBRARIES_PATH%\*.obj" /OUT:"%BROTLI_LIB_FILENAME_X86%" %THIRD_PARTY_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%STATIC_LIBRARIES_PATH%\*.obj"

		REM ------------------------------------------------------------ Brotli x64

		SETLOCAL
			
			CALL "%VCVARSALL_PATH%" x64
			
			ECHO.
			ECHO [%~nx0] Building the static library "%BROTLI_LIB_FILENAME_X64%"...

			@ECHO ON
			cl %THIRD_PARTY_COMPILER_OPTIONS% /Fd"VC_%BROTLI_LIB_FILENAME_X64%.pdb" %BROTLI_CODE_FILES%
			@ECHO OFF

			IF ERRORLEVEL 1 (
				ECHO.
				ECHO [%~nx0] Error while building "%BROTLI_LIB_FILENAME_X64%".
				EXIT /B 1
			)

			@ECHO ON
			lib "%STATIC_LIBRARIES_PATH%\*.obj" /OUT:"%BROTLI_LIB_FILENAME_X64%" %THIRD_PARTY_LINKER_OPTIONS%
			@ECHO OFF
				
		ENDLOCAL

		DEL /Q "%STATIC_LIBRARIES_PATH%\*.obj"

	POPD

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Compile The Program
	REM ------------------------------------------------------------

	ECHO [%~nx0] Building in %BUILD_MODE% mode...
	ECHO.

	REM Add any remaining command line arguments to the compiler options.
	SET "COMMAND_LINE_COMPILER_OPTIONS="%*""
	SET "COMMAND_LINE_COMPILER_OPTIONS=%COMMAND_LINE_COMPILER_OPTIONS:"=%"
	FOR /F "tokens=1,* delims= " %%A IN ("%COMMAND_LINE_COMPILER_OPTIONS%") DO (
		SET "COMMAND_LINE_COMPILER_OPTIONS=%%B"
		REM %%A = BUILD_MODE
	)

	IF NOT "%COMMAND_LINE_COMPILER_OPTIONS%"=="" (
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
			ROBOCOPY "%GROUPS_PATH%" ".\%GROUPS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%EXTERNAL_LOCATIONS_PATH%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source scripts...
			ROBOCOPY "%SCRIPTS_PATH%" ".\%SCRIPTS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_NT_X86% /Fe"%EXE_FILENAME_NT_X86%" /Fd"VC_%EXE_FILENAME_NT_X86%.pdb""
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_NT%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_X86%"

			CALL "%VCVARSALL_PATH%" x86

			SET "COMPILED_RESOURCE_PATH="
			IF /I "%COMPILE_RESOURCES%"=="No" (
				GOTO SKIP_RESOURCES_NT_X86
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_NT_X86%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_NT_X86%\%COMPILED_RESOURCES_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_NT_X86%\" %RESOURCE_COMPILER_OPTIONS_NT_X86%"
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCES_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_NT_X86
			
			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_NT_X86%"...

			@ECHO ON
			cl %COMPILER_OPTIONS% %CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
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

		DEL /Q "%RELEASE_PATH%\*.obj"
		DEL /Q "%RELEASE_PATH%\*.res"
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
			ROBOCOPY "%GROUPS_PATH%" ".\%GROUPS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%EXTERNAL_LOCATIONS_PATH%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source scripts...
			ROBOCOPY "%SCRIPTS_PATH%" ".\%SCRIPTS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_NT_X64% /Fe"%EXE_FILENAME_NT_X64%" /Fd"VC_%EXE_FILENAME_NT_X64%.pdb""
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_NT%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_X64%"

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_NT_X64%\%COMPILED_RESOURCES_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" /D WCE_EXE_FILENAME=\"%EXE_FILENAME_NT_X64%\" %RESOURCE_COMPILER_OPTIONS_NT_X64%"
			
			CALL "%VCVARSALL_PATH%" x64

			SET "COMPILED_RESOURCE_PATH="
			IF /I "%COMPILE_RESOURCES%"=="No" (
				GOTO SKIP_RESOURCES_NT_X64
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_NT_X64%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_NT_X64%\%COMPILED_RESOURCES_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_NT_X64%\""
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCES_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_NT_X64

			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_NT_X64%"...

			@ECHO ON
			cl %COMPILER_OPTIONS% %CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
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

		DEL /Q "%RELEASE_PATH%\*.obj"
		DEL /Q "%RELEASE_PATH%\*.res"
	)

	ECHO.
	ECHO.
	ECHO.

	REM ------------------------------------------------------------
	REM ------------------------------------------------------------ Windows 9x x86 Build
	REM ------------------------------------------------------------

	IF /I "%WIN9X_BUILD%"=="No" (
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
			ROBOCOPY "%GROUPS_PATH%" ".\%GROUPS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%EXTERNAL_LOCATIONS_PATH%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source scripts...
			ROBOCOPY "%SCRIPTS_PATH%" ".\%SCRIPTS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_9X_X86% /Fe"%EXE_FILENAME_9X_X86%" /Fd"VC_%EXE_FILENAME_9X_X86%.pdb""
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_9X%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_X86%"

			CALL "%VCVARSALL_PATH%" x86

			SET "COMPILED_RESOURCE_PATH="
			IF /I "%COMPILE_RESOURCES%"=="No" (
				GOTO SKIP_RESOURCES_9X_X86
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_9X_X86%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_9X_X86%\%COMPILED_RESOURCES_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_9X_X86%\" %RESOURCE_COMPILER_OPTIONS_9X_X86%"
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCES_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_9X_X86

			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_9X_X86%"...

			@ECHO ON		
			cl %COMPILER_OPTIONS% %CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
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

		DEL /Q "%RELEASE_PATH%\*.obj"
		DEL /Q "%RELEASE_PATH%\*.res"
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
	IF /I "%PACKAGE_BUILD%"=="No" (
		GOTO SKIP_PACKAGE_BUILD
	)

	SET "BUILD_PATH_TO_COMPRESS="

	IF "%BUILD_MODE%"=="debug" (

		SET "BUILD_PATH_TO_COMPRESS=%DEBUG_PATH%"

	) ELSE IF "%BUILD_MODE%"=="release" (

		SET "BUILD_PATH_TO_COMPRESS=%RELEASE_PATH%"

	)

	REM Add an identifier to the archive names if we passed any extra command line options to the compiler.
	REM This will prevent accidentally sending someone a debug build that had the WCE_EMPTY_EXPORT macro defined.
	SET "EXTRA_ARGS_ID="
	IF NOT "%~2"=="" (
		SET "EXTRA_ARGS_ID=-extra-args"
	)

	SET "BINARY_ZIP_PATH=%ARCHIVES_PATH_PATH%\web-cache-exporter-x86-%BUILD_VERSION%-%BUILD_MODE%%EXTRA_ARGS_ID%.zip"
	SET "SOURCE_ZIP_PATH=%ARCHIVES_PATH_PATH%\web-cache-exporter-x86-%BUILD_VERSION%-source%EXTRA_ARGS_ID%.zip"
	SET "BINARY_SFX_PATH=%ARCHIVES_PATH_PATH%\web-cache-exporter-x86-%BUILD_VERSION%-%BUILD_MODE%%EXTRA_ARGS_ID%.exe"
	SET "BINARY_SFX_MODULE=7z.sfx"

	IF EXIST "%BINARY_ZIP_PATH%" (
		DEL /Q "%BINARY_ZIP_PATH%"
	)

	IF EXIST "%BINARY_SFX_PATH%" (
		DEL /Q "%BINARY_SFX_PATH%"
	)

	ECHO [%~nx0] Packaging the built executables...
	"%_7ZIP_EXE_PATH%" a "%BINARY_ZIP_PATH%" "%BUILD_PATH_TO_COMPRESS%\*" -r0 "-x!*.sln" "-x!*.suo" >NUL
	"%_7ZIP_EXE_PATH%" a "%BINARY_SFX_PATH%" "%BUILD_PATH_TO_COMPRESS%\" -sfx%BINARY_SFX_MODULE% -r0 "-x!*.sln" "-x!*.suo" >NUL

	ECHO.

	IF EXIST "%SOURCE_ZIP_PATH%" (
		DEL /Q "%SOURCE_ZIP_PATH%"
	)

	ECHO [%~nx0] Packaging the source files...
	"%_7ZIP_EXE_PATH%" a "%SOURCE_ZIP_PATH%" "%SOURCE_PATH%\" -r0 "-x!*.md" "-x!esent.*" "-x!*.class" "-x!*.idx" >NUL
	"%_7ZIP_EXE_PATH%" a "%SOURCE_ZIP_PATH%" "%BATCH_FILE_PATH%" "%BUILDING_FILE_PATH%" "%LICENSE_FILE_PATH%" "%README_BODY_PATH%" "%VERSION_FILE_PATH%" "%VS_CODE_PATH%\" >NUL

	ECHO.

	:SKIP_PACKAGE_BUILD

	ECHO [%~nx0] Finished running.

POPD
ENDLOCAL
