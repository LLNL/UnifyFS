****************
Example Programs
****************

There are several examples_ available on ways to use UnifyFS. These examples
build into static, GOTCHA, and pure POSIX (not linked with UnifyFS) versions
depending on how they are linked. Several of the example programs are also used
in the UnifyFS :doc:`intregraton testing <testing>`.

Locations of Examples
=====================

The example programs can be found in two locations, where UnifyFS is built and
where UnifyFS is installed.

Install Location
----------------

Upon installation of UnifyFS, the example programs are installed into the
*install/libexec* folder.

Installed with Spack
^^^^^^^^^^^^^^^^^^^^

The Spack installation location of UnifyFS can be found with the command
``spack location -i unifyfs``.

To easily navigate to this location and find the examples, do:

.. code-block:: Bash

    $ spack cd -i unifyfs
    $ cd libexec

Installed with Autotools
^^^^^^^^^^^^^^^^^^^^^^^^

The autotools installation of UnifyFS will place the example programs in the
*libexec/* directory of the path provided to ``--prefix=/path/to/install`` during
the configure step of :doc:`building and installing <build>`.

Build Location
--------------

Built with Spack
^^^^^^^^^^^^^^^^

The Spack build location of UnifyFS (on a successful install) only exists when
``--keep-stage`` in included during installation or if the build fails. This
location can be found with the command ``spack location unifyfs``.

To navigate to the location of the static and POSIX examples, do:

.. code-block:: Bash

    $ spack install --keep-stage unifyfs
    $ spack cd unifyfs
    $ cd spack-build/examples/src

The GOTCHA examples are one directory deeper in
*spack-build/examples/src/.libs*.

.. note::

    If you installed UnifyFS with any variants, in order to navigate to the
    build directory you must include these variants in the ``spack cd``
    command. E.g.:

    ``spack cd unifyfs+hdf5 ^hdf5~mpi``

Built with Autotools
^^^^^^^^^^^^^^^^^^^^

The autotools build of UnifyFS will place the static and POSIX example programs
in the *examples/src* directory and the GOTCHA example programs in the
*examples/src/.libs* directory of your build directory.

------------

.. _run-ex-label:

Running the Examples
====================

In order to run any of the example programs you first need to start the UnifyFS
server daemon on the nodes in the job allocation. To do this, see
:doc:`run`.

Each example takes multiple arguments and so each has its own ``--help`` option
to aid in this process.

.. code-block:: none

    [prompt]$ ./write-static --help

    Usage: write-static [options...]

    Available options:
     -a, --library-api           use UnifyFS library API instead of POSIX I/O
                                 (default: off)
     -A, --aio                   use asynchronous I/O instead of read|write
                                 (default: off)
     -b, --blocksize=<bytes>     I/O block size
                                 (default: 16 MiB)
     -c, --chunksize=<bytes>     I/O chunk size for each operation
                                 (default: 1 MiB)
     -d, --debug                 for debugging, wait for input (at rank 0) at start
                                 (default: off)
     -D, --destfile=<filename>   transfer destination file name (or path) outside mountpoint
                                 (default: none)
     -f, --file=<filename>       target file name (or path) under mountpoint
                                 (default: 'testfile')
     -k, --check                 check data contents upon read
                                 (default: off)
     -l, --laminate              laminate file after writing all data
                                 (default: off)
     -L, --listio                use lio_listio instead of read|write
                                 (default: off)
     -m, --mount=<mountpoint>    use <mountpoint> for unifyfs
                                 (default: /unifyfs)
     -M, --mpiio                 use MPI-IO instead of POSIX I/O
                                 (default: off)
     -n, --nblocks=<count>       count of blocks each process will read|write
                                 (default: 32)
     -N, --mapio                 use mmap instead of read|write
                                 (default: off)
     -o, --outfile=<filename>    output file name (or path)
                                 (default: 'stdout')
     -p, --pattern=<pattern>     'n1' (N-to-1 shared file) or 'nn' (N-to-N file per process)
                                 (default: 'n1')
     -P, --prdwr                 use pread|pwrite instead of read|write
                                 (default: off)
     -r, --reuse-filename        remove and reuse the same target file name
                                 (default: off)
     -S, --stdio                 use fread|fwrite instead of read|write
                                 (default: off)
     -t, --pre-truncate=<size>   truncate file to size (B) before writing
                                 (default: off)
     -T, --post-truncate=<size>  truncate file to size (B) after writing
                                 (default: off)
     -u, --unlink                unlink target file
                                 (default: off)
     -U, --disable-unifyfs       do not use UnifyFS
                                 (default: enable UnifyFS)
     -v, --verbose               print verbose information
                                 (default: off)
     -V, --vecio                 use readv|writev instead of read|write
                                 (default: off)
     -x, --shuffle               read different data than written
                                 (default: off)

One form of running this example could be:

.. code-block:: Bash

    $ srun -N4 -n4 write-static -m /unifyfs -f myTestFile

Producer-Consumer Workflow
==========================

UnifyFS can be used to support producer/consumer workflows where processes in a
job perform loosely synchronized communication through files such as in coupled
simulation/analytics workflows.

The *write.c* and *read.c* example programs can be used as a basic test in
running a producer-consumer workflow with UnifyFS.

.. code-block:: Bash
    :caption: All hosts in allocation

    $ # start unifyfs
    $
    $ # write on all hosts
    $ srun -N4 -n16 write-gotcha -f testfile
    $
    $ # read on all hosts
    $ srun -N4 -n16 read-gotcha -f testfile
    $
    $ # stop unifyfs

.. code-block:: Bash
    :caption: Disjoint hosts in allocation

    $ # start unifyfs
    $
    $ # write on half of hosts
    $ srun -N2 -n8 --exclude=$hostlist_subset1 write-gotcha -f testfile
    $
    $ # read on other half of hosts
    $ srun -N2 -n8 --exclude=$hostlist_subset2 read-gotcha -f testfile
    $
    $ # stop unifyfs

.. note::
    Producer/consumer support with UnifyFS has been tested using POSIX and
    MPI-IO APIs on x86_64 (MVAPICH) and Power 9 systems (Spectrum MPI).

    These scenarios have been tested using both the same and disjoint sets of
    hosts as well as using a shared file and a file per process for I/O.

.. explicit external hyperlink targets

.. _examples: https://github.com/LLNL/UnifyFS/tree/dev/examples/src
