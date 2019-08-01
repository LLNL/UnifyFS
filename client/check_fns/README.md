# Wrappers

The files in this folder help manage the set of wrappers that are implemented.
In particluar, they are used to enable a tool that
detects I/O routines used by an application that are not yet supported in UnifyFS.
They are also used to generate the code required for GOTCHA.

- fakechroot_list.txt - lists I/O routines from fakechroot
- gnulibc_list.txt - I/O routines from libc
- cstdio_list.txt - I/O routines from stdio
- posix_list.txt - I/O routines in POSIX
- unifyfs_list.txt - list of wrappers in UnifyFS
- unifyfs_unsupported_list.txt - list of wrappers in UnifyFS that implemented, but not supported

Our [Wrapper Guide](https://unifyfs.readthedocs.io/en/dev/wrappers.html)
has the full documentation on running the unifyfs_check_fns tool, building the
GOTCHA list, and building the other .txt files here.
