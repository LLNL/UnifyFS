/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

/**************************************************************************
 * margo_client.c - Implements the client-server RPC calls (shared-memory)
 **************************************************************************/

#include "unifyfs-internal.h"
#include "unifyfs_rpc_util.h"
#include "margo_client.h"
#include "client_read.h"
#include "client_transfer.h"

/* global rpc context */
static client_rpc_context_t* client_rpc_context; // = NULL

/* register client RPCs */
static void register_client_rpcs(client_rpc_context_t* ctx)
{
    /* shorter name for our margo instance id */
    margo_instance_id mid = ctx->mid;

    hg_id_t hgid;

    /* client-to-server RPCs */

#define CLIENT_REGISTER_RPC(name) \
    do { \
        hgid = MARGO_REGISTER(mid, "unifyfs_" #name "_rpc", \
                              unifyfs_##name##_in_t, \
                              unifyfs_##name##_out_t, \
                              NULL); \
        ctx->rpcs.name##_id = hgid; \
    } while (0)

    CLIENT_REGISTER_RPC(attach);
    CLIENT_REGISTER_RPC(mount);
    CLIENT_REGISTER_RPC(unmount);
    CLIENT_REGISTER_RPC(metaset);
    CLIENT_REGISTER_RPC(metaget);
    CLIENT_REGISTER_RPC(filesize);
    CLIENT_REGISTER_RPC(transfer);
    CLIENT_REGISTER_RPC(truncate);
    CLIENT_REGISTER_RPC(unlink);
    CLIENT_REGISTER_RPC(laminate);
    CLIENT_REGISTER_RPC(fsync);
    CLIENT_REGISTER_RPC(mread);
    CLIENT_REGISTER_RPC(node_local_extents_get);
    CLIENT_REGISTER_RPC(get_gfids);

#undef CLIENT_REGISTER_RPC

    /* server-to-client RPCs */

#define CLIENT_REGISTER_RPC_HANDLER(name) \
    do { \
        hgid = MARGO_REGISTER(mid, "unifyfs_" #name "_rpc", \
                              unifyfs_##name##_in_t, \
                              unifyfs_##name##_out_t, \
                              unifyfs_##name##_rpc); \
        ctx->rpcs.name##_id = hgid; \
    } while (0)

    CLIENT_REGISTER_RPC_HANDLER(heartbeat);
    CLIENT_REGISTER_RPC_HANDLER(mread_req_data);
    CLIENT_REGISTER_RPC_HANDLER(mread_req_complete);
    CLIENT_REGISTER_RPC_HANDLER(transfer_complete);
    CLIENT_REGISTER_RPC_HANDLER(unlink_callback);

#undef CLIENT_REGISTER_RPC_HANDLER
}

/* initialize margo client-server rpc */
int unifyfs_client_rpc_init(double timeout_msecs)
{
    hg_return_t hret;

    if (NULL != client_rpc_context) {
        /* already initialized */
        return UNIFYFS_SUCCESS;
    }

    /* lookup margo server address string,
     * should be something like: "na+sm://7170/0" */
    char* svr_addr_string = rpc_lookup_local_server_addr();
    if (svr_addr_string == NULL) {
        LOGERR("Failed to find local margo RPC server address");
        return UNIFYFS_FAILURE;
    }

    /* duplicate server address string,
     * then parse address to pick out protocol portion
     * which is the piece before the colon like: "na+sm" */
    char* proto = strdup(svr_addr_string);
    char* colon = strchr(proto, ':');
    if (NULL != colon) {
        *colon = '\0';
    }
    LOGDBG("svr_addr:'%s' proto:'%s'", svr_addr_string, proto);

    /* allocate memory for rpc context struct */
    client_rpc_context_t* ctx = calloc(1, sizeof(client_rpc_context_t));
    if (NULL == ctx) {
        LOGERR("Failed to allocate client RPC context");
        free(proto);
        free(svr_addr_string);
        return UNIFYFS_FAILURE;
    }

    /* timeout value to use on rpc operations */
    ctx->timeout = timeout_msecs;

    /* initialize margo */
    int use_progress_thread = 1;
    int ult_pool_sz = 1;
    ctx->mid = margo_init(proto, MARGO_SERVER_MODE, use_progress_thread,
                          ult_pool_sz);
    assert(ctx->mid);

    /* get server margo address */
    ctx->svr_addr = HG_ADDR_NULL;
    margo_addr_lookup(ctx->mid, svr_addr_string, &(ctx->svr_addr));

    /* done with the protocol and address strings, free them */
    free(proto);
    free(svr_addr_string);

    /* check that we got a valid margo address for the server */
    if (ctx->svr_addr == HG_ADDR_NULL) {
        LOGERR("Failed to resolve margo server RPC address");
        margo_finalize(ctx->mid);
        free(ctx);
        return UNIFYFS_FAILURE;
    }

    /* get our own margo address */
    hret = margo_addr_self(ctx->mid, &(ctx->client_addr));
    if (hret != HG_SUCCESS) {
        LOGERR("Failed to acquire our margo address");
        margo_addr_free(ctx->mid, ctx->svr_addr);
        margo_finalize(ctx->mid);
        free(ctx);
        return UNIFYFS_FAILURE;
    }

    /* convert our margo address to a string */
    char addr_self_string[128];
    hg_size_t addr_self_string_sz = sizeof(addr_self_string);
    hret = margo_addr_to_string(ctx->mid,
        addr_self_string, &addr_self_string_sz, ctx->client_addr);
    if (hret != HG_SUCCESS) {
        LOGERR("Failed to convert our margo address to string");
        margo_addr_free(ctx->mid, ctx->client_addr);
        margo_addr_free(ctx->mid, ctx->svr_addr);
        margo_finalize(ctx->mid);
        free(ctx);
        return UNIFYFS_FAILURE;
    }

    /* make a copy of our own margo address string */
    ctx->client_addr_str = strdup(addr_self_string);

    /* look up and record id values for each rpc */
    register_client_rpcs(ctx);

    /* cache context in global variable */
    client_rpc_context = ctx;

    return UNIFYFS_SUCCESS;
}

/* free resources allocated in corresponding call
 * to unifyfs_client_rpc_init, frees structure
 * allocated and sets pcontect to NULL */
int unifyfs_client_rpc_finalize(void)
{
    if (client_rpc_context != NULL) {
        /* define a temporary to refer to context */
        client_rpc_context_t* ctx = client_rpc_context;
        client_rpc_context = NULL;

        /* free margo address for client */
        margo_addr_free(ctx->mid, ctx->client_addr);

        /* free margo address to server */
        margo_addr_free(ctx->mid, ctx->svr_addr);

        /* shut down margo */
        margo_finalize(ctx->mid);

        /* free memory allocated for context structure */
        free(ctx->client_addr_str);
        free(ctx);
    }

    return UNIFYFS_SUCCESS;
}

/*--- Invocation methods for client-to-server RPCs ---*/

/* create and return a margo handle for given rpc id */
static hg_handle_t create_handle(hg_id_t id)
{
    /* define a temporary to refer to global context */
    client_rpc_context_t* ctx = client_rpc_context;

    /* create handle for specified rpc */
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_return_t hret = margo_create(ctx->mid, ctx->svr_addr, id, &handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_create() failed");
    }
    return handle;
}

static int forward_to_server(hg_handle_t hdl,
                             void* input_ptr,
                             double timeout_msec)
{
    hg_return_t hret = margo_forward_timed(hdl, input_ptr, timeout_msec);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward_timed() failed - %s", HG_Error_to_string(hret));
        //margo_state_dump(client_rpc_context->mid, "-", 0, NULL);
        return UNIFYFS_ERROR_MARGO;
    }
    return UNIFYFS_SUCCESS;
}

