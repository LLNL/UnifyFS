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
#include "unifyfs_collectives.h"
#include "unifyfs_metadata.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_fops.h"

struct unifyfs_inode_tree _global_inode_tree;
struct unifyfs_inode_tree* global_inode_tree = &_global_inode_tree;

static int collective_init(unifyfs_cfg_t* cfg)
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

static int collective_metaget(unifyfs_fops_ctx_t* ctx,
                              int gfid, unifyfs_file_attr_t* attr)
{
    return unifyfs_inode_metaget(gfid, attr);
}

static int collective_metaset(unifyfs_fops_ctx_t* ctx,
                              int gfid, int create, unifyfs_file_attr_t* attr)
{
    return unifyfs_invoke_metaset_rpc(gfid, create, attr);
}

static int collective_sync(unifyfs_fops_ctx_t* ctx)
{
    return rm_cmd_sync_collective(ctx->app_id, ctx->client_id);
}

static int collective_fsync(unifyfs_fops_ctx_t* ctx, int gfid)
{
    int ret = 0;

    LOGDBG("%s is called but not implemented yet", __func__);

    return ret;
}

static int collective_filesize(unifyfs_fops_ctx_t* ctx,
                               int gfid, size_t* filesize)
{
    return unifyfs_invoke_filesize_rpc(gfid, filesize);
}

static int collective_truncate(unifyfs_fops_ctx_t* ctx, int gfid, off_t len)
{
    return unifyfs_invoke_truncate_rpc(gfid, len);
}

static int collective_laminate(unifyfs_fops_ctx_t* ctx, int gfid)
{
    int ret = 0;

    LOGDBG("%s is called but not implemented yet", __func__);

    return ret;
}

static int collective_unlink(unifyfs_fops_ctx_t* ctx, int gfid)
{
    return unifyfs_invoke_unlink_rpc(gfid);
}

static int collective_read(unifyfs_fops_ctx_t* ctx,
                           int gfid, off_t offset, size_t len)
{
    int ret = 0;

    LOGDBG("%s is called but not implemented yet", __func__);

    return ret;
}

static int collective_mread(unifyfs_fops_ctx_t* ctx, size_t n_req, void* req)
{
    int ret = 0;

    LOGDBG("%s is called but not implemented yet", __func__);

    return ret;
}

static struct unifyfs_fops _fops_collective = {
    .name = "collective",
    .init = collective_init,
    .metaget = collective_metaget,
    .metaset = collective_metaset,
    .sync = collective_sync,
    .fsync = collective_fsync,
    .filesize = collective_filesize,
    .truncate = collective_truncate,
    .laminate = collective_laminate,
    .unlink = collective_unlink,
    .read = collective_read,
    .mread = collective_mread,
};

struct unifyfs_fops* unifyfs_fops_collective = &_fops_collective;

