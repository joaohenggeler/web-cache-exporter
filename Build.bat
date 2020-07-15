@ECHO OFF

REM xxx

SETLOCAL
PUSHD "%~dp0"

	RMDIR /S /Q "Exported-Cache"
	
	SET "CLEAN_BUILD=Yes"
	REM Debug or Release
	SET "BUILD_MODE=Debug"

	REM C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat
	SET "VCVARSALL_PATH=C:\Program Files (x86)\Microsoft Visual Studio 8\VC\vcvarsall.bat"
	REM Option for vcvarsall.bat that specifies the Visual Studio compiler toolset to use.
	REM Visual C++ 2005 Express Edition (includes the 32-bit Visual C++ compiler toolset).
	SET "VCVARS_VER_OPTION=-vcvars_ver=8.0"

	REM ---------------------------------------------------------------------------
	
	SET "SOURCE_PATH=%~dp0Source"
	SET "SOURCE_FILES=%SOURCE_PATH%\*.cpp"
	SET "THIRD_PARTY_SOURCE_PATH=%SOURCE_PATH%\ThirdParty"
	SET "THIRD_PARTY_INCLUDE_PATH=%THIRD_PARTY_SOURCE_PATH%\Include"
	SET "THIRD_PARTY_LIBRARIES_PATH_32=%THIRD_PARTY_SOURCE_PATH%\Lib"
	SET "THIRD_PARTY_LIBRARIES_PATH_64=%THIRD_PARTY_SOURCE_PATH%\Lib\x64"

	REM /MTd defines the _DEBUG macro
	SET "COMPILER_OPTIONS=/W4 /WX /Oi /GR- /nologo /I "%THIRD_PARTY_INCLUDE_PATH%""
	SET "COMPILER_OPTIONS_RELEASE_ONLY=/O2 /MT"
	SET "COMPILER_OPTIONS_DEBUG_ONLY=/Od /MTd /Zi /Fm /FC /D DEBUG /D NDEBUG /D _DEBUG"
	SET "COMPILER_OPTIONS_WIN_NT_ONLY="
	SET "COMPILER_OPTIONS_WIN_9X_ONLY=/D BUILD_9X"
	SET "COMPILER_OPTIONS_32_ONLY=/D BUILD_32_BIT"
	SET "COMPILER_OPTIONS_64_ONLY="

	SET "LIBRARIES=Kernel32.lib Advapi32.lib Shell32.lib Shlwapi.lib"
	SET "LIBRARIES_RELEASE_ONLY=LIBCMT.lib"
	SET "LIBRARIES_DEBUG_ONLY=LIBCMTD.lib"
	SET "LIBRARIES_32_ONLY="
	SET "LIBRARIES_64_ONLY="
	SET "LIBRARIES_WIN_NT_ONLY=ESENT.lib"
	SET "LIBRARIES_WIN_9X_ONLY="
	
	SET "LINKER_OPTIONS=/link /WX /NODEFAULTLIB"
	SET "LINKER_OPTIONS_RELEASE_ONLY=/OPT:REF"
	SET "LINKER_OPTIONS_DEBUG_ONLY="
	SET "LINKER_OPTIONS_WIN_NT_ONLY=/OPT:NOWIN98"
	SET "LINKER_OPTIONS_WIN_9X_ONLY=/OPT:WIN98"
	SET "LINKER_OPTIONS_32_ONLY=/LIBPATH:"%THIRD_PARTY_LIBRARIES_PATH_32%""
	SET "LINKER_OPTIONS_64_ONLY=/LIBPATH:"%THIRD_PARTY_LIBRARIES_PATH_64%"
	
	REM ---------------------------------------------------------------------------

	SET "MAIN_BUILD_DIR=Builds"

	SET "RELEASE_BUILD_DIR=%MAIN_BUILD_DIR%\Release"

	SET "DEBUG_BUILD_DIR=%MAIN_BUILD_DIR%\Debug"
	SET "DEBUG_BUILD_DIR_32=%DEBUG_BUILD_DIR%\Build-x86-32"
	SET "DEBUG_BUILD_DIR_64=%DEBUG_BUILD_DIR%\Build-x86-64"
	SET "DEBUG_BUILD_DIR_9X_32=%DEBUG_BUILD_DIR%\Build-98-ME-x86-32"

	SET "EXE_FILENAME_32=Web-Cache-Exporter-x86-32.exe"
	SET "EXE_FILENAME_64=Web-Cache-Exporter-x86-64.exe"
	SET "EXE_FILENAME_9X_32=Web-Cache-Exporter-98-ME-x86-32.exe"

	REM ---------------------------------------------------------------------------

	CLS

	SET BUILD_VERSION=
	IF EXIST "version.txt" (
		SET /P BUILD_VERSION=<"version.txt"
	) ELSE (
		ECHO [%~nx0] The version file was not found. A placeholder value will be used instead.
		ECHO.
		SET "BUILD_VERSION=UNKNOWN"
	)

	REM SET "ZIP_PATH=_7z920-extra\7zr.exe"
	REM SET "DIR_TO_ZIP="
	REM SET "ARCHIVE_NAME_RELEASE=Web-Cache-Exporter-"
	REM SET "ARCHIVE_NAME_DEBUG="
	REM "%ZIP_PATH%" a "%ARCHIVE_NAME%" "%DIR_TO_ZIP%\"

	IF "%CLEAN_BUILD%"=="Yes" (

		ECHO [%~nx0] Cleaning build...
		ECHO.

		IF EXIST "%MAIN_BUILD_DIR%" (
			REM RMDIR /S /Q "%MAIN_BUILD_DIR%"

			DEL /S /Q "%MAIN_BUILD_DIR%\*.obj" >NUL
			DEL /S /Q "%MAIN_BUILD_DIR%\*.pdb" >NUL
			DEL /S /Q "%MAIN_BUILD_DIR%\*.exe" >NUL
			DEL /S /Q "%MAIN_BUILD_DIR%\*.map" >NUL
			DEL /S /Q "%MAIN_BUILD_DIR%\*.log" >NUL
			REM Leave .sln and .suo

			ECHO.
		)

	)

	IF "%BUILD_MODE%"=="Debug" (

		ECHO [%~nx0] Compiling in %BUILD_MODE% mode...
		ECHO.

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_DEBUG_ONLY% /D BUILD_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_DEBUG_ONLY%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_DEBUG_ONLY%"

		SET "BUILD_DIR_32=%DEBUG_BUILD_DIR_32%"
		SET "BUILD_DIR_64=%DEBUG_BUILD_DIR_64%"
		SET "BUILD_DIR_9X_32=%DEBUG_BUILD_DIR_9X_32%"

	) ELSE IF "%BUILD_MODE%"=="Release" (

		ECHO [%~nx0] Compiling in %BUILD_MODE% mode...
		ECHO.

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_RELEASE_ONLY% /D BUILD_VERSION=\"%BUILD_VERSION%\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_RELEASE_ONLY%"
		SET "LIBRARIES=%LIBRARIES% %LIBRARIES_RELEASE_ONLY%"

		SET "BUILD_DIR_32=%RELEASE_BUILD_DIR%"
		SET "BUILD_DIR_64=%RELEASE_BUILD_DIR%"
		SET "BUILD_DIR_9X_32=%RELEASE_BUILD_DIR%"

	) ELSE (
		ECHO [%~nx0] Unknown build mode '%BUILD_MODE%'.
		EXIT /B 1
	)

	SET "COMMAND_LINE_COMPILER_OPTIONS=%*"
	IF "%COMMAND_LINE_COMPILER_OPTIONS%" NEQ "" (
		ECHO [%~nx0] Passing extra compiler options from the command line: "%COMMAND_LINE_COMPILER_OPTIONS%"...
		ECHO.
		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMMAND_LINE_COMPILER_OPTIONS%"
	)

	REM ---------------------------------------------------------------------------
	REM ------------------------- Windows NT 32-bit Build -------------------------
	REM ---------------------------------------------------------------------------

	ECHO [%~nx0] Windows NT 32-bit %BUILD_MODE% Build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_DIR_32%" (
		MKDIR "%BUILD_DIR_32%"
	)

	SETLOCAL
		PUSHD "%BUILD_DIR_32%"

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_WIN_NT_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_32_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% /Fe"%EXE_FILENAME_32%""
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_32_ONLY%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_32_ONLY%"

			CALL "%VCVARSALL_PATH%" x86
			@ECHO ON
			cl %COMPILER_OPTIONS% "%SOURCE_FILES%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF

		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_32%"
		EXIT /B 1
	)

	ECHO.
	ECHO.
	ECHO.

	REM ---------------------------------------------------------------------------
	REM ------------------------- Windows NT 64-bit Build -------------------------
	REM ---------------------------------------------------------------------------

	ECHO [%~nx0] Windows NT 64-bit %BUILD_MODE% Build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_DIR_64%" (
		MKDIR "%BUILD_DIR_64%"
	)

	SETLOCAL
		PUSHD "%BUILD_DIR_64%"

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_WIN_NT_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_64_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% /Fe"%EXE_FILENAME_64%""
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_64_ONLY%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_WIN_NT_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_64_ONLY%"
			
			CALL "%VCVARSALL_PATH%" x64
			@ECHO ON
			cl %COMPILER_OPTIONS% "%SOURCE_FILES%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF

		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_64%"
		EXIT /B 1
	)

	ECHO.
	ECHO.
	ECHO.

	REM ---------------------------------------------------------------------------
	REM ------------------------- Windows 9x 32-bit Build -------------------------
	REM ---------------------------------------------------------------------------

	ECHO [%~nx0] Windows 9X 32-bit %BUILD_MODE% Build (v%BUILD_VERSION%)
	ECHO.

	IF NOT EXIST "%BUILD_DIR_9X_32%" (
		MKDIR "%BUILD_DIR_9X_32%"
	)

	SETLOCAL
		PUSHD "%BUILD_DIR_9X_32%"

			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_WIN_9X_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %COMPILER_OPTIONS_32_ONLY%"
			SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% /Fe"%EXE_FILENAME_9X_32%""
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_32_ONLY%"
			SET "LIBRARIES=%LIBRARIES% %LIBRARIES_WIN_9X_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_WIN_9X_ONLY%"
			SET "LINKER_OPTIONS=%LINKER_OPTIONS% %LINKER_OPTIONS_32_ONLY%"
			
			CALL "%VCVARSALL_PATH%" x86
			@ECHO ON
			cl %COMPILER_OPTIONS% "%SOURCE_FILES%" %LIBRARIES% %LINKER_OPTIONS%
			@ECHO OFF

		POPD
	ENDLOCAL

	IF ERRORLEVEL 1 (
		ECHO.
		ECHO [%~nx0] Error while building "%EXE_FILENAME_9X_32%"
		EXIT /B 1
	)

	IF "%BUILD_MODE%"=="Release" (

		ECHO.
		ECHO.
		ECHO.
		ECHO [%~nx0] Deleting *.obj files.

		DEL /Q "%RELEASE_BUILD_DIR%\*.obj"
	)

	ECHO.
	ECHO [%~nx0] The build process for version %BUILD_VERSION% completed successfully.

POPD
ENDLOCAL
