======================
Starting & Stopping
======================

In this section, we describe the mechanisms for starting and stopping UnifyCR in
a user's allocation. The important features to consider are:

    - Initialization of UnifyCR file system instance across compute nodes

---------------------------
Initialization Example
---------------------------

First, we need to start the UnifyCR daemon (``unifycrd``) on the nodes in your
allocation. UnifyCR provides the ``unifycr`` command line utility for this
purpose. The specific paths and job launch command will depend on your
installation and system configuration_.

.. code-block:: Bash
    :linenos:

        user@ unifycr --help

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

For instance, the following script will launch the unifycrd daemon.

.. code-block:: Bash
    :linenos:

        #!/bin/bash

        export UNIFYCR_META_SERVER_RATIO=1
        export UNIFYCR_META_DB_NAME=unifycr_db
        export UNIFYCR_CHUNK_MEM=0
        export UNIFYCR_META_DB_PATH=/mnt/ssd
        export UNIFYCR_SERVER_DEBUG_LOG=/tmp/unifycrd_debug.$$

        unifycr start --mount=/mnt/unifycr

Note that the ``unifycr`` utility automatically detects the allocated nodes and
launches the daemon on each of the allocated node. In addition, the above
environment variables will override any configurations in
``/etc/unifycr/unifycr.conf``. See configurations_ for further details about
the configuration.

Next, we can start run our application with UnifyCR in the following manner:

.. code-block:: Bash
    :linenos:

        #!/bin/bash

        export UNIFYCR_EXTERNAL_META_DIR=/mnt/ssd
        export UNIFYCR_EXTERNAL_DATA_DIR=/mnt/ssd

        NODES=1
        PROCS=1

        mpirun -nodes ${NODES} -np ${PROCS} /path/to/my/app

So, overall the steps taken to run an application with UnifyCR include:

    1. Allocate Nodes

    2. Update any desired configuration variables in the bash scripts

    3. Start the UnifyCR server daemons on each node with the ``unifycr``
       utility.

    4. Run your application with UnifyCR

---------------------------
Stopping
---------------------------

Currently, the UnifyCR server daemon runs throughout a user's job allocation
after it is started. Even if the UnifyCR daemon is running in a user's job the
UnifyCR file system will only be utilized if the user has mounted a path for
UnifyCR to intercept. The UnifyCR daemon is stopped when the user's allocation
is exited.
