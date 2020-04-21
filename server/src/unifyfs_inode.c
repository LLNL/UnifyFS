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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "unifyfs_inode.h"
#include "unifyfs_inode_tree.h"

static inline
struct unifyfs_inode* unifyfs_inode_alloc(int gfid, unifyfs_file_attr_t* attr)
{
    struct unifyfs_inode* ino = calloc(1, sizeof(*ino));

    if (ino) {
        ino->gfid = gfid;
        ino->attr = *attr;
        pthread_rwlock_init(&ino->rwlock, NULL);
    }

    return ino;
}

static inline int unifyfs_inode_destroy(struct unifyfs_inode* ino)
{
    int ret = 0;

    if (ino) {
        if (ino->local_extents) {
            extent_tree_destroy(ino->local_extents);
            free(ino->local_extents);
        }

        if (ino->remote_extents) {
            extent_tree_destroy(ino->remote_extents);
            free(ino->remote_extents);
        }

        pthread_rwlock_destroy(&ino->rwlock);
        free(ino);
    } else {
        ret = EINVAL;
    }

    return ret;
}

/**
 * @brief read lock the inode for ro access.
 *
 * @param ino inode structure to get access
 *
 * @return 0 on success, errno otherwise
 */
static inline int unifyfs_inode_rdlock(struct unifyfs_inode* ino)
{
    return pthread_rwlock_rdlock(&ino->rwlock);
}

/**
 * @brief write lock the inode for w+r access.
 *
 * @param ino inode structure to get access
 *
 * @return 0 on success, errno otherwise
 */
static inline int unifyfs_inode_wrlock(struct unifyfs_inode* ino)
{
    return pthread_rwlock_wrlock(&ino->rwlock);
}

/**
 * @brief unlock the inode.
 *
 * @param ino inode structure to unlock
 */
static inline void unifyfs_inode_unlock(struct unifyfs_inode* ino)
{
    pthread_rwlock_unlock(&ino->rwlock);
}

int unifyfs_inode_create(int gfid, unifyfs_file_attr_t* attr)
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    if (!attr) {
        return EINVAL;
    }

    ino = unifyfs_inode_alloc(gfid, attr);

    unifyfs_inode_tree_wrlock(global_inode_tree);
    {
        ret = unifyfs_inode_tree_insert(global_inode_tree, ino);
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    if (ret) {
        free(ino);
    }

    return ret;
}

