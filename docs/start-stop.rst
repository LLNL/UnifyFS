================================
  Starting & Stopping in a Job
================================

In this section, we describe the mechanisms for starting and stopping UnifyFS in
a user's job allocation.

Overall, the steps taken to run an application with UnifyFS include:

    1. Allocate nodes using the system resource manager (i.e., start a job)

    2. Update any desired UnifyFS server configuration settings

    3. Start UnifyFS servers on each allocated node using ``unifyfs``

    4. Run one or more UnifyFS-enabled applications

    5. Terminate the UnifyFS servers using ``unifyfs``

--------------------
  Starting UnifyFS
--------------------

First, we need to start the UnifyFS server daemon (``unifyfsd``) on the nodes in
the job allocation. UnifyFS provides the ``unifyfs`` command line utility to
simplify this action on systems with supported resource managers. The easiest
way to determine if you are using a supported system is to run
``unifyfs start`` within an interactive job allocation. If no compatible
resource management system is detected, the utility will report an error message
to that effect.

In ``start`` mode, the ``unifyfs`` utility automatically detects the allocated
nodes and launches a server on each node. For example, the following script
could be used to launch the ``unifyfsd`` servers with a customized
configuration. On systems with resource managers that propagate environment
settings to compute nodes, the environment variables will override any
settings in ``/etc/unifyfs/unifyfs.conf``. See :doc:`configuration`
for further details on customizing the UnifyFS runtime configuration.

.. code-block:: Bash
    :linenos:

        #!/bin/bash

        # spillover checkpoint data to node-local ssd storage
        export UNIFYFS_SPILLOVER_DATA_DIR=/mnt/ssd/$USER/data
        export UNIFYFS_SPILLOVER_META_DIR=/mnt/ssd/$USER/meta

        # store server logs in job-specific scratch area
        export UNIFYFS_LOG_DIR=$JOBSCRATCH/logs

        unifyfs start --share-dir=/path/to/shared/file/system &


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
          -c, --cleanup             [OPTIONAL] clean up the UnifyFS storage upon server exit
          -C, --consistency=<model> [OPTIONAL] consistency model (NONE | LAMINATED | POSIX)
          -e, --exe=<path>          [OPTIONAL] <path> where unifyfsd is installed
          -m, --mount=<path>        [OPTIONAL] mount UnifyFS at <path>
          -s, --script=<path>       [OPTIONAL] <path> to custom launch script
          -S, --share-dir=<path>    [REQUIRED] shared file system <path> for use by servers
          -i, --stage-in=<path>     [OPTIONAL] stage in file(s) at <path>
          -o, --stage-out=<path>    [OPTIONAL] stage out file(s) to <path> on termination

        Command options for "terminate":
          -s, --script=<path>       <path> to custom termination script


After UnifyFS servers have been successfully started, you may run your
UnifyFS-enabled applications as you normally would (e.g., using mpirun).
Only applications that explicitly call ``unifyfs_mount()`` and access files
with the specified mountpoint prefix will utilize UnifyFS for their I/O. All
other applications will operate unchanged.

--------------------
  Stopping UnifyFS
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
