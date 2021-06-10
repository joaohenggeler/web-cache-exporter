Option Explicit

' Runs the Web Cache Exporter with a specific set of command line options that tell it to search for potentially
' lost web media (games, animations, virtual worlds, etc). This script is compatible with Windows 98 SE to 10.
'
' [VVI] "VBScript Version Information"
' --> https://www.vbsedit.com/html/04075c37-ceb2-46ea-8b00-47fb430450c3.asp
' --> Used to check if certain VBScript features were available in the targeted Windows versions.

Dim FS
Set FS = WScript.CreateObject("Scripting.FileSystemObject")
Const ForReading = 1

Dim Shell
Set Shell = WScript.CreateObject("WScript.Shell")
Dim ShellErrorCode

WScript.Echo("Running WSH version: " & WScript.Version)

' Choose the correct executable depending on the Windows version. We'll use Shell.Run() to write the VER command's
' output to a temporary file since Shell.Exec() doesn't seem to exist in older Windows versions.

Const TEMPORARY_FILE_PATH = ".\temp.txt"
ShellErrorCode = Shell.Run("%COMSPEC% /C VER > """ & TEMPORARY_FILE_PATH & """", 0, True)

Dim WindowsVersion
Const DEFAULT_WINDOWS_VERSION = 5

If ShellErrorCode = 0 And FS.FileExists(TEMPORARY_FILE_PATH) Then
	
	Dim VersionFile
	Set VersionFile = FS.OpenTextFile(TEMPORARY_FILE_PATH, ForReading)

	Dim VersionOutput
	VersionOutput = VersionFile.ReadAll()
	WScript.Echo("Found the following Windows version: " & VersionOutput)

	VersionFile.Close()
	Set VersionFile = Nothing
	FS.DeleteFile(TEMPORARY_FILE_PATH)
	
	' Extract the major version number from the VER command's output. Some examples:
	' Windows 98: 		"Windows 98 [Version 4.10.2222]"
	' Windows ME: 		"Windows Millennium [Version 4.90.3000]"
	' Windows 2000: 	"Microsoft Windows 2000 [Version 5.00.2195]"
	' Windows XP: 		"Microsoft Windows XP [Version 5.1.2600]"
	' Windows Vista: 	"Microsoft Windows [Version 6.0.6000]"
	' Windows 7: 		"Microsoft Windows [Version 6.1.7601]"
	' Windows 8.1: 		"Microsoft Windows [Version 6.3.9600]"
	' Windows 10: 		"Microsoft Windows [Version 10.0.19042.928]"

	' Skip the first part since the name may contain numbers (e.g. 98 or 8.1).
	Dim SplitVersionOutput
	SplitVersionOutput = Split(VersionOutput, "[")
	VersionOutput = SplitVersionOutput(1)

	Dim VersionRegex
	Set VersionRegex = New RegExp
	VersionRegex.Pattern = "\d+"

	Dim RegexMatches
	Set RegexMatches = VersionRegex.Execute(VersionOutput)

	If RegexMatches.Count > 0 Then
		WindowsVersion = CInt(RegexMatches.Item(0))
	Else
		WScript.Echo("Could not find an integer in the version command's output.")
		WindowsVersion = DEFAULT_WINDOWS_VERSION
	End if

Else
	WScript.Echo("Failed to execute the version command with the error code: " & ShellErrorCode)
	WindowsVersion = DEFAULT_WINDOWS_VERSION
End If

WScript.Echo("Found the following Windows major version: " & WindowsVersion)
WScript.Echo()

Dim ExecutablePath

If WindowsVersion <= 4 Then
	ExecutablePath = "WCE9x32.exe"
Else
	ExecutablePath = "WCE32.exe"
End If

ExecutablePath = FS.BuildPath("..", ExecutablePath)

Dim ExporterCommand
Const COPY_FILES_OPTIONS = "-overwrite -no-create-csv -filter-by-groups -ignore-filter-for ""plugins"" -load-group-files ""006-Plugin/100-Lost-Media-Websites"" -find-and-export-all ""WebMedia\Files"""
Const CREATE_CSV_OPTIONS = "-overwrite -no-copy-files -find-and-export-all ""WebMedia\FullList"""

ExporterCommand = """" & ExecutablePath & """ " & COPY_FILES_OPTIONS
WScript.Echo("Searching for lost web media using the command: " & ExporterCommand)
WScript.Echo()
ShellErrorCode = Shell.Run(ExporterCommand, 0, True)

ExporterCommand = """" & ExecutablePath & """ " & CREATE_CSV_OPTIONS
WScript.Echo("Listing every cached file using the command: " & ExporterCommand)
WScript.Echo()
ShellErrorCode = Shell.Run(ExporterCommand, 0, True)

WScript.Echo("Finished running the Visual Basic Script.")
WScript.Echo()
