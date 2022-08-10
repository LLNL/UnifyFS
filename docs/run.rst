================================
Run UnifyFS
================================

This section describes the mechanisms to start and stop the UnifyFS
server processes within a job allocation.

Overall, the steps to run an application with UnifyFS include:

    1. Allocate nodes using the system resource manager (i.e., start a job)

    2. Update any desired UnifyFS server configuration settings

    3. Start UnifyFS servers on each allocated node using ``unifyfs``

    4. Run one or more UnifyFS-enabled applications

    5. Terminate the UnifyFS servers using ``unifyfs``

-------------
Start UnifyFS
-------------

First, one must start the UnifyFS server process (``unifyfsd``) on the nodes in
the job allocation. UnifyFS provides the ``unifyfs`` command line utility to
simplify this action on systems with supported resource managers. The easiest
way to determine if you are using a supported system is to run
``unifyfs start`` within an interactive job allocation. If no compatible
resource management system is detected, the utility reports an error message
to that effect.

In ``start`` mode, the ``unifyfs`` utility automatically detects the allocated
nodes and launches a server on each node. For example, the following script
could be used to launch the ``unifyfsd`` servers with a customized
configuration. On systems with resource managers that propagate environment
settings to compute nodes, the environment variables override any
settings in ``/etc/unifyfs/unifyfs.conf``. See :doc:`configuration`
for further details on customizing the UnifyFS runtime configuration.

.. code-block:: Bash
    :linenos:

    #!/bin/bash

    # spillover data to node-local ssd storage
    export UNIFYFS_LOGIO_SPILL_DIR=/mnt/ssd/$USER/data

    # store server logs in job-specific scratch area
    export UNIFYFS_LOG_DIR=$JOBSCRATCH/logs

    unifyfs start --share-dir=/path/to/shared/file/system

.. _unifyfs_utility_label:

``unifyfs`` provides command-line options to choose the client mountpoint,
adjust the consistency model, and control stage-in and stage-out of files.
The full usage for ``unifyfs`` is as follows:

.. code-block:: Bash

    [prompt]$ unifyfs --help

    Usage: unifyfs <command> [options...]

    <command> should be one of the following:
      start       start the UnifyFS server daemons
      terminate   terminate the UnifyFS server daemons

    Common options:
      -d, --debug               enable debug output
      -h, --help                print usage

    Command options for "start":
      -C, --consistency=<model>  [OPTIONAL] consistency model (NONE | LAMINATED | POSIX)
      -e, --exe=<path>           [OPTIONAL] <path> where unifyfsd is installed
      -m, --mount=<path>         [OPTIONAL] mount UnifyFS at <path>
      -s, --script=<path>        [OPTIONAL] <path> to custom launch script
      -t, --timeout=<sec>        [OPTIONAL] wait <sec> until all servers become ready
      -S, --share-dir=<path>     [REQUIRED] shared file system <path> for use by servers
      -c, --cleanup              [OPTIONAL] clean up the UnifyFS storage upon server exit
      -i, --stage-in=<manifest>  [OPTIONAL] stage in file(s) listed in <manifest> file
      -P, --stage-parallel       [OPTIONAL] use parallel stage-in
      -T, --stage-timeout=<sec>  [OPTIONAL] timeout for stage-in operation

    Command options for "terminate":
      -o, --stage-out=<manifest> [OPTIONAL] stage out file(s) listed in <manifest> on termination
      -P, --stage-parallel       [OPTIONAL] use parallel stage-out
      -T, --stage-timeout=<sec>  [OPTIONAL] timeout for stage-out operation
      -s, --script=<path>        [OPTIONAL] <path> to custom termination script
      -S, --share-dir=<path>     [REQUIRED for --stage-out] shared file system <path> for use by servers


After UnifyFS servers have been successfully started, you may run your
UnifyFS-enabled applications as you normally would (e.g., using mpirun).
Only applications that explicitly call ``unifyfs_mount()`` and access files
under the specified mountpoint prefix will utilize UnifyFS for their I/O. All
other applications will operate unchanged.

------------
Stop UnifyFS
------------

After all UnifyFS-enabled applications have completed running, use
``unifyfs terminate`` to terminate the servers. Pass the ``--cleanup`` option to
``unifyfs start`` to have the servers remove temporary data locally stored on
each node after termination.

--------------------------------
Resource Manager Job Integration
--------------------------------

UnifyFS includes optional support for integrating directly with compatible
resource managers to automatically start and stop servers at the beginning
and end of a job when requested by users. Resource manager integration
requires administrator privileges to deploy.

Currently, only IBM's Platform LSF with Cluster System Manager (LSF-CSM)
is supported. LSF-CSM is the resource manager on the CORAL2 IBM systems
at ORNL and LLNL. The required job prologue and epilogue scripts, along
with a README documenting the installation instructions, is available
within the source repository at ``util/scripts/lsfcsm``.

