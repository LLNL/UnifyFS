========================================
Adding RPC Functions With Margo Library
========================================

In this section, we describe how to add an RPC function using
the Margo library API.

.. note::

    This uses the `unifycr_mount_rpc` as an example RPC
    function to follow throughout.

---------------------------
Common
---------------------------

1. Define structs for the input and output parameters of your RPC handler.

   The struct definition macro `MERCURY_GEN_PROC()` is used to define
   both input and output parameters. For client-server RPCs, the
   definitions should be placed in `common/src/unifycr_clientcalls_rpc.h`,
   while server-server RPC structs are defined in
   `common/src/unifycr_servercalls_rpc.h`.

   The input parameters struct should contain all values the client needs
   to pass to the server handler function.
   The output parameters struct should contain all values the server needs
   to pass back to the client upon completion of the handler function.
   The following shows the input and output structs for `unifycr_mount_rpc`.

.. code-block:: C
  MERCURY_GEN_PROC(unifycr_mount_in_t,
                   ((int32_t)(app_id))
                   ((int32_t)(local_rank_idx))
                   ((int32_t)(dbg_rank))
                   ((int32_t)(num_procs_per_node))
                   ((hg_const_string_t)(client_addr_str))
                   ((hg_size_t)(req_buf_sz))
                   ((hg_size_t)(recv_buf_sz))
                   ((hg_size_t)(superblock_sz))
                   ((hg_size_t)(meta_offset))
                   ((hg_size_t)(meta_size))
                   ((hg_size_t)(fmeta_offset))
                   ((hg_size_t)(fmeta_size))
                   ((hg_size_t)(data_offset))
                   ((hg_size_t)(data_size))
                   ((hg_const_string_t)(external_spill_dir)))
  MERCURY_GEN_PROC(unifycr_mount_out_t,
                   ((hg_size_t)(max_recs_per_slice))
                   ((int32_t)(ret)))

.. note::
Passing some types can be an issue. Refer to the Mercury documentation for
supported types: `<https://mercury-hpc.github.io/documentation/`_ (look
under Predefined Types). If your type is not predefined, you will need to
either convert it to a supported type or write code to serialize/deserialize
the input/output parameters. Phil Carns said he has starter code for this,
since much of the code is similar.

---------------------------
Server
---------------------------

1. Implement the RPC handler function for the server.

   This is the function that will be invoked on the client and executed on
   the server. Client-server RPC handler functions are implemented in
   `server/src/unifycr_cmd_handler.c`, while server-server RPC handlers go
   in `server/src/unifycr_service_manager.c`. The RPC handler input and output
   parameters structs are defined in `common/src/unifycr_clientcalls_rpc.h`.

   All the RPC handler functions follow the same protoype, which is passed
   a Mercury handle as the only argument. The handler function should use
   `margo_get_input()` to retrieve the input parameters struct provided by
   the client. After the RPC handler finishes its intended action, it replies
   using `margo_respond()`, which takes the handle and output parameters
   struct as arguments. Finally, the handler function should release the
   input struct using `margo_free_input()`, and the handle using
   `margo_destroy()`. See the existing RPC handler functions for more info.

   After implementing the handler function, place the Margo RPC handler
   definition macro immediately following the function, for example:
    `DEFINE_MARGO_RPC_HANDLER(unifycr_mount_rpc`

2. Register the server RPC handler with margo.

   In `server/src/margo_server.c`, update the client-server RPC registration
   function `register_client_server_rpcs()` to include a registration macro
   for the new RPC handler, for example:
.. code-block:: C
    MARGO_REGISTER(unifycrd_rpc_context->mid, "unifycr_mount_rpc",
    unifycr_mount_in_t, unifycr_mount_out_t, unifycr_mount_rpc);

   The last argument to `MARGO_REGISTER()` is the handler function name. The
   prior two arguments are the input and output parameters structs. The input
   struct is passed in by the client to the RPC handler function, and then
   forwarded to the server. When the RPC handler function finishes, the output
   struct will be passed back to the client, which is generally a return code.

---------------------------
Client
---------------------------

1. Add a Mercury id for the RPC handler to the client RPC context.

   In `client/src/margo_client.h`, update the `ClientRpcIds` structure
   to add a new `hg_id_t` variable to hold the RPC handler id.
.. code-block:: C
    typedef struct ClientRpcIds {
        ...
        hg_id_t mount_id;
    }

2. Register the RPC handler with Margo.

   In `client/src/margo_client.c`, update `register_client_rpcs()` to register
   the RPC handler and store its Mercury id in the newly defined `ClientRpcIds`
   variable.
   .. code-block:: C
   client_rpc_context->rpcs.mount_id = MARGO_REGISTER(client_rpc_context->mid, "unifycr_mount_rpc",
                                                      unifycr_mount_in_t, unifycr_mount_out_t, NULL);

   When the client calls `MARGO_REGISTER()` the last parameter is `NULL`. This
   is the RPC handler function that is only defined on the server.

3. Define and implement an invocation function that will execute the RPC.

   The declaration should be placed in `client/src/margo_client.h`, and the
   definition should go in `client/src/margo_client.c`.
   .. code-block:: C
   int invoke_client_mount_rpc();

   A handle for the RPC is obtained using `margo_create()`, which takes the
   server address and the id of the RPC as parameters. The RPC is actually
   initiated using `margo_forward()`, where the RPC handle and input struct
   are supplied. Use `margo_get_output()` to obtain the returned output
   parameters struct, and release it with `margo_free_output()`. Finally,
   `margo_destroy()` is used to release the RPC handle. See the existing
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
