********
Examples
********

There are several examples_ available on ways to use UnifyFS. These examples
build into static and GOTCHA versions (pure POSIX versions coming soon) and are
also used as a form of :doc:`intregraton testing <testing>`.

Examples Locations
==================

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

Installed without Spack
^^^^^^^^^^^^^^^^^^^^^^^

The autotools installation of UnifyFS will place the example programs in the
*libexec/* directory of the path provided to ``--prefix=/path/to/install`` during
the configure step of :doc:`building and installing <build-intercept>`.

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

Built without Spack
^^^^^^^^^^^^^^^^^^^

The autotools build of UnifyFS will place the static and POSIX example programs
in the *examples/src* directory and the GOTCHA example programs in the
*examples/src/.libs* directory of your build directory.

------------

.. _run-ex-label:

Running the Examples
====================

In order to run any of the example programs you first need to start the UnifyFS
server daemon on the nodes in the job allocation. To do this, see
:doc:`start-stop`.

Each example takes multiple arguments and so each has its own ``--help`` option
to aid in this process.

.. code-block:: none

    [prompt]$ ./write-static --help

    Usage: write-static [options...]

    Available options:
     -a, --appid=<id>          use given application id
                               (default: 0)
     -A, --aio                 use asynchronous I/O instead of read|write
                               (default: off)
     -b, --blocksize=<bytes>   I/O block size
                               (default: 16 MiB)
     -c, --chunksize=<bytes>   I/O chunk size for each operation
                               (default: 1 MiB)
     -d, --debug               for debugging, wait for input (at rank 0) at start
                               (default: off)
     -f, --file=<filename>     target file name (or path) under mountpoint
                               (default: 'testfile')
     -k, --check               check data contents upon read
                               (default: off)
     -L, --listio              use lio_listio instead of read|write
                               (default: off)
     -m, --mount=<mountpoint>  use <mountpoint> for unifyfs
                               (default: /unifyfs)
     -M, --mapio               use mmap instead of read|write
                               (default: off)
     -n, --nblocks=<count>     count of blocks each process will read|write
                               (default: 32)
     -p, --pattern=<pattern>   'n1' (N-to-1 shared file) or 'nn' (N-to-N file per process)
                               (default: 'n1')
     -P, --prdwr               use pread|pwrite instead of read|write
                               (default: off)
     -S, --stdio               use fread|fwrite instead of read|write
                               (default: off)
     -U, --disable-unifyfs     do not use UnifyFS
                               (default: enable UnifyFS)
     -v, --verbose             print verbose information
                               (default: off)
     -V, --vecio               use readv|writev instead of read|write
                               (default: off)
     -x, --shuffle             read different data than written
                               (default: off)

One form of running this example could be:

.. code-block:: Bash

    $ srun -N4 -n4 write-static -m /myMountPoint -f myTestFile

.. explicit external hyperlink targets

.. _examples: https://github.com/LLNL/UnifyFS/tree/dev/examples/src

Transfer API
------------

UnifyFS has a transfer API to move files from UnifyFS to external storage (or from external storage into UnifyFS).  The transfer functionality can be invoked by using the "transfer" application in the examples directory as show in the following example:

.. code-block:: Bash

    $ srun -N4 -n4 transfer-static /unifyfs/file1 /scratch/mydir/file1

(assuming that /unifyfs/file1 is a file you've written within the UnifyFS file space by an application.)  To use the transfer API functions directly in a C program, use the following code fragment as a template:

.. code-block:: C

   if (parallel) {
        // use the parallel case for files of any size for increased efficiency
        ret = unifyfs_transfer_file_parallel(srcpath, dstpath);
        if (ret) {
            fprintf(stderr, "copy failed (%d: %s)", ret, strerror(ret));
        }
    } else {
        // use the serial case only for small files
        if (rank_worker >= total_ranks) {
            test_print(rank, "%d is not a valid rank");
            goto out;
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == rank_worker) {
            ret = unifyfs_transfer_file_serial(srcpath, dstpath);
            if (ret) {
                fprintf(stderr, "copy failed (%d: %s)", ret, strerror(ret));
            }
        }
    }
    ...
    out:
    MPI_Finalize();

    return ret;

This code fragment is from the code example.c in the UnifyFS examples directory.
