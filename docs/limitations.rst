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
flushing newly written data to the server during every write operation.
To do this, one can set ``UNIFYFS_CLIENT_WRITE_SYNC=ON``.
``UNIFYFS_CLIENT_WRITE_SYNC=ON`` can decrease write performance
as the number of data flush operations may be more than necessary.

File Locking
************

UnifyFS does not support file locking,
and calls to ``fcntl()`` and ``flock()`` are not intercepted by UnifyFS.
Any calls fall through to the underlying operating system,
which should report the corresponding file descriptor as invalid.
If not detected, an application will encounter data corruption
if it depends on file locking semantics for correctness.
Tracing application I/O calls with :doc:`VerifyIO <verifyio>` can
help determine whether any file locking calls are used.

Directory Operations
********************

UnifyFS does not support directory operations.

----------

---------------------------
MPI-IO Limitations
---------------------------

Data Consistency
****************

When using MPI-I/O without atomic file consistency,
the MPI standard requires the application to manage
data consistency by calling ``MPI_File_sync()``.
After data has been written, the writer must call ``MPI_File_sync()``.
There must then be a synchronization operation between
the writer and reader processes.
Finally, the reader must call ``MPI_File_sync()``
after its synchronization operation with the writer.
A common approach is for the application to execute a
:ref:`"sync-barrier-sync" construct <sync-barrier-sync-label>` as shown below:

.. _sync-barrier-sync-label:

.. code-block:: c
    :caption: Sync-barrier-sync Construct

    MPI_File_sync() //flush newly written bytes from MPI library to file system
    MPI_Barrier()   //ensure all ranks have finished the previous sync
    MPI_File_sync() //invalidate read cache in MPI library

.. Note::

    The "barrier" in "sync-barrier-sync" can be replaced by a send-recv or
    certain collectives that are guaranteed to be synchronized.
    The synchronization operation does not even need to be an MPI call.
    See the "Note on the third step" in the `VerifyIO README`_
    for more information.

Proper data consistency synchronization is also required
between MPI-I/O calls that imply write or read operations.
For example, ``MPI_File_set_size()`` and ``MPI_File_preallocate()``
act as write operations,
and ``MPI_File_get_size()`` acts as a read operation.
There may be other MPI-I/O calls that imply write or read operations.

Both ``MPI_File_open()`` and ``MPI_File_close()``
implicitly call ``MPI_File_sync()``.

Relaxed MPI_File_sync semantics
"""""""""""""""""""""""""""""""

Data consistency in UnifyFS is designed to be compatible
with MPI-I/O application-managed file consistency semantics.
An application that follows proper MPI-I/O file consistency
semantics using ``MPI_File_sync()`` should run correctly on UnifyFS,
provided that the ``MPI_File_sync()`` implementation flushes
newly written data to UnifyFS.

On POSIX-compliant parallel file systems like Lustre,
many applications can run correctly
even when they are missing sufficient file consistency synchronization.
In contrast, to run correctly on UnifyFS, an application should make
all ``MPI_File_sync()`` calls as required by the MPI standard.

.. Note::

    It may be labor intensive to identify and correct all places
    within an application where file synchronization calls are required.
    The :doc:`VerifyIO <verifyio>` tool can assist developers in this effort.

In the current UnifyFS implementation,
it is actually sufficient to make a single call to ``MPI_File_sync()`` followed by
a synchronizing call like ``MPI_Barrier()``, e.g.::

    MPI_File_sync()
    MPI_Barrier()

Assuming that ``MPI_File_sync()`` calls ``fsync()``,
then information about any newly written data
will be transferred to the UnifyFS servers.
The ``MPI_Barrier()`` then ensures that ``fsync()`` will have been called
by all clients that may have written data.
After the ``MPI_Barrier()``, a process may read data from UnifyFS
that was written by any other process before that other process
called ``MPI_File_sync()``.
A second call to ``MPI_File_sync()`` is not (currently) required in UnifyFS.

Furthermore, if ``MPI_File_sync()`` is known to be a synchronizing collective,
then a separate synchronization operation like ``MPI_Barrier()`` is not required.
In this case, an application might simplify to just the following::

    MPI_File_sync()

Having stated those exceptions, it is best practice to adhere to the MPI
standard and execute a full sync-barrier-sync construct.
There exist potential optimizations such that
future implementations of UnifyFS may require the full sequence of calls.

