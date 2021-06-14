================
Assumptions and Semantics
================

In this section, we provide assumptions we make about the behavior of
applications that use UnifyFS and about the file system semantics of UnifyFS.

---------------------------
System Requirements
---------------------------

The system requirements to run UnifyFS are:

    - Compute nodes must be equipped with local storage device(s) that UnifyFS can
      use for storing file data, e.g., SSD or RAM.

    - The system must support the ability for UnifyFS user-level daemon processes
      to run concurrently with user application processes on compute nodes.

---------------------------
Application Behavior
---------------------------

UnifyFS is specifically designed to support the bulk synchronous I/O pattern
that is typical in HPC applications, e.g., checkpoint/restart or output dumps.
In bulk synchronous I/O, I/O operations occur in separate write and read phases,
and files are not read and written simultaneously.
For example, files are written during checkpointing (a write phase)
and read during recovery/restart (a read phase).
Additionally, parallel writes and reads to shared files occur systematically,
where processes access computable, regular offsets of files, e.g., in strided or
segmented access patterns, in contrast to random, interleaved, small writes and reads.

UnifyFS offers the best performance for applications that exhibit the bulk
synchronous I/O pattern. While UnifyFS does support deviations to this pattern,
the performance might be slower and the user may
have to take additional steps to ensure correct execution of the application
with UnifyFS.
For more information on this topic, refer to the section on
:ref:`commit consistency semantics in UnifyFS <commit_consistency_label>`_.


---------------------------
Consistency Model
---------------------------

The UnifyFS file system does not support strict POSIX consistency semantics.
(Please see Chen et al., HPDC 2021 for more details
on different file system consistency semantics models.)
.. paper link needed when proceedings are out
Instead, UnifyFS supports two different consistency models:
*commit consistency semantics* when a file is actively
being modified; and *lamination semantics* when the file is no longer being
modified by the application.
These two consistency models provide opportunities for UnifyFS to
provide better performance for the I/O operations of HPC applications.

'''''''''''''''''''''''''''
Commit Consistency Semantics in UnifyFS
'''''''''''''''''''''''''''
.. _commit_consistency_label:

Commit consistency semantics require
explicit "commit" operations to be performed before updates to a file
are globally visible.
We chose commit consistency semantics for UnifyFS because it is sufficient
for correct execution of typical HPC applications that adhere to
the bulk synchronous I/O pattern, and enables UnifyFS to provide better
performance than with strict POSIX semantics. For example, because
we assume that applications using UnifyFS
will not execute concurrent modifications to the same file offset,
UnifyFS does not have to employ locking to ensure sequential
access to file regions. This assumption allows us to cache file
modifications locally which greatly improves the write performance
of UnifyFS.

The use of synchronization operations are required for applications that exhibit
I/O accesses that deviate from the bulk synchronous I/O pattern to perform
correctly with UnifyFS.
There are several methods by which applications can adhere to the synchronization
requirements.
      - Using MPI-IO. The (MPI-IO_) interface requirements are a good match for the
        consistency model of UnifyFS. Specifically, the MPI-IO interface requires
        explicit synchronization in order for updates made by processes to
        be globally visible. If an application utilizes the MPI-IO interface
        correctly, it will adhere to the requirements of UnifyFS.
      - Using (HDF5_) and other parallel I/O libraries. Most parallel I/O libraries
        hide the synchronization requirements of file systems from their users.
        For example, HDF5 implements the synchronization required by the MPI-IO
        interface so users of HDF5 do not need to perform any synchronization
        operations explicitly in their codes.
      - With explicit synchronization. If an application does not use a compliant
        parallel I/O library or if the developer wishes to perform explicit
        synchronization, the synchronization can be achieved through adding
        explicit "flush" operations with calls to fflush(), close(), or fsync()
        in the application source code,
        or by supplying the "write_sync" configuration parameter to UnifyFS
        on startup, which will cause an implicit "flush" operation after
        every write (note: the "write_sync" mode can significantly slow down
        write performance.).

During a write phase, a process can deviate from the bulk synchronous
I/O pattern and read any byte in
a file, including remote data that has been written by processes
executing on remote compute nodes in the job.
However, the performance will differ based on which process wrote the data that
is being read:
      - If the bytes being read were written by the same process that is reading
        the bytes, UnifyFS offers the fastest performance and no synchronization
        operations are needed. This kind of access is typical in some I/O
        libraries, e.g., HDF5, where file metadata may be updated and read by
        the same process.
      - If the bytes being read were written by a process executing on the same compute
        node as the reading process, UnifyFS can offer slightly slower performance
        than the first case and requires no additional synchronization operations.
      - If the bytes being read were written by a process executing on a different
        compute node than the reading process, then the performance is slower
        than the first two cases and the application must
        introduce synchronization operations to ensure that the most recent
        data is read.
In summary, reading the local data (which has been written by processes
executing on the same compute node) will always be faster than reading
remote data.

