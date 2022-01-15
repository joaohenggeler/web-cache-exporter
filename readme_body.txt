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
* The Mozilla cache - Mozilla Firefox, Netscape Navigator 6.1 to 9, etc.
* The Flash Player's shared library cache and temporary Flash videos.
* The Shockwave Player's cache, including Xtras.
* The Java Plugin's cache - Java 1.3 to 8.
* The Unity Web Player's cache.

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
will be used to refer to any of these three executables.

Usage: WCE.exe [Other Optional Arguments] <Mandatory Export Argument>

Only one export argument may be used. All arguments after the first
export option are ignored.

This tool performs two tasks for each cache location: 1) creates a CSV
file that lists every cached file along with additional info (URL, HTTP
headers, SHA-256 hash, etc); 2) copies each cached file using their
original website's directory structure.

When executed, this tool will create a log file called WCE.log in the
current working directory.

The generated log and CSV files use UTF-8 as their character encoding,
even in Windows 98/ME.

======================================================================
EXPORT ARGUMENTS
======================================================================

The following options can take two arguments:
> WCE.exe -export-option [Optional Cache Path] [Optional Output Path]

If a path is empty or not specified, then the application will replace
it with a default value. For the cache path, this includes several
default locations that vary depending on the browser or plugin. For the
output path, this default value is set to "ExportedCache".

You can use "." to refer to the current working directory's path. Note
that all paths passed to this application are limited to 260 characters.

The names of the output folder and CSV file depend on the export option
(see the "Output Name" fields below). For example, using -export-mozilla
would create a directory called "MZ" and a CSV file called "MZ.csv" in
the output path.

For example:
> WCE.exe -export-internet-explorer
> WCE.exe -export-mozilla "C:\PathToTheCache"
> WCE.exe -export-java "C:\PathToTheCache" "My Cache"
> WCE.exe -export-shockwave "" "My Default Cache"

======================================================================

* Long Option: -export-internet-explorer
* Short Option: -eie
* Arguments: [Optional Cache Path] [Optional Output Path]
* Description: Exports the WinINet cache, including Internet Explorer 4
to 11.
* Output Name: IE

When exporting Internet Explorer 4 to 9's cache, this tool will also
create a second output folder and CSV file called "IE-RAW". This is done
to copy files that might still exist on disk despite not being listed in
the cache index. Exporting the cache from Internet Explorer 10 and 11
is only supported in Windows Vista and later.

======================================================================

* Long Option: -export-mozilla
* Short Option: -emz
* Arguments: [Optional Cache Path] [Optional Output Path]
* Description: Exports the Mozilla cache, including Mozilla Firefox and
Netscape Navigator 6.1 to 9.
* Output Name: MZ

The default locations for the following Mozilla-based browsers are
supported: Mozilla Firefox, SeaMonkey, Pale Moon, Basilisk, Waterfox,
K-Meleon, Netscape Navigator (6.1 to 9), the Mozilla Suite, Mozilla
Firebird, Phoenix.

======================================================================

* Long Option: -export-flash
* Short Option: -efl
* Arguments: [Optional Cache Path] [Optional Output Path]
* Description: Exports the Flash Player's shared library cache and
temporary Flash videos.
* Output Name: FL

======================================================================

* Long Option: -export-shockwave
* Short Option: -esw
* Arguments: [Optional Cache Path] [Optional Output Path]
* Description: Exports the Shockwave Player's cache, including Xtras.
* Output Name: SW

======================================================================

* Long Option: -export-java
* Short Option: -ejv
* Arguments: [Optional Cache Path] [Optional Output Path]
* Description: Exports the Java Plugin's cache from Java 1.3 to 8.
* Output Name: JV

======================================================================

* Long Option: -export-unity
* Short Option: -eun
* Arguments: [Optional Cache Path] [Optional Output Path]
* Description: Exports the Unity Web Player's cache.
* Output Name: UN

======================================================================

There are two other export options that have a similar behavior but that
take different arguments.

======================================================================

* Long Option: -find-and-export-all
* Short Option: -faea
* Arguments: [Optional Output Path] [Optional External Locations File Path]
* Description: Exports all of the previous cache formats at once.
* Output Name: <All Previous Output Names>

This option is used to export every supported cache format from their
default locations at the same time. The second optional argument specifies
a text file that allows you to set these default locations and perform
this operation multiple times. This is useful when you want to export the
web cache from multiple user profiles located in an old computer's files.
To learn how to use this feature, see the "About External Locations" help
file in the "ExternalLocations" folder.

For example:
> WCE.exe -find-and-export-all
> WCE.exe -find-and-export-all "My Default Cache"
> WCE.exe -find-and-export-all "My Default Cache" "C:\PathToExternalLocationsFile"

======================================================================

* Long Option: -explore-files
* Short Option: -ef
* Arguments: <Mandatory Cache Path> [Optional Output Path]
* Description: Exports any files in a directory and its subdirectories.
* Output Name: EXPLORE

This option may be used to explore the files in an unsupported cache location
(e.g. from an obscure web plugin), meaning the first argument must always be
passed. This feature is useful when combined with group files. You can also
use the -csvs-only option to only create the CSV file and prevent a large
number of files from being copied.

For example:
> WCE.exe -explore-files "C:\PathToExplore"
> WCE.exe -csvs-only -explore-files "C:\PathToExplore" "My Exploration"

======================================================================
OTHER ARGUMENTS
======================================================================

These extra command line arguments are optional. The -export-option
argument is used to refer to any of the previous export options.

======================================================================

* Long Option: -version
* Short Option: -v
* Arguments: None.
* Description: Prints the application's version and exits.

For example:
> WCE.exe -version

======================================================================

