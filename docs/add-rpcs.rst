========================================
Adding RPC Functions With Margo Library
========================================

In this section, we describe how to add an RPC function using
the Margo library API.

.. note::

    This uses the `unifycr_mount_rpc` as an example RPC
    function to follow throughout.

---------------------------
Server
---------------------------

1. Write rpc handler function for the server. This is the function that
will be invoked on the client, but executed on the server. Most RPC functions
executed on the server are implemented in `server/src/unifycr_cmd_handler.c`,
such as `unifycr_mount_rpc(hg_handle_t handle)`. After writing the handler
function, use a Margo macro to define the RPC handler function below your
implementation:
    `DEFINE_MARGO_RPC_HANDLER(unifycr_mount_rpc`
That goes at the bottom of your RPC handler. You can look at currently
implemented RPC functions in `server/src/unifycr_cmd_handler.c` for
reference on what this should look like. Generally, the implementations of
these fucntions pass in a handle, then use a `margo_get_input` call to
retrieve the input struct parameters passed in by the client. Then the RPC
handler function operates on those input struct parameters in some way. After
finishing, it replies with a `margo_respond`, where the RPC handle and output
struct are passed back to the client.

2. Register your implementation of the RPC handler on the server with margo in
`server/src/unifycr_init.c`.
.. code-block:: C

    MARGO_REGISTER(unifycr_server_rpc_context->mid,   "unifycr_mount_rpc",
    unifycr_mount_in_t, unifycr_mount_out_t, unifycr_mount_rpc);

The last two parameters to `MARGO_REGISTER` are input and output structs that
are defined on the client, but also need to be registered with the server. The
input struct is passed in by the client to the RPC handler function, and then
forwarded to the server. When the RPC handler function finishes, the output
struct will be passed back to the client, which is generally a return code.

---------------------------
Client
---------------------------

1. Define the input and output structs for your RPC handler in
`common/src/unifycr_clientcalls_rpc.h`. This uses the MERCURY_GEN_PROC macro:
.. code-block:: C
  MERCURY_GEN_PROC(unifycr_mount_out_t,
                   ((hg_size_t)(max_recs_per_slice))
                   ((int32_t)(ret)))
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

The input struct will be all of the parameters the client sends to the RPC
handler function on the server, so they need to be defined in the client
invocation function.

.. note::

    Passing some types can be an issue. You can look through the mercury documentation on what types
    are supported here: `<https://mercury-hpc.github.io/documentation/`_ (look under Predefined Types).
    If your type is not a predefined type you will most likely have to write the code to
    serialize/deserialize the input/output structs.  Phil said he has starter code for this,
    since much of the code is similar.

2. Register the RPC handler with the client in cilent/src/unifycr.c. The pointer
to the margo id for the RPC handler will be stored in an rpc_context that will
be used when the client invokes the RPC handler.
.. code-block:: C

    (*unifycr_rpc_context)->unifycr_mount_rpc_id = MARGO_REGISTER((*unifycr_rpc_context)->mid, "unifycr_mount_rpc",
                                                                  unifycr_mount_in_t, unifycr_mount_out_t, NULL);

When the client calls `MARGO_REGISTER` the last parameter is `NULL`, this is
the RPC handler function that is not defined on the client. When the server
calls this function the last parameter would be the name of the RPC handler
function instead of `NULL`.

3. Add mercury id for the name of the RPC handler to the ClientRpcContext in
`common/src/unifycr_client.h`.
.. code-block:: C

    typedef struct ClientRpcContext {
        margo_instance_id mid;
        hg_context_t* hg_context;
        hg_class_t* hg_class;
        hg_addr_t svr_addr;
        hg_id_t unifycr_mount_rpc_id;

4.  Define an invocation prototype for the client invocation function:
.. code-block:: C
    int32_t unifycr_client_mount_rpc_invoke(unifycr_client_rpc_context_t** unifycr_rpc_context);

5. Invoke the RPC function on the Client:
.. code-block:: C

    unifycr_client_mount_rpc_invoke(&unifycr_rpc_context);

The implementation for these invocation functions have generally been
implemented in `client/src/unifycr_client.c` or the common directory.
The connection to the server is established with a margo_create call
(where the address of the server is passed), then the RPC is actually
forwarded to the server with a `margo_forward` call, where the RPC handle
and input struct are passed. You can get the RPC output with a margo_get_output
call that passes back a return code. There are many more example invocation
functions in `client/src/unifycr_client.c`.

.. note::
The general workflow for creating new RPC functions is the same if you want to
invoke an RPC on the server, and execute it on the client. One difference is
that you will have to pass `NULL` to the last parameter of `MARGO_REGISTER` on
the server, and on the client the last parameter to `MARGO_REGISTER` will be
the name of the RPC handler function. To execute RPCs on the client it needs to
be started in Margo as a `SERVER`, and the server needs to know the address of
the client where the RPC will be executed. The client has already been
configured to do those two things, so the only change going forward is how
`MARGO_REGISTER` is called depending on where the RPC is being executed
(client or server).
