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
    if (NULL != ino) {
        struct extent_tree* tree = calloc(1, sizeof(*tree));
        if (NULL == tree) {
            LOGERR("failed to allocate memory for inode extent tree");
            free(ino);
            return NULL;
        }
        extent_tree_init(tree);
        ino->extents = tree;
        ino->gfid = gfid;
        ino->attr = *attr;
        ino->attr.filename = strdup(attr->filename);

        pthread_rwlock_init(&(ino->rwlock), NULL);
        ABT_mutex_create(&(ino->abt_sync));
    } else {
        LOGERR("failed to allocate memory for inode");
    }

    return ino;
}

static int unifyfs_inode_destroy(struct unifyfs_inode* ino);

/**
 * @brief read lock the inode for ro access.
 *
 * @param ino inode structure to get access
 *
 * @return 0 on success, errno otherwise
 */
static inline
int unifyfs_inode_rdlock(struct unifyfs_inode* ino)
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
static inline
int unifyfs_inode_wrlock(struct unifyfs_inode* ino)
{
    return pthread_rwlock_wrlock(&ino->rwlock);
}

/**
 * @brief unlock the inode.
 *
 * @param ino inode structure to unlock
 */
static inline
void unifyfs_inode_unlock(struct unifyfs_inode* ino)
{
    pthread_rwlock_unlock(&ino->rwlock);
}