----------

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

ROMIO Synchronizing Flush Hint
""""""""""""""""""""""""""""""

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
a full sync-barrier-sync construct.

This hint was added starting with the ROMIO version
available in the MPICH v4.0 release.

ROMIO Data Visibility Hint
""""""""""""""""""""""""""

Starting with the ROMIO version available in the MPICH v4.1 release,
the read-only hint ``romio_visibility_immediate`` was added to inform
the caller as to whether it is necessary to call ``MPI_File_sync``
to manage data consistency.

.. https://github.com/pmodels/mpich/issues/5902

One can query the ``MPI_Info`` associated with a file.
If this hint is defined and if its value is ``true``,
then the underlying file system does not require the sync-barrier-sync
construct in order for a process to read data written by another process.
Newly written data is visible to other processes as soon as the writer
process returns from its write call.
If the value of the hint is ``false``, or if the hint is not defined
in the ``MPI_Info`` object, then a sync-barrier-sync construct is
required.

When using UnifyFS, an application must call ``MPI_File_sync()``
in all situations where the MPI standard requires it.
However, since a sync-barrier-sync construct is costly on some file systems,
and because POSIX-complaint file systems may not require it for correctness,
one can use this hint to conditionally call ``MPI_File_sync()`` only when
required by the underlying file system.

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
one can disable it by calling::

    MPI_File_set_atomicity(fh, 0)

Atomic mode is often disabled by default in ROMIO.

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

----------

---------------------------
HDF5 Limitations
---------------------------

HDF5 uses MPI-I/O.
In addition to restrictions that are specific to HDF5,
one must follow any restrictions associated with the
underlying MPI-I/O implementation.
In particular, if the MPI library uses ROMIO for its MPI-I/O implementation,
one should adhere to any limitations noted above
for both ROMIO and MPI-I/O in general.

Data Consistency
****************

In HDF5, ``H5Fflush()`` calls ``MPI_File_sync()``
and ``H5Fclose()`` calls ``MPI_File_close()``.
When running HDF5 on ROMIO or on other MPI-I/O implementations
where these MPI routines flush newly written data to UnifyFS,
one must invoke these HDF5 functions to properly manage data consistency.

When using HDF5 with the MPI-I/O driver,
for a process to read data written by another
process without closing the HDF file,
the writer must call ``H5Fflush()`` after writing its data.
There must then be a synchronization operation between
the writer and reader processes.
Finally, the reader must call ``H5Fflush()``
after the synchronization operation with the writer.
This executes the sync-barrier-sync construct as required by MPI.
For example::

    H5Fflush(...)
    MPI_Barrier(...)
    H5Fflush(...)

If ``MPI_File_sync()`` is a synchronizing collective, as with
when enabling the ``romio_synchronizing_flush`` MPI-I/O hint,
then a single call to ``H5Fflush()`` suffices to accomplish
the sync-barrier-sync construct::

    H5Fflush(...)

HDF5 FILE_SYNC
""""""""""""""

Starting with the HDF5 v1.13.2 release,
HDF can be configured to call ``MPI_File_sync()``
after every HDF collective write operation.
This configuration is enabled automatically if MPI-I/O
defines the ``romio_visibility_immediate`` hint as ``false``.
One can also enable this option manually by setting the
environment variable ``HDF5_DO_MPI_FILE_SYNC=1``.
Enabling this option can decrease write performance
since it may induce more file flush operations than necessary.

----------

-------------------
PnetCDF Limitations
-------------------
PnetCDF applications can utilize UnifyFS,
and the semantics of the `PnetCDF API`_ align well with UnifyFS constraints.

PnetCDF uses MPI-IO to read and write files.
In addition to any restrictions required when using UnifyFS with PnetCDF,
one must follow any recommendations regarding UnifyFS and the
underlying MPI-IO implementation.

Data Consistency
****************

PnetCDF parallelizes access to NetCDF files using MPI.
An MPI communicator is passed as an argument when opening a file.
Any collective call in PnetCDF is global across the process group
associated with the communicator used to open the file.

PnetCDF follows the data consistency model defined by MPI-IO.
Specifically, from its documentation about `PnetCDF data consistency`_:

