@ECHO OFF

REM This script is used to build the application on Windows using Visual Studio 2005 Professional.
REM It will define any useful macros (like the version string and the Windows 98 and ME build macro), and set the
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
REM This is useful to test the program without having to waiting for Windows to copy each file.

SETLOCAL
PUSHD "%~dp0"
	
	REM ---------------------------------------------------------------------------
	REM Basic build parameters.
	REM Any parameters where the value can be true or false are set to either "Yes" or "No". During development and when
	REM packaging a new release, these are set to "Yes" and VCVARSALL_PATH is set to Visual Studio 2005 Professional:
	REM "C:\Program Files (x86)\Microsoft Visual Studio 8\VC\vcvarsall.bat"
	REM Visual Studio 2019, for example, uses the following:
	REM "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"

	REM The build mode.
	REM - debug - turns off optimizations, enables run-time error checks, generates debug information, and defines
	REM certain macros (like DEBUG).
	REM - release - turns on optimizations, disables any debug features and macros, and puts all the different executable
	REM versions in the same release directory.
	SET "BUILD_MODE=debug"

	REM Set to "Yes" to delete all the build directories before compiling.
	SET "CLEAN_BUILD=Yes"

	REM Set to "Yes" to build the Windows 98 and ME version.
	SET "WIN9X_BUILD=Yes"

	REM Set to "Yes" to use certain compiler and linker options that were removed after Visual Studio 2005.
	REM If this value is "No", it's assumed that a more modern of Visual Studio (2015 or later) is specified in VCVARSALL_PATH.
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

	REM ---------------------------------------------------------------------------
	REM The parameters for the vcvarsall.bat batch file.

	REM Where to look for the source files.
	SET "SOURCE_PATH=%~dp0Source"
	SET "SOURCE_CODE_FILES="%SOURCE_PATH%\*.cpp""
	
	SET "GROUP_FILES_DIR=Groups"
	SET "SOURCE_GROUP_FILES_DIR=%SOURCE_PATH%\%GROUP_FILES_DIR%"
	SET "EXTERNAL_LOCATIONS_DIR=ExternalLocations"
	SET "SOURCE_EXTERNAL_LOCATIONS_DIR=%SOURCE_PATH%\%EXTERNAL_LOCATIONS_DIR%"

	REM Where to look for third party source files, headers, and libraries.
	REM - /I "<Path>" - third party header files for the compiler.
	REM - /LIBPATH:"<Path>" - third party static library files for the linker.
	SET "THIRD_PARTY_BASE_PATH=%SOURCE_PATH%\ThirdParty"
	SET "ESENT_PATH=%THIRD_PARTY_BASE_PATH%\ESENT"
	SET "SHA_256_PATH=%THIRD_PARTY_BASE_PATH%\SHA-256"
	
	SET "SOURCE_CODE_FILES=%SOURCE_CODE_FILES% "%SHA_256_PATH%\*.cpp""
	SET "THIRD_PARTY_INCLUDES=/I "%ESENT_PATH%" /I "%SHA_256_PATH%""
	
	REM Common compiler options and any other ones that only apply to a specific build target or mode.
	REM Disabled warnings:
	REM - C4100 - unused function parameter.
	SET "COMPILER_OPTIONS=/W4 /WX /wd4100 /Oi /GR- /EHa- /nologo %THIRD_PARTY_INCLUDES%"
	SET "COMPILER_OPTIONS_RELEASE_ONLY=/O2 /MT /GL"
	SET "COMPILER_OPTIONS_DEBUG_ONLY=/Od /MTd /RTC1 /RTCc /Zi /Fm /FC /D DEBUG"
	SET "COMPILER_OPTIONS_WIN_NT_ONLY="
	SET "COMPILER_OPTIONS_WIN_9X_ONLY=/D BUILD_9X"
	SET "COMPILER_OPTIONS_32_ONLY=/D BUILD_32_BIT"
	SET "COMPILER_OPTIONS_64_ONLY="

	REM Statically link with the required libraries. Note that we tell the linker not to use any default libraries.
	SET "LIBRARIES=Kernel32.lib Advapi32.lib Shell32.lib Shlwapi.lib Version.lib"
	SET "LIBRARIES_RELEASE_ONLY=LIBCMT.lib LIBCPMT.lib"
	SET "LIBRARIES_DEBUG_ONLY=LIBCMTD.lib LIBCPMTD.lib"
	SET "LIBRARIES_32_ONLY="
	SET "LIBRARIES_64_ONLY="
	SET "LIBRARIES_WIN_NT_ONLY="
	SET "LIBRARIES_WIN_9X_ONLY="

	REM Common linker options and any other ones that only apply to a specific build target or mode.
	REM We used to statically link to ESENT.lib in the Windows 2000 to 10 (NT) builds.
	SET "LINKER_OPTIONS=/link /WX /NODEFAULTLIB"
	SET "LINKER_OPTIONS_RELEASE_ONLY=/LTCG /OPT:REF,ICF"
	SET "LINKER_OPTIONS_DEBUG_ONLY=/DEBUG"
	SET "LINKER_OPTIONS_WIN_NT_ONLY="
	SET "LINKER_OPTIONS_WIN_9X_ONLY="
	SET "LINKER_OPTIONS_32_ONLY="
	SET "LINKER_OPTIONS_64_ONLY="

	REM The names of the resulting executable files.
	SET "EXE_FILENAME_32=WCE32.exe"
	SET "EXE_FILENAME_64=WCE64.exe"
	SET "EXE_FILENAME_9X_32=WCE9x32.exe"

	REM The paths and filenames to the uncompiled and compiled resource files.
	SET "RESOURCE_FILE_PATH=%SOURCE_PATH%\Resources\resources.rc"
	SET "COMPILED_RESOURCE_FILENAME=resources.res"

	REM Resource compiler options that only apply to a specific build target.
	SET "RESOURCE_OPTIONS_32_ONLY=/D BUILD_32_BIT"
	SET "RESOURCE_OPTIONS_WIN_9X_ONLY=/D BUILD_9X"
	
	REM ---------------------------------------------------------------------------
	REM The locations of the output directories and other key files.

	SET "MAIN_BUILD_PATH=%~dp0Builds"

	SET "RELEASE_BUILD_PATH=%MAIN_BUILD_PATH%\Release"

	SET "DEBUG_BUILD_PATH=%MAIN_BUILD_PATH%\Debug"
	SET "DEBUG_BUILD_PATH_32=%DEBUG_BUILD_PATH%\Build-x86-32"
	SET "DEBUG_BUILD_PATH_64=%DEBUG_BUILD_PATH%\Build-x86-64"
	SET "DEBUG_BUILD_PATH_9X_32=%DEBUG_BUILD_PATH%\Build-9x-x86-32"

	REM The location of the compressed archives.
	SET "BUILD_ARCHIVE_DIR=Archives"
	SET "BUILD_ARCHIVE_PATH=%MAIN_BUILD_PATH%\%BUILD_ARCHIVE_DIR%"
	REM The path to the 7-Zip executable.
	SET "_7ZIP_EXE_PATH=_7za920\7za.exe"

	REM The location of this batch file, the version file, the README body template, and the license.
	SET "BATCH_FILE_PATH=%~dpnx0"
	SET "VERSION_FILE_PATH=%~dp0version.txt"
	SET "README_BODY_PATH=%~dp0readme_body.txt"
	SET "LICENSE_FILE_PATH=%~dp0LICENSE"

	REM The location of the final release README. This file is generated using the version, body template,
	REM and license files above. These files must only use ASCII characters and use CRLF for newlines.
	SET "RELEASE_README_PATH=%RELEASE_BUILD_PATH%\readme.txt"

	REM ---------------------------------------------------------------------------

	CLS

	REM Add any extra options that depend on the Visual Studio version.
	IF "%USE_VS_2005_OPTIONS%" == "Yes" (
		REM The /OPT:WIN98 and /OPT:NOWIN98 options don't exist in modern Visual Studio versions
		REM since they dropped support for Windows 95, 98, and ME.
		SET "LINKER_OPTIONS_WIN_NT_ONLY=%LINKER_OPTIONS_WIN_NT_ONLY% /OPT:NOWIN98"
		SET "LINKER_OPTIONS_WIN_9X_ONLY=%LINKER_OPTIONS_WIN_9X_ONLY% /OPT:WIN98"
	) ELSE (
		ECHO [%~nx0] Using compiler and linker options for Visual Studio 2015 or later.
		ECHO.

		REM Starting in Visual Studio 2015, the C Run-time Library was refactored into new binaries
		SET "LIBRARIES_RELEASE_ONLY=%LIBRARIES_RELEASE_ONLY% LIBUCRT.lib LIBVCRUNTIME.lib"
		SET "LIBRARIES_DEBUG_ONLY=%LIBRARIES_DEBUG_ONLY% LIBUCRTD.lib LIBVCRUNTIMED.lib"
	)

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

	)

	REM Check if the build mode is valid and add the previously specified compiler and linkers options, and static libraries. 
	IF "%BUILD_MODE%"=="debug" (

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_DEBUG_ONLY% /D BUILD_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_DEBUG_ONLY%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_DEBUG_ONLY%"

		SET "BUILD_PATH_32=%DEBUG_BUILD_PATH_32%"
		SET "BUILD_PATH_64=%DEBUG_BUILD_PATH_64%"
		SET "BUILD_PATH_9X_32=%DEBUG_BUILD_PATH_9X_32%"

	) ELSE IF "%BUILD_MODE%"=="release" (

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_RELEASE_ONLY% /D BUILD_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_RELEASE_ONLY%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_RELEASE_ONLY%"

		SET "BUILD_PATH_32=%RELEASE_BUILD_PATH%"
		SET "BUILD_PATH_64=%RELEASE_BUILD_PATH%"
		SET "BUILD_PATH_9X_32=%RELEASE_BUILD_PATH%"

	) ELSE (
		ECHO [%~nx0] Unknown build mode "%BUILD_MODE%".
		EXIT /B 1
	)

	ECHO [%~nx0] Building in %BUILD_MODE% mode...
	ECHO.

	REM Any any remaining command line arguments to the compiler options.
	SET "COMMAND_LINE_COMPILER_OPTIONS=%*"
	IF "%COMMAND_LINE_COMPILER_OPTIONS%" NEQ "" (
		ECHO [%~nx0] Passing extra compiler options from the command line: "%COMMAND_LINE_COMPILER_OPTIONS%"...
		ECHO.
		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMMAND_LINE_COMPILER_OPTIONS%"
	)

	REM ---------------------------------------------------------------------------
	REM ------------------------- Windows NT 32-bit Build -------------------------
	REM ---------------------------------------------------------------------------

	REM Build each target by calling the vcvarsall.bat batch file with the correct architecture.
	REM We'll stop the building process if a single target doesn't compile or link successfully.

	ECHO [%~nx0] Windows NT 32-bit %BUILD_MODE% build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_PATH_32%" (
		MKDIR "%BUILD_PATH_32%"
	)

	SETLOCAL
		PUSHD "%BUILD_PATH_32%"

			ECHO [%~nx0] Copying the source group files...
			ROBOCOPY "%SOURCE_GROUP_FILES_DIR%" ".\%GROUP_FILES_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%SOURCE_EXTERNAL_LOCATIONS_DIR%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_WIN_NT_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_32_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% /Fe"%EXE_FILENAME_32%""
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_32_ONLY%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_32_ONLY%"

			CALL "%VCVARSALL_PATH%" x86

			SET "COMPILED_RESOURCE_PATH="
			IF "%COMPILE_RESOURCES%" NEQ "Yes" (
				GOTO SKIP_RESOURCES_32
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_32%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_32%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_32%\" %RESOURCE_OPTIONS_32_ONLY%"
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCE_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_32
			
			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_32%"...

			@ECHO ON
			cl %COMPILER_OPTIONS% %SOURCE_CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF
			
		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_32%".
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

	REM ---------------------------------------------------------------------------
	REM ------------------------- Windows NT 64-bit Build -------------------------
	REM ---------------------------------------------------------------------------

	ECHO [%~nx0] Windows NT 64-bit %BUILD_MODE% build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_PATH_64%" (
		MKDIR "%BUILD_PATH_64%"
	)

	SETLOCAL
		PUSHD "%BUILD_PATH_64%"

			ECHO [%~nx0] Copying the source group files...
			ROBOCOPY "%SOURCE_GROUP_FILES_DIR%" ".\%GROUP_FILES_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%SOURCE_EXTERNAL_LOCATIONS_DIR%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_WIN_NT_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_64_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% /Fe"%EXE_FILENAME_64%""
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_64_ONLY%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_64_ONLY%"

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_64%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" /D WCE_EXE_FILENAME=\"%EXE_FILENAME_64%\""
			
			CALL "%VCVARSALL_PATH%" x64

			SET "COMPILED_RESOURCE_PATH="
			IF "%COMPILE_RESOURCES%" NEQ "Yes" (
				GOTO SKIP_RESOURCES_64
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_64%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_64%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_64%\""
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCE_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_64

			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_64%"...

			@ECHO ON
			cl %COMPILER_OPTIONS% %SOURCE_CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF

		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_64%".
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

	REM ---------------------------------------------------------------------------
	REM ------------------------- Windows 9x 32-bit Build -------------------------
	REM ---------------------------------------------------------------------------

	IF "%WIN9X_BUILD%" NEQ "Yes" (
		GOTO SKIP_BUILD_9X
	)

	ECHO [%~nx0] Windows 9x 32-bit %BUILD_MODE% build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_PATH_9X_32%" (
		MKDIR "%BUILD_PATH_9X_32%"
	)

	SETLOCAL
		PUSHD "%BUILD_PATH_9X_32%"

			ECHO [%~nx0] Copying the source group files...
			ROBOCOPY "%SOURCE_GROUP_FILES_DIR%" ".\%GROUP_FILES_DIR%" /S /XF "*.md" >NUL
			ECHO.

			ECHO [%~nx0] Copying the source external locations directory...
			ROBOCOPY "%SOURCE_EXTERNAL_LOCATIONS_DIR%" ".\%EXTERNAL_LOCATIONS_DIR%" /S /XF "*.md" >NUL
			ECHO.

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_WIN_9X_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_32_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% /Fe"%EXE_FILENAME_9X_32%""
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_32_ONLY%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_WIN_9X_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_WIN_9X_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_32_ONLY%"
			
			CALL "%VCVARSALL_PATH%" x86

			SET "COMPILED_RESOURCE_PATH="
			IF "%COMPILE_RESOURCES%" NEQ "Yes" (
				GOTO SKIP_RESOURCES_9X_32
			)

			ECHO.
			ECHO [%~nx0] Compiling resources for "%EXE_FILENAME_9X_32%"...

			SET "COMPILED_RESOURCE_PATH=%BUILD_PATH_9X_32%\%COMPILED_RESOURCE_FILENAME%"
			SET "RESOURCE_OPTIONS=/FO "%COMPILED_RESOURCE_PATH%" %RESOURCE_VERSION_OPTIONS% /D WCE_EXE_FILENAME=\"%EXE_FILENAME_9X_32%\" %RESOURCE_OPTIONS_32_ONLY% %RESOURCE_OPTIONS_WIN_9X_ONLY%"
			@ECHO ON
			rc %RESOURCE_OPTIONS% "%RESOURCE_FILE_PATH%"
			@ECHO OFF

			:SKIP_RESOURCES_9X_32

			ECHO.
			ECHO [%~nx0] Compiling "%EXE_FILENAME_9X_32%"...

			@ECHO ON		
			cl %COMPILER_OPTIONS% %SOURCE_CODE_FILES% "%COMPILED_RESOURCE_PATH%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF

		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_9X_32%".
		EXIT /B 1
	)

	:SKIP_BUILD_9X

	IF "%BUILD_MODE%"=="release" (
		ECHO.
		ECHO [%~nx0] Deleting any *.obj and *.res files...

		DEL /Q "%RELEASE_BUILD_PATH%\*.obj"
		DEL /Q "%RELEASE_BUILD_PATH%\*.res"

		ECHO.
		ECHO [%~nx0] Creating the release README file...
	
		ECHO Web Cache Exporter v%BUILD_VERSION%> "%RELEASE_README_PATH%"
		ECHO.>> "%RELEASE_README_PATH%"
		TYPE "%README_BODY_PATH%">> "%RELEASE_README_PATH%"
		ECHO.>> "%RELEASE_README_PATH%"
		TYPE "%LICENSE_FILE_PATH%">> "%RELEASE_README_PATH%"
	)

	ECHO.
	ECHO [%~nx0] The build process for version %BUILD_VERSION% completed successfully.
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
	REM This will prevent sending someone a debug build that had the EXPORT_EMPTY_FILES macro defined.
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
	"%_7ZIP_EXE_PATH%" a "%SOURCE_ARCHIVE_PATH%" "%BATCH_FILE_PATH%" "%VERSION_FILE_PATH%" "%README_BODY_PATH%" "%LICENSE_FILE_PATH%" >NUL

	ECHO.

	:SKIP_PACKAGE_BUILD

	ECHO [%~nx0] Finished running.

POPD
ENDLOCAL
