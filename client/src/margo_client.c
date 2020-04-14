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

    hg_id_t hgid;

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
    CLIENT_REGISTER_RPC(truncate);
    CLIENT_REGISTER_RPC(unlink);
    CLIENT_REGISTER_RPC(laminate);
    CLIENT_REGISTER_RPC(sync);
    CLIENT_REGISTER_RPC(read);
    CLIENT_REGISTER_RPC(mread);

#undef CLIENT_REGISTER_RPC
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
    hg_handle_t handle;
    hg_return_t hret = margo_create(ctx->mid, ctx->svr_addr, id, &handle);
    assert(hret == HG_SUCCESS);

    return handle;
}

/* invokes the attach rpc function */
int invoke_client_attach_rpc(void)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.attach_id);

    /* fill in input struct */
    unifyfs_attach_in_t in;
    fill_client_attach_info(&in);

    /* call rpc function */
    LOGDBG("invoking the attach rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_attach_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free memory on input struct */
    if (NULL != in.logio_spill_dir) {
        free((void*)in.logio_spill_dir);
    }

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the mount rpc function */
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
    free((void*)in.mount_prefix);
    free((void*)in.client_addr_str);

    /* decode response */
    unifyfs_mount_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* get assigned client id, and verify app_id */
    unifyfs_client_id = (int) out.client_id;
    int srvr_app_id = (int) out.app_id;
    if (unifyfs_app_id != srvr_app_id) {
        LOGWARN("mismatch on app_id - using %d, server returned %d",
                unifyfs_app_id, srvr_app_id);
    }
    LOGDBG("My client id is %d", unifyfs_client_id);

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
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;

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
    in.attr         = *f_meta;

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
        *file_meta = out.attr;
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
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;

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
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;
    in.filesize  = (hg_size_t) filesize;

    /* call rpc function */
    LOGDBG("invoking the truncate rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_truncate_out_t out;
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
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the unlink rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_unlink_out_t out;
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
    unifyfs_laminate_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;

    /* call rpc function */
    LOGDBG("invoking the laminate rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_laminate_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    int32_t ret = out.ret;
    LOGDBG("Got response ret=%" PRIi32, ret);

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return (int)ret;
}

/* invokes the client sync rpc function */
int invoke_client_sync_rpc(void)
{
    /* check that we have initialized margo */
    if (NULL == client_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* get handle to rpc function */
    hg_handle_t handle = create_handle(client_rpc_context->rpcs.sync_id);

    /* fill in input struct */
    unifyfs_sync_in_t in;
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;

    /* call rpc function */
    LOGDBG("invoking the sync rpc function in client");
    hg_return_t hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    unifyfs_sync_out_t out;
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
    in.app_id    = (int32_t) unifyfs_app_id;
    in.client_id = (int32_t) unifyfs_client_id;
    in.gfid      = (int32_t) gfid;
    in.offset    = (hg_size_t) offset;
    in.length    = (hg_size_t) length;

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
    in.app_id     = (int32_t) unifyfs_app_id;
    in.client_id  = (int32_t) unifyfs_client_id;
    in.read_count = (int32_t) read_count;
    in.bulk_size  = (hg_size_t) size;

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
