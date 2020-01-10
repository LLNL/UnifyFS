=====================
UnifyFS Configuration
=====================

Here, we explain how users can customize the runtime behavior of UnifyFS. In
particular, UnifyFS provides the following ways to configure:

- System-wide configuration file: ``/etc/unifyfs/unifyfs.conf``
- Environment variables
- Command line options to ``unifyfsd``

All configuration settings have corresponding environment variables, but only
certain settings have command line options. When defined via multiple methods,
the command line options have the highest priority, followed by environment
variables, and finally config file options from ``unifyfs.conf``.

The config file is installed in /etc by default. However, one can
specify a custom location for the
unifyfs.conf file with the -f command-line option to unifyfsd (see below).
There is a sample unifyfs.conf file in the installation directory
under etc/unifyfs/.  This file is also available in the "extras" directory
in the source repository.

The unified method for providing configuration control is adapted from
CONFIGURATOR_. Configuration settings are grouped within named sections, and
each setting consists of a key-value pair with one of the following types:
    - ``BOOL``: ``0|1``, ``y|n``, ``Y|N``, ``yes|no``, ``true|false``, ``on|off``
    - ``FLOAT``: scalars convertible to C double, or compatible expression
    - ``INT``: scalars convertible to C long, or compatible expression
    - ``STRING``: quoted character string

.. _CONFIGURATOR: https://github.com/MichaelBrim/tedium/tree/master/configurator

--------------
 unifyfs.conf
--------------

``unifyfs.conf`` specifies the system-wide configuration options. The file is
written in INI_ language format, as supported by the inih_ parser.

.. _INI: http://en.wikipedia.org/wiki/INI_file

.. _inih: https://github.com/benhoyt/inih

The config file has several sections, each with a few key-value settings.
In this description, we use ``section.key`` as shorthand for the name of
a given section and key.


.. table:: ``[unifyfs]`` section - main configuration settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   cleanup        BOOL    cleanup storage on server exit (default: off)
   configfile     STRING  path to custom configuration file
   consistency    STRING  consistency model [ LAMINATED | POSIX | NONE ]
   daemonize      BOOL    enable server daemonization (default: off)
   mountpoint     STRING  mountpoint path prefix (default: /unifyfs)
   =============  ======  =====================================================

.. table:: ``[client]`` section - client settings
   :widths: auto

   ==============  ======  =================================================================
   Key             Type    Description
   ==============  ======  =================================================================
   max_files       INT     maximum number of open files per client process
   flatten_writes  BOOL    enable flattening writes (optimization for overwrite-heavy codes)
   ==============  ======  =================================================================

.. table:: ``[log]`` section - logging settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   dir            STRING  path to directory to contain server log file
   file           STRING  server log file base name (rank will be appended)
   verbosity      INT     server logging verbosity level [0-5] (default: 0)
   =============  ======  =====================================================

.. table:: ``[meta]`` section - metadata settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   db_name        STRING  metadata database file name
   db_path        STRING  path to directory to contain metadata database
   range_size     INT     metadata range size (B) (default: 1 MiB)
   server_ratio   INT     # of UnifyFS servers per metadata server (default: 1)
   =============  ======  =====================================================

.. table:: ``[runstate]`` section - server runstate settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   dir            STRING  path to directory to contain server runstate file
   =============  ======  =====================================================

.. table:: ``[server]`` section - server settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   hostfile       STRING  path to server hostfile
   =============  ======  =====================================================

.. table:: ``[sharedfs]`` section - server shared files settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   dir            STRING  path to directory to contain server shared files
   =============  ======  =====================================================

.. table:: ``[shmem]`` section - shared memory segment usage settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   chunk_bits     INT     data chunk size (bits), size = 2^bits (default: 24)
   chunk_mem      INT     segment size (B) for data chunks (default: 256 MiB)
   recv_size      INT     segment size (B) for receiving data from local server
   req_size       INT     segment size (B) for sending requests to local server
   single         BOOL    use one memory region for all clients (default: off)
   =============  ======  =====================================================

.. table:: ``[spillover]`` section - local data storage spillover settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   enabled        BOOL    use local storage for data spillover (default: on)
   data_dir       STRING  path to spillover data directory
   meta_dir       STRING  path to spillover metadata directory
   size           INT     maximum size (B) of spillover data (default: 1 GiB)
   =============  ======  =====================================================


-----------------------
 Environment Variables
-----------------------

All environment variables take the form ``UNIFYFS_SECTION_KEY``, except for
the ``[unifyfs]`` section, which uses ``UNIFYFS_KEY``. For example,
the setting ``log.verbosity`` has a corresponding environment variable
named ``UNIFYFS_LOG_VERBOSITY``, while ``unifyfs.mountpoint`` corresponds to
``UNIFYFS_MOUNTPOINT``.


----------------------
 Command Line Options
----------------------

For server command line options, we use ``getopt_long()`` format. Thus, all
command line options have long and short forms. The long form uses
``--section-key=value``, while the short form ``-<optchar> value``, where
the short option character is given in the below table.

.. table:: ``unifyfsd`` command line options
   :widths: auto

   ======================  ========
   LongOpt                 ShortOpt
   ======================  ========
   --unifyfs-cleanup         -C
   --unifyfs-configfile      -f
   --unifyfs-consistency     -c
   --unifyfs-daemonize       -D
   --unifyfs-mountpoint      -m
   --log-dir                 -L
   --log-file                -l
   --log-verbosity           -v
   --runstate-dir            -R
   --server-hostfile         -H
   --sharedfs-dir            -S
   ======================  ========

