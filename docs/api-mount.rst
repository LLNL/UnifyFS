=================
Mounting UnifyFS
=================

In this section, we describe how to use the UnifyFS API in an application.

.. Attention:: **Fortran Compatibility**

   ``unifyfs_mount`` and ``unifyfs_unmount`` are now usable  with GFortran.
   There is a known ifort_issue_ with the Intel Fortran compiler as well as an
   xlf_issue_ with the IBM Fortran compiler. Other Fortran compilers are
   currently unknown.

   If using fortran, when :ref:`installing UnifyFS <build-label>` with Spack,
   include the ``+fortran`` variant, or configure UnifyFS with the
   ``--enable-fortran`` option if building manually.

---------------------------
Mounting 
---------------------------

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

To use the UnifyFS filesystem a user will have to provide a path prefix. All
file operations under the path prefix will be intercepted by the UnifyFS
filesystem. For instance, to use UnifyFS on all path prefixes that begin with
/tmp this would require a:

.. code-block:: C
    :caption: C

    unifyfs_mount('/tmp', rank, rank_num, 0);

.. code-block:: Fortran
    :caption: Fortran

    call UNIFYFS_MOUNT('/tmp', rank, size, 0, ierr);

Where ``/tmp`` is the path prefix you want UnifyFS to intercept. The rank and rank
number is the rank you are currently on, and the number of tasks you have
running in your job. Lastly, the zero corresponds to the app id.

---------------------------
Unmounting 
---------------------------

When you are finished using UnifyFS in your application, you should unmount.

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
