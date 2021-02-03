By Joao Henggeler
GitHub Page: https://github.com/joaohenggeler/web-cache-exporter

======================================================================
DESCRIPTION
======================================================================

This command line tool allows you to more easily view and obtain the
contents of a web browser or web plugin's HTTP cache that's stored on
disk. It runs on Windows 98, ME, 2000, XP, Vista, 7, 8.1, and 10, and
supports the following cache formats:

* The WinINet cache - Internet Explorer 4 to 11.
* The Flash Player's shared library cache and temporary Flash videos.
* The Shockwave Player's cache, including Xtras.
* The Java Plugin's cache - Java 1.3 to 8.

This tool was developed to aid the recovery and preservation of lost web
media (games, animations, virtual worlds, etc) whose files might exist
in old computers where they were viewed or played when they were still
available.

======================================================================
COMMAND LINE ARGUMENTS
======================================================================

This utility includes three executables:
* WCE9x32.exe for Windows 98 and ME (32-bit).
* WCE32.exe for Windows 2000, XP, Vista, 7, 8.1, and 10 (32-bit).
* WCE64.exe for Windows XP, Vista, 7, 8.1, and 10 (64-bit).

The command line arguments in the next subsections may be used. WCE.exe
is used to refer to any of these three executables.

Usage: WCE.exe [Other Optional Arguments] <Mandatory Export Argument>

Only one export argument may be used. All arguments after the first
export option are ignored.

When executed, this tool will create a log file called WCE.log in the
current working directory. The generated log and CSV files use UTF-8 as
their character encoding.

======================================================================
EXPORT ARGUMENTS
======================================================================

The following options can take two arguments:
WCE.exe -export-option [Optional Cache Path] [Optional Output Path]

If a path is empty or not specified, then the application will replace
it with a default value. For the cache path, this includes several
default locations that vary depending on the browser or plugin. For the
output path, this value is set to "ExportedCache".

You can use "." to refer to the current working directory's path. Note
that all paths passed to this application are limited to 260 characters.

The names of the output folder and CSV file depend on the export option
(see the "Output Name" fields below). For example, using -export-ie
would create a directory called "IE" and a CSV file called "IE.csv" in
the output path.

* Option: -export-ie
* Description: Exports the WinINet cache, including Internet Explorer 4
to 11.
* Output Name: IE

* Option: -export-flash
* Description: Exports the Flash Player's shared library cache and
temporary Flash videos.
* Output Name: FL

* Option: -export-shockwave
* Description: Exports the Shockwave Player's cache, including Xtras.
* Output Name: SW

* Option: -export-java
* Description: Exports the Java Plugin's cache from Java 1.3 to 8.
* Output Name: JV

For example:
WCE.exe -export-ie
WCE.exe -export-flash "C:\PathToTheCache"
WCE.exe -export-shockwave "C:\PathToTheCache" "My Cache"
WCE.exe -export-java "" "My Default Cache"

When exporting Internet Explorer 4 to 9's cache, this tool will also
create a second output folder and CSV file called IE-RAW. This is done
to copy files that might still exist on disk despite not being listed in
the cache database. Exporting the cache from Internet Explorer 10 and 11
is only supported in Windows Vista and later.

There are two other options that have a similar behavior but that take
different arguments:

* Option: -find-and-export-all
* Description: Exports all of the above at once.
* Output Name: <See Above>

* Option: -explore-files
* Description: Exports any files in a directory and its subdirectories.
* Output Name: EXPLORE

The -find-and-export-all option can take two arguments:
WCE.exe -find-and-export-all [Optional Output Path] [Optional External
Locations File Path]

This option is used to export every supported cache format from their
default locations at the same time. The second optional argument specifies
a text file that allows you to set these default locations and perform
this operation multiple times. This is useful when you want to export the
web cache from multiple user profiles located in an old computer's files.
To learn how to use this feature, see the "About External Locations" help
file in the "ExternalLocations" folder.

