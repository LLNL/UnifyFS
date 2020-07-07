/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "unifyfs_global.h"
#include "unifyfs_inode_tree.h"
#include "unifyfs_inode.h"
#include "unifyfs_group_rpc.h"
#include "unifyfs_metadata_mdhim.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_fops.h"

struct unifyfs_inode_tree _global_inode_tree;
struct unifyfs_inode_tree* global_inode_tree = &_global_inode_tree;

static int rpc_init(unifyfs_cfg_t* cfg)
{
    int ret = 0;
    long range_sz = 0;

    LOGDBG("initializing file operations..");

    ret = unifyfs_inode_tree_init(global_inode_tree);
    if (ret) {
        LOGERR("failed to initialize the inode tree (ret=%d)", ret);
    }

    ret = configurator_int_val(cfg->meta_range_size, &range_sz);
    if (ret != 0) {
        LOGERR("failed to read configuration (meta_range_size)");
    }
    meta_slice_sz = (size_t) range_sz;

    return ret;
}

static int rpc_metaget(unifyfs_fops_ctx_t* ctx,
                              int gfid, unifyfs_file_attr_t* attr)
{
    return unifyfs_inode_metaget(gfid, attr);
}

static int rpc_metaset(unifyfs_fops_ctx_t* ctx,
                              int gfid, int create, unifyfs_file_attr_t* attr)
{
    return unifyfs_invoke_metaset_rpc(gfid, create, attr);
}

/*
 * sync rpc from client contains extents for a single gfid (file).
 */
static int rpc_sync(unifyfs_fops_ctx_t* ctx)
{
    size_t i;

    /* assume we'll succeed */
    int ret = (int)UNIFYFS_SUCCESS;

    /* get memory page size on this machine */
    int page_sz = getpagesize();

    /* get application client */
    app_client* client = get_app_client(ctx->app_id, ctx->client_id);
    if (NULL == client) {
        return EINVAL;
    }

    /* get pointer to superblock for this client and app */
    shm_context* super_ctx = client->shmem_super;
    if (NULL == super_ctx) {
        LOGERR("missing client superblock");
        return EIO;
    }
    char* superblk = (char*)(super_ctx->addr);

    /* get pointer to start of key/value region in superblock */
    char* meta = superblk + client->super_meta_offset;

    /* get number of file extent index values client has for us,
     * stored as a size_t value in meta region of shared memory */
    size_t extent_num_entries = *(size_t*)(meta);

    /* indices are stored in the superblock shared memory
     * created by the client, these are stored as index_t
     * structs starting one page size offset into meta region
     *
     * Is it safe to assume that the index information in this superblock is
     * not going to be modified by the client while we perform this operation?
     */
    char* ptr_extents = meta + page_sz;

    if (extent_num_entries == 0) {
        return UNIFYFS_SUCCESS;  /* Nothing to do */
    }

    unifyfs_index_t* meta_payload = (unifyfs_index_t*)(ptr_extents);

    struct extent_tree_node* extents = calloc(extent_num_entries,
                                              sizeof(*extents));
    if (!extents) {
        LOGERR("failed to allocate memory for local_extents");
        return ENOMEM;
    }

    /* the sync rpc now contains extents from a single file/gfid */
    int gfid = meta_payload[0].gfid;

    for (i = 0; i < extent_num_entries; i++) {
        struct extent_tree_node* tmp = &extents[i];
        unifyfs_index_t* meta = &meta_payload[i];

        tmp->start = meta->file_pos;
        tmp->end = (meta->file_pos + meta->length) - 1;
        tmp->svr_rank = glb_pmi_rank;
        tmp->app_id = ctx->app_id;
        tmp->cli_id = ctx->client_id;
        tmp->pos = meta->log_pos;
    }

    ret = unifyfs_inode_add_extents(gfid, extent_num_entries, extents);
    if (ret) {
        LOGERR("failed to add extents (gfid=%d, ret=%d)", gfid, ret);
        return ret;
    }

    ret = unifyfs_invoke_broadcast_extents_rpc(gfid, extent_num_entries,
                                               extents);
    if (ret) {
        LOGERR("failed to broadcast extents (gfid=%d, ret=%d)", gfid, ret);
    }

    return ret;
}

static int rpc_fsync(unifyfs_fops_ctx_t* ctx, int gfid)
{
    return rpc_sync(ctx);
}

static int rpc_filesize(unifyfs_fops_ctx_t* ctx,
                               int gfid, size_t* filesize)
{
    return unifyfs_invoke_filesize_rpc(gfid, filesize);
}

static int rpc_truncate(unifyfs_fops_ctx_t* ctx, int gfid, off_t len)
{
    return unifyfs_invoke_truncate_rpc(gfid, len);
}

static int rpc_laminate(unifyfs_fops_ctx_t* ctx, int gfid)
{
    return unifyfs_invoke_laminate_rpc(gfid);
}

static int rpc_unlink(unifyfs_fops_ctx_t* ctx, int gfid)
{
    return unifyfs_invoke_unlink_rpc(gfid);
}

