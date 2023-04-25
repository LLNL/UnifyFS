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

#include "margo_server.h"
#include "unifyfs_inode_tree.h"
#include "unifyfs_inode.h"
#include "unifyfs_group_rpc.h"
#include "unifyfs_p2p_rpc.h"
#include "unifyfs_request_manager.h"


static
int rpc_init(unifyfs_cfg_t* cfg)
{
    int ret = UNIFYFS_SUCCESS;

    LOGDBG("initializing RPC file operations..");

    return ret;
}

static
int rpc_metaget(unifyfs_fops_ctx_t* ctx,
                int gfid,
                unifyfs_file_attr_t* attr)
{
    if (gfid == ctx->app_id) {
        /* should always have a local copy of mountpoint attrs */
        return sm_get_fileattr(gfid, attr);
    }
    return unifyfs_invoke_metaget_rpc(gfid, attr);
}

static
int rpc_metaset(unifyfs_fops_ctx_t* ctx,
                int gfid,
                int attr_op,
                unifyfs_file_attr_t* attr)
{
    return unifyfs_invoke_metaset_rpc(gfid, attr_op, attr);
}

/*
 * sync rpc from client contains extents for a single gfid (file).
 */
static
int rpc_fsync(unifyfs_fops_ctx_t* ctx,
              int gfid,
              client_rpc_req_t* client_req)
{
    size_t i;

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* get application client */
    app_client* client = get_app_client(ctx->app_id, ctx->client_id);
    if (NULL == client) {
        return EINVAL;
    }

    /* indices are stored in the superblock shared memory
     * created by the client, these are stored as index_t
     * structs starting one page size offset into meta region
     *
     * Is it safe to assume that the index information in this superblock is
     * not going to be modified by the client while we perform this operation?
     */

    /* get number of file extent index values client has for us,
     * stored as a size_t value in index region of shared memory */
    size_t num_extents = *(client->state.write_index.ptr_num_entries);

    if (num_extents == 0) {
        return UNIFYFS_SUCCESS;  /* Nothing to do */
    }

    unifyfs_index_t* index_entry = client->state.write_index.index_entries;

    /* the sync rpc now contains extents from a single file/gfid */
    assert(gfid == index_entry[0].gfid);

    server_rpc_req_t* svr_req = malloc(sizeof(*svr_req));
    int* pending_gfid = malloc(sizeof(int));
    extent_metadata* extents = calloc(num_extents, sizeof(*extents));
    if ((NULL == svr_req) || (NULL == pending_gfid) || (NULL == extents)) {
        LOGERR("failed to allocate memory for local extents sync");
        return ENOMEM;
    }

    for (i = 0; i < num_extents; i++) {
        unifyfs_index_t* meta = index_entry + i;
        extent_metadata* extent = extents + i;
        extent->start    = meta->file_pos;
        extent->end      = (meta->file_pos + meta->length) - 1;
        extent->svr_rank = glb_pmi_rank;
        extent->app_id   = ctx->app_id;
        extent->cli_id   = ctx->client_id;
        extent->log_pos  = meta->log_pos;
    }

    /* update local inode state first */
    ret = unifyfs_inode_add_pending_extents(gfid, client_req,
                                            num_extents, extents);
    if (ret) {
        LOGERR("failed to add pending local extents (gfid=%d, ret=%d)",
               gfid, ret);
        free(pending_gfid);
        free(svr_req);
        return ret;
    } else {
        /* then ask svcmgr to process the pending extent sync(s) */
        *pending_gfid = gfid;
        svr_req->req_type = UNIFYFS_SERVER_PENDING_SYNC;
        svr_req->handle   = HG_HANDLE_NULL;
        svr_req->input    = (void*) pending_gfid;
        svr_req->bulk_buf = NULL;
        svr_req->bulk_sz  = 0;
        ret = sm_submit_service_request(svr_req);
    }

    return ret;
}

static
int rpc_filesize(unifyfs_fops_ctx_t* ctx,
                 int gfid,
                 size_t* filesize)
{
    return unifyfs_invoke_filesize_rpc(gfid, filesize);
}

static
int rpc_transfer(unifyfs_fops_ctx_t* ctx,
                 int transfer_id,
                 int gfid,
                 int transfer_mode,
                 const char* dest_file)
{
    if (SERVER_TRANSFER_MODE_OWNER == transfer_mode) {
        return unifyfs_invoke_transfer_rpc(ctx->app_id, ctx->client_id,
                                           transfer_id, gfid, transfer_mode,
                                           dest_file);
    } else if (SERVER_TRANSFER_MODE_LOCAL == transfer_mode) {
        return unifyfs_invoke_broadcast_transfer(ctx->app_id, ctx->client_id,
                                                 transfer_id, gfid,
                                                 transfer_mode, dest_file);
    } else {
        LOGERR("invalid transfer mode=%d");
        return EINVAL;
    }

}

static
int rpc_truncate(unifyfs_fops_ctx_t* ctx,
                 int gfid,
                 off_t len)
{
    return unifyfs_invoke_truncate_rpc(gfid, len);
}

static
int rpc_laminate(unifyfs_fops_ctx_t* ctx,
                 int gfid)
{
    return unifyfs_invoke_laminate_rpc(gfid);
}

static
int rpc_unlink(unifyfs_fops_ctx_t* ctx,
               int gfid)
{
    int ret = unifyfs_inode_unlink(gfid);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unlink(gfid=%d) failed", gfid);
    }
    return unifyfs_invoke_broadcast_unlink(gfid);
}

