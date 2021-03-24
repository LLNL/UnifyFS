================
Overview
================

UnifyFS is a user level file system currently under active development. An
application can use node-local storage as burst buffers for shared files.
UnifyFS is designed to support both checkpoint/restart which is the most
important I/O workload for HPC and other common I/O workloads. With
UnifyFS, applications can write to fast, scalable, node-local burst buffers as
easily as they do to the parallel file system. This section provides a high
level design of UnifyFS. It describes the UnifyFS library and the UnifyFS
daemon.

The file system that UnifyFS instantiates only exists in user space and is
only visible to applications linked against the UnifyFS client library.  Since
traditional file system tools (ls, cd, etc.) are not linked against the
UnifyFS client library they cannot see nor manipulate files within UnifyFS.
Each UnifyFS file system lasts as
long as the server processes are running, which is typically as long as the
job they are running within.  When the servers exit the file system is
deleted.  It is the responsibility of the user to copy files that
need to be persisted from UnifyFS to a permanent file system.
UnifyFS provides an API and a utility to conduct such copies.

---------------------------
High Level Design
---------------------------

.. image:: images/design-high-lvl.png

UnifyFS presents a shared namespace (e.g., /unifyfs as a mount point) to
all compute nodes in a job allocation. There are two main components of
UnifyFS: the UnifyFS library and the UnifyFS server.

The UnifyFS library is linked into the user application.
The library intercepts I/O calls from the application and
sends I/O requests for UnifyFS files on to the UnifyFS server.
The library forwards I/O requests for all other files on to the system.
The UnifyFS library uses ECP `GOTCHA <https://github.com/LLNL/GOTCHA>`_
as its primary mechanism to intercept I/O calls.
The user application is linked with the UnifyFS client library
and perhaps a high-level I/O library, like HDF5, ADIOS, or PnetCDF.

A UnifyFS server process runs on each compute node in
the job allocation. The UnifyFS server handles the I/O
requests from the UnifyFS library.
The UnifyFS server uses ECP `Mochi <https://mochi.readthedocs.io/en/latest>`_
to communicate with user application processes and server processes on other nodes.
