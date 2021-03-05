====================
UnifyFS Dependencies
====================

--------
Required
--------

- `Automake <https://ftp.gnu.org/gnu/automake/>`_ version 1.15 or later

- `GOTCHA <https://github.com/LLNL/GOTCHA/releases>`_ version 1.0.3

- `Margo <https://github.com/mochi-hpc/mochi-margo/releases>`_ version 0.9.1 and its dependencies:

  - `Argobots <https://github.com/pmodels/argobots/releases/tag/v1.0.1>`_ version 1.0.1
  - `Mercury <https://github.com/mercury-hpc/mercury/releases/tag/v2.0.0>`_ version 2.0.0

    - `libfabric <https://github.com/ofiwg/libfabric>`_ and/or `bmi <https://github.com/radix-io/bmi/>`_

  - `JSON-C <https://github.com/json-c/json-c>`_

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

- `spath <https://github.com/ecp-veloc/spath>`_ for normalizing relative paths