Note that commit semantics also require synchronization for potentially conflicting
write accesses. If an application does not enforce sequential ordering of file
modifications during a write phase, e.g., with MPI synchronization,
and multiple processes write concurrently to the same file offset or to an
overlapping region, the result is undefined and may
reflect the result of any of the processes' operations to that offset or region.
**I don't think this paragraph is true. I think we won't return the last write even if synchronization is applied**

.. How can users check that their application is correctly synchronized? Will we have the checker scripts ready?

'''''''''''''''''''''''''''
Lamination Consistency Semantics in UnifyFS
'''''''''''''''''''''''''''

The other consistency model that UnifyFS employs is called "lamination
semantics" which is intended to be applied once a file is done being modified
at the end of a write phase of an application.  After a file is
laminated, it becomes permanently read-only and its data is accessible across
all the compute nodes in the job without further synchronization.
Once a file is laminated, it cannot be further modified,
except for being renamed or deleted.
.. Is the next sentence true? Does more need to be added?
If a failure occurs during a job before a file is laminated, the file
contents may be unrecoverable.

A typical use case for lamination is for checkpoint/restart.
An application can laminate checkpoint files after they have
been successfully written so that they can be read by any process on any compute
node in the job in a restart operation. To laminate a file, an application
can simply call chmod() to remove all the write bits, after its write phase
is completed. When write bits of a file are removed, UnifyFS will laminate the
file. A typical checkpoint write operation with UnifyFS will look like:

.. code-block:: C

  fd = open("checkpoint1.chk", O_WRONLY)
  write(fd, <checkpoint data>, <len>)
  close(fd)
  chmod("checkpoint1.chk", 0444)

We plan for future versions of UnifyFS to support different methods for
laminating files, such as
laminating all files on close() or laminating via an explicit API call.

We define the laminated consistency model to enable certain optimizations while
supporting the typical requirements of bulk synchronous I/O.
Recall that for bulk synchronous I/O patterns, reads and writes typically occur in
distinct phases. This means that for the majority of the time,
processes do not need to read arbitrary
bytes of a file until the write phase is completed, which in practice is
when the file is done being modified and closed and can be safely made
read-only with lamination.
By assuming that processes do not need to access file data modified
by other processes before lamination,
UnifyFS can optimize write performance by buffering all metadata and
file data for processes locally, instead of performing costly exchanges of
metadata and file data between compute nodes on every write.
Also, since file contents cannot change after lamination,
aggressive caching may be used during the read phase with minimal locking.
Further, since a file may be lost on application failure unless laminated, data
redundancy schemes can be delayed until lamination.
.. do we need to define our failure behavior better?

---------------------------
File System Behavior
---------------------------

The following lists summarize available application I/O operations according to
our consistency model.

Behavior before lamination (write phase):

  - open/close: A process may open/close a file multiple times.

  - write: A process may write to any part of a file. If two processes write
    to the same location, the value is undefined.

  - read: A process may read bytes it has written. Reading other bytes is
    invalid without explicit synchronization operations.

  - rename: A process may rename a file.

  - truncate: A process may truncate a file.

  - unlink: A process may delete a file.

Behavior after lamination (read phase):

  - open/close: A process may open/close a file multiple times.

  - write: All writes are invalid.

  - read: A process may read any byte in the file.

  - rename: A process may rename a file.

  - truncate: Truncation is invalid (considered to be a write operation).

  - unlink: A process may delete a file.

The additional behavior of UnifyFS can be summarized as follows.

    - UnifyFS exists on node local storage only and is not automatically
      persisted to stable storage like a parallel file system (PFS). When the
      data needs to be persisted to an external file system, users can use
      :ref:`unifyfs utility <unifyfs_utility_label>` with its data staging
      options.

    - UnifyFS also can be coupled with SymphonyFS_, high level I/O libraries, or
      a checkpoint library like (SCR_) or (VeloC_) to move data to the PFS periodically.

    - UnifyFS can be used with checkpointing libraries like (SCR_) or (VeloC_),
      or with I/O libraries libraries like (HDF5_) to support shared files on burst buffers.

    - The UnifyFS file system will be empty at job start. A user job must populate the file system
      manually or by using
      :ref:`unifyfs utility <unifyfs_utility_label>`.

    - UnifyFS creates a shared file system namespace across all compute nodes in
      a job, even if an application process is not running on all compute nodes.

    - UnifyFS survives across multiple application runs within a job.

    - If a failure occurs during a job before a file is laminated, the file
      contents may be unrecoverable.
.. is this adequate to describe failure behavior?

    - UnifyFS transparently intercepts system level I/O calls of
      applications and I/O libraries.

.. _SymphonyFS: https://code.ornl.gov/techint/SymphonyFS
.. _VeloC: https://github.com/ECP-VeloC/VELOC
.. _SCR: https://github.com/llnl/scr
.. _HDF5: https://www.hdfgroup.org/
.. _MPI-IO: https://www.mpi-forum.org/docs/
