@ECHO OFF

REM Runs the Web Cache Exporter with a specific set of command line options that tell it to search for potentially
REM lost web media (games, animations, virtual worlds, etc). This batch file is compatible with Windows 98 to 10.
REM
REM This script is a simple wrapper for a Visual Basic Script that does the actual work. This is because batch files
REM in Windows 98 didn't have certain useful commands and features which were only added in later versions.

ECHO Are you sure you want to run this script?
PAUSE
ECHO.

CSCRIPT //nologo "SearchForLostWebMedia.vbs"

ECHO Finished running the batch file.
PAUSE
