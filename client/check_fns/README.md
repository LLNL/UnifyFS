# Wrappers

The files in this folder help manage the set of wrappers that are implemented.
In particluar, they are used to enable a tool that
detects I/O routines used by an application that are not yet supported in UnifyCR.
They are also used to generate the code required for GOTCHA.

- fakechroot_list.txt - lists I/O routines from fakechroot
- gnulibc_list.txt - I/O routines from libc
- cstdio_list.txt - I/O routines from stdio
- posix_list.txt - I/O routines in POSIX
- unifycr_list.txt - list of wrappers in UnifyCR
- unifycr_unsupported_list.txt - list of wrappers in UnifyCR that implemented, but not supported

# Running the unifycr_check_fns tool

This tool identifies the set of I/O calls used in an application by running nm on the executable.
It reports any I/O routines used by the app, which are not supported by UnifyCR.
If an application uses an I/O routine that is not supported, it likely cannot use UnifyCR.
If the tool does not report unsupported wrappers,
the app may work with UnifyCR but it is not guaranteed to work.

    unifycr_check_fns <executable>

# Building the GOTCHA list

The gotcha_map_unifycr_list.h file contains the code necessary to wrap I/O functions with GOTCHA.
This is generated from the unifycr_list.txt file by running the following command:

    python unifycr_translate.py unifycr_list

# Commands to build files

## fakechroot_list.txt
The fakechroot_list.txt file lists I/O routines implemented in fakechroot.
This list was generated using the following commands:

    git clone https://github.com/fakechroot/fakechroot.git fakechroot.git
    cd fakechroot.git/src
    ls *.c > fakechroot_list.txt

## gnulibc_list.txt
The gnulibc_list.txt file lists I/O routines available in libc.
This list was writte by hand using information from
[http://www.gnu.org/software/libc/manual/html_node/I_002fO-Overview.html#I_002fO-Overview](http://www.gnu.org/software/libc/manual/html_node/I_002fO-Overview.html#I_002fO-Overview).

## cstdio_list.txt
The cstdio_list.txt file lists I/O routines available in libstdio..
This list was written by hand using information from
[http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf](http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf).

## unifycr_list.txt
The unifycr_list.txt file specifies the set of wrappers in UnifyCR.
Most but not all such wrappers are supported.
The command to build unifycr list:

    grep UNIFYCR_WRAP ../runtime/lib/*.c > unifycr_list.txt

## unifycr_unsupported_list.txt
The unify_unsupported_list.txt file specifies wrappers that are in UnifyCR, but are known to not actually be supported.
This list is written by hand.
