===========================
Limitations and Workarounds
===========================

-------------------
General Limitations
-------------------

.. rubric:: Synchronization Across Processes

Any overlapping write operations or simultaneous read and write operations
require proper synchronization within UnifyFS. This includes ensuring updates to
a file are visible by other processes as well as proper inter-process
communication to enforce ordering of conflicting I/O operations. Refer to the
section on
:ref:`commit consistency semantics in UnifyFS <commit_consistency_label>`
for more detail.

.. rubric:: File Locking

UnifyFS does not support file locking (calls to ``fcntl()``). This results in
some less obvious limitations when using some I/O libraries with UnifyFS
(:ref:`see below <file-lock-label>`).

.. warning::

    Any calls to ``fcntl()`` are not even intercepted by UnifyFS and calls to
    such will be ignored, resulting in silent errors and possible data
    corruption. Running the application through :doc:`VerifyIO <verifyio>` can
    help determine if any file locking calls are made.

----------

---------------------------
MPI-IO and HDF5 Limitations
---------------------------

Synchronization
***************

Applications that make use of inter-process communication (e.g., MPI) to enforce
a particular order of potentially conflicting I/O operations from multiple
processes must properly use synchronization calls (e.g., ``MPI_File_sync()``) to
avoid possible data corruption. Within UnifyFS, this requires use of the
:ref:`"sync-barrier-sync" construct <sync-barrier-sync-label>` to ensure proper
synchronization.

Properly synchronizing includes doing so between any less obvious MPI-I/O calls
that also imply a write/read. For example, ``MPI_File_set_size()`` and
``MPI_File_preallocate()`` act as write operations and ``MPI_File_get_size()``
acts as a read operation. There may be other implied write/read calls as well.

.. TODO: Mention use/need of ``romio_visibility_immediate`` hint once available.
.. https://github.com/pmodels/mpich/issues/5902

Synchronization Workarounds
^^^^^^^^^^^^^^^^^^^^^^^^^^^

If your application does not adhere to proper syncronization requirements there
are four workaround options available to still allow UnifyFS intregration.

UnifyFS Sync-per-write Configuration (``client.write_sync``)
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

UnifyFS provided config option that makes UnifyFS act more “POSIX like” by
forcing a metadata sync to the server after **every** write operation. Set
``UNIFYFS_CLIENT_WRITE_SYNC=ON`` to enable this option.

**Cost:** Can cause a significant decrease in write performance as the amount of
file sync operations that are performed will be far more than necessary.

Manually Add Sync Operations
""""""""""""""""""""""""""""

Edit the application code and manually add the proper synchronization calls
everywhere necessary. For example, wherever an ``MPI_File_sync()`` is required,
the "sync-barrier-sync" construct needs to be used.

.. _sync-barrier-sync-label:

.. code-block:: C
    :caption: Sync-barrier-sync Construct

    MPI_File_sync() //flush newly written bytes from MPI library to file system
    MPI_Barrier()   //ensure all ranks have finished the previous sync
    MPI_File_sync() //invalidate read cache in MPI library

.. Note::

    The "barrier" in "sync-barrier-sync" can be replaced by a send-recv or
    certain collectives that are guaranteed to be synchronized. See the "Note on
    the third step" in the `VerifyIO README`_ for more information.

**Cost:** If making edits to the application source code is an option, the
amount of time and effort required to track down all the places that proper
synchonization calls are needed can be very labor intensive.
:doc:`VerifyIO <verifyio>` can help with in this effort.

HDF5 FILE_SYNC
""""""""""""""

HDF5 provided config option that forces HDF5 to add an ``MPI_File_sync()`` call
after every collective write operation when needed by the underlying MPI-IO
driver. Set ``HDF5_DO_MPI_FILE_SYNC=1`` to enable this option.

.. Note::

    This option will soon be available in the `HDF5 develop branch`_ as well as
    in the next HDF5 release.

**Cost:** Can cause a significant decrease in write performance as the amount of
file sync operations performed will likely be more than necessary. Similar to,
but potentially more efficient than, the ``WRITE_SYNC`` workaround as less
overall file syncs may be performed in comparision, but still likely more than
needed.

ROMIO Driver Hint
"""""""""""""""""

A ROMIO provided hint that will cause the ROMIO driver (in a supported MPI
library) to add an ``MPI_Barrier()`` call and an additional ``MPI_File_sync()``
call after each already existing ``MPI_File_sync()`` call within the
application. In other words, this hint converts each existing
``MPI_File_sync()`` call into the "sync-barrier-sync" construct. Enable the
``romio_synchronizing_flush`` hint to use this workaround.

**Cost:** Potentially more efficient that the ``WRITE_SYNC`` and HDF5
``FILE_SYNC`` workarounds as this will cause the application to use the
synchronization construct required by UnifyFS everywhere the application already
intends them to occur (i.e., whenever there is already an ``MPI_File_sync()``).
However, if (1) any existing ``MPI_File_sync()`` calls are only meant to make
data visible to the other processes (rather than to avoid potential conflicts)
or (2) the application contains a mix of lone ``MPI_File_sync()`` calls along
with the "sync-barrier-sync" construct, then this approach will result in more
syncs than necessary.

----------

.. _file-lock-label:
File Locking
************

UnifyFS not supporting file locks results in some I/O library features to not
work with UnifyFS.

.. topic:: Atomicity

    ROMIO uses ``fcntl()`` to implement atomicity. It is recommended to disable
    atomicity when integrating with UnifyFS. To disable, run
    ``MPI_File_set_atomicity(fh, 0)``.

.. topic:: Data Sieving

    It is recommended to disable data sieving when integrating with UnifyFS.
    Even with locking support, use of data sieving will drastically increase the
    time and space overhead within UnifyFS, significantly decreasing application
    performance. For ROMIO, set the hints ``romio_ds_write disable`` and
    ``romio_ds_read disable`` to disable data sieving.

.. topic:: Shared File Pointers

    Avoid using shared file pointers in MPI-I/O under UnifyFS as they require
    file locking to implement.
    Functions that use shared file pointers include:

    - ``MPI_File_write_shared()``
    - ``MPI_File_read_shared()``
    - ``MPI_File_write_ordered()``
    - ``MPI_File_read_ordered()``

File Locking Workarounds
^^^^^^^^^^^^^^^^^^^^^^^^

UnifyFS doesn't provide any direct workarounds for anything that requires file
locking. Simply disable atomicity and data sieving and avoid using shared file
pointers to get around this.

In the future, UnifyFS may add support for file locking. However, it is strongly
suggested to avoid file locking unless the application cannot run properly
without its use. Enabling file lock support within UnifyFS will result in
decreased I/O performance for the application.

.. explicit external hyperlink targets

.. _HDF5 develop branch: https://github.com/HDFGroup/hdf5
.. _VerifyIO README: https://github.com/uiuc-hpc/Recorder/tree/pilgrim/tools/verifyio#note-on-the-third-step
