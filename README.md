# UnifyCR: A Distributed Burst Buffer File System - 0.1.0

Node-local burst buffers are becoming an indispensable hardware resource on
large-scale supercomputers to buffer the bursty I/O from scientific
applications. However, there is a lack of software support for burst buffers to
be efficiently shared by applications within a batch-submitted job and recycled
across different batch jobs. In addition, burst buffers need to cope with a
variety of challenging I/O patterns from data-intensive scientific
applications.

UnifyCR is a user-level burst buffer file system under active development.
UnifyCR supports scalable and efficient aggregation of I/O bandwidth from burst
buffers while having the same life cycle as a batch-submitted job. While UnifyCR
is designed for N-N write/read, UnifyCR compliments its functionality with the
support for N-1 write/read. It efficiently accelerates scientific I/O based on
scalable metadata indexing, co-located I/O delegation, and server-side read
clustering and pipelining.

## Documentation
Full UnifyCR documentation is contained [here](http://unifycr.readthedocs.io).

Use [Build & I/O Interception](http://unifycr.readthedocs.io/en/latest/build-intercept.html)
for instructions on how to build and install UnifyCR.

## Build Status
The current status of the UnifyCR dev branch is:

[![Build Status](https://api.travis-ci.org/LLNL/UnifyCR.png?branch=dev)](https://travis-ci.org/LLNL/UnifyCR)

## Contribute and Develop
We have a separate document with
[contribution guidelines](./.github/CONTRIBUTING.md).
