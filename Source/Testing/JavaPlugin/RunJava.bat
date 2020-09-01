@ECHO OFF

REM A very simple script used to run any Java classes in this directory.

SETLOCAL
PUSHD "%~dp0"

	CLS

	java -cp "." GenerateIndexFiles "WCE.idx"

POPD
ENDLOCAL
