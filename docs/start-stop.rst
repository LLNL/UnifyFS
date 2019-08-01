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

        unifyfs start --mount=/mnt/unifyfs


``unifyfs`` provides command-line options to choose the client mountpoint,
adjust the consistency model, and control stage-in and stage-out of files.
The full usage for ``unifyfs`` is as follows:

.. code-block:: Bash
    :linenos:

        [prompt]$ unifyfs --help

        Usage: unifyfs <command> [options...]

        <command> should be one of the following:
          start       start the unifyfs server daemon
          terminate   terminate the unifyfs server daemon

        Available options for "start":
          -C, --consistency=<model> consistency model (NONE | LAMINATED | POSIX)
          -e, --exe=<path>          <path> where unifyfsd is installed
          -m, --mount=<path>        mount unifyfs at <path>
          -s, --script=<path>       <path> to custom launch script
          -i, --stage-in=<path>     stage in file(s) at <path>
          -o, --stage-out=<path>    stage out file(s) to <path> on termination

        Available options for "terminate":
          -c, --cleanup             clean up the unifyfs storage on termination
          -s, --script=<path>       <path> to custom termination script


After UnifyFS servers have been successfully started, you may run your
UnifyFS-enabled applications as you normally would (e.g., using mpirun).
Only applications that explcitly call ``unifyfs_mount()`` and access files
with the specified mountpoint prefix will utils UnifyFS for their I/O. All
other applications will operate unchanged.

--------------------
  Stopping UnifyFS
--------------------

After all UnifyFS-enabled applications have completed running, you should
use ``unifyfs terminate`` to terminate the servers. Typically, one would
also pass the ``--cleanup`` option to have the servers remove temporary data
locally stored on each node.

