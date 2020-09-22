abc

def

123

==================================================
HOW TO RUN IT
==================================================

Groups files are text files that define zero or more groups, each one
specifying a list of attributes to match to files found on a web browser
or plugin's cache. There are two types of groups:

- File groups, which act on a file's properties (file signature, MIME
type, and file extension). These are used to label files according to
their file format. For example, files that are used by the Flash Player.

- URL groups, which act on a cached file's URL. These are used to label
files according to their original location on the internet. For example,
files that were hosted on a web portal like Newgrounds.com.

Groups files must always end in a .group extension. The Web Cache Exporter
only loads groups from files with this extension located in this directory.
This about file, for example, isn't loaded. Group files are loaded in
alphabetical order. In each file, groups are loaded from top to bottom.

The following section will show you how to define each type of group. Lines
that start with a semicolon ";" are treated as comments and are not processed.
Groups files must always end in a newline. You can only use spaces and tabs
as whitespace.

==================================================
LICENSE
==================================================
