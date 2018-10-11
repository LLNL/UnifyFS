=====================
UnifyCR Configuration
=====================

Here, we explain how users can customize the runtime behavior of UnifyCR. In
particular, UnifyCR provides the following ways to configure:

- System-wide configuration file: ``/etc/unifycr/unifycr.conf``
- Environment variables
- Command line options to ``unifycrd``

All configuration settings have corresponding environment variables, but only
certain settings have command line options. When defined via multiple methods,
the command line options have the highest priority, followed by environment
variables, and finally config file options from ``unifycr.conf``.

The unified method for providing configuration control is adapted from
CONFIGURATOR_. Configuration settings are grouped within named sections, and
each setting consists of a key-value pair with one of the following types:
    - ``BOOL``: ``0|1``, ``y|n``, ``Y|N``, ``yes|no``, ``true|false``, ``on|off``
    - ``FLOAT``: scalars convertible to C double, or compatible expression
    - ``INT``: scalars convertible to C long, or compatible expression
    - ``STRING``: quoted character string

.. _CONFIGURATOR: https://github.com/MichaelBrim/tedium/tree/master/configurator

--------------
 unifycr.conf
--------------

``unifycr.conf`` specifies the system-wide configuration options. The file is
written in INI_ language format, as supported by the inih_ parser.

.. _INI: http://en.wikipedia.org/wiki/INI_file

.. _inih: https://github.com/benhoyt/inih

The config file has several sections, each with a few key-value settings.
In this description, we use ``section.key`` as shorthand for the name of
a given section and key.


.. table:: ``[unifycr]`` section - main configuration settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   configfile     STRING  path to custom configuration file
   consistency    STRING  consistency model [ LAMINATED | POSIX | NONE ]
   daemonize      BOOL    enable server daemonization (default: off)
   debug          BOOL    enable debug output (default: off)
   mountpoint     STRING  mountpoint path prefix (default: /unifycr)
   =============  ======  =====================================================

.. table:: ``[client]`` section - client settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   max_files      INT     maximum number of open files per client process
   =============  ======  =====================================================

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
   server_ratio   INT     # of UnifyCR servers per metadata server (default: 1)
   =============  ======  =====================================================

.. table:: ``[runstate]`` section - server runstate settings
   :widths: auto

   =============  ======  =====================================================
   Key            Type    Description
   =============  ======  =====================================================
   dir            STRING  path to directory to contain server runstate file
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

All environment variables take the form ``UNIFYCR_SECTION_KEY``, except for
the ``[unifycr]`` section, which uses ``UNIFYCR_KEY``. For example,
the setting ``log.verbosity`` has a corresponding environment variable
named ``UNIFYCR_LOG_VERBOSITY``, while ``unifycr.mountpoint`` corresponds to
``UNIFYCR_MOUNTPOINT``.


----------------------
 Command Line Options
----------------------

For server command line options, we use ``getopt_long()`` format. Thus, all
command line options have long and short forms. The long form uses
``--section-key=value``, while the short form ``-<optchar> value``, where
the short option character is given in the below table.

.. table:: ``unifycrd`` command line options
   :widths: auto

   ======================  ========
   LongOpt                 ShortOpt
   ======================  ========
   --unifycr-configfile      -C
   --unifycr-consistency     -c
   --unifycr-daemonize       -D
   --unifycr-debug           -d
   --unifycr-mountpoint      -m
   --log-dir                 -L
   --log-file                -l
   --log-verbosity           -v
   --runstate-dir            -R
   ======================  ========

