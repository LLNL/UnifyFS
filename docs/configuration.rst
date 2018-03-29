=====================
UnifyCR Configuration
=====================

Here, we explain how users can customize the runtime behavior of UnifyCR. In
particular, UnifyCR provides the following ways to configure:

- System-wide configuration file: ``/etc/unifycr/unifycr.conf``
- Environment variables
- Command line arguments

For the duplicated entries, the command line arguments have the highest
priority, overriding any configuration options from ``unifycr.conf`` and
environment variables. Similarily, environment variables have higher priority
than options in the ``unifycr.conf`` file. ``unifycr`` command line utility
creates the final configuration file (``unifycr-runstate.conf``) based on all
forementioned configuration options.

---------------------------
unifycr.conf
---------------------------

unifycr.conf specifies the system-wide configuration options. The file is
written in TOML_ language format. The unifycr.conf file has four different
sections, i.e., global, filesystem, server, and client sections.

.. _TOML: https://github.com/toml-lang/toml

- [global] section
    - runstatedir: a directory where the final configuration file
      (``unifycr-runstate.conf``) has to be created
    - unifycrd_path: path to unifycrd server daemon process

- [filesystem] section
    - mountpoint: unifycr file system mountpoint
    - consistency: consistency model to be used, one of:
        - ``none``:
        - ``laminated``:
        - ``posix``:

- [server] section
    - meta_server_ratio: the ratio between the number of unifycrd daemon and
      the number of metadata key-value storage instance
    - meta_db_name: name of the database file to store unifycr file system
      metadata
    - meta_db_path: the pathname of the metadata database file will be created
    - server_debug_log_path: the debug log file of the unifycrd daemon

- [client] section
    - chunk_mem: allocation chunk size for unifycr file system memory storage

---------------------------
Environment Variables
---------------------------

The following is the list of the environment variables that UnifyCR supports.

- ``UNIFYCR_META_SERVER_RATIO``: the ratio between the number of unifycrd
  daemon and the number of metadata key-value storage instance
- ``UNIFYCR_META_DB_NAME``: the name of the database file to store unifycr file
  system metadata
- ``UNIFYCR_META_DB_PATH``: the pathname of the metadata database file will be
  created
- ``UNIFYCR_SERVER_DEBUG_LOG``: the debug log file of the unifycrd daemon
- ``UNIFYCR_CHUNK_MEM``: allocation chunk size for unifycr file system memory
  storage

---------------------------
Command Line arguments
---------------------------

Lastly, unifycr command line utility accepts arguments to configure the runtime options.

.. code-block:: Bash
    :linenos:

        Usage: unifycr <command> [options...]

        <command> should be one of the following:
          start       start the unifycr server daemon
          terminate   terminate the unifycr server daemon

        Available options for "start":
          -C, --consistency=<model> consistency model (none, laminated, or posix)
          -m, --mount=<path>        mount unifycr at <path>
          -i, --transfer-in=<path>  stage in file(s) at <path>
          -o, --transfer-out=<path> transfer file(s) to <path> on termination

        Available options for "terminate":
          -c, --cleanup             clean up the unifycr storage on termination

