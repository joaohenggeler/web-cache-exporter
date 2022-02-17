# Web Cache Exporter

A command line tool that allows you to more easily view and obtain the contents of a web browser or web plugin's HTTP cache that's stored on disk. It runs on Windows 98, ME, 2000, XP, Vista, 7, 8.1, and 10, and supports the following cache formats:

* The WinINet cache - Internet Explorer 4 to 11.
* The Mozilla cache - Mozilla Firefox, Netscape Navigator 6.1 to 9, etc.
* The Flash Player's shared library cache and temporary Flash videos.
* The Shockwave Player's cache, including Xtras.
* The Java Plugin's cache - Java 1.3 to 8.
* The Unity Web Player's cache.

This tool was developed to aid the [recovery and preservation of lost web media](https://bluemaxima.org/flashpoint/) (games, animations, virtual worlds, etc) whose files might exist in old computers where they were viewed or played when they were still available. [Here's a list of browser games whose assets were found by searching the web cache](recovered_games.md).

![A web game being recovered from Internet Explorer's cache.](Images/recovered_game.png)

## Features

* Runs natively on Windows 98, ME, 2000, XP, Vista, 7, 8.1, and 10.

* Copies each cached file using their original website's directory structure.

* Creates CSV files that list every cached file along with additional information (URL, HTTP headers, SHA-256 hash, etc).

* Finds the default and user-defined cache locations of various web browsers.

* Supports labelling cached files based on their file signatures, MIME types, file extensions, and URLs. Files can be grouped and filtered by their format or original domain.

<!-- * Decompresses cached files based on the Content-Encoding value (Gzip, Zlib, Raw DEFLATE, Brotli, Compress) in their HTTP headers. -->

## Screenshots

![The Web Cache Exporter being executed in the command line.](Images/command_line.png)

![A CSV file created by the Web Cache Exporter.](Images/csv_file.png)

![The website directory structure created by the Web Cache Exporter.](Images/website_structure.png)

<img alt="Some cached files exported by the Web Cache Exporter." src="Images/exported_files.png" width="622" height="414">

## Command Line Arguments

See the [help file](readme_body.txt) to learn how to use this application.

## Building

See the [building instructions](Building.txt) to learn how to compile this application.

## Resources And Tools

This section lists some useful resources and tools that were used throughout this application's development. This includes learning how to process certain cache formats, validating the application's output, extracting assets from plugin-specific file formats, and other general purpose tools.

### Internet Explorer

* [Geoff Chappell - The INDEX.DAT File Format](https://www.geoffchappell.com/studies/windows/ie/wininet/api/urlcache/indexdat.htm).

* [libmsiecf - MSIE Cache File (index.dat) format specification](https://github.com/libyal/libmsiecf/blob/master/documentation/MSIE%20Cache%20File%20%28index.dat%29%20format.asciidoc).

* [NirSoft - IECacheView - Internet Explorer Cache Viewer](https://www.nirsoft.net/utils/ie_cache_viewer.html).

* [NirSoft - A few words about the cache / history on Internet Explorer 10](https://blog.nirsoft.net/2012/12/08/a-few-words-about-the-cache-history-on-internet-explorer-10/).

* [NirSoft - Improved solution for reading the history of Internet Explorer 10](https://blog.nirsoft.net/2013/05/02/improved-solution-for-reading-the-history-of-internet-explorer-10/).

### Mozilla Firefox And Friends

* [firefox-cache-forensics - FfFormat.wiki](https://code.google.com/archive/p/firefox-cache-forensics/wikis/FfFormat.wiki).

* [dtformats - Firefox cache file format](https://github.com/libyal/dtformats/blob/main/documentation/Firefox%20cache%20file%20format.asciidoc).

* The Mozilla Firefox repository - [first](https://hg.mozilla.org/mozilla-central/file/2d6becec52a482ad114c633cf3a0a5aa2909263b/netwerk/cache) and [second](https://hg.mozilla.org/mozilla-central/file/tip/netwerk/cache2) version of the Mozilla cache format.

* [NirSoft - MZCacheView - View the cache files of Firefox Web browsers](https://www.nirsoft.net/utils/mozilla_cache_viewer.html).

### Flash Player

* [NirSoft's FlashCookiesView](https://www.nirsoft.net/utils/flash_cookies_view.html) can be used to view Flash cookies (.SOL files) where a game might save its progress or cache assets.

* [NirSoft's VideoCacheView](https://www.nirsoft.net/utils/video_cache_view.html) can be used to recover Flash videos (.FLV files) from the web cache.

### Shockwave Player

* [TOMYSSHADOW's Movie Restorer Xtra](https://github.com/tomysshadow/Movie-Restorer-Xtra) allows you to open Shockwave movies in Director. This is useful when trying to find out the name of a Shockwave game (e.g. by looking at the game's menu screen) since the plugin's cache does not store the original filename or URL.

* [Valentin's Unpack tool](https://valentin.dasdeck.com/lingo/unpack/) allows you to extract Xtras from Xtra-Packages (.W32 files). This is useful for finding out which Xtras were stored in the plugin's cache.

### Unity Web Player

* [uTinyRipper](https://github.com/mafaca/UtinyRipper) and [DerPopo's Unity Assets Bundle Extractor](https://github.com/DerPopo/UABE) can be used to extract assets from the cached AssetBundle files.

### Other

* [Geoff Chappell's software analysis website](https://www.geoffchappell.com) was also used to check the minimum supported Windows version for some functions in the Windows API.

* [NirSoft's CSVFileView](https://www.nirsoft.net/utils/csv_file_view.html) is a useful lightweight tool for viewing the resulting CSV files.

* See also [NirSoft's browser tools](https://www.nirsoft.net/web_browser_tools.html), including [ChromeCacheView](https://www.nirsoft.net/utils/chrome_cache_view.html), [OperaCacheView](https://www.nirsoft.net/utils/opera_cache_view.html), and [SafariCacheView](https://www.nirsoft.net/utils/safari_cache_view.html).

## Special Thanks

* Special thanks to Computerdude77 for his general assistance and for helping me test this tool in older Windows versions.
* Special thanks to [TOMYSSHADOW](https://github.com/tomysshadow) for his extensive Director and Shockwave knowledge.
