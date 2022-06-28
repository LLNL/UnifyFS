=============
Build UnifyFS
=============

This section describes how to build UnifyFS and its dependencies.
There are three options:

* build both UnifyFS and dependencies with Spack,
* build the dependencies with Spack, but build UnifyFS with autotools
* build the dependencies with a bootstrap script, and build UnifyFS with autotools

----------

-----------------------------------------
Build UnifyFS and Dependencies with Spack
-----------------------------------------

One may install UnifyFS and its dependencies with Spack_.
If you already have Spack, make sure you have the latest release.
If you use a clone of the Spack develop branch, be sure to pull the latest changes.

.. warning:: **Thallium, Mochi Suite, and SDS Repo Users**

    The available and UnifyFS-compatible Mochi-Margo versions that are in the
    ``mochi-margo`` Spack package do not match up with the latest/default
    versions in the Mochi Suite, SDS Repo, and ``mochi-thallium`` Spack
    packages. It is likely that a different version of ``mochi-margo`` will need
    to be specified in the install command of UnifyFS (E.g.: ``spack install
    unifyfs ^mochi-margo@0.9.6``).

.. _build-label:
Install Spack
*************

.. code-block:: Bash

    $ git clone https://github.com/spack/spack
    $ # create a packages.yaml specific to your machine
    $ . spack/share/spack/setup-env.sh

Use `Spack's shell support`_ to add Spack to your ``PATH`` and enable use of the
``spack`` command.

Build and Install UnifyFS
*************************

.. code-block:: Bash

    $ spack install unifyfs
    $ spack load unifyfs

If the most recent changes on the development branch ('dev') of UnifyFS are
desired, then do ``spack install unifyfs@develop``.

Include or remove variants with Spack when installing UnifyFS when a custom
build is desired. Run ``spack info unifyfs`` for more information on available
variants.

.. table:: UnifyFS Build Variants
   :widths: auto

   ==========  =============================  =======  ===========================
      Variant  Command                        Default  Description
               (``spack install <package>``)
   ==========  =============================  =======  ===========================
   Auto-mount  ``unifyfs+auto-mount``         True     Enable transparent mounting
   Fortran     ``unifyfs+fortran``            False    Enable Fortran support
   PMI         ``unifyfs+pmi``                False    Enable PMI2 support
   PMIx        ``unifyfs+pmix``               False    Enable PMIx support
   spath       ``unifyfs+spath``              True     Normalize relative paths
   ==========  =============================  =======  ===========================

.. attention::

    The initial install could take a while as Spack will install build
    dependencies (autoconf, automake, m4, libtool, and pkg-config) as well as
    any dependencies of dependencies (cmake, perl, etc.) if you don't already
    have these dependencies installed through Spack or haven't told Spack where
    they are locally installed on your system (i.e., through a custom
    packages.yaml_).
    Run ``spack spec -I unifyfs`` before installing to see what Spack is going
    to do.

----------

-----------------------------------------------------------
Build Dependencies with Spack, Build UnifyFS with Autotools
-----------------------------------------------------------

One can install the UnifyFS dependencies with Spack and build UnifyFS
with autotools.
This is useful if one needs to modify the UnifyFS source code
between builds.
Take advantage of `Spack Environments`_ to streamline this process.

.. _spack-build-label:

Build the Dependencies
**********************

Once Spack is installed on your system (see :ref:`above <build-label>`),
the UnifyFS dependencies can then be installed.

.. code-block:: Bash

    $ spack install gotcha
    $ spack install mochi-margo@0.9.6 ^libfabric fabrics=rxm,sockets,tcp
    $ spack install spath~mpi

.. tip::

    Run ``spack install --only=dependencies unifyfs`` to install all UnifyFS
    dependencies without installing UnifyFS itself.

    Keep in mind this will also install all the build dependencies and
    dependencies of dependencies if you haven't already installed them through
    Spack or told Spack where they are locally installed on your system via a
    packages.yaml_.

Build UnifyFS
*************

Download the latest UnifyFS release from the Releases_ page or clone the develop
branch ('dev') from the UnifyFS repository
`https://github.com/LLNL/UnifyFS <https://github.com/LLNL/UnifyFS>`_.

Load the dependencies into your environment and then
configure and build UnifyFS from its source code directory.

.. code-block:: Bash

    $ spack load gotcha
    $ spack load argobots
    $ spack load mercury
    $ spack load mochi-margo
    $ spack load spath
    $
    $ gotcha_install=$(spack location -i gotcha)
    $ spath_install=$(spack location -i spath)
    $
    $ ./autogen.sh
    $ ./configure --prefix=/path/to/install --with-gotcha=${gotcha_install} --with-spath=${spath_install}
    $ make
    $ make install

Alternatively, UnifyFS can be configured using ``CPPFLAGS`` and ``LDFLAGS``:

.. code-block:: Bash

    $ ./configure --prefix=/path/to/install CPPFLAGS="-I${gotcha_install}/include -I{spath_install}/include" LDFLAGS="-L${gotcha_install}/lib64 -L${spath_install}/lib64

To see all available build configuration options, run ``./configure --help``
after ``./autogen.sh`` has been run.

----------

