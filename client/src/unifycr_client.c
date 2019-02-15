
/*******************************************************************************************
 * unifycr_client.c - Implements the RPC handlers for the
 * application-client, shared-memory interface.
 ********************************************************************************************/

#include "unifycr-internal.h"
#include "unifycr_client.h"
#include "unifycr_clientcalls_rpc.h"
#include "unifycr_log.h"

/* add margo and argobots */
#include <abt.h>
#include <margo.h>

/* declaration should be moved to header file */
int unifycr_sync_to_del(unifycr_mount_in_t* in);

static int set_global_file_meta(unifycr_metaset_in_t* in,
                                unifycr_file_attr_t* f_meta)
{
    in->fid      = f_meta->fid;
    in->gfid     = f_meta->gfid;
    in->filename = f_meta->filename;

    /* TODO: unifycr_metaset_in_t is missing struct stat info
     * in->file_attr = f_meta->file_attr; */

    return UNIFYCR_SUCCESS;
}

static int get_global_file_meta(int gfid, unifycr_metaget_out_t* out,
                                unifycr_file_attr_t* file_meta)
{
    memset(file_meta, 0, sizeof(unifycr_file_attr_t));

    file_meta->gfid = gfid;
    strcpy(file_meta->filename, out->filename);
    file_meta->file_attr.st_size = out->st_size;

    return UNIFYCR_SUCCESS;
}

/* invokes the mount rpc function by calling unifycr_sync_to_del */
int32_t unifycr_client_mount_rpc_invoke(unifycr_client_rpc_context_t**
                                        unifycr_rpc_context)
{
    hg_handle_t handle;
    unifycr_mount_in_t in;
    unifycr_mount_out_t out;
    hg_return_t hret;

    LOGDBG("invoking the mount rpc function in client");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                        (*unifycr_rpc_context)->svr_addr,
                        (*unifycr_rpc_context)->unifycr_mount_rpc_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct by calling unifycr_sync_to_del */
    unifycr_sync_to_del(&in);
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);
    free((void*)in.external_spill_dir);
    free((void*)in.client_addr_str);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    unifycr_key_slice_range = out.max_recs_per_slice;
    LOGDBG("set unifycr_key_slice_range=%zu", unifycr_key_slice_range);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* function invokes the unmount rpc
 * TODO: Need secondary function to do actual cleanup */
int32_t unifycr_client_unmount_rpc_invoke(unifycr_client_rpc_context_t**
                                          unifycr_rpc_context)
{
    hg_handle_t handle;
    unifycr_unmount_in_t in;
    unifycr_unmount_out_t out;
    hg_return_t hret;
    int ret;

    LOGDBG("invoking the unmount rpc function in client");

    hret = margo_create((*unifycr_rpc_context)->mid,
                            (*unifycr_rpc_context)->svr_addr,
                            (*unifycr_rpc_context)->unifycr_unmount_rpc_id,
                            &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id = app_id;
    in.local_rank_idx = local_rank_idx;

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);
    ret = out.ret;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return ret;
}