/* invokes the mount rpc function */
int invoke_client_mount_rpc(unifyfs_client* client)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.mount_id);

    /* fill in input struct */
    unifyfs_mount_in_t in;
    in.dbg_rank = client->state.app_rank;
    in.mount_prefix = strdup(client->cfg.unifyfs_mountpoint);

    /* pass our margo address to the server */
    in.client_addr_str = strdup(client_rpc_context->client_addr_str);

    /* call rpc function */
    LOGDBG("invoking the mount rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of mount rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* free memory on input struct */
    free((void*)in.mount_prefix);
    free((void*)in.client_addr_str);

    /* decode response */
    int ret;
    unifyfs_mount_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        if (ret == (int)UNIFYFS_SUCCESS) {
            /* get assigned client id, and verify app_id */
            client->state.client_id = (int) out.client_id;
            int srvr_app_id   = (int) out.app_id;
            if (client->state.app_id != srvr_app_id) {
                LOGWARN("mismatch on app_id - using %d, server returned %d",
                        client->state.app_id, srvr_app_id);
            }
            LOGDBG("My client id is %d", client->state.client_id);
        }
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* Fill attach rpc input struct with client-side context info */
static void fill_client_attach_info(unifyfs_client* client,
                                    unifyfs_attach_in_t* in)
{
    in->app_id            = client->state.app_id;
    in->client_id         = client->state.client_id;
    in->shmem_super_size  = client->state.shm_super_ctx->size;
    in->meta_offset       = client->state.write_index.index_offset;
    in->meta_size         = client->state.write_index.index_size;

    if (NULL != client->state.logio_ctx->shmem) {
        in->logio_mem_size = client->state.logio_ctx->shmem->size;
    } else {
        in->logio_mem_size = 0;
    }

    in->logio_spill_size = client->state.logio_ctx->spill_sz;
    if (client->state.logio_ctx->spill_sz) {
        in->logio_spill_dir = strdup(client->cfg.logio_spill_dir);
    } else {
        in->logio_spill_dir = NULL;
    }
}


/* invokes the attach rpc function */
int invoke_client_attach_rpc(unifyfs_client* client)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.attach_id);

    /* fill in input struct */
    unifyfs_attach_in_t in;
    fill_client_attach_info(client, &in);

    /* call rpc function */
    LOGDBG("invoking the attach rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of attach rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_attach_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);
    if (NULL != in.logio_spill_dir) {
        free((void*)in.logio_spill_dir);
    }

    return ret;
}

