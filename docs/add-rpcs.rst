========================================
Adding RPC Functions With Margo Library
========================================

In this section, we describe how to add an RPC function using
the Margo library API.

.. note::

    The following documentation uses ``unifyfs_mount_rpc()`` as an example
    client-server RPC function to demonstrate the required code modifications.

---------------------------
Common
---------------------------

1. Define structs for the input and output parameters of your RPC handler.

    The struct definition macro ``MERCURY_GEN_PROC()`` is used to define
    both input and output parameters. For client-server RPCs, the
    definitions should be placed in ``common/src/unifyfs_client_rpcs.h``,
    while server-server RPC structs are defined in
    ``common/src/unifyfs_server_rpcs.h``.

    The input parameters struct should contain all values the client needs
    to pass to the server handler function.
    The output parameters struct should contain all values the server needs
    to pass back to the client upon completion of the handler function.
    The following shows the input and output structs used by
    ``unifyfs_mount_rpc()``.

    .. code-block:: c

        MERCURY_GEN_PROC(unifyfs_mount_in_t,
                        ((int32_t)(dbg_rank))
                        ((hg_const_string_t)(mount_prefix))
                        ((hg_const_string_t)(client_addr_str)))
        MERCURY_GEN_PROC(unifyfs_mount_out_t,
                        ((int32_t)(app_id))
                        ((int32_t)(client_id))
                        ((int32_t)(ret)))

.. note::

   Passing some types can be an issue. Refer to the Mercury documentation for
   supported types: `<https://mercury-hpc.github.io/documentation/>`_ (look
   under `Predefined Types`). If your type is not predefined, you will need to
   either convert it to a supported type or write code to serialize/deserialize
   the input/output parameters. Phil Carns said he has starter code for this,
   since much of the code is similar.

---------------------------
Server
---------------------------

1. Implement the RPC handler function for the server.

    This is the function that will be invoked on the client and executed on
    the server. Client-server RPC handler functions are implemented in
    ``server/src/unifyfs_client_rpc.c``, while server-server RPC handlers go
    in ``server/src/unifyfs_p2p_rpc.c`` or ``server/src/unifyfs_group_rpc.c``.

    All the RPC handler functions follow the same protoype, which is passed
    a Mercury handle as the only argument. The handler function should use
    ``margo_get_input()`` to retrieve the input parameters struct provided by
    the client. After the RPC handler finishes its intended action, it replies
    using ``margo_respond()``, which takes the handle and output parameters
    struct as arguments. Finally, the handler function should release the
    input struct using ``margo_free_input()``, and the handle using
    ``margo_destroy()``. See the existing RPC handler functions for more info.

    After implementing the handler function, place the Margo RPC handler
    definition macro immediately following the function.

    .. code-block:: c

        static void unifyfs_mount_rpc(hg_handle_t handle)
        {
            ...
        }
        DEFINE_MARGO_RPC_HANDLER(unifyfs_mount_rpc)

2. Register the server RPC handler with margo.

    In ``server/src/margo_server.c``, update the client-server RPC registration
    function ``register_client_server_rpcs()`` to include a registration macro
    for the new RPC handler. As shown below, the last argument to
    ``MARGO_REGISTER()`` is the handler function address. The prior two arguments
    are the input and output parameters structs.

    .. code-block:: c

        MARGO_REGISTER(unifyfsd_rpc_context->mid,
                       "unifyfs_mount_rpc",
                       unifyfs_mount_in_t,
                       unifyfs_mount_out_t,
                       unifyfs_mount_rpc);



---------------------------
Client
---------------------------

1. Add a Mercury id for the RPC handler to the client RPC context.

    In ``client/src/margo_client.h``, update the ``ClientRpcIds`` structure
    to add a new ``hg_id_t <name>_id`` variable to hold the RPC handler id.

    .. code-block:: c

        typedef struct ClientRpcIds {
            ...
            hg_id_t mount_id;
        }

2. Register the RPC handler with Margo.

    In ``client/src/margo_client.c``, update ``register_client_rpcs()`` to
    register the new RPC handler by its name using
    ``CLIENT_REGISTER_RPC(<name>)``, which will store its Mercury id in the
    ``<name>_id`` structure variable defined in the first step. For example:

    .. code-block:: c

        CLIENT_REGISTER_RPC(mount);

3. Define and implement an invocation function that will execute the RPC.

    The declaration should be placed in ``client/src/margo_client.h``, and the
    definition should go in ``client/src/margo_client.c``.

    .. code-block:: c

        int invoke_client_mount_rpc(unifyfs_client* client, ...);

    A handle for the RPC is obtained using ``create_handle()``, which takes the
    the id of the RPC as its only parameter. The RPC is actually
    initiated using ``forward_to_server()``, where the RPC handle, input
    struct address, and RPC timeout are given as parameters. Use
    ``margo_get_output()`` to obtain the returned output
    parameters struct, and release it with ``margo_free_output()``. Finally,
    ``margo_destroy()`` is used to release the RPC handle. See the existing
    invocation functions for more info.

.. note::

    The general workflow for creating new RPC functions is the same if you want to
    invoke an RPC on the server, and execute it on the client. One difference is
    that you will have to pass `NULL` to the last parameter of `MARGO_REGISTER()` on
    the server, and on the client the last parameter to `MARGO_REGISTER()` will be
    the name of the RPC handler function. To execute RPCs on the client it needs to
    be started in Margo as a `SERVER`, and the server needs to know the address of
    the client where the RPC will be executed. The client has already been
    configured to do those two things, so the only change going forward is how
    `MARGO_REGISTER()` is called depending on where the RPC is being executed
    (client or server).