------------------------------------------------------------------
Build Dependencies with Bootstrap and Build UnifyFS with Autotools
------------------------------------------------------------------

Download the latest UnifyFS release from the Releases_ page or clone the develop
branch ('dev') from the UnifyFS repository
`https://github.com/LLNL/UnifyFS <https://github.com/LLNL/UnifyFS>`_.

Build the Dependencies
**********************

UnifyFS requires MPI, GOTCHA, Margo and OpenSSL.
References to these dependencies can be found on the :doc:`dependencies` page.

A bootstrap.sh_ script in the UnifyFS source distribution downloads and installs
all dependencies.  Simply run the script in the top level directory of the
source code.

.. code-block:: Bash

    $ ./bootstrap.sh

.. note::

    UnifyFS requires automake version 1.15 or newer in order to build.

    Before building the UnifyFS dependencies, the bootstrap.sh script will check
    the system's current version of automake and attempt to build the autotools
    suite if an older version is detected.

Build UnifyFS
*************

After bootstrap.sh installs the dependencies,
it prints the commands one needs to execute to build UnifyFS.
As an example, the commands may look like:

.. code-block:: Bash

    $ export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig:$INSTALL_DIR/lib64/pkgconfig:$PKG_CONFIG_PATH
    $ export LD_LIBRARY_PATH=$INSTALL_DIR/lib:$INSTALL_DIR/lib64:$LD_LIBRARY_PATH
    $ ./autogen.sh
    $ ./configure --prefix=/path/to/install CPPFLAGS=-I/path/to/install/include LDFLAGS="-L/path/to/install/lib -L/path/to/install/lib64"
    $ make
    $ make install

Alternatively, UnifyFS can be configured using ``--with`` options:

.. code-block:: Bash

    $ ./configure --prefix=/path/to/install --with-gotcha=$INSTALL_DIR --with-spath=$INSTALL_DIR

To see all available build configuration options, run ``./configure --help``
after ``./autogen.sh`` has been run.


.. note::

    On Cray systems, the detection of MPI compiler wrappers requires passing the
    following flags to the configure command: ``MPICC=cc MPIFC=ftn``

----------

-----------------
Configure Options
-----------------

When building UnifyFS with autotools, a number of options are available to
configure its functionality.

Fortran
*******

To use UnifyFS in Fortran applications, pass the ``--enable-fortran``
option to configure. Note that only GCC Fortran (i.e., gfortran) is known to
work with UnifyFS. There is an open ifort_issue_ with the Intel Fortran compiler
as well as an xlf_issue_ with the IBM Fortran compiler.

.. note::

    UnifyFS requires GOTCHA when Fortran support is enabled

GOTCHA
******

GOTCHA is the preferred method for I/O interception with UnifyFS, but it is not
available on all platforms. If GOTCHA is not available on your target system,
you can omit it during UnifyFS configuration by using the ``--without-gotcha``
configure option. Without GOTCHA, static linker wrapping is required for I/O
interception, see :doc:`link`.

.. warning::

    UnifyFS requires GOTCHA for dynamic I/O interception of MPI-IO functions. If
    UnifyFS is configured using ``--without-gotcha``, support will be lost for
    MPI-IO (and as a result, HDF5) applications.

HDF5
****

UnifyFS includes example programs that use HDF5. If HDF5 is not available on
your target system, it can be omitted during UnifyFS configuration by using
the ``--without-hdf5`` configure option.

PMI2/PMIx Key-Value Store
*************************

When available, UnifyFS uses the distributed key-value store capabilities
provided by either PMI2 or PMIx. To enable this support, pass either
the ``--enable-pmi`` or ``--enable-pmix`` option to configure. Without
PMI support, a distributed file system accessible to all servers is required.

SPATH
******

The spath library can be optionally used to normalize relative paths (e.g., ones
containing ".", "..", and extra or trailing "/") and enable the support of using
relative paths within an application. To enable, use the ``--with-spath``
configure option or provide the appropriate ``CPPFLAGS`` and ``LDFLAGS`` at
configure time.

.. _auto-mount-label:
Transparent Mounting for MPI Applications
*****************************************

MPI applications written in C or C++ may take advantage of the UnifyFS transparent
mounting capability. With transparent mounting, calls to ``unifyfs_mount()`` and
``unifyfs_unmount()`` are automatically performed during ``MPI_Init()`` and
``MPI_Finalize()``, respectively. Transparent mounting always uses ``/unifyfs`` as
the namespace mountpoint. To enable transparent mounting, use the
``--enable-mpi-mount`` configure option.

---------------------------

.. explicit external hyperlink targets

.. _bootstrap.sh: https://github.com/LLNL/UnifyFS/blob/dev/bootstrap.sh
.. _ifort_issue: https://github.com/LLNL/UnifyFS/issues/300
.. _Releases: https://github.com/LLNL/UnifyFS/releases
.. _Spack: https://github.com/spack/spack
.. _Spack Environments: https://spack.readthedocs.io/en/latest/environments.html
.. _Spack's shell support: https://spack.readthedocs.io/en/latest/getting_started.html#add-spack-to-the-shell
.. _packages.yaml: https://spack.readthedocs.io/en/latest/build_settings.html#external-packages
.. _xlf_issue: https://github.com/LLNL/UnifyFS/issues/304