/* function invokes the unmount rpc */
int invoke_client_unmount_rpc(unifyfs_client* client)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.unmount_id);

    /* fill in input struct */
    unifyfs_unmount_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;

    /* call rpc function */
    LOGDBG("invoking the unmount rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of unmount rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_unmount_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/*
 * Set the metadata values for a file (after optionally creating it).
 * The gfid for the file is in f_meta->gfid.
 *
 * create: If set to 1, attempt to create the file first.  If the file
 *         already exists, then update its metadata with the values in
 *         f_meta.  If set to 0, and the file does not exist, then
 *         the server will return an error.
 *
 * f_meta: The metadata values to update.
 */
int invoke_client_metaset_rpc(unifyfs_client* client,
                              unifyfs_file_attr_op_e attr_op,
                              unifyfs_file_attr_t* f_meta)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.metaset_id);

    /* fill in input struct */
    unifyfs_metaset_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.attr_op   = (int32_t) attr_op;
    memcpy(&(in.attr), f_meta, sizeof(*f_meta));

    /* call rpc function */
    LOGDBG("invoking the metaset rpc function in client - gfid:%d file:%s",
           in.attr.gfid, in.attr.filename);
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of metaset rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_metaset_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client metaget rpc function */
int invoke_client_metaget_rpc(unifyfs_client* client,
                              int gfid,
                              unifyfs_file_attr_t* file_meta)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.metaget_id);

    /* fill in input struct */
    unifyfs_metaget_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the metaget rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of metaget rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_metaget_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        if (ret == (int)UNIFYFS_SUCCESS) {
            /* fill in results  */
            memset(file_meta, 0, sizeof(unifyfs_file_attr_t));
            *file_meta = out.attr;
            if (NULL != out.attr.filename) {
                file_meta->filename = strdup(out.attr.filename);
            }
        }
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client filesize rpc function */
int invoke_client_filesize_rpc(unifyfs_client* client,
                              int gfid,
                              size_t* outsize)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.filesize_id);

    /* fill in input struct */
    unifyfs_filesize_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the filesize rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of filesize rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_filesize_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        if (ret == (int)UNIFYFS_SUCCESS) {
            *outsize = (size_t) out.filesize;
        }
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client truncate rpc function */
int invoke_client_transfer_rpc(unifyfs_client* client,
                               int transfer_id,
                               int gfid,
                               int parallel_transfer,
                               const char* dest_file)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.transfer_id);

    /* fill in input struct */
    unifyfs_transfer_in_t in;
    in.app_id      = (int32_t) client->state.app_id;
    in.client_id   = (int32_t) client->state.client_id;
    in.transfer_id = (int32_t) transfer_id;
    in.gfid        = (int32_t) gfid;
    in.mode        = (int32_t) parallel_transfer;
    in.dst_file    = (hg_const_string_t) dest_file;

    /* call rpc function */
    LOGDBG("invoking the transfer rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of transfer rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_transfer_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client truncate rpc function */
int invoke_client_truncate_rpc(unifyfs_client* client,
                               int gfid,
                               size_t filesize)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.truncate_id);

    /* fill in input struct */
    unifyfs_truncate_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.gfid      = (int32_t) gfid;
    in.filesize  = (hg_size_t) filesize;

    /* call rpc function */
    LOGDBG("invoking the truncate rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of truncate rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_truncate_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client unlink rpc function */
int invoke_client_unlink_rpc(unifyfs_client* client,
                             int gfid)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.unlink_id);

    /* fill in input struct */
    unifyfs_unlink_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the unlink rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of unlink rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_unlink_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client-to-server laminate rpc function */
