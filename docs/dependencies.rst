====================
UnifyFS Dependencies
====================

--------
Required
--------

- `GOTCHA <https://github.com/LLNL/GOTCHA/releases>`_ version 1.0.3

- `Margo <https://xgitlab.cels.anl.gov/sds/margo>`_ version 0.4.3 and its dependencies:

  - `Argobots <https://github.com/pmodels/argobots/releases/tag/v1.0>`_ version 1.0
  - `Mercury <https://github.com/mercury-hpc/mercury/releases/tag/v1.0.1>`_ version 1.0.1

    - `bmi <https://xgitlab.cels.anl.gov/sds/bmi.git>`_

- `OpenSSL <https://www.openssl.org/source/>`_

.. important::

    Margo uses pkg-config to ensure it compiles and links correctly with all of
    its dependencies' libraries. When building manually, you'll need to set the
    ``PKG_CONFIG_PATH`` environment variable and include in
    that variable the paths for the ``.pc`` files for Mercury, Argobots, and
    Margo separated by colons.

--------
Optional
--------

- `leveldb <https://github.com/google/leveldb/releases/tag/1.22>`_ version 1.22
  needed when building with ``--enable-mdhim`` configure option

- `spath <https://github.com/ecp-veloc/spath>`_ for normalizing relative paths