int unifyfs_inode_update_attr(int gfid, unifyfs_file_attr_t* attr)
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    if (!attr) {
        return EINVAL;
    }

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_wrlock(ino);
        {
            unifyfs_file_attr_update(&ino->attr, attr);
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_metaget(int gfid, unifyfs_file_attr_t* attr)
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    if (!global_inode_tree || !attr) {
        return EINVAL;
    }

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (ino) {
            *attr = ino->attr;
        } else {
            ret = ENOENT;
        }
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_unlink(int gfid)
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_wrlock(global_inode_tree);
    {
        ret = unifyfs_inode_tree_remove(global_inode_tree, gfid, &ino);
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    if (ret) {
        goto out;
    }

    ret = unifyfs_inode_destroy(ino);
out:
    return ret;
}

int unifyfs_inode_truncate(int gfid, unsigned long size)
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_rdlock(ino);
        {
            ret = extent_tree_truncate(ino->local_extents, size);
            ret |= extent_tree_truncate(ino->remote_extents, size);
            if (ret == 0) {
                ino->attr.size = size;
            }
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

/**
 * NOTE: inode rwlock should be hold by caller.
 */
enum {
    UNIFYFS_INODE_LOCAL = 0,
    UNIFYFS_INODE_REMOTE = 1,
};

static struct extent_tree*
inode_get_extent_tree(struct unifyfs_inode* ino, int is_remote)
{
    struct extent_tree* tree = is_remote ? ino->remote_extents
                                         : ino->local_extents;

    /* create one if it doesn't exist yet */
    if (!tree) {
        tree = calloc(1, sizeof(*tree));

        if (!tree) {
            LOGERR("failed to allocate memory for extent tree");
            return NULL;
        }

        extent_tree_init(tree);

        if (is_remote) {
            ino->remote_extents = tree;
        } else {
            ino->local_extents = tree;
        }
    }

    return tree;
}

static inline
struct extent_tree* inode_get_local_extent_tree(struct unifyfs_inode* ino)
{
    return inode_get_extent_tree(ino, UNIFYFS_INODE_LOCAL);
}

static inline
struct extent_tree* inode_get_remote_extent_tree(struct unifyfs_inode* ino)
{
    return inode_get_extent_tree(ino, UNIFYFS_INODE_REMOTE);
}

/*
 * @ino should be properly locked by the caller
 */
static inline uint64_t inode_get_filesize(struct unifyfs_inode* ino)
{
    uint64_t size_local = 0;
    uint64_t size_remote = 0;

    if (ino->local_extents) {
        size_local = extent_tree_max(ino->local_extents);
        if (extent_tree_count(ino->local_extents)) {
            size_local += 1;
        }
    }

    if (ino->remote_extents) {
        size_remote = extent_tree_max(ino->remote_extents);
        if (extent_tree_count(ino->remote_extents)) {
            size_remote += 1;
        }
    }

    return size_local > size_remote ? size_local : size_remote;
}

static int unifyfs_inode_add_extents(int gfid,
                                     int num_extents,
                                     struct extent_tree_node* nodes,
                                     int is_remote)
{
    int ret = 0;
    int i = 0;
    struct unifyfs_inode* ino = NULL;
    struct extent_tree* tree = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_wrlock(ino);
        {
            tree = inode_get_extent_tree(ino, is_remote);

            if (!tree) { /* failed to create one */
                ret = ENOMEM;
                goto out_unlock_inode;
            }

            for (i = 0; i < num_extents; i++) {
                struct extent_tree_node* current = &nodes[i];

                LOGDBG("new extent[%4d]: (%lu, %lu)",
                        i, current->start, current->end);

                ret = extent_tree_add(tree, current->start, current->end,
                                      current->svr_rank, current->app_id,
                                      current->cli_id, current->pos);
                if (ret) {
                    LOGERR("failed to add extents");
                    goto out_unlock_inode;
                }
            }

            ino->attr.size = inode_get_filesize(ino);

            LOGDBG("adding %d %s extents to inode (gfid=%d, filesize=%lu)",
                   num_extents, is_remote ? "remote" : "local",
                   gfid, ino->attr.size);

        }
out_unlock_inode:
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_add_local_extents(int gfid, int num_extents,
                                    struct extent_tree_node* nodes)
{
    return unifyfs_inode_add_extents(gfid, num_extents, nodes,
                                     UNIFYFS_INODE_LOCAL);
}

int unifyfs_inode_add_remote_extents(int gfid, int num_extents,
                                     struct extent_tree_node* nodes)
{
    return unifyfs_inode_add_extents(gfid, num_extents, nodes,
                                     UNIFYFS_INODE_REMOTE);
}

static inline void __unifyfs_inode_dump(struct unifyfs_inode* ino)
{
    LOGDBG("== inode (gfid=%d) ==\n", ino->gfid);
    if (ino->local_extents) {
        LOGDBG("local extents:");
        extent_tree_dump(ino->local_extents);
    }
    if (ino->remote_extents) {
        LOGDBG("remote extents:");
        extent_tree_dump(ino->remote_extents);
    }
}

int unifyfs_inode_get_filesize(int gfid, size_t* offset)
{
    int ret = 0;
    size_t filesize = 0;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_rdlock(ino);
        {
            filesize = inode_get_filesize(ino);
            __unifyfs_inode_dump(ino);
        }
        unifyfs_inode_unlock(ino);

        *offset = filesize;
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    LOGDBG("local file size (gfid=%d): %lu", gfid, *offset);

    return ret;
}

int unifyfs_inode_get_local_extents(int gfid, size_t* n,
                                    struct extent_tree_node** nodes)
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    if (!n || !nodes) {
        return EINVAL;
    }

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_rdlock(ino);
        {
            int i = 0;
            struct extent_tree* tree = ino->local_extents;
            size_t n_nodes = tree->count;
            struct extent_tree_node* _nodes = calloc(n_nodes, sizeof(*_nodes));
            struct extent_tree_node* current = NULL;

            if (!_nodes) {
                ret = ENOMEM;
                goto out_unlock_inode;
            }

            while (NULL != (current = extent_tree_iter(tree, current))) {
                _nodes[i] = *current;
                i++;
            }

            *n = n_nodes;
            *nodes = _nodes;
        }
out_unlock_inode:
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_span_extents(
    int gfid,                      /* global file id we're looking in */
    unsigned long start,           /* starting logical offset */
    unsigned long end,             /* ending logical offset */
    int max,                       /* maximum number of key/vals to return */
    void* keys,                    /* array of length max for output keys */
    void* vals,                    /* array of length max for output values */
    int* outnum)                   /* number of entries returned */
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_rdlock(ino);
        {
            ret = extent_tree_span(ino->local_extents, gfid, start, end,
                                   max, keys, vals, outnum);
            if (ret) {
                LOGERR("extent_tree_span failed (gfid=%d, ret=%d)",
                        gfid, ret);
            }
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_dump(int gfid)
{
    int ret = 0;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_rdlock(ino);
        {
            __unifyfs_inode_dump(ino);
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}