int invoke_client_laminate_rpc(unifyfs_client* client,
                               int gfid)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.laminate_id);

    /* fill in input struct */
    unifyfs_laminate_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the laminate rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of laminate rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_laminate_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client sync rpc function */
int invoke_client_sync_rpc(unifyfs_client* client,
                           int gfid)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.fsync_id);

    /* fill in input struct */
    unifyfs_fsync_in_t in;
    in.app_id    = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGINFO("invoking the sync rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of sync rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_fsync_out_t out;
    hg_return_t hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client mread rpc function */
int invoke_client_mread_rpc(unifyfs_client* client,
                            unsigned int reqid,
                            int read_count,
                            size_t extents_size,
                            void* extents_buffer)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.mread_id);

    /* initialize bulk handle for extents */
    unifyfs_mread_in_t in;
    hg_return_t hret = margo_bulk_create(client_rpc_context->mid,
                                         1, &extents_buffer, &extents_size,
                                         HG_BULK_READ_ONLY, &in.bulk_extents);
    if (hret != HG_SUCCESS) {
        return UNIFYFS_ERROR_MARGO;
    }

    /* fill input struct */
    in.mread_id   = (int32_t) reqid;
    in.app_id     = (int32_t) client->state.app_id;
    in.client_id  = (int32_t) client->state.client_id;
    in.read_count = (int32_t) read_count;
    in.bulk_size  = (hg_size_t) extents_size;

    /* call rpc function */
    LOGDBG("invoking the mread rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of mread rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_mread_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* margo_forward serializes all data before returning, and it's safe to
     * free the rpc params */
    margo_bulk_free(in.bulk_extents);

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client metaget rpc function */
int invoke_client_node_local_extents_get_rpc(unifyfs_client* client,
                                             int num_req,
                                             extents_list_t* read_req,
                                             size_t* extent_count,
                                             unifyfs_client_index_t** extents)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    size_t extents_size = num_req * sizeof(unifyfs_extent_t);
    void* buffer = malloc(extents_size);
    if (NULL == buffer) {
        return ENOMEM;
    }

    unifyfs_extent_t* int_extents = (unifyfs_extent_t*)buffer;
    extents_list_t* cur = read_req;
    for (int i = 0; i < num_req; i++) {
        unifyfs_extent_t* ext = int_extents + i;
        ext->gfid = cur->value.gfid;
        ext->offset = cur->value.file_pos;
        ext->length = cur->value.length;
        cur = cur->next;
    }
    /* get handle to rpc function */
    hg_handle_t handle = create_handle(
            client_rpc_context->rpcs.node_local_extents_get_id);

    /* fill in input struct */
    unifyfs_node_local_extents_get_in_t in;
    hg_return_t hret = margo_bulk_create(client_rpc_context->mid,
                                         1, &buffer, &extents_size,
                                         HG_BULK_READ_ONLY, &in.bulk_data);
    if (hret != HG_SUCCESS) {
        return UNIFYFS_ERROR_MARGO;
    }
    in.app_id = (int32_t) client->state.app_id;
    in.client_id = (int32_t) client->state.client_id;
    in.num_req = num_req;
    in.bulk_size = extents_size;

    /* call rpc function */
    LOGDBG("invoking the node_local_extents_get rpc function in client");
    double timeout = client_rpc_context->timeout;
    int rc = forward_to_server(handle, &in, timeout);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("forward of metaget rpc to server failed");
        margo_destroy(handle);
        return rc;
    }

    /* decode response */
    int ret;
    unifyfs_node_local_extents_get_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        if (ret == (int) UNIFYFS_SUCCESS) {
            *extent_count = out.extent_count;
            void* out_buffer = pull_margo_bulk_buffer(handle, out.bulk_data,
                                                  out.bulk_size, NULL);
            *extents = (unifyfs_client_index_t*) out_buffer;
        }
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }
    /* margo_forward serializes all data before
     * returning, and it's safe to free the rpc params */
    margo_bulk_free(in.bulk_data);
    /* free resources */

    margo_destroy(handle);
    free(buffer);
    return ret;
}

