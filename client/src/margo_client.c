/**************************************************************************
 * margo_client.c - Implements the client-server RPC calls (shared-memory)
 **************************************************************************/

#include "unifyfs-internal.h"
#include "unifyfs_rpc_util.h"
#include "margo_client.h"

/* global rpc context */
static client_rpc_context_t* client_rpc_context; // = NULL

/* register client RPCs */
static void register_client_rpcs(client_rpc_context_t* ctx)
{
    /* shorter name for our margo instance id */
    margo_instance_id mid = ctx->mid;

    ctx->rpcs.mount_id = MARGO_REGISTER(mid, "unifyfs_mount_rpc",
                       unifyfs_mount_in_t,
                       unifyfs_mount_out_t,
                       NULL);

    ctx->rpcs.unmount_id = MARGO_REGISTER(mid, "unifyfs_unmount_rpc",
                       unifyfs_unmount_in_t,
                       unifyfs_unmount_out_t,
                       NULL);

    ctx->rpcs.metaset_id = MARGO_REGISTER(mid, "unifyfs_metaset_rpc",
                       unifyfs_metaset_in_t,
                       unifyfs_metaset_out_t,
                       NULL);

    ctx->rpcs.metaget_id = MARGO_REGISTER(mid, "unifyfs_metaget_rpc",
                       unifyfs_metaget_in_t,
                       unifyfs_metaget_out_t,
                       NULL);

    ctx->rpcs.filesize_id = MARGO_REGISTER(mid, "unifyfs_filesize_rpc",
                       unifyfs_filesize_in_t,
                       unifyfs_filesize_out_t,
                       NULL);

    ctx->rpcs.truncate_id = MARGO_REGISTER(mid, "unifyfs_truncate_rpc",
                       unifyfs_truncate_in_t,
                       unifyfs_truncate_out_t,
                       NULL);

    ctx->rpcs.unlink_id = MARGO_REGISTER(mid, "unifyfs_unlink_rpc",
                       unifyfs_unlink_in_t,
                       unifyfs_unlink_out_t,
                       NULL);

    ctx->rpcs.laminate_id = MARGO_REGISTER(mid, "unifyfs_laminate_rpc",
                       unifyfs_laminate_in_t,
                       unifyfs_laminate_out_t,
                       NULL);

    ctx->rpcs.fsync_id = MARGO_REGISTER(mid, "unifyfs_fsync_rpc",
                       unifyfs_fsync_in_t,
                       unifyfs_fsync_out_t,
                       NULL);

    ctx->rpcs.read_id = MARGO_REGISTER(mid, "unifyfs_read_rpc",
                       unifyfs_read_in_t,
                       unifyfs_read_out_t,
                       NULL);

    ctx->rpcs.mread_id = MARGO_REGISTER(mid, "unifyfs_mread_rpc",
                       unifyfs_mread_in_t,
                       unifyfs_mread_out_t,
                       NULL);
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

    /* TODO: want to keep this enabled all the time */
    /* what's this do? */
    margo_diag_start(ctx->mid);

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

        /* free memory allocated for context structure,
         * and set caller's pointer to NULL */
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
    hg_handle_t handle;
    hg_return_t hret = margo_create(ctx->mid, ctx->svr_addr, id, &handle);
    assert(hret == HG_SUCCESS);

    return handle;
}

/* invokes the mount rpc function by calling unifyfs_sync_to_del */
int invoke_client_mount_rpc(void)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.mount_id);

    /* fill in input struct */
    unifyfs_mount_in_t in;
    fill_client_mount_info(&in);

    /* pass our margo address to the server */
    in.client_addr_str = strdup(client_rpc_context->client_addr_str);

    /* call rpc function */
    LOGDBG("invoking the mount rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* free memory on input struct */
    free((void*)in.external_spill_dir);
    free((void*)in.client_addr_str);

    /* decode response */
    unifyfs_mount_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* get slice size for write index key/value store */
    unifyfs_key_slice_range = out.max_recs_per_slice;
    LOGDBG("set unifyfs_key_slice_range=%zu", unifyfs_key_slice_range);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
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
    in.app_id         = app_id;
    in.local_rank_idx = local_rank_idx;

    /* call rpc function */
    LOGDBG("invoking the unmount rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_unmount_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
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
int invoke_client_metaset_rpc(int create, unifyfs_file_attr_t* f_meta)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.metaset_id);

    /* fill in input struct */
    unifyfs_metaset_in_t in;
    in.create       = (int32_t) create;
    in.gfid         = f_meta->gfid;
    in.filename     = f_meta->filename;
    in.mode         = f_meta->mode;
    in.uid          = f_meta->uid;
    in.gid          = f_meta->gid;
    in.size         = f_meta->size;
    in.atime        = f_meta->atime;
    in.mtime        = f_meta->mtime;
    in.ctime        = f_meta->ctime;
    in.is_laminated = f_meta->is_laminated;

    /* call rpc function */
    LOGDBG("invoking the metaset rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_metaset_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
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
    in.gfid = (int32_t)gfid;

    /* call rpc function */
    LOGDBG("invoking the metaget rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_metaget_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    if (ret == (int32_t)UNIFYFS_SUCCESS) {
        /* fill in results  */
        memset(file_meta, 0, sizeof(unifyfs_file_attr_t));
        strcpy(file_meta->filename, out.filename);
        file_meta->gfid  = gfid;
        file_meta->mode  = out.mode;
        file_meta->uid   = out.uid;
        file_meta->gid   = out.gid;
        file_meta->size  = out.size;
        file_meta->atime = out.atime;
        file_meta->mtime = out.mtime;
        file_meta->ctime = out.ctime;
        file_meta->is_laminated = out.is_laminated;
    }

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
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
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;

    /* call rpc function */
    LOGDBG("invoking the filesize rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_filesize_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* save output from function */
    *outsize = (size_t) out.filesize;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
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
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;
    in.filesize       = (hg_size_t)filesize;

    /* call rpc function */
    LOGDBG("invoking the truncate rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_filesize_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
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
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;

    /* call rpc function */
    LOGDBG("invoking the unlink rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_filesize_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
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
    unifyfs_unlink_in_t in;
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;

    /* call rpc function */
    LOGDBG("invoking the laminate rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_filesize_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client fsync rpc function */
int invoke_client_fsync_rpc(int gfid)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.fsync_id);

    /* fill in input struct */
    unifyfs_fsync_in_t in;
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;

    /* call rpc function */
    LOGDBG("invoking the fsync rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_fsync_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIu32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client read rpc function */
int invoke_client_read_rpc(int gfid, size_t offset, size_t length)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.read_id);

    /* fill in input struct */
    unifyfs_read_in_t in;
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.gfid           = (int32_t)gfid;
    in.offset         = (hg_size_t)offset;
    in.length         = (hg_size_t)length;

    /* call rpc function */
    LOGDBG("invoking the read rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_read_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client mread rpc function */
int invoke_client_mread_rpc(int read_count, size_t size, void* buffer)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.mread_id);

    unifyfs_mread_in_t in;
    hg_return_t hret = margo_bulk_create(
        client_rpc_context->mid, 1, &buffer, &size,
        HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = (int32_t)app_id;
    in.local_rank_idx = (int32_t)local_rank_idx;
    in.read_count     = (int32_t)read_count;
    in.bulk_size      = (hg_size_t)size;

    /* call rpc function */
    LOGDBG("invoking the read rpc function in client");
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_mread_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_bulk_free(in.bulk_handle);
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}
