/**************************************************************************
 * margo_client.c - Implements the client-server RPC calls (shared-memory)
 **************************************************************************/

#include "unifycr-internal.h"
#include "unifycr_rpc_util.h"
#include "margo_client.h"

/* global rpc context */
client_rpc_context_t* client_rpc_context; // = NULL

/* register client RPCs */
static void register_client_rpcs(void)
{
    margo_instance_id mid = client_rpc_context->mid;

    client_rpc_context->rpcs.read_id =
        MARGO_REGISTER(mid, "unifycr_read_rpc",
                       unifycr_read_in_t,
                       unifycr_read_out_t,
                       NULL);

    client_rpc_context->rpcs.mread_id =
        MARGO_REGISTER(mid, "unifycr_mread_rpc",
                       unifycr_mread_in_t,
                       unifycr_mread_out_t,
                       NULL);

    client_rpc_context->rpcs.mount_id   =
        MARGO_REGISTER(mid, "unifycr_mount_rpc",
                       unifycr_mount_in_t,
                       unifycr_mount_out_t,
                       NULL);

    client_rpc_context->rpcs.unmount_id   =
        MARGO_REGISTER(mid, "unifycr_unmount_rpc",
                       unifycr_unmount_in_t,
                       unifycr_unmount_out_t,
                       NULL);

    client_rpc_context->rpcs.metaget_id =
        MARGO_REGISTER(mid, "unifycr_metaget_rpc",
                       unifycr_metaget_in_t, unifycr_metaget_out_t,
                       NULL);

    client_rpc_context->rpcs.metaset_id =
        MARGO_REGISTER(mid, "unifycr_metaset_rpc",
                       unifycr_metaset_in_t, unifycr_metaset_out_t,
                       NULL);

    client_rpc_context->rpcs.fsync_id =
        MARGO_REGISTER(mid, "unifycr_fsync_rpc",
                       unifycr_fsync_in_t, unifycr_fsync_out_t,
                       NULL);

    client_rpc_context->rpcs.filesize_id =
        MARGO_REGISTER(mid, "unifycr_filesize_rpc",
                       unifycr_filesize_in_t,
                       unifycr_filesize_out_t,
                       NULL);
}

/* initialize margo client-server rpc */
int unifycr_client_rpc_init(void)
{
    /* initialize margo */
    hg_return_t hret;
    char addr_self_string[128];
    hg_size_t addr_self_string_sz = sizeof(addr_self_string);
    client_rpc_context_t* rpc_ctx;

    rpc_ctx = calloc(1, sizeof(client_rpc_context_t));
    if (NULL == rpc_ctx) {
        LOGERR("Failed to allocate client RPC context");
        return UNIFYCR_FAILURE;
    }

    /* initialize margo */
    char* svr_addr_string = rpc_lookup_local_server_addr();
    if (svr_addr_string == NULL) {
        LOGERR("Failed to find local margo RPC server address");
        return UNIFYCR_FAILURE;
    }
    char* proto = strdup(svr_addr_string);
    char* colon = strchr(proto, ':');
    if (NULL != colon) {
        *colon = '\0';
    }
    LOGDBG("svr_addr:'%s' proto:'%s'", svr_addr_string, proto);

    rpc_ctx->mid = margo_init(proto, MARGO_SERVER_MODE, 1, 1);
    assert(rpc_ctx->mid);
    free(proto);
    margo_diag_start(rpc_ctx->mid);

    /* get server margo address */
    rpc_ctx->svr_addr = HG_ADDR_NULL;
    margo_addr_lookup(rpc_ctx->mid, svr_addr_string,
                      &(rpc_ctx->svr_addr));
    free(svr_addr_string);
    if (rpc_ctx->svr_addr == HG_ADDR_NULL) {
        LOGERR("Failed to resolve margo server RPC address");
        free(rpc_ctx);
        return UNIFYCR_FAILURE;
    }

    /* get client margo address */
    hret = margo_addr_self(rpc_ctx->mid, &(rpc_ctx->client_addr));
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_self()");
        margo_finalize(rpc_ctx->mid);
        free(rpc_ctx);
        return UNIFYCR_FAILURE;
    }

    hret = margo_addr_to_string(rpc_ctx->mid, addr_self_string,
                                &addr_self_string_sz, rpc_ctx->client_addr);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_to_string()");
        margo_finalize(rpc_ctx->mid);
        free(rpc_ctx);
        return UNIFYCR_FAILURE;
    }
    rpc_ctx->client_addr_str = strdup(addr_self_string);

    client_rpc_context = rpc_ctx;
    register_client_rpcs();

    return UNIFYCR_SUCCESS;
}

/* free resources allocated in corresponding call
 * to unifycr_client_rpc_init, frees structure
 * allocated and sets pcontect to NULL */
int unifycr_client_rpc_finalize(void)
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

        /* free memory allocated for context structure,
         * and set caller's pointer to NULL */
        free(ctx->client_addr_str);
        free(ctx);
    }
    return UNIFYCR_SUCCESS;
}

