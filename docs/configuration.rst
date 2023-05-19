UnifyFS Configuration
=====================

Here, we explain how users can customize the runtime behavior of UnifyFS. In
particular, UnifyFS provides the following ways to configure:

- Configuration file: ``$INSTALL_PREFIX/etc/unifyfs/unifyfs.conf``
- Environment variables
- Command line options to ``unifyfsd``

All configuration settings have corresponding environment variables, but only
certain settings have command line options. When defined via multiple methods,
the command line options have the highest priority, followed by environment
variables, and finally config file options from ``unifyfs.conf``.

The system-wide configuration file is used by default when available.
However, users can specify a custom location for the configuration file using
the ``-f`` command-line option to ``unifyfsd`` (see below).
There is a sample ``unifyfs.conf`` file in the installation directory
under ``etc/unifyfs/``. This file is also available in the ``extras`` directory
in the source repository.

The unified method for providing configuration control is adapted from
CONFIGURATOR_. Configuration settings are grouped within named sections, and
each setting consists of a key-value pair with one of the following types:

- ``BOOL``: ``0|1``, ``y|n``, ``Y|N``, ``yes|no``, ``true|false``, ``on|off``
- ``FLOAT``: scalars convertible to C double, or compatible expression
- ``INT``: scalars convertible to C long, or compatible expression
- ``STRING``: quoted character string

.. _CONFIGURATOR: https://github.com/MichaelBrim/tedium/tree/master/configurator


System Configuration File (``unifyfs.conf``)
--------------------------------------------

``unifyfs.conf`` specifies the system-wide configuration options. The file is
written in INI_ language format, as supported by the inih_ parser.

.. _INI: http://en.wikipedia.org/wiki/INI_file

.. _inih: https://github.com/benhoyt/inih

The config file has several sections, each with a few key-value settings.
In this description, we use ``section.key`` as shorthand for the name of
a given section and key.

-----------

.. table:: ``[unifyfs]`` section - main configuration settings
   :widths: auto

   =============  ======  ===============================================
   Key            Type    Description
   =============  ======  ===============================================
   cleanup        BOOL    cleanup storage on server exit (default: off)
   configfile     STRING  path to custom configuration file
   daemonize      BOOL    enable server daemonization (default: off)
   mountpoint     STRING  mountpoint path prefix (default: /unifyfs)
   =============  ======  ===============================================

-----------

.. table:: ``[client]`` section - client settings
   :widths: auto

   ==================  ======  =================================================================
   Key                 Type    Description
   ==================  ======  =================================================================
   cwd                 STRING  effective starting current working directory
   excl_private        BOOL    create node-local private files when given O_EXCL (default: on)
   fsync_persist       BOOL    persist data to storage on fsync() (default: on)
   local_extents       BOOL    service reads from local data (default: off)
   max_files           INT     maximum number of open files per client process (default: 128)
   node_local_extents  BOOL    service reads from node local data for laminated files (default: off)
   super_magic         BOOL    whether to return UNIFYFS (on) or TMPFS (off) statfs magic (default: on)
   unlink_usecs        INT     number of microseconds to sleep after initiating unlink rpc (default: 0)
   write_index_size    INT     maximum size (B) of memory buffer for storing write log metadata
   write_sync          BOOL    sync data to server after every write (default: off)
   ==================  ======  =================================================================

The ``client.cwd`` setting is used to emulate the behavior one
expects when changing into a working directory before starting a job
and then using relative file names within the application.
If set, the application changes its working directory to
the value specified in ``client.cwd`` when ``unifyfs_mount()`` is called.
The value specified in ``client.cwd`` must be within the directory space
of the UnifyFS mount point.

Enabling the ``client.local_extents`` optimization may significantly improve
read performance for extents written by the same process.  However, it should
not be used by applications in which different processes write to the same byte
offset within a file, nor should it be used with applications that truncate
files.

-----------

