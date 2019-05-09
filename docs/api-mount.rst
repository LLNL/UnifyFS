=================
Mounting UnifyCR
=================

In this section, we describe how to use the UnifyCR API in an application.

.. Attention:: **Fortran Compatibility**

   ``unifycr_mount`` and ``unifycr_unmount`` are now usable  with GFortran.
   There is a known ifort_issue_ with the Intel Fortran compiler as well as an
   xlf_issue_ with the IBM Fortran compiler. Other Fortran compilers are
   currently unknown.

   If using fortran, when :ref:`installing UnifyCR <build-label>` with Spack,
   include the ``+fortran`` variant, or configure UnifyCR with the
   ``--enable-fortran`` option if building manually.

---------------------------
Mounting 
---------------------------

In ``C`` applications, include *unifycr.h*. See writeread.c_ for a full
example.

.. code-block:: C

        #include <unifycr.h>

In ``Fortran`` applications, include *unifycrf.h*. See writeread.f90_ for a
full example.

.. code-block:: Fortran

        include 'unifycrf.h'

To use the UnifyCR filesystem a user will have to provide a path prefix. All 
file operations under the path prefix will be intercepted by the UnifyCR 
filesystem. For instance, to use UnifyCR on all path prefixes that begin with 
/tmp this would require a:

.. code-block:: C
    :caption: C

        unifycr_mount('/tmp', rank, rank_num, 0);

.. code-block:: Fortran
    :caption: Fortran

        call UNIFYCR_MOUNT('/tmp', rank, size, 0, ierr);

Where /tmp is the path prefix you want UnifyCR to intercept. The rank and rank 
number is the rank you are currently on, and the number of tasks you have 
running in your job. Lastly, the zero corresponds to the app id.

---------------------------
Unmounting 
---------------------------

When you are finished using UnifyCR in your application, you should unmount. 
  
.. code-block:: C
    :caption: C

        if (rank == 0) {
            unifycr_unmount();
        }

.. code-block:: Fortran
    :caption: Fortran

        call UNIFYCR_UNMOUNT(ierr);

It is only necessary to call unmount once on rank zero.

.. explicit external hyperlink targets

.. _ifort_issue: https://github.com/LLNL/UnifyCR/issues/300
.. _writeread.c: https://github.com/LLNL/UnifyCR/blob/dev/examples/src/writeread.c
.. _writeread.f90: https://github.com/LLNL/UnifyCR/blob/dev/examples/src/writeread.f90
.. _xlf_issue: https://github.com/LLNL/UnifyCR/issues/304
