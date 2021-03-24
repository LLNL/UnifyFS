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

To intercept I/O calls using gotcha, use the following syntax to link an
application:

C
**************

.. code-block:: Bash

    $ mpicc -o test_write test_write.c \
        -I<unifyfs>/include -L<unifyfs>/lib -lunifyfs_gotcha \
        -L<gotcha>/lib64 -lgotcha

Fortran
**************

.. code-block:: Bash

    $ mpif90 -o test_write test_write.F \
        -I<unifyfs>/include -L<unifyfs>/lib -lunifyfsf -lunifyfs_gotcha
