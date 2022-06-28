=========================
Integrate the UnifyFS API
=========================

This section describes how to use the UnifyFS API in an application.

.. Attention:: **Fortran Compatibility**

   ``unifyfs_mount`` and ``unifyfs_unmount`` are usable with GFortran.
   There is a known ifort_issue_ with the Intel Fortran compiler as well as an
   xlf_issue_ with the IBM Fortran compiler. Other Fortran compilers are
   currently unknown.

   If using fortran, when :ref:`installing UnifyFS <build-label>` with Spack,
   include the ``+fortran`` variant, or configure UnifyFS with the
   ``--enable-fortran`` option if building manually.

.. rubric:: Transparent Mount Caveat

MPI applications that take advantage of the :ref:`transparent mounting
<auto-mount-label>` feature (through configuring with ``--enable-mpi-mount`` or
with ``+auto-mount`` through Spack) do not need to be modified in any way in
order to use UnifyFS. Move on to the :doc:`link` section next as this step can
be skipped.

-----

--------------------------
Include the UnifyFS Header
--------------------------

In C or C++ applications, include ``unifyfs.h``. See writeread.c_ for a full
example.

.. code-block:: C
    :caption: C

    #include <unifyfs.h>

In Fortran applications, include ``unifyfsf.h``. See writeread.f90_ for a
full example.

.. code-block:: Fortran
    :caption: Fortran

    include 'unifyfsf.h'

--------
Mounting
--------

UnifyFS implements a file system in user space, which the system has no knowledge about.
The UnifyFS library intecepts and handles I/O calls whose path matches a prefix that is defined by the user.
Calls corresponding to matching paths are handled by UnifyFS and all other calls are forwarded to the original I/O routine.

To use UnifyFS, the application must register the path that the UnifyFS library should intercept
by making a call to ``unifyfs_mount``.
This must be done once on each client process,
and it must be done before the client process attempts to access any UnifyFS files.

For instance, to use UnifyFS on all path prefixes that begin with
``/unifyfs`` this would require a:

.. code-block:: C
    :caption: C

    unifyfs_mount('/unifyfs', rank, rank_num);

.. code-block:: Fortran
    :caption: Fortran

    call UNIFYFS_MOUNT('/unifyfs', rank, size, ierr);

Here, ``/unifyfs`` is the path prefix for UnifyFS to intercept.
The ``rank`` parameter specifies the MPI rank of the calling process.
The ``size`` parameter specifies the number of MPI ranks in the user job.

----------
Unmounting
----------

When the application is done using UnifyFS, it should call ``unifyfs_unmount``.

.. code-block:: C
    :caption: C

    unifyfs_unmount();

.. code-block:: Fortran
    :caption: Fortran

    call UNIFYFS_UNMOUNT(ierr);

.. explicit external hyperlink targets

.. _ifort_issue: https://github.com/LLNL/UnifyFS/issues/300
.. _writeread.c: https://github.com/LLNL/UnifyFS/blob/dev/examples/src/writeread.c
.. _writeread.f90: https://github.com/LLNL/UnifyFS/blob/dev/examples/src/writeread.f90
.. _xlf_issue: https://github.com/LLNL/UnifyFS/issues/304
