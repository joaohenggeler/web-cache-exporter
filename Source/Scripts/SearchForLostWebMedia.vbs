Option Explicit

' Runs the Web Cache Exporter with a specific set of command line options that tell it to search for potentially
' lost web media (games, animations, virtual worlds, etc). This script is compatible with Windows 98 to 10.

Dim FS
Set FS = WScript.CreateObject("Scripting.FileSystemObject")

Dim Shell
Set Shell = WScript.CreateObject("WScript.Shell")

Const WshRunning = 0, WshFinished = 1, WshFailed = 2

' Choose the correct executable depending on the Windows version.

Dim VersionCommand
Set VersionCommand = Shell.Exec("%COMSPEC% /C VER")

Do While VersionCommand.Status = WshRunning
	WScript.Sleep(100)
Loop

Dim WindowsVersion
Const DEFAULT_WINDOWS_VERSION = 5

Select Case VersionCommand.Status
	
	Case WshFinished
		Dim VersionOutput
		VersionOutput = VersionCommand.StdOut.ReadAll()
		
		' Extract the major version number from the VER command's output. Some examples:
		' Windows 98: 		"Windows 98 [Version 4.10.2222]"
		' Windows ME: 		"Windows Millennium [Version 4.90.3000]"
		' Windows 2000: 	"Microsoft Windows 2000 [Version 5.00.2195]"
		' Windows XP: 		"Microsoft Windows XP [Version 5.1.2600]"
		' Windows Vista: 	"Microsoft Windows [Version 6.0.6000]"
		' Windows 7: 		"Microsoft Windows [Version 6.1.7601]"
		' Windows 8.1: 		"Microsoft Windows [Version 6.3.9600]"
		' Windows 10: 		"Microsoft Windows [Version 10.0.19042.928]"
		Dim VersionRegex
		Set VersionRegex = New RegExp
		VersionRegex.Pattern = "\d+"

		Dim RegexMatches
		Set RegexMatches = VersionRegex.Execute(VersionOutput)

		If RegexMatches.Count > 0 Then
			WindowsVersion = CInt(RegexMatches(0))
		Else
			WScript.Echo("Could not find an integer in the version command's output: " & VersionOutput)
			WindowsVersion = DEFAULT_WINDOWS_VERSION
		End if
	
	Case WshFailed
		WScript.Echo("Failed to execute the version command.")
		WindowsVersion = DEFAULT_WINDOWS_VERSION

End Select

Dim ExecutablePath
Const COMMAND_LINE_OPTIONS = "-overwrite -filter-by-groups -ignore-filter-for ""plugins"" -load-group-files ""006-Plugin/100-Lost-Media-Websites"" -find-and-export-all"

If WindowsVersion <= 4 Then
	ExecutablePath = "WCE9x32.exe"
Else
	ExecutablePath = "WCE32.exe"
End If

ExecutablePath = FS.BuildPath("..", ExecutablePath)

Dim ExporterCommand
ExporterCommand = """" & ExecutablePath & """ " & COMMAND_LINE_OPTIONS

WScript.Echo("Searching for lost web media using the command: " & ExporterCommand)
WScript.Echo()

Dim ShellErrorCode
ShellErrorCode = Shell.Run(ExporterCommand, 0, True)

WScript.Echo("Finished running the Visual Basic Script.")
WScript.Echo()
