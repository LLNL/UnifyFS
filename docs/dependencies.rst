====================
UnifyFS Dependencies
====================

--------
Required
--------

- `Automake <https://ftp.gnu.org/gnu/automake/>`_ version 1.15 (or later)

- `GOTCHA <https://github.com/LLNL/GOTCHA/releases>`_ version 1.0.3 (or later)

- `Margo <https://github.com/mochi-hpc/mochi-margo/releases>`_ version 0.9.6 and its dependencies:

  - `Argobots <https://github.com/pmodels/argobots/releases>`_ version 1.1 (or later)
  - `Mercury <https://github.com/mercury-hpc/mercury/releases>`_ version 2.0.1 (or later)

    - `libfabric <https://github.com/ofiwg/libfabric>`_ (avoid versions 1.13 and 1.13.1) or `bmi <https://github.com/radix-io/bmi/>`_

  - `JSON-C <https://github.com/json-c/json-c>`_

- `OpenSSL <https://www.openssl.org/source/>`_

.. important::

    Margo uses pkg-config to ensure it compiles and links correctly with all of
    its dependencies' libraries. When building manually, you'll need to set the
    ``PKG_CONFIG_PATH`` environment variable to include the paths of the
    directories containing the ``.pc`` files for Mercury, Argobots, and Margo.

--------
Optional
--------

- `spath <https://github.com/ecp-veloc/spath>`_ for normalizing relative paths

----------

===================
UnifyFS Error Codes
===================

Wherever sensible, UnifyFS uses the error codes defined in POSIX `errno.h
<https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html>`_.

UnifyFS specific error codes are defined as follows:

.. table::
    :widths: auto

    =====  =========  ======================================
    Value  Error      Description
    =====  =========  ======================================
    1001   BADCONFIG  Configuration has invalid setting
    1002   GOTCHA     Gotcha operation error
    1003   KEYVAL     Key-value store operation error
    1004   MARGO      Mercury/Argobots operation error
    1005   MDHIM      MDHIM operation error
    1006   META       Metadata store operation error
    1007   NYI        Not yet implemented
    1008   PMI        PMI2/PMIx error
    1009   SHMEM      Shared memory region init/access error
    1010   THREAD     POSIX thread operation failed
    1011   TIMEOUT    Timed out
    =====  =========  ======================================