* Long Option: -no-log
* Short Option: -nl
* Arguments: None.
* Description: Stops the tool from creating the log file.

For example:
> WCE.exe -no-log -export-option

======================================================================

* Long Option: -quiet
* Short Option: -q
* Arguments: None.
* Description: Stops the tool from printing any messages to the console.

For example:
> WCE.exe -quiet -export-option

======================================================================

* Long Option: -no-decompress
* Short Option: -nd
* Arguments: None.
* Description: Stops the tool from automatically decompressing each
cached file according to the Content-Encoding value in their HTTP headers.

This tool supports the following compression formats and Content-Encoding
values:

- Gzip, Zlib, Raw DEFLATE: gzip, x-gzip, deflate
- Brotli: br

For example:
> WCE.exe -no-decompress -export-option

Failing to decompress a supported format or finding an unknown one are
not fatal errors. The tool will simply export the cached file as is.

======================================================================

* Long Option: -no-delete-old-temp
* Short Option: -ndot
* Arguments: None.
* Description: Stops the tool from automatically deleting any temporary
directories created by previous executions.

This tool creates a temporary directory on startup to perform certain
operations when exporting cached files. This directory and its contents
are deleted when the tool terminates.

Because these files may contain sensitive data, this tool tries to make
sure that any files from a previous execution are always deleted. Since
this would prevent the application from being run multiple times in
parallel, this option was added to skip this step.

For example (ran in parallel):
> WCE.exe -no-delete-old-temp -export-option
> WCE.exe -no-delete-old-temp -export-option
> WCE.exe -no-delete-old-temp -export-option

These temporary directories all begin with the "WCE" prefix and are
created inside the Windows Temporary Files directory.

======================================================================

* Long Option: -csvs-only
* Short Option: -co
* Arguments: None.
* Description: Only creates CSV files (don't export cached files).

* Long Option: -files-only
* Short Option: -fo
* Arguments: None.
* Description: Only exports cached files (don't create CSV files).

Using both -csvs-only and -files-only will result in an error and
terminate the application.

For example:
> WCE.exe -csvs-only -export-option
> WCE.exe -files-only -export-option

======================================================================

* Long Option: -overwrite
* Short Option: -o
* Arguments: None.
* Description: Deletes the previous output folder of the same name
before running.

For example:
> WCE.exe -overwrite -export-option

======================================================================

* Long Option: -show-full-paths
* Short Option: -sfp
* Arguments: None.
* Description: Replaces the "Location On Cache" and "Location In Output"
CSV columns with the absolute paths on disk.

The -show-full-paths option does nothing if -files-only is also used.

For example:
> WCE.exe -show-full-paths -export-option

======================================================================

* Long Option: -group-by-origin
* Short Option: -gbo
* Arguments: None.
* Description: Adds a cached file's request origin domain to the beginning
of the website directory structure. If a cached file doesn't have this
information, the normal URL structure is used instead.

The request origin currently only exists in the Mozilla cache format.

For example:
> WCE.exe -group-by-origin -export-option

======================================================================

The following options change how group files behave. Group files are
simple text files that tell the application how to label cached files
based on their file signatures, MIME types, file extensions, and URLs.
To learn more about this feature, see the "About Groups" help file in
the "Groups" folder.

======================================================================

* Long Option: -filter-by-groups
* Short Option: -fbg
* Arguments: <Group Files>
* Description: Only exports files that match groups defined in the
specified group files.

The <Group Files> argument is mandatory and specifies a filename list,
where the filenames are separated by forward slashes and appear without
the .group file extension. This option tells the application that a cached
file may only be exported if it matches the groups that are defined in the
files specified by this list.

For example:
> WCE.exe -filter-by-groups "006-Plugin/101-Gaming-Websites" -export-option

This would filter the output based on the groups that are defined in the
files "006-Plugin.group" and "101-Gaming-Websites.group".

======================================================================

* Long Option: -ignore-filter-for
* Short Option: -iff
* Arguments: <Cache Names>
* Description: Overrides the -filter-by-groups option for specific browsers
or plugins.

The <Cache Names> argument is mandatory and specifies a list of browser
or plugin names, separated by forward slashes. The names "browsers" and
"plugins" can be used to refer to all browsers or plugins, respectively.
These two categories are mutually exclusive.

Web Browsers: "ie", "mozilla".
Web Plugins: "flash", "shockwave", "java", "unity".

For example:
> WCE.exe -fbg "006-Plugin/101-Gaming-Websites" -ignore-filter-for "plugins/mozilla" -export-option

This would filter the output based on any loaded groups, except for any
web plugins or Mozilla-based browsers.

======================================================================

* Long Option: -hint-ie
* Short Option: -hie
* Arguments: <Local AppData Path>
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
> WCE.exe -hint-ie "C:\Users\My Old PC\AppData\Local" -eie "D:\Path To The Cache Database Files That Came From Another Computer"

This option can only be used with -export-internet-explorer, and cannot
be used with -find-and-export-all (as different user profiles would
have different Local AppData locations).

======================================================================
SPECIAL THANKS
======================================================================

* Special thanks to Computerdude77 for his general assistance and for
helping me test this tool in older Windows versions.

* Special thanks to TOMYSSHADOW for his extensive Director and Shockwave
knowledge: https://github.com/tomysshadow

======================================================================
ACKNOWLEDGMENTS
======================================================================

The Web Cache Exporter is made possible thanks to the following
third-party software:

* "Portable C++ Hashing Library" by Stephan Brumme
* Project Page: https://github.com/stbrumme/hash-library

* "Zlib" by Jean-loup Gailly and Mark Adler
* Project Page: https://zlib.net/

* "Brotli" by Google
* Project Page: https://github.com/google/brotli

======================================================================
LICENSE
======================================================================
