# UnifyFS: A Distributed Burst Buffer File System

Node-local burst buffers are becoming an indispensable hardware resource on
large-scale supercomputers to buffer the bursty I/O from scientific
applications. However, there is a lack of software support for burst buffers to
be efficiently shared by applications within a batch-submitted job and recycled
across different batch jobs. In addition, burst buffers need to cope with a
variety of challenging I/O patterns from data-intensive scientific
applications.

UnifyFS is a user-level burst buffer file system under active development.
UnifyFS supports scalable and efficient aggregation of I/O bandwidth from burst
buffers while having the same life cycle as a batch-submitted job. While UnifyFS
is designed for N-N write/read, UnifyFS compliments its functionality with the
support for N-1 write/read. It efficiently accelerates scientific I/O based on
scalable metadata indexing, co-located I/O delegation, and server-side read
clustering and pipelining.

## Documentation
UnifyFS documentation is at [https://unifyfs.readthedocs.io](https://unifyfs.readthedocs.io).

For instructions on how to build and install UnifyFS,
see [Build UnifyFS](http://unifyfs.readthedocs.io/en/dev/build.html).

## Build Status
Status of UnifyFS development branch (dev):

![Build Status](https://github.com/LLNL/UnifyFS/actions/workflows/build-and-test.yml/badge.svg?branch=dev)

[![Read the Docs](https://readthedocs.org/projects/unifyfs/badge/?version=dev)](https://unifyfs.readthedocs.io)

## Contribute and Develop
If you would like to help, please see our [contributing guidelines](https://unifyfs.readthedocs.io/en/dev/contribute-ways.html).
