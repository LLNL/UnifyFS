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
    struct unifyfs_inode *ino = calloc(1, sizeof(*ino));

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
        if (ino->extents) {
            extent_tree_destroy(ino->extents);
            free(ino->extents);
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
    struct unifyfs_inode *ino = NULL;

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
    struct unifyfs_inode *ino = NULL;

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

    if (!global_inode_tree || !attr)
        return EINVAL;

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
    struct unifyfs_inode *ino = NULL;

    unifyfs_inode_tree_wrlock(global_inode_tree);
    {
        ret = unifyfs_inode_tree_remove(global_inode_tree, gfid, &ino);
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    if (ret)
        goto out;

    ret = unifyfs_inode_destroy(ino);
out:
    return ret;
}

int unifyfs_inode_truncate(int gfid, unsigned long size)
{
    int ret = 0;
    struct unifyfs_inode *ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_wrlock(ino);
        {
            ret = extent_tree_truncate(ino->extents, size);
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
static struct extent_tree *inode_get_extent_tree(struct unifyfs_inode *ino)
{
    /* create one if it doesn't exist yet */
    if (!ino->extents) {
        struct extent_tree *tree = calloc(1, sizeof(*tree));

        if (tree) {
            extent_tree_init(tree);
            ino->extents = tree;
        }
    }

    return ino->extents;
}

int unifyfs_inode_add_local_extents(int gfid, int num_extents,
                                    struct extent_tree_node* nodes)
{
    int ret = 0;
    struct unifyfs_inode *ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_wrlock(ino);
        {
            int i = 0;
            struct extent_tree *tree = inode_get_extent_tree(ino);

            if (!tree) { /* failed to create one */
                ret = ENOMEM;
                goto out_unlock_inode;
            }

            LOGDBG("adding %d extents to inode (gfid=%d)", num_extents, gfid);

            /* TODO: technically adding extents itself can go with rdlock */
            for (i = 0; i < num_extents; i++) {
                struct extent_tree_node *current = &nodes[i];
                ret = extent_tree_add(tree, current->start, current->end,
                                      current->svr_rank, current->app_id,
                                      current->cli_id, current->pos);
                if (ret) {
                    LOGERR("failed to add extents");
                    goto out_unlock_inode;
                }
            }
        }
out_unlock_inode:
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

static inline int unifyfs_inode_merge_shadow(struct unifyfs_inode *ino)
{
    int ret = 0;

    struct extent_tree *current = ino->extents;

    ino->extents = ino->shadow;
    ino->shadow = current;

    extent_tree_clear(ino->shadow);

    return ret;
}

int unifyfs_inode_get_extent_size(int gfid, size_t* offset)
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

        /* FIXME: this operation should be performed elsewhere.
         * e.g., after fsync() broadcasting has been completed.
         */
        unifyfs_inode_wrlock(ino);
        {
            if (ino->shadow && (extent_tree_count(ino->shadow) > 0)) {
                ret = unifyfs_inode_merge_shadow(ino);
                if (ret) {
                    LOGERR("merging extent free failed (ret=%d)", ret);
                }
            }
        }
        unifyfs_inode_unlock(ino);

        unifyfs_inode_rdlock(ino);
        {
            if (ino->extents)
                filesize = extent_tree_get_size(ino->extents);
        }
        unifyfs_inode_unlock(ino);

        *offset = filesize;
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    LOGDBG("local file size (gfid=%d): %lu", gfid, filesize);

    return ret;
}

int unifyfs_inode_get_local_extents(int gfid, size_t *n,
                                    struct extent_tree_node **nodes)
{
    int ret = 0;
    struct unifyfs_inode *ino = NULL;

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
            struct extent_tree *tree = ino->extents;
            size_t n_nodes = tree->count;
            struct extent_tree_node *_nodes = calloc(n_nodes, sizeof(*_nodes));
            struct extent_tree_node *current = NULL;

            if (!_nodes) {
                ret = ENOMEM;
                goto out_unlock_inode;
            }

            while (NULL != (current = extent_tree_iter(tree, current))) {
                _nodes[i] = *current;
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

static int unifyfs_inode_prepare_shadow(struct unifyfs_inode *ino)
{
    int ret = 0;
    struct extent_tree *shadow = NULL;

    if (ino->shadow) {
        shadow = ino->shadow;
        extent_tree_clear(shadow);
    } else {
        shadow = calloc(1, sizeof(*shadow));
        if (!shadow) {
            return ENOMEM;
        }

        extent_tree_init(shadow);
        ino->shadow = shadow;
    }

    return ret;
}

int unifyfs_inode_add_shadow_extents(int gfid, int n,
                                     struct extent_tree_node *nodes)
{
    int ret = 0;
    int i = 0;
    struct unifyfs_inode *ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_inode_tree;
        }

        unifyfs_inode_wrlock(ino);
        {
            if (!ino->shadow) {
                ret = unifyfs_inode_prepare_shadow(ino);
                if (ret) {
                    LOGERR("failed to create a shadow tree (gfid=%d)", gfid);
                    goto out_unlock_inode;
                }
            }
        }
        unifyfs_inode_unlock(ino);

        unifyfs_inode_rdlock(ino);
        {
            struct extent_tree *shadow = ino->shadow;

            LOGDBG("adding %d extents to file (gfid=%d)\n", n, gfid);

            for (i = 0; i < n; i++) {
                struct extent_tree_node *n = &nodes[i];
                ret = extent_tree_add(shadow, n->start, n->end, n->svr_rank,
                                      n->app_id, n->cli_id, n->pos);
                if (ret) {
                    LOGERR("failed to add shadow extents (ret=%d)\n", ret);
                    break;
                }
            }
        }
out_unlock_inode:
        unifyfs_inode_unlock(ino);
    }
out_unlock_inode_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_span_extents(
    int gfid,                        /* global file id we're looking in */
    unsigned long start,             /* starting logical offset */
    unsigned long end,               /* ending logical offset */
    int max,                         /* maximum number of key/vals to return */
    void* keys,             /* array of length max for output keys */
    void* vals,             /* array of length max for output values */
    int* outnum)                     /* number of entries returned */
{
    int ret = 0;
    struct unifyfs_inode *ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
            goto out_unlock_tree;
        }

        unifyfs_inode_rdlock(ino);
        {
            ret = extent_tree_span(ino->extents, gfid, start, end,
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

