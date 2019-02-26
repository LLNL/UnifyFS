================================
  Starting & Stopping in a Job
================================

In this section, we describe the mechanisms for starting and stopping UnifyCR in
a user's job allocation.

Overall, the steps taken to run an application with UnifyCR include:

    1. Allocate nodes using the system resource manager (i.e., start a job)

    2. Update any desired UnifyCR server configuration settings

    3. Start UnifyCR servers on each allocated node using ``unifycr``

    4. Run one or more UnifyCR-enabled applications

    5. Terminate the UnifyCR servers using ``unifycr``

--------------------
  Starting UnifyCR
--------------------

First, we need to start the UnifyCR server daemon (``unifycrd``) on the nodes in
the job allocation. UnifyCR provides the ``unifycr`` command line utility to
simplify this action on systems with supported resource managers. The easiest
way to determine if you are using a supported system is to run
``unifycr start`` within an interactive job allocation. If no compatible
resource management system is detected, the utility will report an error message
to that effect.

In ``start`` mode, the ``unifycr`` utility automatically detects the allocated
nodes and launches a server on each node. For example, the following script
could be used to launch the ``unifycrd`` servers with a customized
configuration. On systems with resource managers that propagate environment
settings to compute nodes, the environment variables will override any
settings in ``/etc/unifycr/unifycr.conf``. See :doc:`configuration`
for further details on customizing the UnifyCR runtime configuration.

.. code-block:: Bash
    :linenos:

        #!/bin/bash

        # spillover checkpoint data to node-local ssd storage
        export UNIFYCR_SPILLOVER_DATA_DIR=/mnt/ssd/$USER/data
        export UNIFYCR_SPILLOVER_META_DIR=/mnt/ssd/$USER/meta

        # store server logs in job-specific scratch area
        export UNIFYCR_LOG_DIR=$JOBSCRATCH/logs

        unifycr start --mount=/mnt/unifycr


``unifycr`` provides command-line options to choose the client mountpoint,
adjust the consistency model, and control stage-in and stage-out of files.
The full usage for ``unifycr`` is as follows:

.. code-block:: Bash
    :linenos:

        [prompt]$ unifycr --help

        Usage: unifycr <command> [options...]

        <command> should be one of the following:
          start       start the unifycr server daemon
          terminate   terminate the unifycr server daemon

        Available options for "start":
          -C, --consistency=<model> consistency model (NONE | LAMINATED | POSIX)
          -e, --exe=<path>          <path> where unifycrd is installed
          -m, --mount=<path>        mount unifycr at <path>
          -s, --script=<path>       <path> to custom launch script
          -i, --stage-in=<path>     stage in file(s) at <path>
          -o, --stage-out=<path>    stage out file(s) to <path> on termination

        Available options for "terminate":
          -c, --cleanup             clean up the unifycr storage on termination
          -s, --script=<path>       <path> to custom termination script


After UnifyCR servers have been successfully started, you may run your
UnifyCR-enabled applications as you normally would (e.g., using mpirun).
Only applications that explcitly call ``unifycr_mount()`` and access files
with the specified mountpoint prefix will utils UnifyCR for their I/O. All
other applications will operate unchanged.

--------------------
  Stopping UnifyCR
--------------------

After all UnifyCR-enabled applications have completed running, you should
use ``unifycr terminate`` to terminate the servers. Typically, one would
also pass the ``--cleanup`` option to have the servers remove temporary data
locally stored on each node.

