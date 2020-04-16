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
#include "unifyfs_metadata.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_fops.h"

static int mdhim_init(unifyfs_cfg_t* cfg)
{
    int ret = 0;

    LOGDBG("initializing file operations..");

    ret = meta_init_store(cfg);
    if (ret) {
        LOGERR("failed to initialize the meta kv store (ret=%d)", ret);
    }

    return ret;
}

static int mdhim_metaget(unifyfs_fops_ctx_t* ctx,
                         int gfid, unifyfs_file_attr_t* attr)
{
    return unifyfs_get_file_attribute(gfid, attr);
}

static int mdhim_metaset(unifyfs_fops_ctx_t* ctx,
                         int gfid, int create, unifyfs_file_attr_t* attr)
{
    return unifyfs_set_file_attribute(create, create, attr);
}

static int mdhim_sync(unifyfs_fops_ctx_t* ctx)
{
    return rm_cmd_sync_mdhim(ctx->app_id, ctx->client_id);
}

/*
 * currently, we publish all key-value pairs (regardless of the @gfid).
 */
static int mdhim_fsync(unifyfs_fops_ctx_t* ctx, int gfid)
{
    int ret = 0;

    LOGDBG("%s is called but not implemented yet", __func__);

    return ret;
}

static int mdhim_filesize(unifyfs_fops_ctx_t* ctx, int gfid, size_t* filesize)
{
    return rm_cmd_filesize(ctx->app_id, ctx->client_id, gfid, filesize);
}

static int mdhim_truncate(unifyfs_fops_ctx_t* ctx, int gfid, off_t len)
{
    return rm_cmd_truncate(ctx->app_id, ctx->client_id, gfid, len);
}

static int mdhim_laminate(unifyfs_fops_ctx_t* ctx, int gfid)
{
    return rm_cmd_laminate(ctx->app_id, ctx->client_id, gfid);
}

static int mdhim_unlink(unifyfs_fops_ctx_t* ctx, int gfid)
{
    return rm_cmd_unlink(ctx->app_id, ctx->client_id, gfid);
}


static int mdhim_read(unifyfs_fops_ctx_t* ctx,
                      int gfid, off_t offset, size_t len)
{
    return rm_cmd_read(ctx->app_id, ctx->client_id, gfid, offset, len);
}

static int mdhim_mread(unifyfs_fops_ctx_t* ctx, size_t n_req, void* req)
{
    return rm_cmd_mread(ctx->app_id, ctx->client_id, n_req, req);
}

static struct unifyfs_fops _fops_mdhim = {
    .name = "mdhim",
    .init = mdhim_init,
    .metaget = mdhim_metaget,
    .metaset = mdhim_metaset,
    .sync = mdhim_sync,
    .fsync = mdhim_fsync,
    .filesize = mdhim_filesize,
    .truncate = mdhim_truncate,
    .laminate = mdhim_laminate,
    .unlink = mdhim_unlink,
    .read = mdhim_read,
    .mread = mdhim_mread,
};

struct unifyfs_fops* unifyfs_fops_mdhim = &_fops_mdhim;

