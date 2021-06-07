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

/* global rpc context */
static client_rpc_context_t* client_rpc_context; // = NULL

/* register client RPCs */
static void register_client_rpcs(client_rpc_context_t* ctx)
{
    /* shorter name for our margo instance id */
    margo_instance_id mid = ctx->mid;

    hg_id_t hgid;

#define CLIENT_REGISTER_RPC(name) \
    do { \
        hgid = MARGO_REGISTER(mid, "unifyfs_" #name "_rpc", \
                              unifyfs_##name##_in_t, \
                              unifyfs_##name##_out_t, \
                              NULL); \
        ctx->rpcs.name##_id = hgid; \
    } while (0)

#define CLIENT_REGISTER_RPC_HANDLER(name) \
    do { \
        hgid = MARGO_REGISTER(mid, "unifyfs_" #name "_rpc", \
                              unifyfs_##name##_in_t, \
                              unifyfs_##name##_out_t, \
                              unifyfs_##name##_rpc); \
        ctx->rpcs.name##_id = hgid; \
    } while (0)

    CLIENT_REGISTER_RPC(attach);
    CLIENT_REGISTER_RPC(mount);
    CLIENT_REGISTER_RPC(unmount);
    CLIENT_REGISTER_RPC(metaset);
    CLIENT_REGISTER_RPC(metaget);
    CLIENT_REGISTER_RPC(filesize);
    CLIENT_REGISTER_RPC(truncate);
    CLIENT_REGISTER_RPC(unlink);
    CLIENT_REGISTER_RPC(laminate);
    CLIENT_REGISTER_RPC(fsync);
    CLIENT_REGISTER_RPC(mread);
    CLIENT_REGISTER_RPC_HANDLER(mread_req_data);
    CLIENT_REGISTER_RPC_HANDLER(mread_req_complete);
    CLIENT_REGISTER_RPC_HANDLER(heartbeat);

#undef CLIENT_REGISTER_RPC
#undef CLIENT_REGISTER_RPC_HANDLER
}

/* initialize margo client-server rpc */
int unifyfs_client_rpc_init(void)
{
    hg_return_t hret;

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

    /* initialize margo */
    ctx->mid = margo_init(proto, MARGO_SERVER_MODE, 1, 1);
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

/* invokes the attach rpc function */
int invoke_client_attach_rpc(unifyfs_cfg_t* clnt_cfg)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.attach_id);

    /* fill in input struct */
    unifyfs_attach_in_t in;
    fill_client_attach_info(clnt_cfg, &in);

    /* call rpc function */
    LOGDBG("invoking the attach rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_attach_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);
    if (NULL != in.logio_spill_dir) {
        free((void*)in.logio_spill_dir);
    }

    return ret;
}

/* invokes the mount rpc function */
int invoke_client_mount_rpc(unifyfs_cfg_t* clnt_cfg)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.mount_id);

    /* fill in input struct */
    unifyfs_mount_in_t in;
    fill_client_mount_info(clnt_cfg, &in);

    /* pass our margo address to the server */
    in.client_addr_str = strdup(client_rpc_context->client_addr_str);

    /* call rpc function */
    LOGDBG("invoking the mount rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* free memory on input struct */
    free((void*)in.mount_prefix);
    free((void*)in.client_addr_str);

    /* decode response */
    int ret;
    unifyfs_mount_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        if (ret == (int)UNIFYFS_SUCCESS) {
            /* get assigned client id, and verify app_id */
            unifyfs_client_id = (int) out.client_id;
            int srvr_app_id = (int) out.app_id;
            if (unifyfs_app_id != srvr_app_id) {
                LOGWARN("mismatch on app_id - using %d, server returned %d",
                        unifyfs_app_id, srvr_app_id);
            }
            LOGDBG("My client id is %d", unifyfs_client_id);
        }
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* function invokes the unmount rpc */
int invoke_client_unmount_rpc(void)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.unmount_id);

    /* fill in input struct */
    unifyfs_unmount_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;

    /* call rpc function */
    LOGDBG("invoking the unmount rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_unmount_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
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
int invoke_client_metaset_rpc(unifyfs_file_attr_op_e attr_op,
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
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.attr_op   = (int32_t) attr_op;
    memcpy(&(in.attr), f_meta, sizeof(*f_meta));

    /* call rpc function */
    LOGDBG("invoking the metaset rpc function in client - gfid:%d file:%s",
           in.attr.gfid, in.attr.filename);
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_metaset_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client metaget rpc function */
int invoke_client_metaget_rpc(int gfid, unifyfs_file_attr_t* file_meta)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.metaget_id);

    /* fill in input struct */
    unifyfs_metaget_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t)gfid;

    /* call rpc function */
    LOGDBG("invoking the metaget rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_metaget_out_t out;
    hret = margo_get_output(handle, &out);
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
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client filesize rpc function */
int invoke_client_filesize_rpc(int gfid, size_t* outsize)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.filesize_id);

    /* fill in input struct */
    unifyfs_filesize_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the filesize rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_filesize_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        if (ret == (int)UNIFYFS_SUCCESS) {
            *outsize = (size_t) out.filesize;
        }
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client truncate rpc function */
int invoke_client_truncate_rpc(int gfid, size_t filesize)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.truncate_id);

    /* fill in input struct */
    unifyfs_truncate_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;
    in.filesize  = (hg_size_t) filesize;

    /* call rpc function */
    LOGDBG("invoking the truncate rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_truncate_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client unlink rpc function */
int invoke_client_unlink_rpc(int gfid)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.unlink_id);

    /* fill in input struct */
    unifyfs_unlink_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the unlink rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_unlink_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client-to-server laminate rpc function */
