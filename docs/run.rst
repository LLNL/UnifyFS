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

--------------------
  Start UnifyFS
--------------------

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
    :linenos:

    [prompt]$ unifyfs --help

    Usage: unifyfs <command> [options...]

    <command> should be one of the following:
      start       start the UnifyFS server daemons
      terminate   terminate the UnifyFS server daemons

    Common options:
      -d, --debug               enable debug output
      -h, --help                print usage

    Command options for "start":
      -C, --consistency=<model> [OPTIONAL] consistency model (NONE | LAMINATED | POSIX)
      -e, --exe=<path>          [OPTIONAL] <path> where unifyfsd is installed
      -m, --mount=<path>        [OPTIONAL] mount UnifyFS at <path>
      -s, --script=<path>       [OPTIONAL] <path> to custom launch script
      -t, --timeout=<sec>       [OPTIONAL] wait <sec> until all servers become ready
      -S, --share-dir=<path>    [REQUIRED] shared file system <path> for use by servers
      -c, --cleanup             [OPTIONAL] clean up the UnifyFS storage upon server exit
      -i, --stage-in=<path>     [OPTIONAL] stage in manifest file(s) at <path>

    Command options for "terminate":
      -s, --script=<path>       [OPTIONAL] <path> to custom termination script
      -o, --stage-out=<path>    [OPTIONAL] stage out manifest file(s) at <path>


After UnifyFS servers have been successfully started, you may run your
UnifyFS-enabled applications as you normally would (e.g., using mpirun).
Only applications that explicitly call ``unifyfs_mount()`` and access files
under the specified mountpoint prefix will utilize UnifyFS for their I/O. All
other applications will operate unchanged.

--------------------
  Stop UnifyFS
--------------------

After all UnifyFS-enabled applications have completed running, you should
use ``unifyfs terminate`` to terminate the servers. Typically, one would pass
the ``--cleanup`` option to ``unifyfs start`` to have the servers remove
temporary data locally stored on each node after termination.

------------------------------------
  Resource Manager Job Integration
------------------------------------

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

-----------------------------------------------
  Stage-in and Stage-out Manifest File Format
-----------------------------------------------

The transfer subsystem within UnifyFS can be invoked as above
within the start process (to transfer files into the UnifyFS
volume) or during the stop process (to transfer files out of
the UnifyFS volume).  To do so, the user supplies a manifest
file specifying the requested transfers.  Here is the formatted
for that manifest file.

The manifest file contains one or more file copy requests.
Each line in the manifest corresponds to one file copy request,
and it contains both the source and destination file paths. Currently,
directory copies are not supported.

Each line is formatted as: ``<source filename> <whitespace> <destination filename>``.
If either of the filenames
contain whitespace or special characters, then both filenames should
be surrounded by double-quote characters (") (ASCII character 34 decimal).
The double-quote character and the linefeed end-of-line character are forbidden
in any filenames used in a unifyfs manifest file, but any other
characters are allowed, including control characters.
If a filename contains any characters that might be misinterpreted, then
enclosing the filename in double-quotes is always
a safe thing to do.

Here is an example of a valid stage-in manifest file:

``/scratch/users/me/input_data/input_1.dat /unifyfs/input/input_1.dat``
``/home/users/me/configuration/run_12345.conf /unifyfs/config/run_12345.conf``
``"/home/users/me/file with space.dat" "/unifyfs/file with space.dat"``

-----------------------------------------------
  Stand-alone file stage application
-----------------------------------------------
The manifest subsystem can also be used during the job by
invoking stand-alone transfer executable.  The command to use is
"unify-static".  It's used similarly to the unix
"cp" command, with source and destination, except that unify-static
is aware that it is copying files between the external parallel
file system and the internal UnifyFS volume.

The stand-alone can be invoked at any time the UnifyFS servers
are up and responding to requests.  Thus the user can move files
in or out during a job where UnifyFS is running at any time
during the job, if UnifyFS has been "start"ed as above.  This
could be done during the job to bring in new input, or perhaps
transfer results files out so they can be verified before they
job terminates.