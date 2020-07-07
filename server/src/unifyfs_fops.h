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

#ifndef __UNIFYFS_FOPS_H
#define __UNIFYFS_FOPS_H

#include "unifyfs_configurator.h"
#include "unifyfs_log.h"
#include "unifyfs_meta.h"

/*
 * extra information that we need to pass for file operations.
 */
struct _unifyfs_fops_ctx {
    int app_id;
    int client_id;
};

typedef struct _unifyfs_fops_ctx unifyfs_fops_ctx_t;

typedef int (*unifyfs_fops_init_t)(unifyfs_cfg_t* cfg);

typedef int (*unifyfs_fops_metaget_t)(unifyfs_fops_ctx_t* ctx,
                                      int gfid, unifyfs_file_attr_t* attr);

typedef int (*unifyfs_fops_metaset_t)(unifyfs_fops_ctx_t* ctx,
                                      int gfid, int create,
                                      unifyfs_file_attr_t* attr);

typedef int (*unifyfs_fops_sync_t)(unifyfs_fops_ctx_t* ctx);

typedef int (*unifyfs_fops_fsync_t)(unifyfs_fops_ctx_t* ctx, int gfid);

typedef int (*unifyfs_fops_filesize_t)(unifyfs_fops_ctx_t* ctx,
                                       int gfid, size_t* filesize);

typedef int (*unifyfs_fops_truncate_t)(unifyfs_fops_ctx_t* ctx,
                                       int gfid, off_t len);

typedef int (*unifyfs_fops_laminate_t)(unifyfs_fops_ctx_t* ctx, int gfid);

typedef int (*unifyfs_fops_unlink_t)(unifyfs_fops_ctx_t* ctx, int gfid);

typedef int (*unifyfs_fops_read_t)(unifyfs_fops_ctx_t* ctx,
                                   int gfid, off_t offset, size_t len);

typedef int (*unifyfs_fops_mread_t)(unifyfs_fops_ctx_t* ctx,
                                    size_t n_req, void* req);

struct unifyfs_fops {
    const char* name;
    unifyfs_fops_init_t init;
    unifyfs_fops_metaget_t metaget;
    unifyfs_fops_metaset_t metaset;
    unifyfs_fops_sync_t sync;
    unifyfs_fops_fsync_t fsync;
    unifyfs_fops_filesize_t filesize;
    unifyfs_fops_truncate_t truncate;
    unifyfs_fops_laminate_t laminate;
    unifyfs_fops_unlink_t unlink;
    unifyfs_fops_read_t read;
    unifyfs_fops_mread_t mread;
};

/* available file operations.  */
extern struct unifyfs_fops* unifyfs_fops_impl;

/* the one that is configured to be used: defined in unifyfs_server.c */
extern struct unifyfs_fops* global_fops_tab;

static inline int unifyfs_fops_init(unifyfs_cfg_t* cfg)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_fops* fops = unifyfs_fops_impl;

    if (!fops) {
        LOGERR("failed to get the file operation table");
    }

    if (fops->init) {
        ret = fops->init(cfg);
        if (ret) {
            LOGERR("failed to initialize fops table (ret=%d)", ret);
            return ret;
        }
    }

    global_fops_tab = fops;

    return ret;
}

static inline int unifyfs_fops_metaget(unifyfs_fops_ctx_t* ctx,
                                       int gfid, unifyfs_file_attr_t* attr)
{
    if (!global_fops_tab->metaget) {
        return ENOSYS;
    }

    return global_fops_tab->metaget(ctx, gfid, attr);
}

static inline int unifyfs_fops_metaset(unifyfs_fops_ctx_t* ctx,
                                       int gfid, int create,
                                       unifyfs_file_attr_t* attr)
{
    if (!global_fops_tab->metaset) {
        return ENOSYS;
    }

    return global_fops_tab->metaset(ctx, gfid, create, attr);
}

static inline int unifyfs_fops_sync(unifyfs_fops_ctx_t* ctx)
{
    if (!global_fops_tab->sync) {
        return ENOSYS;
    }

    return global_fops_tab->sync(ctx);
}

static inline int unifyfs_fops_fsync(unifyfs_fops_ctx_t* ctx, int gfid)
{
    if (!global_fops_tab->fsync) {
        return ENOSYS;
    }

    return global_fops_tab->fsync(ctx, gfid);
}

static inline int unifyfs_fops_filesize(unifyfs_fops_ctx_t* ctx,
                                        int gfid, size_t* filesize)
{
    if (!global_fops_tab->filesize) {
        return ENOSYS;
    }

    return global_fops_tab->filesize(ctx, gfid, filesize);
}

static inline int unifyfs_fops_truncate(unifyfs_fops_ctx_t* ctx,
                                        int gfid, off_t len)
{
    if (!global_fops_tab->truncate) {
        return ENOSYS;
    }

    return global_fops_tab->truncate(ctx, gfid, len);
}

static inline int unifyfs_fops_laminate(unifyfs_fops_ctx_t* ctx, int gfid)
{
    if (!global_fops_tab->laminate) {
        return ENOSYS;
    }

    return global_fops_tab->laminate(ctx, gfid);
}

static inline int unifyfs_fops_unlink(unifyfs_fops_ctx_t* ctx, int gfid)
{
    if (!global_fops_tab->unlink) {
        return ENOSYS;
    }

    return global_fops_tab->unlink(ctx, gfid);
}

static inline int unifyfs_fops_read(unifyfs_fops_ctx_t* ctx,
                                    int gfid, off_t offset, size_t len)
{
    if (!global_fops_tab->read) {
        return ENOSYS;
    }

    return global_fops_tab->read(ctx, gfid, offset, len);
}

static inline int unifyfs_fops_mread(unifyfs_fops_ctx_t* ctx,
                                     size_t n_req, void* req)
{
    if (!global_fops_tab->mread) {
        return ENOSYS;
    }

    return global_fops_tab->mread(ctx, n_req, req);
}

#endif /* __UNIFYFS_FOPS_H */