/* invokes the mount rpc function by calling unifycr_sync_to_del */
int invoke_client_mount_rpc(void)
{
    hg_handle_t handle;
    unifycr_mount_in_t in;
    unifycr_mount_out_t out;
    hg_return_t hret;
    int32_t ret;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    hret = margo_create(client_rpc_context->mid,
                        client_rpc_context->svr_addr,
                        client_rpc_context->rpcs.mount_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    fill_client_mount_info(&in);
    in.client_addr_str = strdup(client_rpc_context->client_addr_str);

    LOGDBG("invoking the mount rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);
    free((void*)in.external_spill_dir);
    free((void*)in.client_addr_str);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    unifycr_key_slice_range = out.max_recs_per_slice;
    LOGDBG("set unifycr_key_slice_range=%zu", unifycr_key_slice_range);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return ret;
}

/* function invokes the unmount rpc */
int invoke_client_unmount_rpc(void)
{
    hg_handle_t handle;
    unifycr_unmount_in_t in;
    unifycr_unmount_out_t out;
    hg_return_t hret;
    int32_t ret;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    hret = margo_create(client_rpc_context->mid,
                        client_rpc_context->svr_addr,
                        client_rpc_context->rpcs.unmount_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id = app_id;
    in.local_rank_idx = local_rank_idx;

    LOGDBG("invoking the unmount rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client metaset rpc function */
int invoke_client_metaset_rpc(unifycr_file_attr_t* f_meta)
{
    hg_handle_t handle;
    unifycr_metaset_in_t in;
    unifycr_metaset_out_t out;
    hg_return_t hret;
    int32_t ret;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    hret = margo_create(client_rpc_context->mid,
                        client_rpc_context->svr_addr,
                        client_rpc_context->rpcs.metaset_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.fid      = f_meta->fid;
    in.gfid     = f_meta->gfid;
    in.filename = f_meta->filename;
    in.mode     = f_meta->mode;
    in.uid      = f_meta->uid;
    in.gid      = f_meta->gid;
    in.size     = f_meta->size;
    in.atime    = f_meta->atime;
    in.mtime    = f_meta->mtime;
    in.ctime    = f_meta->ctime;

    LOGDBG("invoking the metaset rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client metaget rpc function */
int invoke_client_metaget_rpc(int gfid,
                              unifycr_file_attr_t* file_meta)
{
    hg_handle_t handle;
    unifycr_metaget_in_t in;
    unifycr_metaget_out_t out;
    hg_return_t hret;
    int32_t ret;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    hret = margo_create(client_rpc_context->mid,
                        client_rpc_context->svr_addr,
                        client_rpc_context->rpcs.metaget_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.gfid = (int32_t)gfid;
    LOGDBG("invoking the metaget rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    if (ret == (int32_t)UNIFYCR_SUCCESS) {
        /* fill in results  */
        memset(file_meta, 0, sizeof(unifycr_file_attr_t));
        strcpy(file_meta->filename, out.filename);
        file_meta->gfid  = gfid;
        file_meta->mode  = out.mode;
        file_meta->uid   = out.uid;
        file_meta->gid   = out.gid;
        file_meta->size  = out.size;
        file_meta->atime = out.atime;
        file_meta->mtime = out.mtime;
        file_meta->ctime = out.ctime;
    }

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client fsync rpc function */
int invoke_client_fsync_rpc(int gfid)
{
    hg_handle_t handle;
    unifycr_fsync_in_t in;
    unifycr_fsync_out_t out;
    hg_return_t hret;
    int32_t ret;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    hret = margo_create(client_rpc_context->mid,
                        client_rpc_context->svr_addr,
                        client_rpc_context->rpcs.fsync_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;

    LOGDBG("invoking the fsync rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client filesize rpc function */
int invoke_client_filesize_rpc(int gfid,
                               size_t* outsize)
{
    int32_t ret;
    hg_handle_t handle;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    /* get handle to rpc function */
    hg_return_t hret = margo_create(client_rpc_context->mid,
                                    client_rpc_context->svr_addr,
                                    client_rpc_context->rpcs.filesize_id,
                                    &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    unifycr_filesize_in_t in;
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;

    /* call rpc function */
    LOGDBG("invoking the filesize rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifycr_filesize_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIu32, ret);

    /* save output from function */
    *outsize = (size_t) out.filesize;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client read rpc function */
int invoke_client_read_rpc(int gfid,
                           size_t offset,
                           size_t length)
{
    hg_handle_t handle;
    unifycr_read_in_t in;
    unifycr_read_out_t out;
    hg_return_t hret;
    int32_t ret;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    /* fill in input struct */
    hret = margo_create(client_rpc_context->mid,
                        client_rpc_context->svr_addr,
                        client_rpc_context->rpcs.read_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;
    in.offset         = (hg_size_t)offset;
    in.length         = (hg_size_t)length;

    LOGDBG("invoking the read rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client mread rpc function */
int invoke_client_mread_rpc(int read_count,
                            size_t size,
                            void* buffer)
{
    hg_handle_t handle;
    unifycr_mread_in_t in;
    unifycr_mread_out_t out;
    hg_return_t hret;
    int32_t ret;

    if (NULL == client_rpc_context) {
        return UNIFYCR_FAILURE;
    }

    /* fill in input struct */
    hret = margo_create(client_rpc_context->mid,
                        client_rpc_context->svr_addr,
                        client_rpc_context->rpcs.mread_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    hret = margo_bulk_create(client_rpc_context->mid, 1, &buffer, &size,
                             HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.read_count     = (int32_t)read_count;
    in.bulk_size      = (hg_size_t)size;

    LOGDBG("invoking the read rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    margo_bulk_free(in.bulk_handle);
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}
