## UnifyCR: A Distributed Burst Buffer File System - 0.1.1
Node-local burst buffers are becoming an indispensable hardware resource on
large-scale supercomputers to buffer the bursty I/O from scientific
applications. However, there is a lack of software support for burst buffers to
be efficiently shared by applications within a batch-submitted job and recycled
across different batch jobs. In addition, burst buffers need to cope with a
variety of challenging I/O patterns from data-intensive scientific
applications.

UnifyCR is a user-level burst buffer file system that supports scalable and
efficient aggregation of I/O bandwidth from burst buffers while having the same
life cycle as a batch-submitted job. It is layered on top of UNIFYCR, a
scalable checkpointing/restart I/O library.  While UNIFYCR is designed for N-N
write/read, UnifyCR compliments its functionality with the support for N-1
write/read. It efficiently accelerates scientific I/O based on scalable
metadata indexing, co-located I/O delegation, and server-side read clustering
and pipelining.

Please note that the current implementation of UnifyCR is still in development and
is not yet of production quality. The client-side interface is
based on POSIX, including open, pwrite, lio_listio, pread, write, read, lseek,
close and fsync. UnifyCR is designed for batched write and read operations
under a typical bursty I/O workload (e.g. checkpoint/restart); it is optimized
for bursty write/read based on pwrite/lio_listio respectively. 

Below is the guide on how to install and use
UnifyCR server.

## How to build (with UNIFYCR tests):
UnifyCR requires MPI and LevelDB. After installing required packages:

```
shell $ ./autogen.sh             # this will generate the build script
shell $ ./configure --prefix=/install/path/you/want --enable-debug
shell $ make
shell $ make install
```

For the complete build options:

```
shell $ ./configure --help
```

## How to run (with UNIFYCR tests):
1. Make sure you are in UnifyCR_Server directory
2. allocate a node
3. ./runserver.sh
4. ./runwrclient.sh

## I/O Interception in UnifyCR_Client:

**Steps for switching between and/or building with different I/O wrapping strategies**

In order to use gotcha for I/O wrapping install the latest release:
https://github.com/LLNL/GOTCHA/releases

**Steps for using gotcha:**
1. Run buildme_opt like this: "./buildme_opt -DUNIFYCR_GOTCHA"

**Steps for using --wrap:**
1. This is the default so you shouldn't have to do anything unless
a different option is turned on
2. Run buildme_opt normally: "./buildme_opt"

**Steps for using UNIFYCR_PRELOAD (LD_PRELOAD):**
1. Run buildme_opt like this: "./buildme_opt -DUNIFYCR_PRELOAD"

## Contribute and Develop
We have a separate document with
[contribution guidelines](./.github/CONTRIBUTING.md).
