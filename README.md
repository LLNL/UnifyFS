## BurstFS: A Distributed Burst Buffer File System - 0.1.1
Node-local burst buffers are becoming an indispensable hardware
resource on large-scale supercomputers to buffer the bursty
I/O from scientific applications. However, there is a lack of
software support for burst buffers to be efficiently shared by
applications within a batch-submitted job and recycled across
different batch jobs. In addition, burst buffers need to cope with
a variety of challenging I/O patterns from data-intensive scientific
applications.

BurstFS is a user-level burst buffer file system that supports scalable 
and efficient aggregation of I/O bandwidth from burst buffers while
having the same life cycle as a batch-submitted job. It is layered 
on top of CRUISE, a scalable checkpointing/restart I/O library. 
While CRUISE is designed for N-N write/read, BurstFS compliments its 
functionality with the support for N-1 write/read. It efficiently 
accelerates scientific I/O based on scalable metadata 
indexing, co-located I/O delegation, and server-side read clustering and
pipelining.


Please note that the current implementation of BurstFS is not of production 
quality, and is for research purpose only. The current client-side interface is based on POSIX, 
including open, pwrite, lio_listio, pread, write, read, lseek, close and fsync. BurstFS is designed 
for batched write and read operations under a typical bursty I/O workload (e.g. checkpoint/restart);
 it is optimized for bursty write/read based on pwrite/lio_listio respectively. It is still an open 
research question on whether we should give BurstFS a comprehensive POSIX support, or provide a few POSIX 
APIs, but layer on top of them with other higher-level I/O functions. Below is the 
guide on how to install and use BurstFS server.

**How to build (with CRUISE tests):**
1. download and build leveldb-1.14 (wget https://github.com/google/leveldb/archive/v1.14.tar.gz). 
   Then, change the path in BurstFS_Meta/Makefile.cfg to point to your leveldb directory  
2. change directory to BurstFS_Meta
   make
3. change directory to BurstFS_Client
   ./buildme_opt
   cd tests
   ./buildme.sh
4. change directory to BurstFS_Server
   ./buildme_autotools (the first time you build the server only)
   ./buildme.sh

**How to run (with CRUISE tests): **
1. Make sure you are in BurstFS_Server directory
2. allocate a node 
3. ./runserver.sh
4. ./runwrclient.sh

Note: If at Oak Ridge you will probably have to change the MPI path as well in 
BurstFS_Meta/Makefile.cfg in the 1st build step.
 
**I/O Interception in BurstFS_Client:**

Steps for switching between and/or building with different I/O wrapping strategies 

In order to use gotcha for I/O wrapping install the latest release:
https://github.com/LLNL/GOTCHA/releases

Steps for using gotcha:
1. Run buildme_opt like this: "./buildme_opt -DCRUISE_GOTCHA"

Steps for using --wrap:
1. This is the default so you shouldn't have to do anything unless 
a different option is turned on 
2. Run buildme_opt normally: "./buildme_opt"

Steps for using CRUISE_PRELOAD (LD_PRELOAD):
1. Run buildme_opt like this: "./buildme_opt -DCRUISE_PRELOAD"
  