Support for the SLURM resource manager is under development.

-----------------------------------------
Transferring Data In and Out of UnifyFS
-----------------------------------------

Data can be transferred in/out of UnifyFS during server startup and termination,
or at any point during a job using two stand-alone applications.

Transfer at Server Start/Terminate
**********************************

The transfer subsystem within UnifyFS can be invoked by providing the
``-i|--stage-in`` option to ``unifyfs start`` to transfer files into UnifyFS:

.. code-block:: Bash

    $ unifyfs start --stage-in=/path/to/input/manifest/file --share-dir=/path/to/shared/file/system

and/or by providing the ``-o|--stage-out`` option to ``unifyfs terminate``
to transfer files out of UnifyFS:

.. code-block:: Bash

    $ unifyfs terminate --stage-out=/path/to/output/manifest/file --share-dir=/path/to/shared/file/system

The argument to both staging options is the path to a manifest file that contains
the source and destination file pairs. Both stage-in and stage-out also require
passing the ``-S|--share-dir=<path>`` option.

.. _manifest_file_label:

Manifest File
^^^^^^^^^^^^^

UnifyFS's file staging functionality requires a manifest file in order to move data.

The manifest file contains one or more file copy requests. Each line in the
manifest corresponds to one transfer request, and it contains both the source
and destination file paths. Directory copies are currently not supported.

Each line is formatted as:
``<source filename> <whitespace> <destination filename>``.

If either of the filenames contain whitespace or special characters, then both
filenames should be surrounded by double-quote characters (") (ASCII character
34 decimal).
The double-quote and linefeed end-of-line characters are not supported in any
filenames used in a manifest file. Any other characters are allowed,
including control characters.  If a filename contains any characters that might
be misinterpreted, we suggest enclosing the filename in double-quotes.
Comment lines are also allowed, and are indicated by beginning a line with the
``#`` character.

Here is an example of a valid stage-in manifest file:

.. code-block:: Bash

    $ [prompt] cat example_stage_in.manifest

    /scratch/users/me/input_data/input_1.dat /unifyfs/input/input_1.dat
    # example comment line
    /home/users/me/configuration/run_12345.conf /unifyfs/config/run_12345.conf
    "/home/users/me/file with space.dat" "/unifyfs/file with space.dat"

Transfer During Job
*******************

Data can also be transferred in/out of UnifyFS using the ``unifyfs-stage``
helper program. This is the same program used internally by ``unifyfs`` to
provide file staging during server startup and termination.

The helper program can be invoked at any time while the UnifyFS servers
are up and responding to requests. This allows for bringing in new input
and/or transferring results out to be verified before the job terminates.

UnifyFS Stage Executable
^^^^^^^^^^^^^^^^^^^^^^^^

The ``unifyfs-stage`` program is installed in the same directory as the
``unifyfs`` utility (i.e., ``$UNIFYFS_INSTALL/bin``).

A manifest file (see :ref:`above <manifest_file_label>`) needs to be provided
as an argument to use this approach.

.. code-block:: Bash

    [prompt]$ unifyfs-stage --help

    Usage: unifyfs-stage [OPTION]... <manifest file>

    Transfer files between unifyfs volume and external file system.
    The <manifest file> should contain list of files to be transferred,
    and each line should be formatted as

      /source/file/path /destination/file/path

    OR in the case of filenames with spaces or special characters:

      "/source/file/path" "/destination/file/path"

    One file per line; Specifying directories is not currently supported.

    Available options:
      -c, --checksum           Verify md5 checksum for each transfer
                               (default: off)
      -h, --help               Print usage information
      -m, --mountpoint=<mnt>   Use <mnt> as UnifyFS mountpoint
                               (default: /unifyfs)
      -p, --parallel           Transfer all files concurrently
                               (default: off, use sequential transfers)
      -s, --skewed             Use skewed data distribution for stage-in
                               (default: off, use balanced distribution)
      -S, --status-file=<path> Create stage status file at <path>
      -v, --verbose            Print verbose information
                               (default: off)

    By default, each file in the manifest will be transferred in sequence (i.e.,
    only a single file will be in transfer at any given time). If the
    '-p, --parallel' option is specified, files in the manifest will be
    transferred concurrently. The number of concurrent transfers is limited by
    the number of parallel ranks used to execute unifyfs-stage.

Examples:

.. code-block:: Bash
    :caption: Sequential Transfer using a Single Client

    $ srun -N 1 -n 1 unifyfs-stage $MY_MANIFEST_FILE

.. code-block:: Bash
    :caption: Parallel Transfer using 8 Clients (up to 8 concurrent file transfers)

    $ srun -N 4 -n 8 unifyfs-stage --parallel $MY_MANIFEST_FILE

