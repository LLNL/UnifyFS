
/*******************************************************************************************
 * unifycr_client.c - Implements the RPC handlers for the
 * application-client, shared-memory interface.
 ********************************************************************************************/

/* add margo and argobots */
#include <abt.h>
#include <margo.h>
#include "unifycr_client.h"
#include "unifycr_clientcalls_rpc.h"

/* declaration should be moved to header file */
int unifycr_sync_to_del(unifycr_mount_in_t* in);

static int set_global_file_meta(unifycr_metaset_in_t* in,
                                unifycr_file_attr_t* f_meta)
{
    in->fid      = f_meta->fid;
    in->gfid     = f_meta->gfid;
    in->filename = f_meta->filename;

    return UNIFYCR_SUCCESS;
}

static int get_global_file_meta(int fid, int gfid, unifycr_metaget_out_t* out,
                                unifycr_file_attr_t **file_meta)
{
    *file_meta = (unifycr_file_attr_t *)calloc(1, sizeof(unifycr_file_attr_t));
    (*file_meta)->fid = fid;
    (*file_meta)->gfid = gfid;
	strcpy( (*file_meta)->filename, out->filename);
	(*file_meta)->file_attr.st_size = out->st_size;
	
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

	unifycr_key_slice_range = out.max_recs_per_slice;
	printf("set  unifycr_key_slice_range to %ld\n",  unifycr_key_slice_range);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client metaset rpc function by calling set_global_file_meta */
uint32_t unifycr_client_metaset_rpc_invoke(unifycr_client_rpc_context_t**
                                                unifycr_rpc_context,
                                                unifycr_file_attr_t* f_meta)
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
                                                unifycr_file_attr_t** file_meta, int fid, int gfid)
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

	in.gfid = gfid;
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got metaget  response ret: %d\n", out.ret);
	if (out.ret == ACK_SUCCESS)
    	/* fill in results  */
    	get_global_file_meta(fid, gfid, &out, file_meta);

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
                                                uint64_t offset,
                                                uint64_t length)
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
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.app_id         = app_id;
    in.local_rank_idx = local_rank_idx;
    in.gfid           = gfid;
    in.offset         = offset;
    in.length         = length;

	

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);
    printf("Forwarded\n");

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response in read, ret: %d\n", out.ret);

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

/* invokes the client mread rpc function */
uint32_t unifycr_client_mread_rpc_invoke(unifycr_client_rpc_context_t**
                                                unifycr_rpc_context,
                                                uint32_t app_id,
                                                uint32_t local_rank_idx,
                                                uint32_t gfid,
                                                uint32_t read_count,
                                                hg_size_t size,
                                                void* buffer)
{
    hg_handle_t handle;
    unifycr_mread_in_t in;
    unifycr_mread_out_t out;
    hg_return_t hret;

    printf("invoking the read rpc function in client\n");

    /* fill in input struct */
    hret = margo_create((*unifycr_rpc_context)->mid,
                            (*unifycr_rpc_context)->svr_addr,
                            (*unifycr_rpc_context)->unifycr_read_rpc_id,
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
	in.bulk_size = size;
	

    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);
    printf("Forwarded\n");

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response ret: %d\n", out.ret);

	margo_bulk_free(in.bulk_handle);
    margo_free_output(handle, &out);
    margo_destroy(handle);
    return out.ret;
}

