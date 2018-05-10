========================
Build & I/O Interception
========================

In this section, we describe how to build UnifyCR with I/O interception.

.. note::

    The current version of UnifyCR adopts the mdhim key-value store, which strictly
    requires:

    "An MPI distribution that supports MPI_THREAD_MULTIPLE and per-object locking of
    critical sections (this excludes OpenMPI up to version 3.0.1, the current version as of this writing)"

    as specified in the project `github <https://github.com/mdhim/mdhim-tng>`_

---------------------------
How to build UnifyCR
---------------------------

Download the latest UnifyCR release from the `Releases
<https://github.com/LLNL/UnifyCR/releases>`_ page. UnifyCR requires MPI,
LevelDB, and GOTCHA.

**Building with Spack**
***************************

To install leveldb and gotcha and set up your build environment, we recommend
using the `Spack package manager <https://github.com/spack/spack>`_.

The instructions assume that you do not already have a module system installed
such as LMod, Dotkit, or Environment Modules. If your system already has Dotkit
or LMod installed then installing the environment-modules package with spack
is unnecessary (so you can safely skip that step).

If you use Dotkit then replace ``spack load`` with ``spack use``.

.. code-block:: Bash

    $ git clone https://github.com/spack/spack
    $ ./spack/bin/spack install leveldb
    $ ./spack/bin/spack install gotcha
    $ ./spack/bin/spack install environment-modules
    $ . spack/share/spack/setup-env.sh
    $ spack load leveldb
    $ spack load gotcha

Then to build UnifyCR:

.. code-block:: Bash

    $ ./autogen.sh
    $ ./configure --prefix=/path/to/install --enable-debug
    $ make
    $ make install

**Building without Spack**
***************************

For users who cannot use Spack, you may fetch the latest release of
`GOTCHA <https://github.com/LLNL/GOTCHA>`_

And leveldb (if not already installed on your system):
`leveldb <https://github.com/google/leveldb/releases/tag/v1.20>`_

If you installed leveldb from source then you may have to add the pkgconfig file
for leveldb manually. This is assuming your install of leveldb does not contain
a .pc file (it usually doesn't). Then, add the path to that file to
PKG_CONFIG_PATH.

.. code-block:: Bash

    $ cat leveldb.pc
    #leveldb.pc
    prefix=/path/to/leveldb/install
    exec_prefix=/path/to/leveldb/install
    libdir=/path/to/leveldb/install/lib64
    includedir=/path/to/leveldb/install/include
    Name: leveldb
    Description: a fast key-value storage library
    Version: 1.20
    Cflags: -I${includedir}
    Libs: -L${libdir} -lleveldb

    $ export PKG_CONFIG_PATH=/path/to/leveldb/pkgconfig

Leave out the path to leveldb in your configure line if you didn't install it
from source.

.. code-block:: Bash

    $ ./configure --prefix=/path/to/install --with-gotcha=/path/to/gotcha --enable-debug --with-leveldb=/path/to/leveldb
    $ make
    $ make install

.. note::

    You may need to add the following to your configure line if it is not in
    your default path on a linux machine:

    ``--with-numa=$PATH_TO_NUMA``

    This is needed to enable NUMA-aware memory allocation on Linux machines. Set the
    NUMA policy at runtime with ``UNIFYCR_NUMA_POLICY = local | interleaved``, or set
    NUMA nodes explicitly with ``UNIFYCR_USE_NUMA_BANK = <node no.>``

---------------------------
I/O Interception
---------------------------

POSIX calls can be intercepted via the methods described below.

Statically
**************

Steps for static linking using --wrap:

To intercept I/O calls using a static link, you must add flags to your link
line. UnifyCR installs a unifycr-config script that returns those flags, e.g.,

.. code-block:: Bash

    $ mpicc -o test_write \
          `<unifycr>/bin/unifycr-config --pre-ld-flags` \
          test_write.c \
          `<unifycr>/bin/unifycr-config --post-ld-flags`

Dynamically
**************

Steps for dynamic linking using gotcha:

To intercept I/O calls using gotcha, use the following syntax to link an
application.

.. code-block:: Bash

    $ mpicc -o test_write test_write.c \
        -I<unifycr>/include -L<unifycy>/lib -lunifycr_gotcha \
        -L<gotcha>/lib64 -lgotcha