static int compare_chunks(const void* _c1, const void* _c2)
{
    chunk_read_req_t* c1 = (chunk_read_req_t*) _c1;
    chunk_read_req_t* c2 = (chunk_read_req_t*) _c2;

    if (c1->rank > c2->rank) {
        return 1;
    } else if (c1->rank < c2->rank) {
        return -1;
    } else {
        return 0;
    }
}

static int rpc_read(unifyfs_fops_ctx_t* ctx,
                    int gfid, off_t offset, size_t length)
{
    int ret = 0;
    int i;
    int prev_rank = -1;
    int app_id = ctx->app_id;
    int client_id = ctx->client_id;
    size_t data_sz = 0;
    client_read_req_t* req = NULL;
    unsigned int n_chunks = 0;
    chunk_read_req_t* chunks = NULL;
    int num_remote_reads = 0;
    remote_chunk_reads_t* remote_reads = NULL;
    server_read_req_t rdreq = { 0, };

    /*
     * we now use the inode structure instead of mdhim and can skip all
     * key-value data structures. Basically:
     *
     * 1) get the list of extents (@chunks) from the inode extent tree
     * 2) sort the @chunks according to the server ranks
     * 3) fill the server_read_req_t
     * 4) hand over the server_read_req_t to the delegator thread
     */

    /* 1) get the list of extents (@chunks) from the inode extent tree */
    ret = unifyfs_inode_get_chunk_list(gfid, offset, length,
                                        &n_chunks, &chunks);
    if (ret) {
        LOGERR("failed to get the chunk list from inode (err=%d)", ret);
        return ret;
    }

    LOGDBG("%u chunks for read(gfid=%d, offset=%lu, len=%lu):",
           n_chunks, gfid, offset, length);

    for (i = 0; i < n_chunks; i++) {
        chunk_read_req_t* tmp = &chunks[i];

        LOGDBG("[%2d] (offset=%lu, nbytes=%lu) @ (%d log(%d:%d:%lu))",
               i, tmp->offset, tmp->nbytes, tmp->rank,
               tmp->log_client_id, tmp->log_app_id, tmp->log_offset);
    }

    /* 2) sort the @chunks according to the server ranks */
    qsort(chunks, n_chunks, sizeof(*chunks), compare_chunks);

    for (i = 0; i < n_chunks; i++) {
        chunk_read_req_t* curr_chunk = &chunks[i];
        int curr_rank = curr_chunk->rank;

        if (curr_rank != prev_rank) {
            num_remote_reads++;
        }

        prev_rank = curr_rank;
    }

    remote_reads = (remote_chunk_reads_t*) calloc(num_remote_reads,
                                                  sizeof(*remote_reads));
    if (!remote_reads) {
        LOGERR("failed to allocate memory for remote_reads");
        goto out_free;
    }

    prev_rank = -1;
    remote_chunk_reads_t* curr_read = remote_reads;

    for (i = 0; i < n_chunks; i++) {
        chunk_read_req_t* curr_chunk = &chunks[i];
        int curr_rank = curr_chunk->rank;

        if (curr_rank != prev_rank) {
            curr_read->total_sz = data_sz;
            data_sz = 0;

            if (prev_rank != -1) {
                curr_read += 1;
            }
        }

        prev_rank = curr_rank;
        data_sz += curr_chunk->nbytes;

        if (0 == curr_read->num_chunks) {
            curr_read->rank = curr_rank;
            //curr_read->rdreq_id = rdreq->req_ndx;
            curr_read->reqs = &chunks[i];
            curr_read->resp = NULL;
        }

        curr_read->num_chunks += 1;
    }

    /* is this still necessary?
     * record total data size for final delegator (if any),
     * would have missed doing this in the above loop */
    if (n_chunks > 0) {
        curr_read->total_sz = data_sz;
    }

    rdreq.app_id = app_id;
    rdreq.client_id = client_id;
    rdreq.chunks = chunks;
    rdreq.num_remote_reads = num_remote_reads;
    rdreq.remote_reads = remote_reads;

    req = &rdreq.extent;
    req->gfid = gfid;
    req->offset = offset;
    req->length = length;
    req->errcode = 0;

    return rm_submit_read_request(&rdreq);

out_free:
    if (remote_reads) {
        free(remote_reads);
        remote_reads = NULL;
    }

    if (chunks) {
        free(chunks);
        chunks = NULL;
    }

    return ret;
}

static int rpc_mread(unifyfs_fops_ctx_t* ctx, size_t n_req, void* req)
{
    int ret = -ENOSYS;

    LOGDBG("%s is called but not implemented yet", __func__);

    return ret;
}

static struct unifyfs_fops _fops_rpc = {
    .name = "rpc",
    .init = rpc_init,
    .metaget = rpc_metaget,
    .metaset = rpc_metaset,
    .sync = rpc_sync,
    .fsync = rpc_fsync,
    .filesize = rpc_filesize,
    .truncate = rpc_truncate,
    .laminate = rpc_laminate,
    .unlink = rpc_unlink,
    .read = rpc_read,
    .mread = rpc_mread,
};

struct unifyfs_fops* unifyfs_fops_rpc = &_fops_rpc;

