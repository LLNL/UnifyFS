********
Examples
********

There are several examples_ available on ways to use UnifyCR. These examples
build into static and GOTCHA versions (pure POSIX versions coming soon) and are
also used as a form of :doc:`intregraton testing <testing>`.

Examples Locations
==================

The example programs can be found in two locations, where UnifyCR is built and
where UnifyCR is installed.

Install Location
----------------

Upon installation of UnifyCR, the example programs are installed into the
*install/libexec* folder.

Installed with Spack
^^^^^^^^^^^^^^^^^^^^

The Spack installation location of UnifyCR can be found with the command
``spack location -i unifycr``.

To easily navigate to this location and find the examples, do:

.. code-block:: Bash

    $ spack cd -i unifycr
    $ cd libexec

Installed without Spack
^^^^^^^^^^^^^^^^^^^^^^^

The autotools installation of UnifyCR will place the example programs in the
*libexec/* directory of the path provided to ``--prefix=/path/to/install`` during
the configure step of :doc:`building and installing <build-intercept>`.

Build Location
--------------

Built with Spack
^^^^^^^^^^^^^^^^

The Spack build location of UnifyCR (on a successful install) only exists when
``--keep-stage`` in included during installation or if the build fails. This
location can be found with the command ``spack location unifycr``.

To navigate to the location of the static and POSIX examples, do:

.. code-block:: Bash

    $ spack install --keep-stage unifycr
    $ spack cd unifycr
    $ cd spack-build/examples/src

The GOTCHA examples are one directory deeper in
*spack-build/examples/src/.libs*.

.. note::

    If you installed UnifyCR with any variants, in order to navigate to the
    build directory you must include these variants in the ``spack cd``
    command. E.g.:

    ``spack cd unifycr+hdf5 ^hdf5~mpi``

Built without Spack
^^^^^^^^^^^^^^^^^^^

The autotools build of UnifyCR will place the static and POSIX example programs
in the *examples/src* directory and the GOTCHA example programs in the
*examples/src/.libs* directory of your build directory.

------------

Running the Examples
====================

In order to run any of the example programs you first need to start the UnifyCR
server daemon on the nodes in the job allocation. To do this, see
:doc:`start-stop`.

Each example takes multiple arguments and so each has its own ``--help`` option
to aid in this process.

.. code-block:: none

    [prompt]$ ./sysio-write-static --help

    Usage: sysio-write-static [options...]

    Available options:
     -b, --blocksize=<size in bytes>  logical block size for the target file
                                      (default 1048576, 1MB)
     -n, --nblocks=<count>            count of blocks each process will write
                                      (default 128)
     -c, --chunksize=<size in bytes>  I/O chunk size for each write operation
                                      (default 64436, 64KB)
     -d, --debug                      pause before running test
                                      (handy for attaching in debugger)
     -f, --filename=<filename>        target file name under mountpoint
                                      (default: testfile)
     -h, --help                       help message
     -L, --lipsum                     generate contents to verify correctness
     -m, --mount=<mountpoint>         use <mountpoint> for unifycr
                                      (default: /unifycr)
     -P, --pwrite                     use pwrite(2) instead of write(2)
     -p, --pattern=<pattern>          should be 'n1'(n to 1) or 'nn' (n to n)
                                      (default: n1)
     -S, --synchronous                sync metadata on each write
     -s, --standard                   do not use unifycr but run standard I/O
     -u, --unmount                    unmount the filesystem after test

Notice the mountpoint is defaulted to ``-mount=/unifycr``. If you chose a
different mountpoint during :doc:`start-stop`, the ``-m`` option for the
example will need to be provided to match.

One form of running this example could be:

.. code-block:: Bash

    $ srun -N4 -n4 sysio-write-static -m /myMountPoint -f myTestFile

.. explicit external hyperlink targets

.. _examples: https://github.com/LLNL/UnifyCR/tree/dev/examples/src
