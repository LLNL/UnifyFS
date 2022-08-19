=============================
Link with the UnifyFS library
=============================

This section describes how to link an application with the UnifyFS library.
The UnifyFS library contains symbols for the UnifyFS API, like unifyfs_mount,
as well as wrappers for I/O routines, like open, write, and close.
In the examples below, replace <unifyfs> with the path to your UnifyFS install.

---------------------------

-----------
Static link
-----------

For a static link, UnifyFS utilizes the ``--wrap`` feature of the ``ld`` command.
One must specify a ``--wrap`` option for every I/O call that is wrapped,
for which there are many.
To make this easier, UnifyFS installs a unifyfs-config script that one should invoke to specify those flags, e.g.,

.. code-block:: Bash

    $ mpicc -o test_write \
          `<unifyfs>/bin/unifyfs-config --pre-ld-flags` \
          test_write.c \
          `<unifyfs>/bin/unifyfs-config --post-ld-flags`

------------
Dynamic link
------------

A build of UnifyFS includes two different shared libraries.  Which one you
should link against depends on your application.  If you wish to take advantage
of the UnifyFS auto-mount feature (assuming the feature was enabled at
compile-time), then you should link against ``libunifyfs_mpi_gotcha.so``.  If
you are not building an MPI-enabled application, or if you want explicit
control over when UnifyFS filesystem is mounted and unmounted, then link
against ``libunifyfs_gotcha.so``.  In this case, you will also have to add
calls to ``unifyfs_mount`` and ``unifyfs_unmount`` in the appropriate
locations in your code. See :doc:`api`.

To intercept I/O calls using gotcha, use the following syntax to link an
application:

C
**************
For code that uses the auto-mount feature:

.. code-block:: Bash

    $ mpicc -o test_write test_write.c \
        -L<unifyfs>/lib -lunifyfs_mpi_gotcha


For code that explicitly calls ``unifyfs_mount`` and ``unifyfs_unmount``:

.. code-block:: Bash

    $ mpicc -o test_write test_write.c \
        -I<unifyfs>/include -L<unifyfs>/lib -lunifyfs_gotcha

Note the use of the ``-I`` option so that the compiler knows where to find
the ``unifyfs.h`` header file.


Fortran
**************
For code that uses the auto-mount feature:

.. code-block:: Bash

    $ mpif90 -o test_write test_write.F \
        -L<unifyfs>/lib -lunifyfs_mpi_gotcha

For code that explicitly calls ``unifyfs_mount`` and ``unifyfs_unmount``:

.. code-block:: Bash

    $ mpif90 -o test_write test_write.F \
        -I<unifyfs>/include -L<unifyfs>/lib -lunifyfsf -lunifyfs_gotcha

Note the use of the ``-I`` option to specify the location of the
``unifyfsf.h`` header.  Also note the use of the ``unifyfsf`` library.  This
library  provides the Fortran bindings for the ``unifyfs_mount`` and
``unifyfs_unmount`` functions.