static
int create_remote_read_requests(unsigned int n_chunks,
                                chunk_read_req_t* chunks,
                                unsigned int* outlen,
                                server_chunk_reads_t** out)
{
    int prev_rank = -1;
    unsigned int num_server_reads = 0;
    unsigned int i = 0;
    server_chunk_reads_t* remote_reads = NULL;
    server_chunk_reads_t* current = NULL;
    chunk_read_req_t* pos = NULL;

    /* count how many servers we need to contact */
    for (i = 0; i < n_chunks; i++) {
        chunk_read_req_t* curr_chunk = &chunks[i];
        int curr_rank = curr_chunk->rank;
        if (curr_rank != prev_rank) {
            num_server_reads++;
        }
        prev_rank = curr_rank;
    }

    /* allocate and fill the per-server request data structure */
    remote_reads = (server_chunk_reads_t*) calloc(num_server_reads,
                                                  sizeof(*remote_reads));
    if (!remote_reads) {
        LOGERR("failed to allocate memory for remote_reads");
        return ENOMEM;
    }

    pos = chunks;
    unsigned int processed = 0;

    LOGDBG("preparing remote read request for %u chunks (%d servers)",
           n_chunks, num_server_reads);

    for (i = 0; i < num_server_reads; i++) {
        int rank = pos->rank;

        current = &remote_reads[i];
        current->rank = rank;
        current->reqs = pos;

        for ( ; processed < n_chunks; pos++) {
            if (pos->rank != rank) {
                break;
            }
            current->total_sz += pos->nbytes;
            current->num_chunks++;
            processed++;
        }

        LOGDBG("%u/%u chunks processed: server %d (%u chunks, %zu bytes)",
               processed, n_chunks, rank,
               current->num_chunks, current->total_sz);
    }

    *outlen = num_server_reads;
    *out = remote_reads;
    return UNIFYFS_SUCCESS;
}

static
int submit_read_request(unifyfs_fops_ctx_t* ctx,
                        unsigned int count,
                        unifyfs_extent_t* extents)
{
    if ((count == 0) || (NULL == extents)) {
        return EINVAL;
    }

    LOGDBG("handling read request (%u extents)", count);

    /* see if we have a valid app information */
    int app_id = ctx->app_id;
    int client_id = ctx->client_id;
    int client_mread = ctx->mread_id;

    /* get application client */
    app_client* client = get_app_client(app_id, client_id);
    if (NULL == client) {
        return UNIFYFS_FAILURE;
    }

    /* TODO: when multiple extents from the same file are requested, it would
     *       be nice to fetch all the chunks in one call to find_extents rpc.
     *       this is difficult now because the returned chunks are not
     *       necessarily in the same order as the requested extents */

    int ret = UNIFYFS_SUCCESS;
    unsigned int extent_ndx = 0;
    for ( ; extent_ndx < count; extent_ndx++) {
        unifyfs_extent_t* ext = extents + extent_ndx;
        unsigned int n_chunks = 0;
        chunk_read_req_t* chunks = NULL;
        int rc = unifyfs_invoke_find_extents_rpc(ext->gfid, 1, ext,
                                                 &n_chunks, &chunks);
        if (rc) {
            LOGERR("failed to find extent locations");
            return rc;
        }
        if (n_chunks > 0) {
            /* prepare the remote read requests */
            unsigned int n_remote_reads = 0;
            server_chunk_reads_t* remote_reads = NULL;
            rc = create_remote_read_requests(n_chunks, chunks,
                                             &n_remote_reads, &remote_reads);
            if (rc) {
                LOGERR("failed to prepare the remote read requests");
                if (NULL != chunks) {
                    free(chunks);
                }
                return rc;
            }

            /* fill the information of server_read_req_t and submit */
            server_read_req_t rdreq = { 0, };
            rdreq.app_id           = app_id;
            rdreq.client_id        = client_id;
            rdreq.client_mread     = client_mread;
            rdreq.client_read_ndx  = extent_ndx;
            rdreq.chunks           = chunks;
            rdreq.num_server_reads = (int) n_remote_reads;
            rdreq.remote_reads     = remote_reads;
            rdreq.extent           = *ext;
            ret = rm_submit_read_request(&rdreq);
        } else {
            LOGDBG("extent(gfid=%d, offset=%lu, len=%lu) has no data",
                   ext->gfid, ext->offset, ext->length);
            invoke_client_mread_req_complete_rpc(app_id, client_id,
                                                 client_mread, extent_ndx,
                                                 ENODATA);
        }
    }

    return ret;
}

static
int rpc_read(unifyfs_fops_ctx_t* ctx,
             int gfid,
             off_t offset,
             size_t length)
{
    unifyfs_extent_t extent = { 0 };
    extent.gfid = gfid;
    extent.offset = (unsigned long) offset;
    extent.length = (unsigned long) length;

    return submit_read_request(ctx, 1, &extent);
}

static
int rpc_mread(unifyfs_fops_ctx_t* ctx,
              size_t n_req,
              void* read_reqs)
{
    unsigned int count = (unsigned int) n_req;
    unifyfs_extent_t* extents = (unifyfs_extent_t*) read_reqs;
    return submit_read_request(ctx, count, extents);
}

static
int rpc_get_gfids(
    int** gfid_list,
    int* num_gfids)
{
    return unifyfs_get_gfids(num_gfids, gfid_list);
}

static struct unifyfs_fops _fops_rpc = {
    .name      = "rpc",
    .init      = rpc_init,
    .filesize  = rpc_filesize,
    .fsync     = rpc_fsync,
    .get_gfids = rpc_get_gfids,
    .laminate  = rpc_laminate,
    .metaget   = rpc_metaget,
    .metaset   = rpc_metaset,
    .mread     = rpc_mread,
    .read      = rpc_read,
    .transfer  = rpc_transfer,
    .truncate  = rpc_truncate,
    .unlink    = rpc_unlink
};

struct unifyfs_fops* unifyfs_fops_impl = &_fops_rpc;
