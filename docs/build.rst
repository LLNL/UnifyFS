========================
Build UnifyFS
========================

This section describes how to build UnifyFS and its dependencies.
There are three options:

* build both UnifyFS and its dependencies with Spack,
* build the dependencies with Spack, but build UnifyFS with autotools
* build the dependencies with a bootstrap script, and build UnifyFS with autotools

---------------------------

---------------------------------------------
Build UnifyFS and its dependencies with Spack
---------------------------------------------

One may install UnifyFS and its dependencies with `Spack <https://github.com/spack/spack>`_.
If you already have Spack, make sure you have the latest release.
If you use a clone of the Spack develop branch, be sure to pull the latest changes.

.. _build-label:

Install Spack
*************

.. code-block:: Bash

    $ git clone https://github.com/spack/spack
    $ # create a packages.yaml specific to your machine
    $ . spack/share/spack/setup-env.sh

Use Spack's `shell support <https://spack.readthedocs.io/en/latest/getting_started.html#add-spack-to-the-shell>`_
to add Spack to your ``PATH`` and enable use of the ``spack`` command.

Build UnifyFS and its dependencies
**********************************

.. code-block:: Bash

    $ spack install unifyfs
    $ spack load unifyfs

If the most recent changes on the development branch ('dev') of UnifyFS are
desired, then do ``spack install unifyfs@develop``.

.. Edit the following admonition if the default of variants are changed or when
   new variants are added.

Include or remove variants with Spack when installing UnifyFS when a custom
build is desired. Type ``spack info unifyfs`` for more info.

.. table:: UnifyFS Build Variants
   :widths: auto

   ==========  ========================================  ===========================
      Variant  Command                                   Description
   ==========  ========================================  ===========================
   Auto-mount  ``spack install unifyfs+auto-mount``      Enable transparent mounting
   HDF5        ``spack install unifyfs+hdf5``            Build with parallel HDF5

               ``spack install unifyfs+hdf5 ^hdf5~mpi``  Build with serial HDF5
   Fortran     ``spack install unifyfs+fortran``         Enable Fortran support
   MDHIM       ``spack install unifyfs+mdhim``           Enable MDHIM build options
   PMI         ``spack install unifyfs+pmi``             Enable PMI2 support
   PMIx        ``spack install unifyfs+pmix``            Enable PMIx support
   spath       ``spack install unifyfs+spath``           Normalize relative paths
   ==========  ========================================  ===========================

.. attention::

    The initial install could take a while as Spack will install build
    dependencies (autoconf, automake, m4, libtool, and pkg-config) as well as
    any dependencies of dependencies (cmake, perl, etc.) if you don't already
    have these dependencies installed through Spack or haven't told Spack where
    they are locally installed on your system (i.e., through a custom
    `packages.yaml <https://spack.readthedocs.io/en/latest/build_settings.html#external-packages>`_).
    Type ``spack spec -I unifyfs`` before installing to see what Spack is going
    to do.

---------------------------

-----------------------------------------------------------
Build dependencies with Spack, build UnifyFS with autotools
-----------------------------------------------------------

One can install the UnifyFS dependencies with Spack and build UnifyFS
with autotools.
This is useful if one needs to modify the UnifyFS source code
between builds.
Take advantage of
`Spack Environments <https://spack.readthedocs.io/en/latest/environments.html>`_
to streamline this process.

.. _spack-build-label:

Build the dependencies
**********************

Once Spack is installed on your system (see :ref:`above <build-label>`),
the UnifyFS dependencies can then be installed.

.. code-block:: Bash

    $ spack install gotcha
    $ spack install margo ^mercury+bmi

.. tip::

    You can use ``spack install --only=dependencies unifyfs`` to install all
    UnifyFS dependencies without installing UnifyFS.

    Keep in mind this will also install all the build dependencies and
    dependencies of dependencies if you haven't already installed them through
    Spack or told Spack where they are locally installed on your system via a
    `packages.yaml <https://spack.readthedocs.io/en/latest/build_settings.html#external-packages>`_.

Build UnifyFS
*************

Download the latest UnifyFS release from the `Releases
<https://github.com/LLNL/UnifyFS/releases>`_ page or clone the develop branch
from the UnifyFS repository `https://github.com/LLNL/UnifyFS <https://github.com/LLNL/UnifyFS>`_.

