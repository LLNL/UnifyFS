
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

int unifycr_sync_to_del(unifycr_mount_in_t* in);
#if 0
{
    int num_procs_per_node = local_rank_cnt;
    int req_buf_sz = shm_req_size;
    int recv_buf_sz = shm_recv_size;
    long superblock_sz = glb_superblock_size;
    long meta_offset = (void *)unifycr_indices.ptr_num_entries -
        unifycr_superblock;
    long meta_size = unifycr_max_index_entries * sizeof(unifycr_index_t);
    long fmeta_offset = (void *)unifycr_fattrs.ptr_num_entries -
        unifycr_superblock;
    long fmeta_size = unifycr_max_fattr_entries * sizeof(unifycr_fattr_t);
    long data_offset = (void *)unifycr_chunks - unifycr_superblock;
    long data_size = (long)unifycr_max_chunks * unifycr_chunk_size;
    char external_spill_dir[UNIFYCR_MAX_FILENAME] = {0};

    strcpy(external_spill_dir, external_data_dir);

/*
 *
 * Copy the client-side information to the
 * input struct
*/
    in->app_id             = app_id;
    in->local_rank_idx     = local_rank_idx;
    in->dbg_rank           = dbg_rank;
    in->num_procs_per_node = num_procs_per_node;
    in->req_buf_sz         = req_buf_sz;
    in->recv_buf_sz        = recv_buf_sz;
    in->superblock_sz      = superblock_sz;
    in->meta_offset        = meta_offset;
    in->meta_size          = meta_size;
    in->fmeta_offset       = fmeta_offset;
    in->fmeta_size         = fmeta_size;
    in->data_offset        = data_offset;
    in->data_size          = data_size;
    in->external_spill_dir = external_spill_dir;
    return 0;
}
#endif

static int set_global_file_meta(unifycr_metaset_in_t* in,
                                unifycr_fattr_t* f_meta)
{
    in->fid      = f_meta->fid;
    in->gfid     = f_meta->gfid;
    in->filename = f_meta->filename;

    return UNIFYCR_SUCCESS;
}

static int get_global_file_meta(unifycr_metaget_in_t* in, unifycr_fattr_t **file_meta)
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
	printf("rpc external_spill_dir: [%s]\n", in.external_spill_dir);
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
                                                unifycr_fattr_t** file_meta, int gfid)
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
    	/* fill in input struct by calling unifycr_sync_to_del */
    	get_global_file_meta(&in, file_meta);

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

