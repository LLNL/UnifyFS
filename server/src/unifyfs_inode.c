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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "unifyfs_inode.h"
#include "unifyfs_inode_tree.h"

struct unifyfs_inode_tree _global_inode_tree;
struct unifyfs_inode_tree* global_inode_tree = &_global_inode_tree;

static inline
struct unifyfs_inode* unifyfs_inode_alloc(int gfid, unifyfs_file_attr_t* attr)
{
    struct unifyfs_inode* ino = calloc(1, sizeof(*ino));

    if (ino) {
        ino->gfid = gfid;
        ino->attr = *attr;
        ino->attr.filename = strdup(attr->filename);
        pthread_rwlock_init(&ino->rwlock, NULL);
    }

    return ino;
}

static inline int unifyfs_inode_destroy(struct unifyfs_inode* ino)
{
    int ret = 0;

    if (ino) {
        if (NULL != ino->attr.filename) {
            free(ino->attr.filename);
        }

        if (NULL != ino->extents) {
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
            if (ino->laminated) {
                LOGERR("cannot truncate a laminated file (gfid=%d)", gfid);
                ret = EINVAL;
                goto unlock_inode;
            }
            ino->attr.size = size;

            if (NULL != ino->extents) {
                ret = extent_tree_truncate(ino->extents, size);
            }
        }
unlock_inode:
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

static struct extent_tree* inode_get_extent_tree(struct unifyfs_inode* ino)
{
    struct extent_tree* tree = ino->extents;

    /* create one if it doesn't exist yet */
    if (!tree) {
        tree = calloc(1, sizeof(*tree));

        if (!tree) {
            LOGERR("failed to allocate memory for extent tree");
            return NULL;
        }

        extent_tree_init(tree);

        ino->extents = tree;
    }

    return tree;
}

int unifyfs_inode_add_extents(int gfid, int num_extents,
                              struct extent_tree_node* nodes)
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
            if (ino->laminated) {
                LOGERR("trying to add extents to a laminated file (gfid=%d)",
                       gfid);
                ret = EINVAL;
                goto out_unlock_inode;
            }

            tree = inode_get_extent_tree(ino);
            if (!tree) { /* failed to create one */
                ret = ENOMEM;
                goto out_unlock_inode;
            }

            for (i = 0; i < num_extents; i++) {
                struct extent_tree_node* current = &nodes[i];

                /* the output becomes too noisy with this:
                 * LOGDBG("new extent[%4d]: (%lu, %lu)",
                 *        i, current->start, current->end);
                 */

                ret = extent_tree_add(tree, current->start, current->end,
                                      current->svr_rank, current->app_id,
                                      current->cli_id, current->pos);
                if (ret) {
                    LOGERR("failed to add extents");
                    goto out_unlock_inode;
                }
            }

            /* if the extent tree max offset is greater than the size we
             * we currently have in the inode attributes, then update the
             * inode size */
            unsigned long extent_sz = extent_tree_max_offset(ino->extents) + 1;
            if ((uint64_t)extent_sz > ino->attr.size) {
                ino->attr.size = extent_sz;
            }

            LOGDBG("added %d extents to inode (gfid=%d, filesize=%" PRIu64 ")",
                   num_extents, gfid, ino->attr.size);

        }
out_unlock_inode:
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
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
            /* the size is updated each time we add extents or truncate,
             * so no need to recalculate */
            filesize = ino->attr.size;
        }
        unifyfs_inode_unlock(ino);

        *offset = filesize;

        LOGDBG("local file size (gfid=%d): %lu", gfid, filesize);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_laminate(int gfid)
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

        unifyfs_inode_wrlock(ino);
        {
            ino->laminated = 1;
            ino->attr.is_laminated = 1;
        }
        unifyfs_inode_unlock(ino);

        LOGDBG("file laminated (gfid=%d)", gfid);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_get_extents(int gfid, size_t* n,
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
            struct extent_tree* tree = ino->extents;
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

int unifyfs_inode_get_chunk_list(
    int gfid,
    unsigned long offset,
    unsigned long len,
    unsigned int* n_chunks,
    chunk_read_req_t** chunks)
{
    int ret = UNIFYFS_SUCCESS;
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
            if (NULL != ino->extents) {
                ret = extent_tree_get_chunk_list(ino->extents, offset, len,
                                                 n_chunks, chunks);
                if (ret) {
                    LOGERR("failed to get chunks for gfid:%d, ret=%d",
                            gfid, ret);
                }
            }
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    if (ret == UNIFYFS_SUCCESS) {
        unsigned int i;

        /* extent_tree_get_chunk_list does not populate the gfid field */
        for (i = 0; i < *n_chunks; i++) {
            (*chunks)[i].gfid = gfid;
        }
    }

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
            LOGDBG("== inode (gfid=%d) ==\n", ino->gfid);
            if (NULL != ino->extents) {
                LOGDBG("extents:");
                extent_tree_dump(ino->extents);
            }
        }
        unifyfs_inode_unlock(ino);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}
