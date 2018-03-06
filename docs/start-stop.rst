======================
Starting & Stopping
======================

In this section, we describe the mechanisms for starting and stopping UnifyCR in
a user's allocation. The important features to consider are:

        - Initialization of UnifyCR file system instance across compute nodes

---------------------------
Initialization Example
---------------------------

First, we need to start the UnifyCR daemon on the nodes in your allocation.
The specific paths and job launch command will depend on your system
configuration.

.. code-block:: Bash
    :linenos:

        #!/bin/bash

        export UNIFYCR_META_SERVER_RATIO=1
        export UNIFYCR_META_DB_NAME=unifycr_db
        export UNIFYCR_CHUNK_MEM=0
        export UNIFYCR_META_DB_PATH=/mnt/ssd
        export UNIFYCR_SERVER_DEBUG_LOG=/tmp/unifycrd_debug.$$

        NODES=1
        PROCS=1

        mpirun -nodes ${NODES} -np ${PROCS} ./unifycrd &

The example above will start the UnifyCR daemon on the number of nodes specified
in the NODES variable.

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

        3. Start the UnifyCR server daemons on each node

        4. Run your application with UnifyCR

---------------------------
Stopping
---------------------------

Currently, the UnifyCR server daemon runs throughout a user's job allocation
after it is started. Even if the UnifyCR daemon is running in a user's job the
UnifyCR file system will only be utilized if the user has mounted a path for
UnifyCR to intercept. The UnifyCR daemon is stopped when the user's allocation
is exited.