int invoke_client_laminate_rpc(int gfid)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.laminate_id);

    /* fill in input struct */
    unifyfs_laminate_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the laminate rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_laminate_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client sync rpc function */
int invoke_client_sync_rpc(int gfid)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.fsync_id);

    /* fill in input struct */
    unifyfs_fsync_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGINFO("invoking the sync rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_fsync_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client mread rpc function */
int invoke_client_mread_rpc(unsigned int reqid, int read_count,
                            size_t extents_size, void* extents_buffer)
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
    in.app_id     = (int32_t) unifyfs_app_id;
    in.client_id  = (int32_t) unifyfs_client_id;
    in.read_count = (int32_t) read_count;
    in.bulk_size  = (hg_size_t) extents_size;

    /* call rpc function */
    LOGDBG("invoking the mread rpc function in client");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
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
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* margo_forward serializes all data before returning, and it's safe to
     * free the rpc params */
    margo_bulk_free(in.bulk_extents);

    /* free resources */
    margo_destroy(handle);

    return ret;
}

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
        int client_mread = (int) in.mread_id;
        LOGDBG("looking up mread[%d]", client_mread);
        client_mread_status* mread = client_get_mread_status(client_mread);
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
                    margo_instance_id mid = margo_hg_info_get_instance(hgi);
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
                        hg_size_t remain = in.bulk_size;
                        do {
                            hg_size_t offset = i * MAX_BULK_TX_SIZE;
                            hg_size_t len = remain < MAX_BULK_TX_SIZE ?
                                            remain : MAX_BULK_TX_SIZE;
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

    LOGDBG("responding");

    /* return to caller */
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
        int client_mread = (int) in.mread_id;
        LOGDBG("looking up mread[%d]", client_mread);
        client_mread_status* mread = client_get_mread_status(client_mread);
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

/* for client read request identified by mread_id and request index,
 * update request completion state according to input params */
static void unifyfs_heartbeat_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_heartbeat_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);

    /* set rpc result status */
    unifyfs_heartbeat_out_t out;
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
DEFINE_MARGO_RPC_HANDLER(unifyfs_heartbeat_rpc)