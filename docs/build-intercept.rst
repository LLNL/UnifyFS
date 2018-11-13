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

.. _build-label:

---------------------------
How to Build UnifyCR
---------------------------

To install all dependencies and set up your build environment, we recommend
using the `Spack package manager <https://github.com/spack/spack>`_.

Building with Spack
********************

These instructions assume that you do not already have a module system installed
such as LMod, Dotkit, or Environment Modules. If your system already has Dotkit
or LMod installed then installing the environment-modules package with spack
is unnecessary (so you can safely skip that step).

If you use Dotkit then replace ``spack load`` with ``spack use``.

.. code-block:: Bash

    $ git clone https://github.com/spack/spack
    $ ./spack/bin/spack install environment-modules
    $ . spack/share/spack/setup-env.sh
    $ spack install unifycr
    $ spack load unifycr

.. Edit the following admonition if the default of variants are changed or when
   new variants are added.

Include or remove variants with Spack when installing UnifyCR when a custom
build is desired. Type ``spack info unifycr`` for more info.

.. table:: UnifyCR Build Variants
   :widths: auto

   =======  ========================================  ========================
   Variant  Command                                   Description
   =======  ========================================  ========================
   Debug    ``spack install unifycr+debug``           Enable debug build
   HDF5     ``spack install unifycr+hdf5``            Build with parallel HDF5

            ``spack install unifycr+hdf5 ^hdf5~mpi``  Build with serial HDF5
   NUMA     ``spack install unifycr+numa``            Build with NUMA
   =======  ========================================  ========================

.. attention::

    The initial install could take a while as Spack will install build
    dependencies (autoconf, automake, m4, libtool, and pkg-config) as well as
    any dependencies of dependencies (cmake, perl, etc.) if you don't already
    have these dependencies installed through Spack or haven't told Spack where
    they are locally installed on your system (i.e., through a custom
    `packages.yaml <https://spack.readthedocs.io/en/latest/build_settings.html#external-packages>`_).
    Type ``spack spec -I unifycr`` before installing to see what Spack is going
    to do.

---------------------------

Building with Autotools
************************

Download the latest UnifyCR release from the `Releases
<https://github.com/LLNL/UnifyCR/releases>`_ page.

Building the Dependencies
^^^^^^^^^^^^^^^^^^^^^^^^^^

UnifyCR requires MPI, LevelDB, and GOTCHA(version 0.0.2).

Build the Dependencies with Spack
""""""""""""""""""""""""""""""""""

Once Spack is installed on your system (see :ref:`above <build-label>`), you
can install just the dependencies for an easier manual installation of UnifyCR.

If you use Dotkit then replace ``spack load`` with ``spack use``.

.. code-block:: Bash

    $ spack install environment-modules
    $
    $ spack install leveldb
    $ spack install gotcha@0.0.2
    $ spack install flatcc
    $ 
    $ git clone https://xgitlab.cels.anl.gov/sds/sds-repo.git sds-repo.git
    $ cd sds-repo.git
    $   spack repo add .
    $ cd ..
    $ spack install margo
    $ spack install argobots

.. tip::

    You can use ``spack install --only=dependencies unifycr`` to install all of
    UnifyCR's dependencies without installing UnifyCR.

    Keep in mind this will also install all the build dependencies and
    dependencies of dependencies if you haven't already installed them through
    Spack or told Spack where they are locally installed on your system.

Then to build UnifyCR:

.. code-block:: Bash

    $ spack load leveldb
    $ spack load gotcha@0.0.2
    $ spack load flatcc
    $ spack load mercury
    $ spack load argobots
    $ spack load margo
    $
    $ ./autogen.sh
    $ ./configure --prefix=/path/to/install --enable-debug
    $ make
    $ make install

Build the Dependencies without Spack
"""""""""""""""""""""""""""""""""""""

For users who cannot use Spack, you may fetch version 0.0.2 (compatibility with
latest release in progress) of `GOTCHA <https://github.com/LLNL/GOTCHA/releases>`_

And leveldb (if not already installed on your system):
`leveldb <https://github.com/google/leveldb/releases/tag/v1.20>`_

To download and install Margo and its dependencies (Mercury and Argobots)
follow the instructions here: `Margo <https://xgitlab.cels.anl.gov/sds/margo>`_

To get flatcc `flatcc <https://github.com/dvidelabs/flatcc>`_

Then to build UnifyCR:

.. code-block:: Bash

    $ ./configure --prefix=/path/to/install --enable-debug --with-gotcha=/path/to/gotcha --with-leveldb=/path/to/leveldb --with-mercury=/path/to/mercury --with-argobots=/path/to/argobots --with-margo=/path/to/margo --with-flatcc=/path/to/flatcc
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