/*--- Handler methods for server-to-client RPCs ---*/

/* simple heartbeat ping rpc */
static void unifyfs_heartbeat_rpc(hg_handle_t handle)
{
    int ret;

    /* get input params */
    unifyfs_heartbeat_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* lookup client */
        unifyfs_client* client;
        int client_app = (int) in.app_id;
        int client_id  = (int) in.client_id;
        client = unifyfs_find_client(client_app, client_id, NULL);
        if (NULL == client) {
            /* unknown client */
            ret = EINVAL;
        } else if (client->state.is_mounted) {
            /* client is still active */
            ret = UNIFYFS_SUCCESS;
        } else {
            ret = UNIFYFS_FAILURE;
        }
        margo_free_input(handle, &in);
    }

    /* set rpc result status */
    unifyfs_heartbeat_out_t out;
    out.ret = ret;

    /* return to caller */
    LOGDBG("responding");
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_heartbeat_rpc)

/* for client read request identified by mread_id and request index, copy bulk
 * data to request's user buffer at given byte offset from start of request */
static void unifyfs_mread_req_data_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_mread_req_data_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* lookup client mread request */
        unifyfs_client* client;
        int client_app   = (int) in.app_id;
        int client_id    = (int) in.client_id;
        int client_mread = (int) in.mread_id;
        client = unifyfs_find_client(client_app, client_id, NULL);
        client_mread_status* mread = client_get_mread_status(client,
                                                             client_mread);
        if (NULL == mread) {
            /* unknown client request */
            ret = EINVAL;
        } else {
            int read_index = (int) in.read_index;
            size_t data_size = (size_t) in.bulk_size;
            size_t data_offset = (size_t) in.read_offset;

            if (data_size != 0) {
                /* set up pointer to user buffer at read req offset */
                ABT_mutex_lock(mread->sync);
                assert(read_index < mread->n_reads);
                read_req_t* rdreq = mread->reqs + read_index;
                void* user_buf = (void*)(rdreq->buf + data_offset);
                size_t data_space = rdreq->length - data_offset;
                ABT_mutex_unlock(mread->sync);

                if (data_size > data_space) {
                    LOGERR("data size exceeds available user buffer space");
                    ret = EINVAL;
                } else {
                    /* get margo/mercury info to set up bulk transfer */
                    const struct hg_info* hgi = margo_get_info(handle);
                    assert(hgi);
                    margo_instance_id mid =
                        margo_hg_handle_get_instance(handle);
                    assert(mid != MARGO_INSTANCE_NULL);

                    /* register user buffer for bulk access */
                    hg_bulk_t bulk_local;
                    hret = margo_bulk_create(mid, 1, &user_buf, &data_size,
                                             HG_BULK_WRITE_ONLY, &bulk_local);
                    if (hret != HG_SUCCESS) {
                        LOGERR("margo_bulk_create() failed");
                        ret = UNIFYFS_ERROR_MARGO;
                    } else {
                        /* execute the transfer to pull data from remote side
                         * into our local buffer.
                         *
                         * NOTE: mercury/margo bulk transfer does not check the
                         * maximum transfer size that the underlying transport
                         * supports, and a large bulk transfer may result in
                         * failure. */
                        int i = 0;
                        hg_size_t offset, len;
                        hg_size_t remain = in.bulk_size;
                        hg_size_t max_bulk = UNIFYFS_SERVER_MAX_BULK_TX_SIZE;
                        do {
                            offset = i * max_bulk;
                            len = (remain < max_bulk) ? remain : max_bulk;
                            hret = margo_bulk_transfer(mid, HG_BULK_PULL,
                                                       hgi->addr,
                                                       in.bulk_data, offset,
                                                       bulk_local, offset,
                                                       len);
                            if (hret != HG_SUCCESS) {
                                LOGERR("margo_bulk_transfer(buf_offset=%zu, "
                                       "len=%zu) failed",
                                       (size_t)offset, (size_t)len);
                                ret = UNIFYFS_ERROR_MARGO;
                                break;
                            }
                            remain -= len;
                            i++;
                        } while (remain > 0);

                        if (hret == HG_SUCCESS) {
                            ABT_mutex_lock(mread->sync);
                            update_read_req_coverage(rdreq, data_offset,
                                                     data_size);
                            ABT_mutex_unlock(mread->sync);
                            LOGINFO("updated coverage for mread[%d] request %d",
                                   client_mread, read_index);
                        }
                        margo_bulk_free(bulk_local);
                    }
                }
            }
        }
        margo_free_input(handle, &in);
    }

    /* set rpc result status */
    unifyfs_mread_req_data_out_t out;
    out.ret = ret;

    /* return to caller */
    LOGDBG("responding");
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_mread_req_data_rpc)

