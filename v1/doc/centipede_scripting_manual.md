# Centipede Tcl Scripting Manual

This document provides a reference for the custom Tcl commands implemented in the Centipede firmware. These commands extend the standard Tcl 6.7 environment to interact with the device's filesystem, control the hardware, and provide additional utility functions. 

Standard Tcl 6.7 commands (like `set`, `if`, `while`, `proc`, etc.) are not documented here.

## 1. Centipede Filesystem

The following commands are used to interact with the Centipede's Virtual File System (VFS), which includes the internal flash and any mounted archives. Note that standard commands like `cd`, `pwd`, and `source` have been overridden to work correctly with this VFS.

*   **`ls [-ald] [directory...]`**: List the contents of a directory. Supports options `-a` (show all files), `-l` (long format, show file sizes), and `-d` (list directories themselves, not their contents).
*   **`mkdir <directory>`**: Create a new directory.
*   **`rmdir <directory>`**: Remove an empty directory.
*   **`cp <source> <destination>`**: Copy a file.
*   **`mv <source> <destination>`**: Move or rename a file or directory.
*   **`rm <file>`**: Remove a file.
*   **`cat <file>`**: Print the contents of a file to the console.
*   **`head [-N] <file>`**: Print the first `N` lines of a file (default is 10).
*   **`tail [-N] <file>`**: Print the last `N` lines of a file (default is 10).
*   **`hd <file>`**: Print a hexdump of a file's contents.
*   **`wc <file>`**: Print newline, word, and byte counts for a file.
*   **`grep [-nvli] <pattern> <file>...`**: Search for a regular expression pattern within files. Supports options for line numbers (`-n`), invert match (`-v`), list files (`-l`), and case-insensitive (`-i`).
*   **`edit <file>`**: Open a simple text editor for the specified file.
*   **`cd [directory]`**: Change the current working directory.
*   **`pwd`**: Print the current working directory.
*   **`df`**: Report file system disk space usage.
*   **`du [directory]`**: Estimate file space usage.
*   **`fs command args... [>filename]`**: Perform globbing on the arguments, then execute the command, optionally saving the output in the named file. 
*   **`file <filename> [filename...]`**: Determine and print the file type for the given files.
*   **`nice <filename>`**: Returns `1` if the filename is considered "nice" (conforming to specific formatting rules), otherwise `0`.
*   **`zip names <zipfile>`**: List the names of all files contained within a zip archive.
*   **`zip get <zipfile> <membername>`**: Extract and return the contents of a specific file from within a zip archive.
*   **`lzip <list1> [list2...]`**: Combines multiple lists by interleaving their elements (similar to Python's `zip` function).

## 2. Centipede Control

These commands are used to control the state and execution of the Centipede hardware.

*   **`centipede restart`**: Performs a standard soft reboot of the Centipede board, restarting the firmware.
*   **`centipede reflash`**: Reboots the Centipede board into BOOT_SEL mode, allowing new firmware to be flashed over USB.
*   **`centipede force-reformat-flash-filesystem`**: Unmounts, forcibly reformats, and remounts the LittleFS filesystem on the board.
*   **`centipede flash-filesystem-stats`**: Checks the LittleFS filesystem for errors and returns statistics including used blocks and configuration parameters.

## 3. Utilities

These commands provide general-purpose programming utilities that supplement the standard Tcl library. Some standard commands have been overridden to integrate better with the Centipede environment.

*   **`clock seconds`**: Returns the current system uptime in seconds and milliseconds as a string formatted as `"secs ms"`.
*   **`echo [arg...]`**: Print arguments to the console, separated by spaces.
*   **`puts [string] [fd]`**: Print a string. The `fd` can be `tether`, `coco`, or `tty` (default). Overridden to route output to the correct serial interfaces.
*   **`source <filename>`**: Read and evaluate a Tcl script from a file on the VFS. Overridden to use the VFS.
*   **`glob <pattern> [pattern...]`**: Return a list of filenames that match the given patterns. Overridden to work with the VFS.
*   **`map <varName> <list> <command>`**: Evaluates `<command>` for each element in `<list>`, with the element assigned to `<varName>`. Returns a new list containing the results of each evaluation.
*   **`comb <varName> <list> <command>`**: Acts as a filter. Evaluates `<command>` for each element in `<list>`. Returns a new list containing only the elements for which the command evaluated to true.
*   **`iota <n>`**: Generates and returns a list of integers from `0` to `n-1`.
*   **`k <arg1> [arg2...]`**: The K-combinator. Always returns its first argument (`arg1`), ignoring any subsequent arguments. Useful in functional programming paradigms.
*   **`ini names <filename>`**: Reads an INI-like text file and returns a list of all section headers found within `[` and `]`. If there are lines before the first header, the empty string `""` is included at the front of the list. Comment lines starting with `#` are ignored.
*   **`ini get <filename> <header>`**: Reads an INI-like text file and returns the body lines associated with the specified `[header]`.

## 4. Special Features of the REPL

The Centipede REPL (Read-Eval-Print Loop) includes a few special behaviors to make interactive use more convenient:

1.  **`bye` Command**: Entering `bye` will exit the REPL and allow the Coco to resume normal execution.
2.  **Automatic Globbing and Redirection**: If a command entered at the REPL does not contain any Tcl metacharacters (i.e., it lacks `;`, `"`, `[`, `]`, `{`, or `}`), the REPL automatically wraps the command with the `fs` command. This enables automatic globbing of arguments and allows you to save the output to a file using `>filename` syntax, just like a traditional shell.
