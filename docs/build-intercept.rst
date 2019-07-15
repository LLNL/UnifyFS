========================
Build & I/O Interception
========================

In this section, we describe how to build UnifyCR with I/O interception.

.. note::

    The current version of UnifyCR adopts the mdhim key-value store, which strictly
    requires:

    "An MPI distribution that supports MPI_THREAD_MULTIPLE and per-object locking of
    critical sections (this excludes OpenMPI up to version 3.0.1, the current version as of this writing)"

    as specified in the project `github <https://github.com/mdhim/mdhim-tng>`_.

.. _build-label:

---------------------------
How to Build UnifyCR
---------------------------

To install all dependencies and set up your build environment, we recommend
using the `Spack package manager <https://github.com/spack/spack>`_. If you
already have Spack, make sure you have the latest release or if using a clone
of their develop branch, ensure you have pulled the latest changes.

Building with Spack
********************

These instructions assume that you do not already have a module system installed
such as LMod, Dotkit, or Environment Modules. If your system already has Dotkit
or LMod installed then installing the environment-modules package with spack
is unnecessary (so you can safely skip that step).

If you use Dotkit then replace ``spack load`` with ``spack use``.
First, install Spack if you don't already have it:

.. code-block:: Bash

    $ git clone https://github.com/spack/spack
    $ ./spack/bin/spack install environment-modules
    $ . spack/share/spack/setup-env.sh

Make use of Spack's `shell support <https://spack.readthedocs.io/en/latest/getting_started.html#add-spack-to-the-shell>`_
to automatically add Spack to your ``PATH`` and allow the use of the ``spack``
command.

Then install UnifyCR:

.. code-block:: Bash

    $ spack install unifycr
    $ spack load unifycr

.. Edit the following admonition if the default of variants are changed or when
   new variants are added.

Include or remove variants with Spack when installing UnifyCR when a custom
build is desired. Type ``spack info unifycr`` for more info.

.. table:: UnifyCR Build Variants
   :widths: auto

   =======  ========================================  =========================
   Variant  Command                                   Description
   =======  ========================================  =========================
   HDF5     ``spack install unifycr+hdf5``            Build with parallel HDF5

            ``spack install unifycr+hdf5 ^hdf5~mpi``  Build with serial HDF5
   Fortran  ``spack install unifycr+fortran``         Build with gfortran
   NUMA     ``spack install unifycr+numa``            Build with NUMA
   pmpi     ``spack install unifycr+pmpi``            Transparent mount/unmount
   PMI      ``spack install unifycr+pmi``             Enable PMI2 build options
   PMIx     ``spack install unifycr+pmix``            Enable PMIx build options
   =======  ========================================  =========================

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

.. _spack-build-label:

Build the Dependencies with Spack
""""""""""""""""""""""""""""""""""

Once Spack is installed on your system (see :ref:`above <build-label>`), you
can install just the dependencies for an easier manual installation of UnifyCR.

If you use Dotkit then replace ``spack load`` with ``spack use``.

.. code-block:: Bash

    $ spack install leveldb
    $ spack install gotcha@0.0.2
    $ spack install flatcc
    $ spack install margo

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
    $ ./configure --prefix=/path/to/install
    $ make
    $ make install

.. note:: **Fortran Compatibility**

    To build with gfortran compatibility, include the ``--enable-fortran``
    configure option:

    ``./configure --prefix=/path/to/install/ --enable-fortran``

    There is a known `ifort_issue <https://github.com/LLNL/UnifyCR/issues/300>`_
    with the Intel Fortran compiler as well as an `xlf_issue <://github.com/LLNL/UnifyCR/issues/304>`_
    with the IBM Fortran compiler. Other Fortran compilers are currently
    unknown.

To see all available build configuration options, type ``./configure --help``
after ``./autogen.sh`` has been run.

.. TODO: Add a section in build docs that shows all the build config options

Build the Dependencies without Spack
"""""""""""""""""""""""""""""""""""""

For users who cannot use Spack, you may fetch version 0.0.2 (compatibility with
latest release in progress) of `GOTCHA <https://github.com/LLNL/GOTCHA/releases>`_

And leveldb (if not already installed on your system):
`leveldb <https://github.com/google/leveldb/releases/tag/v1.20>`_

To get flatcc `flatcc <https://github.com/dvidelabs/flatcc>`_

To download and install Margo and its dependencies (Mercury and Argobots)
follow the instructions here: `Margo <https://xgitlab.cels.anl.gov/sds/margo>`_

.. important::

    Margo uses pkg-config to ensure it compiles and links correctly with all of
    its dependencies' libraries. When building without Spack, you'll need to
    manually set the ``PKG_CONFIG_PATH`` environment variable and include in
    that variable the paths for the ``.pc`` files for Mercury, Argobots, and
    Margo separated by colons.

Then to build UnifyCR:

.. code-block:: Bash

    $ export PKG_CONFIG_PATH=path/to/mercury/lib/pkgconfig:path/to/argobots/lib/pkgconfig:path/to/margo/lib/pkgconfig
    $ ./autogen.sh
    $ ./configure --prefix=/path/to/install --with-gotcha=/path/to/gotcha --with-leveldb=/path/to/leveldb  --with-flatcc=/path/to/flatcc
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

C
^^^^^^^^^^^^^^

.. code-block:: Bash

    $ mpicc -o test_write test_write.c \
        -I<unifycr>/include -L<unifycy>/lib -lunifycr_gotcha \
        -L<gotcha>/lib64 -lgotcha

Fortran
^^^^^^^^^^^^^^

.. code-block:: Bash

    $ mpif90 -o test_write test_write.F \
        -I<unifycr>/include -L<unifycy>/lib -lunifycrf -lunifycr_gotcha