/* for client read request identified by mread_id and request index,
 * update request completion state according to input params */
static void unifyfs_mread_req_complete_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_mread_req_complete_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* lookup client mread request */
        unifyfs_client* client;
        int client_app   = (int) in.app_id;
        int client_id    = (int) in.client_id;
        int client_mread = (int) in.mread_id;
        client = unifyfs_find_client(client_app, client_id, NULL);
        client_mread_status* mread = client_get_mread_status(client,
                                                             client_mread);
        if (NULL == mread) {
            /* unknown client request */
            ret = EINVAL;
        } else {
            int read_index = (int) in.read_index;
            int read_error = (int) in.read_error;
            int complete = 1;

            /* Update the mread state, which will signal completion if all data
             * has been processed for all the requests in the mread */
            ret = client_update_mread_request(mread, read_index,
                                              complete, read_error);
        }
        margo_free_input(handle, &in);
    }

    /* set rpc result status */
    unifyfs_mread_req_complete_out_t out;
    out.ret = ret;

    LOGDBG("responding");

    /* return to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_mread_req_complete_rpc)

/* for client transfer request identified by transfer_id,
 * update request completion state according to input params */
static void unifyfs_transfer_complete_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_transfer_complete_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* lookup client transfer request */
        unifyfs_client* client;
        int client_app     = (int) in.app_id;
        int client_id      = (int) in.client_id;
        int transfer_id    = (int) in.transfer_id;
        size_t transfer_sz = (size_t) in.transfer_size_bytes;
        uint32_t xfer_sec  = (uint32_t) in.transfer_time_sec;
        uint32_t xfer_usec = (uint32_t) in.transfer_time_usec;
        int error_code     = (int) in.error_code;
        client = unifyfs_find_client(client_app, client_id, NULL);
        if (NULL == client) {
            /* unknown client */
            ret = EINVAL;
        } else {
            /* Update the transfer state */
            double transfer_time = (double) xfer_sec;
            transfer_time += (double) xfer_usec / 1000000.0;
            ret = client_complete_transfer(client, transfer_id, error_code,
                                           transfer_sz, transfer_time);
        }
        margo_free_input(handle, &in);
    }

    /* set rpc result status */
    unifyfs_transfer_complete_out_t out;
    out.ret = ret;

    LOGDBG("responding");

    /* return to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_transfer_complete_rpc)

/* unlink callback rpc */
static void unifyfs_unlink_callback_rpc(hg_handle_t handle)
{
    int ret;

    /* get input params */
    unifyfs_unlink_callback_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* lookup client */
        unifyfs_client* client;
        int client_app = (int) in.app_id;
        int client_id  = (int) in.client_id;
        client = unifyfs_find_client(client_app, client_id, NULL);
        if (NULL == client) {
            /* unknown client */
            ret = EINVAL;
        } else {
            int gfid = (int) in.gfid;
            int fid = unifyfs_fid_from_gfid(client, gfid);
            if (-1 != fid) {
                unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client,
                                                                     fid);
                if ((meta != NULL) && (fid == meta->fid)) {
                    meta->pending_unlink = 1;
                }
            }
            ret = UNIFYFS_SUCCESS;
        }
        margo_free_input(handle, &in);
    }

    /* set rpc result status */
    unifyfs_unlink_callback_out_t out;
    out.ret = ret;

    /* return to caller */
    LOGDBG("responding");
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_unlink_callback_rpc)