.. table:: ``[log]`` section - logging settings
   :widths: auto

   ==========  ======  ================================================================
   Key         Type    Description
   ==========  ======  ================================================================
   dir         STRING  path to directory to contain server log file
   file        STRING  log file base name (rank will be appended)
   on_error    BOOL    increase log verbosity upon encountering an error (default: off)
   verbosity   INT     logging verbosity level [0-5] (default: 0)
   ==========  ======  ================================================================

-----------

.. table:: ``[logio]`` section - log-based write data storage settings
   :widths: auto

   ===========  ======  ============================================================
   Key          Type    Description
   ===========  ======  ============================================================
   chunk_size   INT     data chunk size (B) (default: 4 MiB)
   shmem_size   INT     maximum size (B) of data in shared memory (default: 256 MiB)
   spill_size   INT     maximum size (B) of data in spillover file (default: 4 GiB)
   spill_dir    STRING  path to spillover data directory
   ===========  ======  ============================================================


-----------

.. table:: ``[margo]`` section - margo server NA settings
   :widths: auto

   ==============  ====  =================================================================================
   Key             Type  Description
   ==============  ====  =================================================================================
   tcp             BOOL  Use TCP for server-to-server rpcs (default: on, turn off to enable libfabric RMA)
   client_timeout  INT   timeout in milliseconds for rpcs between client and server (default: 5000)
   server_timeout  INT   timeout in milliseconds for rpcs between servers (default: 15000)
   ==============  ====  =================================================================================


-----------

.. table:: ``[runstate]`` section - server runstate settings
   :widths: auto

   ========  ======  ===============================================
   Key       Type    Description
   ========  ======  ===============================================
   dir       STRING  path to directory to contain server-local state
   ========  ======  ===============================================


-----------

.. table:: ``[server]`` section - server settings
   :widths: auto

   =============  ======  =============================================================================
   Key            Type    Description
   =============  ======  =============================================================================
   hostfile       STRING  path to server hostfile
   init_timeout   INT     timeout in seconds to wait for servers to be ready for clients (default: 120)
   local_extents  BOOL    use server extents to service local reads without consulting file owner
   =============  ======  =============================================================================


-----------

.. table:: ``[sharedfs]`` section - server shared files settings
   :widths: auto

   ========  ======  =================================================
   Key       Type    Description
   ========  ======  =================================================
   dir       STRING  path to directory to contain server shared files
   ========  ======  =================================================

-----------

Environment Variables
---------------------

All environment variables take the form ``UNIFYFS_SECTION_KEY``, except for
the ``[unifyfs]`` section, which uses ``UNIFYFS_KEY``. For example,
the setting ``log.verbosity`` has a corresponding environment variable
named ``UNIFYFS_LOG_VERBOSITY``, while ``unifyfs.mountpoint`` corresponds to
``UNIFYFS_MOUNTPOINT``.


Command Line Options
---------------------

For server command line options, we use ``getopt_long()`` format. Thus, all
command line options have long and short forms. The long form uses
``--section-key=value``, while the short form ``-<optchar> value``, where
the short option character is given in the below table.

Note that for configuration options of type BOOL, the value is optional.
When not provided, the ``true`` value is assumed. If the short form option
is used, the value must immediately follow the option character (e.g., ``-Cyes``).

.. table:: ``unifyfsd`` command line options
   :widths: auto

   =========================  ========
   LongOpt                    ShortOpt
   =========================  ========
   ``--unifyfs-cleanup``       ``-C``
   ``--unifyfs-configfile``    ``-f``
   ``--unifyfs-daemonize``     ``-D``
   ``--log-verbosity``         ``-v``
   ``--log-file``              ``-l``
   ``--log-dir``               ``-L``
   ``--runstate-dir``          ``-R``
   ``--server-hostfile``       ``-H``
   ``--sharedfs-dir``          ``-S``
   ``--server-init_timeout``   ``-t``
   =========================  ========