Load the dependencies into your environment and then
configure and build UnifyFS from its source code directory.

.. code-block:: Bash

    $ spack load gotcha
    $ spack load mercury
    $ spack load argobots
    $ spack load margo
    $
    $ ./autogen.sh
    $ ./configure --prefix=/path/to/install
    $ make
    $ make install

To see all available build configuration options, run ``./configure --help``
after ``./autogen.sh`` has been run.

---------------------------

------------------------------------------------------------------
Build dependencies with bootstrap and build UnifyFS with autotools
------------------------------------------------------------------

Download the latest UnifyFS release from the `Releases
<https://github.com/LLNL/UnifyFS/releases>`_ page or clone the develop branch
from the UnifyFS repository `https://github.com/LLNL/UnifyFS <https://github.com/LLNL/UnifyFS>`_.

Build the Dependencies
**********************

UnifyFS requires MPI, GOTCHA, Margo and OpenSSL.
References to these dependencies can be found on our :doc:`dependencies` page.

A `bootstrap.sh <https://github.com/LLNL/UnifyFS/blob/dev/bootstrap.sh>`_ script
in the UnifyFS source distribution downloads and installs all dependencies.
Simply run the script in the top level directory of the source code.

.. code-block:: Bash

    $ ./bootstrap.sh

Build UnifyFS
*************

After bootstrap.sh installs the dependencies,
it prints the commands one needs to execute to build UnifyFS.
As an example, the commands may look like:

.. code-block:: Bash

    $ export PKG_CONFIG_PATH=path/to/mercury/lib/pkgconfig:path/to/argobots/lib/pkgconfig:path/to/margo/lib/pkgconfig
    $ ./autogen.sh
    $ ./configure --prefix=/path/to/install --with-gotcha=/path/to/gotcha
    $ make
    $ make install

To see all available build configuration options, run ``./configure --help``
after ``./autogen.sh`` has been run.

---------------------------

-----------------
Configure Options
-----------------

When building UnifyFS with autotools,
a number of options are available to configure its functionality.

Fortran
*******

To use UnifyFS in Fortran applications, pass the ``--enable-fortran``
option to configure. Note that only GCC Fortran (i.e., gfortran) is known to
work with UnifyFS. There is an open
`ifort_issue <https://github.com/LLNL/UnifyFS/issues/300>`_ with the Intel
Fortran compiler as well as an
`xlf_issue <https://github.com/LLNL/UnifyFS/issues/304>`_ with the IBM Fortran
compiler.

GOTCHA
******

GOTCHA is the preferred method for I/O interception with UnifyFS, but it is not
available on all platforms. If GOTCHA is not available on your target system,
you can omit it during UnifyFS configuration by using the ``--without-gotcha``
configure option. Without GOTCHA, static linker wrapping is required for I/O
interception, see :doc:`link`.

HDF5
****

UnifyFS includes example programs that use HDF5. If HDF5 is not available on
your target system, it can be omitted during UnifyFS configuration by using
the ``--without-hdf5`` configure option.

MDHIM
*****

Previous MDHIM-based support for file operations can be selected at configure
time using the ``--enable-mdhim`` option. Using this option requires LevelDB as
a dependency. Provide the path to your LevelDB install at configure time with
the ``--with-leveldb=/path/to/leveldb`` option. Note that this may not
currently be in a usable state.

PMI2/PMIx Key-Value Store
*************************

When available, UnifyFS uses the distributed key-value store capabilities
provided by either PMI2 or PMIx. To enable this support, pass either
the ``--enable-pmi`` or ``--enable-pmix`` option to configure. Without
PMI support, a distributed file system accessible to all servers is required.

Transparent Mounting for MPI Applications
*****************************************

MPI applications written in C or C++ may take advantage of the UnifyFS transparent
mounting capability. With transparent mounting, calls to ``unifyfs_mount()`` and
``unifyfs_unmount()`` are automatically performed during ``MPI_Init()`` and
``MPI_Finalize()``, respectively. Transparent mounting always uses ``/unifyfs`` as
the namespace mountpoint. To enable transparent mounting, use the
``--enable-mpi-mount`` configure option.

---------------------------
