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

.. code-block:: Bash
    :linenos:

        #!/bin/bash

        export UNIFYCR_META_SERVER_RATIO=1
        export UNIFYCR_META_DB_NAME=unifycr_db
        export UNIFYCR_CHUNK_MEM=0

        NODES=1
        PROCS=1

        mpirun -nodes ${NODES} -np ${PROCS} ./unifycrd &

The example above will start the UnifyCR daemon on the number of nodes specified
in the NODES variable.

Next, we can start run our application with UnifyCR in the following manner:

.. code-block:: Bash
    :linenos:

        #!/bin/bash

        export UNIFYCR_USE_SPILLOVER=1
        APP_PATH=/path/to/my/app

        NODES=1
        PROCS=1

        mpirun -nodes ${NODES} -np ${PROCS} ${HOME}/my_app

The APP_PATH variable is the directory where your application lives.

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
