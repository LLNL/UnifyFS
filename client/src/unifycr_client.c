/*******************************************************************************************
 * unifycr_client.c - Implements the RPC handlers for the
 * application-client, shared-memory interface.
 ********************************************************************************************/

/* add margo and argobots */
#include <abt.h>
#include <abt-snoozer.h>
#include <margo.h>
#include "unifycr_client.h"
#include "unifycr_clientcalls_rpc.h"

/*
 * send global file metadata to the delegator,
 * which puts it to the key-value store
 * @param gfid: global file id
 * @return: error code
 * */
int set_global_file_meta(unifycr_metaset_in_t* in,
                                unifycr_fattr_t* f_meta)
{
    in->gfid      = f_meta->gfid;
    in->filename = f_meta->filename;

    return UNIFYCR_SUCCESS;
}

/*
 * get global file metadata from the delegator,
 * which retrieves the data from key-value store
 * @param gfid: global file id
 * @return: error code
 * @return: file_meta that point to the structure of
 * the retrieved metadata
 * */
int get_global_file_meta(unifycr_metaget_in_t* in, unifycr_fattr_t **file_meta)
{
    *file_meta = (unifycr_fattr_t *)malloc(sizeof(unifycr_fattr_t));
    in->gfid     = (*file_meta)->gfid;
    return UNIFYCR_SUCCESS;
}

/* invokes the mount rpc function by calling unifycr_sync_to_del */
uint32_t unifycr_client_mount_rpc_invoke(unifycr_client_rpc_context_t** unifycr_rpc_context)
{
    hg_handle_t handle;
    unifycr_mount_in_t in;
    unifycr_mount_out_t out;
    hg_return_t hret;

    printf("invoking the mount rpc function in client\n");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                            (*unifycr_rpc_context)->svr_addr,
                            (*unifycr_rpc_context)->unifycr_mount_rpc_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct by calling unifycr_sync_to_del */
    unifycr_sync_to_del(&in);

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response ret: %d\n", out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client metaset rpc function by calling set_global_file_meta */
uint32_t unifycr_client_metaset_rpc_invoke(unifycr_client_rpc_context_t**
                                                unifycr_rpc_context,
                                                unifycr_fattr_t* f_meta)
{
    hg_handle_t handle;
    unifycr_metaset_in_t in;
    unifycr_metaset_out_t out;
    hg_return_t hret;

    printf("invoking the metaset rpc function in client\n");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                            (*unifycr_rpc_context)->svr_addr,
                            (*unifycr_rpc_context)->unifycr_metaset_rpc_id,
                            &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct by calling unifycr_sync_to_del */
    set_global_file_meta(&in, f_meta);

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response ret: %d\n", out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client metaget rpc function by calling get_global_file_meta */
uint32_t unifycr_client_metaget_rpc_invoke(unifycr_client_rpc_context_t**
                                                unifycr_rpc_context,
                                                unifycr_fattr_t** file_meta)
{
    hg_handle_t handle;
    unifycr_metaget_in_t in;
    unifycr_metaget_out_t out;
    hg_return_t hret;

    printf("invoking the metaget rpc function in client\n");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                            (*unifycr_rpc_context)->svr_addr,
                            (*unifycr_rpc_context)->unifycr_metaget_rpc_id,
                            &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct by calling unifycr_sync_to_del */
    get_global_file_meta(&in, file_meta);

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response ret: %d\n", out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client fsync rpc function */
uint32_t unifycr_client_fsync_rpc_invoke(unifycr_client_rpc_context_t**
                                                unifycr_rpc_context,
                                                uint32_t app_id,
                                                uint32_t local_rank_idx,
                                                uint32_t gfid)
{
    hg_handle_t handle;
    unifycr_fsync_in_t in;
    unifycr_fsync_out_t out;
    hg_return_t hret;

    printf("invoking the fsync rpc function in client\n");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                            (*unifycr_rpc_context)->svr_addr,
                            (*unifycr_rpc_context)->unifycr_fsync_rpc_id,
                            &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = app_id;
    in.local_rank_idx = local_rank_idx;
    in.gfid           = gfid;

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response ret: %d\n", out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client read rpc function */
uint32_t unifycr_client_read_rpc_invoke(unifycr_client_rpc_context_t**
                                                unifycr_rpc_context,
                                                uint32_t app_id,
                                                uint32_t local_rank_idx,
                                                uint32_t gfid,
                                                uint32_t read_count)
{
    hg_handle_t handle;
    unifycr_read_in_t in;
    unifycr_read_out_t out;
    hg_return_t hret;

    printf("invoking the read rpc function in client\n");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                            (*unifycr_rpc_context)->svr_addr,
                            (*unifycr_rpc_context)->unifycr_read_rpc_id,
                            &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = app_id;
    in.local_rank_idx = local_rank_idx;
    in.gfid           = gfid;
    in.read_count     = read_count;

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response ret: %d\n", out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}