/* invokes the get_gfids rpc function */
int invoke_client_get_gfids_rpc(unifyfs_client* client,
                                int* num_gfids,
                                int** gfid_list)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    // Anything we check in the cleanup block needs to be initialized to a
    // rational value up here.  Also, there are a couple of margo calls for
    // which the only way to know if we need to clean up is to save their
    // return values.  That's why we have separate hg_return_t's for them.
    hg_return_t get_output_hret = HG_OTHER_ERROR;
    hg_return_t bulk_create_hret = HG_OTHER_ERROR;
    int ret = UNIFYFS_ERROR_MARGO;
     int* _gfid_list = NULL;

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.get_gfids_id);

    /* fill in input struct */
    unifyfs_get_gfids_in_t in;
    in.app_id     = (int32_t) client->state.app_id;
    in.client_id  = (int32_t) client->state.client_id;
    /* TODO: What are app_id and client_id for?!? */

    /* call rpc function */
    LOGDBG("invoking the get_gfids rpc function in client");
    double timeout = client_rpc_context->timeout;
    ret = forward_to_server(handle, &in, timeout);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("forward of get_gfids rpc to server failed");
        goto exit;
    }

    /* decode response */
    unifyfs_get_gfids_out_t out;
    get_output_hret = margo_get_output(handle, &out);
    if (get_output_hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s",
               HG_Error_to_string(get_output_hret));
        ret = UNIFYFS_ERROR_MARGO;
        goto exit;
    }

    LOGDBG("Got response ret=%" PRIi32, out.ret);
    ret = (int) out.ret;
    if (ret != (int)UNIFYFS_SUCCESS) {
        goto exit;
    }

    LOGDBG("Number of GFIDs returned: %d", out.num_gfids);
    *num_gfids = out.num_gfids;

    // Pull the bulk data (the list of gfids) over from the server
    hg_bulk_t local_bulk;
    hg_size_t buf_size = out.num_gfids * sizeof(**gfid_list);

    // Allocate local memory for the list of GFIDs
    _gfid_list = calloc(out.num_gfids, sizeof(**gfid_list));

    // Figure out some margo-specific info that we need for the transfer
    const struct hg_info* info = margo_get_info(handle);
    // TODO: Is this the correct handle??
    hg_addr_t server_addr = info->addr;
    // address of the bulk data on the server side
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    // TODO: Is this the correct handle??

    bulk_create_hret = margo_bulk_create(mid, 1, (void**)&_gfid_list, &buf_size,
                                         HG_BULK_WRITE_ONLY, &local_bulk);
    if (bulk_create_hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed - %s",
               HG_Error_to_string(bulk_create_hret));
        ret = UNIFYFS_ERROR_MARGO;
        goto exit;
    }

    hg_return_t hret = margo_bulk_transfer(mid, HG_BULK_PULL, server_addr,
                                           out.bulk_gfids, 0, local_bulk,
                                           0, buf_size);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_transfer() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
        goto exit;
    }

    // copy the pointer so it can be returned to the caller
    // (Caller will need to eventually call free() on this pointer!)
    *gfid_list = _gfid_list;

exit:  // normally, we use "out", but that's also the name of a variable
    /* free resources */
    if (ret != UNIFYFS_SUCCESS) {
        free(_gfid_list);
    }

    // If margo_bulk_create() succeeded, we need to free local_bulk
    if (bulk_create_hret == HG_SUCCESS) {
        hret = margo_bulk_free(local_bulk);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_bulk_free() failed - %s", HG_Error_to_string(hret));
        }
    }

    // If margo_get_output() succeeded, we need to free out
    if (get_output_hret == HG_SUCCESS) {
        margo_free_output(handle, &out);
    }

    margo_destroy(handle);
    return ret;
}