/* invokes the client metaset rpc function by calling set_global_file_meta */
int32_t unifycr_client_metaset_rpc_invoke(unifycr_client_rpc_context_t**
                                          unifycr_rpc_context,
                                          unifycr_file_attr_t* f_meta)
{
    hg_handle_t handle;
    unifycr_metaset_in_t in;
    unifycr_metaset_out_t out;
    hg_return_t hret;

    LOGDBG("invoking the metaset rpc function in client");

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

    LOGDBG("Got response ret: %d", (int)out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client metaget rpc function by calling get_global_file_meta */
int32_t unifycr_client_metaget_rpc_invoke(unifycr_client_rpc_context_t**
                                          unifycr_rpc_context,
                                          int32_t gfid,
                                          unifycr_file_attr_t* file_meta)
{
    hg_handle_t handle;
    unifycr_metaget_in_t in;
    unifycr_metaget_out_t out;
    hg_return_t hret;

    LOGDBG("invoking the metaget rpc function in client");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                        (*unifycr_rpc_context)->svr_addr,
                        (*unifycr_rpc_context)->unifycr_metaget_rpc_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    in.gfid = gfid;
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    LOGDBG("Got metaget  response ret: %d", (int)out.ret);
    if (out.ret == ACK_SUCCESS) {
        /* fill in results  */
        get_global_file_meta(gfid, &out, file_meta);
    }

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client fsync rpc function */
int32_t unifycr_client_fsync_rpc_invoke(unifycr_client_rpc_context_t**
                                        unifycr_rpc_context,
                                        int32_t app_id,
                                        int32_t local_rank_idx,
                                        int32_t gfid)
{
    hg_handle_t handle;
    unifycr_fsync_in_t in;
    unifycr_fsync_out_t out;
    hg_return_t hret;

    LOGDBG("invoking the fsync rpc function in client");

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

    LOGDBG("Got response ret: %d", (int)out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client filesize rpc function */
uint32_t unifycr_client_filesize_rpc_invoke(unifycr_client_rpc_context_t**
                                            unifycr_rpc_context,
                                            int32_t app_id,
                                            int32_t local_rank_idx,
                                            int32_t gfid,
                                            hg_size_t* outsize)
{
    printf("invoking the filesize rpc function in client\n");

    /* get handle to rpc function */
    hg_handle_t handle;
    hg_return_t hret = margo_create((*unifycr_rpc_context)->mid,
                        (*unifycr_rpc_context)->svr_addr,
                        (*unifycr_rpc_context)->unifycr_filesize_rpc_id,
                        &handle);
    assert(hret == HG_SUCCESS);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    unifycr_filesize_in_t in;
    in.app_id         = app_id;
    in.local_rank_idx = local_rank_idx;
    in.gfid           = gfid;

    /* call rpc function */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);
    printf("Forwarded\n");

    /* decode response */
    unifycr_filesize_out_t out;
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* save output from function */
    printf("Got response in read, ret: %d\n", out.ret);
    uint32_t ret = (uint32_t) out.ret;
    *outsize     = (hg_size_t) out.filesize;

    /* free resources */
    margo_free_output(handle, &out);
    margo_destroy(handle);

    return ret;
}

/* invokes the client read rpc function */
int32_t unifycr_client_read_rpc_invoke(unifycr_client_rpc_context_t**
                                       unifycr_rpc_context,
                                       int32_t app_id,
                                       int32_t local_rank_idx,
                                       int32_t gfid,
                                       hg_size_t offset,
                                       hg_size_t length)
{
    hg_handle_t handle;
    unifycr_read_in_t in;
    unifycr_read_out_t out;
    hg_return_t hret;

    LOGDBG("invoking the read rpc function in client");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                        (*unifycr_rpc_context)->svr_addr,
                        (*unifycr_rpc_context)->unifycr_read_rpc_id,
                        &handle);
    assert(hret == HG_SUCCESS);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = app_id;
    in.local_rank_idx = local_rank_idx;
    in.gfid           = gfid;
    in.offset         = offset;
    in.length         = length;

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);
    LOGDBG("Forwarded");

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    LOGDBG("Got response in read, ret: %d", (int)out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client mread rpc function */
int32_t unifycr_client_mread_rpc_invoke(unifycr_client_rpc_context_t**
                                        unifycr_rpc_context,
                                        int32_t app_id,
                                        int32_t local_rank_idx,
                                        int32_t gfid,
                                        int32_t read_count,
                                        hg_size_t size,
                                        void* buffer)
{
    hg_handle_t handle;
    unifycr_mread_in_t in;
    unifycr_mread_out_t out;
    hg_return_t hret;

    LOGDBG("invoking the read rpc function in client");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                        (*unifycr_rpc_context)->svr_addr,
                        (*unifycr_rpc_context)->unifycr_mread_rpc_id,
                        &handle);
    assert(hret == HG_SUCCESS);
    hret = margo_bulk_create((*unifycr_rpc_context)->mid, 1, &buffer, &size,
                             HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = app_id;
    in.local_rank_idx = local_rank_idx;
    in.gfid           = gfid;
    in.read_count     = read_count;
    in.bulk_size      = size;

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);
    LOGDBG("Forwarded");

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    LOGDBG("Got response ret: %d", (int)out.ret);

    margo_bulk_free(in.bulk_handle);
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