int unifyfs_inode_create(int gfid, unifyfs_file_attr_t* attr)
{
    if (NULL == attr) {
        return EINVAL;
    }

    struct unifyfs_inode* ino = unifyfs_inode_alloc(gfid, attr);
    if (NULL == ino) {
        return ENOMEM;
    }

    int ret = UNIFYFS_SUCCESS;
    unifyfs_inode_tree_wrlock(global_inode_tree);
    {
        ret = unifyfs_inode_tree_insert(global_inode_tree, ino);
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    if (ret != UNIFYFS_SUCCESS) {
        unifyfs_inode_destroy(ino);
    }

    return ret;
}

static
int unifyfs_inode_destroy(struct unifyfs_inode* ino)
{
    int ret = UNIFYFS_SUCCESS;

    if (ino) {
        if (NULL != ino->attr.filename) {
            free(ino->attr.filename);
        }

        if (NULL != ino->extents) {
            /* iterate over extents and release local logio allocations */
            unifyfs_inode_rdlock(ino);
            {
                struct extent_tree* tree = ino->extents;
                struct extent_tree_node* curr = NULL;
                while (NULL != (curr = extent_tree_iter(tree, curr))) {
                    if (curr->svr_rank == glb_pmi_rank) {
                        /* lookup client's logio context and release
                         * allocation for this extent */
                        int app_id    = curr->app_id;
                        int client_id = curr->cli_id;
                        app_client* client = get_app_client(app_id, client_id);
                        if ((NULL == client) ||
                            (NULL == client->state.logio_ctx)) {
                            continue;
                        }
                        logio_context* logio = client->state.logio_ctx;
                        size_t nbytes = (1 + (curr->end - curr->start));
                        off_t log_off = curr->pos;
                        int rc = unifyfs_logio_free(logio, log_off, nbytes);
                        if (UNIFYFS_SUCCESS != rc) {
                            LOGERR("failed to free logio allocation for "
                                   "client[%d:%d] log_offset=%zu nbytes=%zu",
                                   app_id, client_id, (size_t)log_off, nbytes);
                        }
                    }
                }
            }
            unifyfs_inode_unlock(ino);

            extent_tree_destroy(ino->extents);
            free(ino->extents);
        }

        pthread_rwlock_destroy(&(ino->rwlock));
        ABT_mutex_free(&(ino->abt_sync));

        free(ino);
    } else {
        ret = EINVAL;
    }

    return ret;
}

int unifyfs_inode_update_attr(int gfid, int attr_op,
                              unifyfs_file_attr_t* attr)
{
    if (NULL == attr) {
        return EINVAL;
    }

    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (NULL == ino) {
            ret = ENOENT;
        } else {
            unifyfs_inode_wrlock(ino);
            unifyfs_file_attr_update(attr_op, &ino->attr, attr);
            unifyfs_inode_unlock(ino);
        }
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_metaset(int gfid, int attr_op,
                          unifyfs_file_attr_t* attr)
{
    int ret;

    if (attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) {
        ret = unifyfs_inode_create(gfid, attr);
    } else {
        ret = unifyfs_inode_update_attr(gfid, attr_op, attr);
    }

    return ret;
}

int unifyfs_inode_metaget(int gfid, unifyfs_file_attr_t* attr)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    if ((NULL == global_inode_tree) || (NULL == attr)) {
        return EINVAL;
    }

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (NULL != ino) {
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
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_wrlock(global_inode_tree);
    {
        ret = unifyfs_inode_tree_remove(global_inode_tree, gfid, &ino);
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    if (ret == UNIFYFS_SUCCESS) {
        ret = unifyfs_inode_destroy(ino);
    }

    return ret;
}

int unifyfs_inode_truncate(int gfid, unsigned long size)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
        } else {
            unifyfs_inode_wrlock(ino);
            {
                if (ino->attr.is_laminated) {
                    LOGERR("cannot truncate a laminated file (gfid=%d)", gfid);
                    ret = EINVAL;
                } else {
                    ino->attr.size = size;
                    if (NULL != ino->extents) {
                        ret = extent_tree_truncate(ino->extents, size);
                    }
                }
            }
            unifyfs_inode_unlock(ino);
        }
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_add_extents(int gfid, int num_extents,
                              struct extent_tree_node* nodes)
{
    int ret = UNIFYFS_SUCCESS;
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

        if (ino->attr.is_laminated) {
            LOGERR("trying to add extents to a laminated file (gfid=%d)",
                   gfid);
            ret = EINVAL;
            goto out_unlock_tree;
        }

        ABT_mutex_lock(ino->abt_sync);

        unifyfs_inode_wrlock(ino);
        {
            tree = ino->extents;
            if (NULL == tree) {
                LOGERR("inode extent tree is missing");
                goto out_unlock_inode;
            }

            for (i = 0; i < num_extents; i++) {
                struct extent_tree_node* current = &nodes[i];

                /* debug output becomes too noisy with this:
                 * LOGDBG("extent[%4d]: [%lu, %lu] @ server[%d] log(%d:%d:%lu)",
                 *        i, current->start, current->end, current->svr_rank,
                 *        current->app_id, current->cli_id, current->pos);
                 */
                ret = extent_tree_add(tree, current->start, current->end,
                                      current->svr_rank, current->app_id,
                                      current->cli_id, current->pos);
                if (ret) {
                    LOGERR("failed to add extent [%lu, %lu] to gfid=%d",
                           current->start, current->end, gfid);
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
        }
out_unlock_inode:
        unifyfs_inode_unlock(ino);

        LOGINFO("added %d extents to inode (gfid=%d, filesize=%" PRIu64 ")",
               num_extents, gfid, ino->attr.size);

        ABT_mutex_unlock(ino->abt_sync);
    }
out_unlock_tree:
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_get_filesize(int gfid, size_t* outsize)
{
    int ret = UNIFYFS_SUCCESS;
    size_t filesize = 0;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
        } else {
            unifyfs_inode_rdlock(ino);
            {
                /* the size is updated each time we add extents or truncate,
                 * so no need to recalculate */
                filesize = ino->attr.size;
            }
            unifyfs_inode_unlock(ino);

            *outsize = filesize;
            LOGDBG("local file size (gfid=%d): %lu", gfid, filesize);
        }
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_laminate(int gfid)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
        } else {
            unifyfs_inode_wrlock(ino);
            ino->attr.is_laminated = 1;
            unifyfs_inode_unlock(ino);

            LOGDBG("file laminated (gfid=%d)", gfid);
        }
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_get_extents(int gfid, size_t* n,
                              struct extent_tree_node** nodes)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    if (!n || !nodes) {
        return EINVAL;
    }

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
        } else {
            unifyfs_inode_rdlock(ino);
            {
                int i = 0;
                struct extent_tree* tree = ino->extents;
                size_t n_nodes = tree->count;
                struct extent_tree_node* _nodes = calloc(n_nodes,
                                                         sizeof(*_nodes));
                struct extent_tree_node* curr = NULL;

                if (!_nodes) {
                    ret = ENOMEM;
                } else {
                    while (NULL != (curr = extent_tree_iter(tree, curr))) {
                        _nodes[i] = *curr;
                        i++;
                    }

                    *n = n_nodes;
                    *nodes = _nodes;
                }
            }
            unifyfs_inode_unlock(ino);
        }
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_get_extent_chunks(unifyfs_inode_extent_t* extent,
                                    unsigned int* n_chunks,
                                    chunk_read_req_t** chunks)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;
    int gfid = extent->gfid;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
        } else {
            unifyfs_inode_rdlock(ino);
            {
                if (NULL != ino->extents) {
                    unsigned long offset = extent->offset;
                    unsigned long len = extent->length;
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
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    if (ret == UNIFYFS_SUCCESS) {
        /* extent_tree_get_chunk_list does not populate the gfid field */
        for (unsigned int i = 0; i < *n_chunks; i++) {
            (*chunks)[i].gfid = gfid;
        }
    } else {
        *n_chunks = 0;
        *chunks = NULL;
    }

    return ret;
}

static
int compare_chunk_read_reqs(const void* _c1, const void* _c2)
{
    chunk_read_req_t* c1 = (chunk_read_req_t*) _c1;
    chunk_read_req_t* c2 = (chunk_read_req_t*) _c2;

    if (c1->rank > c2->rank) {
        return 1;
    } else if (c1->rank < c2->rank) {
        return -1;
    } else {
        if (c1->offset > c2->offset) {
            return 1;
        } else if (c1->offset < c2->offset) {
            return -1;
        }
        return 0;
    }
}


int unifyfs_inode_resolve_extent_chunks(unsigned int n_extents,
                                        unifyfs_inode_extent_t* extents,
                                        unsigned int* n_locs,
                                        chunk_read_req_t** chunklocs)
{
    int ret = UNIFYFS_SUCCESS;
    unsigned int i = 0;
    unsigned int j = 0;
    unsigned int n_chunks = 0;
    chunk_read_req_t* chunks = NULL;
    unsigned int* n_resolved = NULL;
    chunk_read_req_t** resolved = NULL;

    void* buf = calloc(n_extents, (sizeof(*n_resolved) + sizeof(*resolved)));
    if (NULL == buf) {
        LOGERR("failed to allocate memory");
        ret = ENOMEM;
        goto out_fail;
    }

    n_resolved = (unsigned int*) buf;
    resolved = (chunk_read_req_t**) &n_resolved[n_extents];

    /* resolve chunks addresses for all requests from inode tree */
    for (i = 0; i < n_extents; i++) {
        unifyfs_inode_extent_t* current = &extents[i];

        LOGDBG("resolving extent request [gfid=%d, offset=%lu, length=%lu]",
               current->gfid, current->offset, current->length);

        ret = unifyfs_inode_get_extent_chunks(current,
                                              &n_resolved[i], &resolved[i]);
        if (ret) {
            LOGERR("failed to resolve extent request "
                   "[gfid=%d, offset=%lu, length=%lu] (ret=%d)",
                   current->gfid, current->offset, current->length, ret);
            goto out_fail;
        }

        n_chunks += n_resolved[i];
    }

    LOGDBG("resolved %d chunks for read request", n_chunks);
    if (n_chunks > 0) {
        /* store all chunks in a flat array */
        chunks = calloc(n_chunks, sizeof(*chunks));
        if (!chunks) {
            LOGERR("failed to allocate memory for storing resolved chunks");
            ret = ENOMEM;
            goto out_fail;
        }

        chunk_read_req_t* pos = chunks;
        for (i = 0; i < n_extents; i++) {
            chunk_read_req_t* ext_chunks = resolved[i];
            for (j = 0; j < n_resolved[i]; j++) {
                //debug_print_chunk_read_req(ext_chunks + j);
                *pos = ext_chunks[j];
                pos++;
            }
            if (resolved[i]) {
                free(resolved[i]);
            }
        }

        if (n_chunks > 1) {
            /* sort the requests based on server rank */
            qsort(chunks, n_chunks, sizeof(*chunks), compare_chunk_read_reqs);
        }
        chunk_read_req_t* chk = chunks;
        for (i = 0; i < n_chunks; i++, chk++) {
            debug_print_chunk_read_req(chk);
        }
    }

    *n_locs = n_chunks;
    *chunklocs = chunks;

out_fail:
    if (ret != UNIFYFS_SUCCESS) {
        if (chunks) {
            free(chunks);
            chunks = NULL;
        }
    }

    if (NULL != buf) {
        free(buf);
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
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
        } else {
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

    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}

int unifyfs_inode_dump(int gfid)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = NULL;

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
        if (!ino) {
            ret = ENOENT;
        } else {
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
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    return ret;
}
