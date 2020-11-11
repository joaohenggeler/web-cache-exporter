@ECHO OFF

REM A very simple script used to compile any Java classes in this directory.

SETLOCAL
PUSHD "%~dp0"

	CLS

	javac *.java

	ECHO.

	IF ERRORLEVEL 1 (
		ECHO [%~nx0] An error occurred while compiling the classes.
	) ELSE (
		ECHO [%~nx0] Compiled every class successfully.
	)

POPD
ENDLOCAL