For example:
WCE.exe -find-and-export-all
WCE.exe -find-and-export-all "My Default Cache"
WCE.exe -find-and-export-all "My Default Cache" "C:\PathToExternalLocationsFile"

The -explore-files option can take two arguments:
WCE.exe -explore-files <Mandatory Cache Path> [Optional Output Path]

This option may be used to explore the files in an unsupported cache location
(e.g. from an obscure web plugin), meaning the first argument must always be
passed. This feature is useful when combined with group files. You can also
use the -no-copy-files option to only create the CSV file and prevent a large
number of files from being copied.

For example:
WCE.exe -explore-files "C:\PathToExplore"
WCE.exe -no-copy-files -explore-files "C:\PathToExplore" "My Exploration"

======================================================================
OTHER ARGUMENTS
======================================================================

These extra command line arguments are optional. The -export-option
argument is used to refer to any of the previous export options.

The following options don't require any additional arguments.

* Option: -no-copy-files
* Description: Stops the exporter from copying files.

* Option: -no-create-csv
* Description: Stops the exporter from creating CSV files.

* Option: -overwrite
* Description: Deletes the previous output folder of the same name
before running.

* Option: -show-full-paths
* Description: Replaces the "Location On Cache" and "Location In Output"
CSV columns with the absolute paths on disk.

Using both -no-copy-files and -no-create-csv will result in an error
and terminate the application. The -show-full-paths option does nothing
if -no-create-csv is also used.

For example:
WCE.exe -no-copy-files -show-full-paths -export-option
WCE.exe -no-create-csv -overwrite -export-option

The following options change how group files behave. Group files are
simple text files that tell the application how to label cached files
based on their file signatures, MIME types, file extensions, and URLs.
To learn more about this feature, see the "About Groups" help file in
the "Groups" folder.

* Option: -filter-by-groups
* Description: Only exports files that match any loaded groups.

* Option: -load-group-files <Group Files>
* Description: Only loads specific group files.

The <Group Files> argument is mandatory and specifies a filename list,
where the filenames are separated by forward slashes and appear without
the .group file extension. All group files are loaded by default. This
tool will always look for group files in the "Groups" subdirectory in
the executable's directory (and not in the current working directory).

For example:
WCE.exe -filter-by-groups -load-group-files "006-Plugin/101-Gaming-Websites"
-export-option

This would load the group files "006-Plugin.group" and "101-Gaming-Websites.group",
and would filter the output based on the groups that they define.

The following options should only be used when exporting the WinINet
cache using -export-ie or -find-and-export-all.

* Option: -hint-ie <Local AppData Path>
* Description: Specifies the absolute path to the Local AppData folder
in the computer where the cache originated.

The <Local AppData Path> argument is mandatory. This option should only
be used if both of the following are true:
1. You're exporting the cache from Internet Explorer 10 and 11.
2. You're not exporting from a default location, i.e., if the cache
database files were copied from another computer.

If this is option is not used, the exporter will try to guess this
location. You should rerun this application with this option if you meet
the criteria above and you notice that some cached files were not exported.

For example:
WCE.exe -hint-ie "C:\Users\My Old PC\AppData\Local" -export-ie "C:\Path To
The Cache Database Files That Came From Another Computer"

======================================================================
SPECIAL THANKS
======================================================================

* Special thanks to Computerdude77 for his general assistance and for
helping me test this tool in older Windows versions.

* Special thanks to TOMYSSHADOW for his extensive Director and Shockwave
knowledge: https://github.com/tomysshadow

======================================================================
ACKNOWLEDGMENT
======================================================================

The Web Cache Exporter is made possible thanks to the following third
party software:

* "Portable C++ Hashing Library" by Stephan Brumme.
* Project Page: https://github.com/stbrumme/hash-library

======================================================================
LICENSE
======================================================================
