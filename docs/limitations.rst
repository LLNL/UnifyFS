===========================
Limitations and Workarounds
===========================

-------------------
General Limitations
-------------------

Data Consistency
****************

Overlapping write operations or simultaneous read and write operations
require proper synchronization when using UnifyFS.
This includes ensuring updates to a file are visible to other
processes as well as inter-process communication to enforce
ordering of conflicting I/O operations.
Refer to the section on
:ref:`commit consistency semantics in UnifyFS <commit_consistency_label>`
for more detail.

In short, for a process to read data written by another process,
the reader must wait for the writer to first flush any data it
has written to the UnifyFS servers.
After the writer flushes its data,
there must be a synchronization operation between the writer
and the reader processes,
such that the reader does not attempt to read newly written data
until the writer has completed its flush operation.

UnifyFS can be configured to flush data to servers at various points.
A common mechanism to flush data is for the writer process to
call ``fsync()`` or ``fflush()``.
Also, by default, data is flushed when a file is closed
with ``close()`` or ``fclose()``.

UnifyFS can be configured to behave more “POSIX like” by
flushing newly written data to the server after every write operation.
To do this, one can set ``UNIFYFS_CLIENT_WRITE_SYNC=ON``.

**Cost:** ``UNIFYFS_CLIENT_WRITE_SYNC=ON`` can cause a significant
decrease in write performance as the amount of file sync operations
that are performed will be far more than necessary.

File Locking
************

UnifyFS does not support file locking (e.g., calls
to ``fcntl()`` or ``flock()``).
Calls to ``fcntl()`` and ``flock()`` are not intercepted by UnifyFS.
Any calls will fall through to the underlying operating system,
which may report the corresponding file descriptor as invalid.
If not detected, an application will encounter data corruption
if it depends on file locking semantics for correctness.
Tracing an application with :doc:`VerifyIO <verifyio>` can
help determine whether any file locking calls are used.

Directory Operations
********************

UnifyFS does not support directory operations.

---------------------------
MPI-IO Limitations
---------------------------

Data Consistency
****************

When using MPI-I/O without atomic file consistency,
the MPI standard requires the application to manage
its data consistency by calling ``MPI_File_sync()``.
Both ``MPI_File_open()`` and ``MPI_File_close()``
imply a call to ``MPI_File_sync()``.
After data has been written, the writer must call ``MPI_File_sync()``.
There must then be a synchronization operation between
the writer and reader processes.
Finally, the reader must call ``MPI_File_sync()``
after the synchronization operation with the writer.
A common approach is for the application to execute a
:ref:`"sync-barrier-sync" construct <sync-barrier-sync-label>` as shown below:

.. _sync-barrier-sync-label:

.. code-block:: C
    :caption: Sync-barrier-sync Construct

    MPI_File_sync() //flush newly written bytes from MPI library to file system
    MPI_Barrier()   //ensure all ranks have finished the previous sync
    MPI_File_sync() //invalidate read cache in MPI library

.. Note::

    The "barrier" in "sync-barrier-sync" can be replaced by a send-recv or
    certain collectives that are guaranteed to be synchronized.
    See the "Note on the third step" in the `VerifyIO README`_
    for more information.

Proper data consistency synchronization is also required
between MPI-I/O calls that imply write/read operations.
For example, ``MPI_File_set_size()`` and ``MPI_File_preallocate()``
act as write operations,
and ``MPI_File_get_size()`` acts as a read operation.
There may be other MPI-I/O calls that imply write/read operations.

