@ECHO OFF

REM xxx

SETLOCAL
PUSHD "%~dp0"

	RMDIR /S /Q "ExportedCache"
	
	SET "CLEAN_BUILD=Yes"
	REM Debug or Release
	SET "BUILD_MODE=Debug"

	REM C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat
	SET "VCVARSALL_PATH=C:\Program Files (x86)\Microsoft Visual Studio 8\VC\vcvarsall.bat"
	REM Option for vcvarsall.bat that specifies the Visual Studio compiler toolset to use.
	REM Visual C++ 2005 Express Edition (includes the 32-bit Visual C++ compiler toolset).
	SET "VCVARS_VER_OPTION=-vcvars_ver=8.0"

	REM ---------------------------------------------------------------------------
	
	REM /MTd defines the _DEBUG macro
	SET "COMPILER_OPTIONS=/W4 /WX /Oi /GR- /nologo"
	SET "RELEASE_ONLY_COMPILER_OPTIONS=/O2 /MT"
	SET "DEBUG_ONLY_COMPILER_OPTIONS=/Od /MTd /Zi /Fm /FC /D DEBUG /D NDEBUG /D _DEBUG"

	SET "SOURCE_FILES=%~dp0Source\*.cpp"

	SET "LINKER_OPTIONS=/link /WX /NODEFAULTLIB"
	SET "WIN_NT_LINKER_OPTIONS=/OPT:NOWIN98"
	SET "WIN_9X_LINKER_OPTIONS=/OPT:WIN98"
	SET "RELEASE_ONLY_LINKER_OPTIONS=/OPT:REF"
	SET "DEBUG_ONLY_LINKER_OPTIONS="

	SET "LIBRARIES=Kernel32.lib User32.lib Advapi32.lib Shlwapi.lib Shell32.lib"
	SET "RELEASE_ONLY_LIBRARIES=LIBCMT.lib"
	SET "DEBUG_ONLY_LIBRARIES=LIBCMTD.lib"

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
	)

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

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %DEBUG_ONLY_COMPILER_OPTIONS% /D BUILD_VERSION=\"%BUILD_VERSION%-debug\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %DEBUG_ONLY_LINKER_OPTIONS%"
		SET "LIBRARIES=%LIBRARIES% %DEBUG_ONLY_LIBRARIES%"

		SET "BUILD_DIR_32=%DEBUG_BUILD_DIR_32%"
		SET "BUILD_DIR_64=%DEBUG_BUILD_DIR_64%"
		SET "BUILD_DIR_9X_32=%DEBUG_BUILD_DIR_9X_32%"

	) ELSE IF "%BUILD_MODE%"=="Release" (

		ECHO [%~nx0] Compiling in %BUILD_MODE% mode...
		ECHO.

		SET "COMPILER_OPTIONS=%COMPILER_OPTIONS% %RELEASE_ONLY_COMPILER_OPTIONS% /D BUILD_VERSION=\"%BUILD_VERSION%-release\""
		SET "LINKER_OPTIONS=%LINKER_OPTIONS% %RELEASE_ONLY_LINKER_OPTIONS%"
		SET "LIBRARIES=%LIBRARIES% %RELEASE_ONLY_LIBRARIES%"

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

	ECHO [%~nx0] Windows NT 32-bit %BUILD_MODE% Build
	ECHO.

	IF NOT EXIST "%BUILD_DIR_32%" (
		MKDIR "%BUILD_DIR_32%"
	)

	SETLOCAL
		PUSHD "%BUILD_DIR_32%"
			CALL "%VCVARSALL_PATH%" x86
			@ECHO ON
			cl %COMPILER_OPTIONS% /D BUILD_32_BIT /Fe"%EXE_FILENAME_32%" "%SOURCE_FILES%" %LIBRARIES% %LINKER_OPTIONS% %WIN_NT_LINKER_OPTIONS%
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

	ECHO [%~nx0] Windows NT 64-bit %BUILD_MODE% Build
	ECHO.

	IF NOT EXIST "%BUILD_DIR_64%" (
		MKDIR "%BUILD_DIR_64%"
	)

	SETLOCAL
		PUSHD "%BUILD_DIR_64%"
			CALL "%VCVARSALL_PATH%" x64
			@ECHO ON
			cl %COMPILER_OPTIONS% /D BUILD_64_BIT /Fe"%EXE_FILENAME_64%" "%SOURCE_FILES%" %LIBRARIES% %LINKER_OPTIONS% %WIN_NT_LINKER_OPTIONS%
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

	ECHO [%~nx0] Windows 9X 32-bit %BUILD_MODE% Build
	ECHO.

	IF NOT EXIST "%BUILD_DIR_9X_32%" (
		MKDIR "%BUILD_DIR_9X_32%"
	)

	SETLOCAL
		PUSHD "%BUILD_DIR_9X_32%"
			CALL "%VCVARSALL_PATH%" x86
			@ECHO ON
			cl %COMPILER_OPTIONS% /D BUILD_9X /D BUILD_32_BIT /Fe"%EXE_FILENAME_9X_32%" "%SOURCE_FILES%" %LIBRARIES% %LINKER_OPTIONS% %WIN_9X_LINKER_OPTIONS%
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

POPD
ENDLOCAL
