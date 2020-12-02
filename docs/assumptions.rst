================
Assumptions
================

In this section, we provide assumptions we make about the behavior of
applications that use UnifyFS, and about how UnifyFS currently functions.

---------------------------
System Requirements
---------------------------

UnifyFS uses node-local storage devices, e.g., RAM and SSD, for storing the
application data. Therefore, a system should support the following requirements
to run UnifyFS.

    - A compute node is equipped with a local storage device that UnifyFS can
      use for storing file data, e.g., SSD or RAM.

    - An ability for UnifyFS to launch user-level daemon processes on compute
      nodes, which run concurrently with user application processes

---------------------------
Application Behavior
---------------------------

UnifyFS is specifically designed to support globally synchronous checkpointing
workloads. In such a workload, the expected application behavior is as follows.

    - I/O operations occur in separate write and read phases, and thus files are
      not read and written simultaneously. For instance, files are only written
      during the checkpointing (a write phase) and only read during the
      recovery/restart (a read phase).

    - During the read phase, a process can read any byte in a file including
      remote data that has been written by processes in remote compute nodes.
      However, reading the local data (which has been written by processes in
      the same compute node) will be faster than reading the remote data.

    - During the write phase, the result of concurrently writing to the same
      file offset by multiple processes is undefined. Similarly, multiple
      processes writing to an overlapped region also leads to an undefined
      result. For example, if a command in the job renames a file while the
      parallel application is writing to it, the outcome is undefined, i.e., it
      could be a success or failure depending on timing.

---------------------------
Consistency Model
---------------------------

One key aspect of UnifyFS is the idea of "laminating" a file.  After a file is
laminated, it becomes "set in stone," and its data is accessible across all the
nodes. Laminated files are permanently read-only and cannot be further modified,
except for being renamed or deleted.  If the application process group fails
before a file has been laminated, UnifyFS may delete the file.

A typical use case is to laminate application checkpoint files after they have
been successfully written. To laminate a file, an application can simply call
chmod() to remove all the write bits, after its write phase is completed. When
write bits of a file are all canceled, UnifyFS will internally laminate the
file. A typical checkpoint will look like:

.. code-block:: C

  fd = open("checkpoint1.chk", O_WRONLY)
  write(fd, <checkpoint data>, <len>)
  close(fd)
  chmod("checkpoint1.chk", 0444)

Future versions of UnifyFS may support different laminate semantics, such as
laminate on close() or laminate via an explicit API call.

We define the laminated consistency model to enable certain optimizations while
supporting the perceived requirements of application checkpoints.  Since remote
processes are not permitted to read arbitrary bytes of a file until its
lamination, UnifyFS can buffer all data and metadata of the file locally
(instead of exchanging indexing information between compute nodes) before the
lamination occurs.  Also, since file contents cannot change after lamination,
aggressive caching may be used during the read phase with minimal locking.
Further, since a file may be lost on application failure unless laminated, data
redundancy schemes can be delayed until lamination.

The following lists summarize available application I/O operations according to
our consistency model.

Behavior before lamination (write phase):

  - open/close: A process may open/close a file multiple times.

  - write: A process may write to any part of a file. If two processes write
    to the same location, the value is undefined.

  - read: A process may read bytes it has written. Reading other bytes is
    invalid.

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

---------------------------
File System Behavior
---------------------------

The additional behavior of UnifyFS can be summarized as follows.

    - UnifyFS exists on node local storage only and is not automatically
      persisted to stable storage like a parallel file system (PFS). When the
      data needs to be persisted to an external file system, users can use
      :ref:`unifyfs utility <unifyfs_utility_label>` with its data staging
      options.

    - UnifyFS also can be coupled with SymphonyFS_, high level I/O libraries, or
      a checkpoint library (VeloC_) to move data to PFS periodically.

    - UnifyFS can be used with checkpointing libraries (VeloC_) or other I/O
      libraries to support shared files on burst buffers.

    - UnifyFS starts empty at job start. User job must populate the file system
      manually or by using
      :ref:`unifyfs utility <unifyfs_utility_label>`.

    - UnifyFS creates a shared file system namespace across all compute nodes in
      a job, even if an application process is not running on all compute nodes.

    - UnifyFS survives across multiple application runs within a job.

    - UnifyFS will transparently intercept system level I/O calls of
      applications and I/O libraries.

.. _SymphonyFS: https://code.ornl.gov/techint/SymphonyFS
.. _VeloC: https://github.com/ECP-VeloC/VELOC

