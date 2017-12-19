## UnifyCR: A Distributed Burst Buffer File System - 0.1.0
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

## How to build UnifyCR
Download the latest UnifyCR release from the [Releases](https://github.com/LLNL/UnifyCR/releases) page.

UnifyCR requires MPI, LevelDB, and GOTCHA.

## Building with spack
To install leveldb and gotcha and set up your build environment, we recommend
using the spack package manager. For example, the following commands will
build and install leveldb and gotcha using spack and configure your environment
so UnifyCR can find the libraries and headers it needs:

    git clone https://github.com/spack/spack
    . spack/share/spack/setup-env.sh
    spack install leveldb
    spack install gotcha
    spack install environment-modules
    spack load environment-modules
    source <(spack module loads gotcha leveldb)

Then to build UnifyCR:

    ./configure --prefix=/path/to/install --enable-debug
    make
    make install

## Building without spack
For users who cannot use spack, you may fetch the latest release of GOTCHA from:
[https://github.com/LLNL/GOTCHA/releases](https://github.com/LLNL/GOTCHA/releases)

Then to build UnifyCR:

    ./configure --prefix=/path/to/install --with-gotcha=/path/to/gotcha --enable-debug
    make
    make install

## How to run (with UNIFYCR tests):
1. cd server
2. Allocate a compute node on your system
3. Set LD_LIBRARY_PATH to include the path to the LevelDB library (and gotcha library if used)
4. ./runserver.sh
5. ./runwclient.sh

## I/O Interception

**Steps for static link using --wrap:**

To intercept I/O calls using a static link,
you must add flags to your link line.
UnifyCR installs a unifycr-config script that returns those flags, e.g.,

    mpicc -o test_write \
      `<unifycr>/bin/unifycr-config --pre-ld-flags` \
      test_write.c \
      `<unifycr>/bin/unifycr-config --post-ld-flags`

**Steps for link using gotcha:**

To intercept I/O calls using gotcha,
use the following syntax to link an application.

    mpicc -o test_write test_write.c \
      -I<unifycr>/include -L<unifycy>/lib -lunifycr_gotcha \
      -L<gotcha>/lib64 -lgotcha

## Contribute and Develop
We have a separate document with
[contribution guidelines](./.github/CONTRIBUTING.md).