.. Note::

    PnetCDF follows the same parallel I/O data consistency as MPI-IO standard.

    If users would like PnetCDF to enforce a stronger consistency,
    they should add ``NC_SHARE`` flag when open/create the file.
    By doing so, PnetCDF adds ``MPI_File_sync()`` after each MPI I/O calls.

    If ``NC_SHARE`` is not set, then users are responsible for their
    desired data consistency. To enforce a stronger consistency,
    users can explicitly call ``ncmpi_sync()``. In ``ncmpi_sync()``,
    ``MPI_File_sync()`` and ``MPI_Barrier()`` are called.

Upon inspection of the implementation of the PnetCDF v1.12.3 release,
the following PnetCDF functions include the following calls::

    ncmpio_file_sync
     - calls MPI_File_sync(ncp->independent_fh)
     - calls MPI_File_sync(ncp->collective_fh)
     - calls MPI_Barrier

    ncmpio_sync
     - calls ncmpio_file_sync

    ncmpi__enddef
     - calls ncmpio_file_sync if NC_doFsync (NC_SHARE)

    ncmpio_enddef
     - calls ncmpi__enddef

    ncmpio_end_indep_data
     - calls MPI_File_sync if NC_doFsync (NC_SHARE)

    ncmpio_redef
      - does *NOT* call ncmpio_file_sync

    ncmpio_close
     - calls ncmpio_file_sync if NC_doFsync (NC_SHARE)
     - calls MPI_File_close (MPI_File_close calls MPI_File_sync by MPI standard)

If a program must read data written by another process,
PnetCDF users must do one of the following when using UnifyFS:

1) Add explicit calls to ``ncmpi_sync()`` after writing and before reading.
2) Set ``UNIFYFS_CLIENT_WRITE_SYNC=1``, in which case each POSIX
   write operation invokes a flush.
3) Use ``NC_SHARE`` when opening files so that the PnetCDF library invokes
   ``MPI_File_sync()`` and ``MPI_Barrier()`` calls after its MPI-IO operations.

Of these options,
it is recommended that one add ``ncmpi_sync()`` calls where necessary.
Setting ``UNIFYFS_CLIENT_WRITE_SYNC=1`` is convenient since one does not
need to change the application, but it may have a larger impact on performance.
Opening or creating a file with ``NC_SHARE`` may work for some applications,
but it depends on whether the PnetCDF implementation
internally calls ``MPI_File_sync()`` at all appropriate places,
which is not guaranteed.

A number of PnetCDF calls invoke write operations on the underlying file.
In addition to the ``ncmpi_put_*`` collection of calls
that write data to variables or attributes,
``ncmpi_enddef`` updates variable definitions,
and it can fill variables with default values.
Users may also explicitly fill variables by calling ``ncmpi_fill_var_rec()``.
One must ensure necessary ``ncmpi_sync()`` calls are placed between
any fill and write operations in case
they happen to write to overlapping regions of a file.

Note that ``ncmpi_sync()`` calls ``MPI_File_sync()`` and ``MPI_Barrier()``,
but it does not call ``MPI_File_sync()`` again after calling ``MPI_Barrier()``.
To execute a full sync-barrier-sync construct,
one technically must call ``ncmpi_sync()`` twice::

    // to accomplish sync-barrier-sync
    ncmpi_sync(...) // call MPI_File_sync and MPI_Barrier
    ncmpi_sync(...) // call MPI_File_sync again

When using UnifyFS,
a single call to ``ncmpi_sync()`` should suffice since UnifyFS
does not (currently) require the second call to ``MPI_File_sync()``
as noted above.

.. explicit external hyperlink targets

.. _HDF5 develop branch: https://github.com/HDFGroup/hdf5
.. _VerifyIO README: https://github.com/uiuc-hpc/Recorder/tree/pilgrim/tools/verifyio#note-on-the-third-step
.. _ROMIO hints file: https://wordpress.cels.anl.gov/romio/2008/09/26/system-hints-hints-via-config-file
.. _PnetCDF API: https://parallel-netcdf.github.io/wiki/pnetcdf-api.pdf
.. _PnetCDF data consistency: https://github.com/Parallel-NetCDF/PnetCDF/blob/e47596438326bfa7b9ed0b3857800d3a0d09ff1a/doc/README.consistency.md