Manually Add Sync Operations
""""""""""""""""""""""""""""

Data consistency in UnifyFS is designed to be compatible
with MPI-I/O application-managed file consistency semantics.
An application that follows proper MPI-I/O file consistency
semantics should run correctly on UnifyFS,
provided that the ``MPI_File_sync()`` implementation flushes
newly written data to UnifyFS.

On POSIX-compliant parallel file systems like Lustre,
many applications may run correctly
even when they are missing sufficient file consistency synchronization.
To run correctly on UnifyFS, an application must make
all ``MPI_File_sync()`` calls required by the MPI standard.

**Cost:** It may be labor intensive to identify and correct all places
within an application where file synchronization calls are required.
The :doc:`VerifyIO <verifyio>` tool can assist with this effort.

.. TODO: Mention use/need of ``romio_visibility_immediate`` hint once available.
.. https://github.com/pmodels/mpich/issues/5902

---------------------------
ROMIO Limitations
---------------------------

Data Consistency
****************

In ROMIO, ``MPI_File_sync()`` calls ``fsync()``
and ``MPI_File_close()`` calls ``close()``,
each of which flush information about newly
written data to the UnifyFS servers.
When using ROMIO, an application having appropriate
"sync-barrier-sync" constructs as required by the
MPI standard will run correctly on UnifyFS.

ROMIO Synchronizing Flush
"""""""""""""""""""""""""

Although ``MPI_File_sync()`` is an MPI collective,
it is not required to be synchronizing.
One can configure ROMIO such that ``MPI_File_sync()``
is also a synchronizing collective.
To enable this behavior, one can set the following ROMIO hint
through an ``MPI_Info`` object or within
a `ROMIO hints file`_::

    romio_synchronizing_flush true

This configuration can be useful to applications that
only call ``MPI_File_sync()`` once rather than execute
the full sync-barrier-sync construct.

**Cost:** Potentially more efficient than the ``WRITE_SYNC``
workaround as this will cause the application to use the
synchronization construct required by MPI everywhere that
the application already intends them to occur.

File Locking
************

ROMIO requires file locking with ``fcntl()`` to implement various functionality.
Since ``fcntl()`` is not supported in UnifyFS,
one must avoid any ROMIO features that require file locking.

MPI-I/O Atomic File Consistency
"""""""""""""""""""""""""""""""

ROMIO uses ``fcntl()`` to implement atomic file consistency.
One cannot use atomic mode when using UnifyFS.
Provided an application still executes correctly without atomic mode,
one can disable atomicity by calling::

    MPI_File_set_atomicity(fh, 0)

Data Sieving
""""""""""""

ROMIO uses ``fcntl()`` to support its data sieving optimization.
One must disable ROMIO data sieving when using UnifyFS.
To disable data sieving, one can set the following ROMIO hints::

    romio_ds_read disable
    romio_ds_write disable

These hints can be set in the ``MPI_Info`` object when opening a file,
e.g.,::

    MPI_Info info;
    MPI_Info_create(&info);
    MPI_Info_set(info, "romio_ds_read",  "disable");
    MPI_Info_set(info, "romio_ds_write", "disable");
    MPI_File_open(comm, filename, amode, info, &fh);
    MPI_Info_free(&info);

or the hints may be listed in a `ROMIO hints file`_, e.g.,::

    >>: cat romio_hints.txt
    romio_ds_read disable
    romio_ds_write disable

    >>: export ROMIO_HINTS="romio_hints.txt"

MPI-I/O Shared File Pointers
""""""""""""""""""""""""""""

ROMIO uses file locking to support MPI-I/O shared file pointers.
One cannot use MPI-I/O shared file pointers when using UnifyFS.
Functions that use shared file pointers include::

    MPI_File_write_shared()
    MPI_File_read_shared()
    MPI_File_write_ordered()
    MPI_File_read_ordered()

---------------------------
HDF5 Limitations
---------------------------

HDF5 uses MPI-I/O.
In addition to restrictions that are specific to HDF5,
one must follow any restrictions associated with the
underlying MPI-I/O implementation.
In particular, if the MPI library uses ROMIO for its MPI-I/O implementation,
one should adhere to any limitations noted above for ROMIO.

Data Consistency
****************

HDF5 FILE_SYNC
""""""""""""""

HDF5 provides a configuration option that internally calls ``MPI_File_sync()``
after every collective HDF write operation as needed by MPI-I/O.
Set the environment variable ``HDF5_DO_MPI_FILE_SYNC=1`` to enable this option.

.. Note::

    This option will soon be available in the `HDF5 develop branch`_ as well as
    in the next HDF5 release.

**Cost:** This causes a significant decrease in write performance as the amount
of file sync operations performed will likely be more than necessary. Similar to
but potentially more efficient than the ``WRITE_SYNC`` workaround as less
overall file syncs may be performed in comparison, but still likely more than
needed.

.. explicit external hyperlink targets

.. _HDF5 develop branch: https://github.com/HDFGroup/hdf5
.. _VerifyIO README: https://github.com/uiuc-hpc/Recorder/tree/pilgrim/tools/verifyio#note-on-the-third-step
.. _ROMIO hints file: https://wordpress.cels.anl.gov/romio/2008/09/26/system-hints-hints-via-config-file
