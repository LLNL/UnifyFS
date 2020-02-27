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

struct extent_tree* unifyfs_inode_get_extent_tree(int gfid)
{
    struct unifyfs_inode *ino = NULL;
    struct extent_tree* extent_tree = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            goto out_unlock_tree;
        }

        unifyfs_inode_rdlock(ino);
        {
            extent_tree = ino->extents;
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return extent_tree;
}

int unifyfs_inode_add_extent(int gfid, struct extent_tree* extents)
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
            ino->extents = extents;
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

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

        unifyfs_inode_rdlock(ino);
        {
            filesize = extent_tree_get_size(ino->extents);
        }
        unifyfs_inode_unlock(ino);

        *offset = filesize;
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

