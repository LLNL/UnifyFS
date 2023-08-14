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
#include "unifyfs_request_manager.h"
#include "unifyfs_p2p_rpc.h"  // for hash_gfid_to_server()

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

        ino->pending_extents = NULL;

        unifyfs_file_attr_set_invalid(&(ino->attr));
        unifyfs_file_attr_update(UNIFYFS_FILE_ATTR_OP_CREATE,
                                 &(ino->attr), attr);

        ABT_rwlock_create(&(ino->rwlock));
    } else {
        LOGERR("failed to allocate memory for inode");
    }

    return ino;
}

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
    return ABT_rwlock_rdlock(ino->rwlock);
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
    return ABT_rwlock_wrlock(ino->rwlock);
}

/**
 * @brief unlock the inode.
 *
 * @param ino inode structure to unlock
 */
static inline
void unifyfs_inode_unlock(struct unifyfs_inode* ino)
{
    ABT_rwlock_unlock(ino->rwlock);
}


/**
 * @brief get the inode for a gfid.
 *
 * @param gfid gfid of inode to retrieve
 */
static inline
struct unifyfs_inode* unifyfs_inode_lookup(int gfid)
{
    struct unifyfs_inode* ino = NULL;
    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        ino = unifyfs_inode_tree_search(global_inode_tree, gfid);
    }
    unifyfs_inode_tree_unlock(global_inode_tree);
    return ino;
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

int unifyfs_inode_destroy(struct unifyfs_inode* ino)
{
    int ret = UNIFYFS_SUCCESS;

    if (ino) {
        if (NULL != ino->attr.filename) {
            free(ino->attr.filename);
        }

        if (NULL != ino->extents) {

            /* allocate an array to track local clients to which we should
             * send an unlink callback */
            size_t n_clients = 0;
            size_t max_clients = (size_t) ino->extents->count;
            int* local_clients = calloc(max_clients, sizeof(int));
            int last_client;
            int cb_app_id = -1;

            /* iterate over extents and release local logio allocations */
            unifyfs_inode_rdlock(ino);
            {
                last_client = -1;
                struct extent_tree* tree = ino->extents;
                struct extent_tree_node* curr = NULL;
                while (NULL != (curr = extent_tree_iter(tree, curr))) {
                    if (curr->extent.svr_rank == glb_pmi_rank) {
                        /* lookup client's logio context and release
                         * allocation for this extent */
                        int app_id    = curr->extent.app_id;
                        int client_id = curr->extent.cli_id;
                        app_client* client = get_app_client(app_id, client_id);
                        if ((NULL == client) ||
                            (NULL == client->state.logio_ctx)) {
                            continue;
                        }
                        logio_context* logio = client->state.logio_ctx;
                        size_t nbytes = extent_length(&(curr->extent));
                        off_t log_off = curr->extent.log_pos;
                        int rc = unifyfs_logio_free(logio, log_off, nbytes);
                        if (UNIFYFS_SUCCESS != rc) {
                            LOGERR("failed to free logio allocation for "
                                   "client[%d:%d] log_offset=%zu nbytes=%zu",
                                   app_id, client_id, (size_t)log_off, nbytes);
                        }

                        if (NULL != local_clients) {
                            if (-1 == cb_app_id) {
                                cb_app_id = app_id;
                            }
                            /* add client id to local clients array */
                            if (last_client != client_id) {
                                assert(n_clients < max_clients);
                                local_clients[n_clients] = client_id;
                                n_clients++;
                            }
                            last_client = client_id;
                        }
                    }
                }
            }
            unifyfs_inode_unlock(ino);

            extent_tree_destroy(ino->extents);
            free(ino->extents);

            if (NULL != local_clients) {
                qsort(local_clients, n_clients, sizeof(int), int_compare_fn);
                last_client = -1;
                for (size_t i = 0; i < n_clients; i++) {
                    int cb_client_id = local_clients[i];
                    if (cb_client_id == last_client) {
                        continue;
                    }
                    last_client = cb_client_id;

                    /* submit a request to the client's reqmgr thread
                     * to cleanup client state */
                    client_callback_req* cb = malloc(sizeof(*cb));
                    if (NULL != cb) {
                        cb->req_type  = UNIFYFS_CLIENT_CALLBACK_UNLINK;
                        cb->app_id    = cb_app_id;
                        cb->client_id = cb_client_id;
                        cb->gfid      = ino->gfid;
                        int rc = rm_submit_client_callback_request(cb);
                        if (UNIFYFS_SUCCESS != rc) {
                            LOGERR("failed to submit unlink callback "
                                   "req to client[%d:%d]",
                                   cb_app_id, cb_client_id);
                        }
                    }
                }
                free(local_clients);
            }
        }

        ABT_rwlock_free(&(ino->rwlock));

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
    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        ret = ENOENT;
    } else {
        unifyfs_inode_wrlock(ino);
        {
            unifyfs_file_attr_update(attr_op, &ino->attr, attr);
        }
        unifyfs_inode_unlock(ino);
    }
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
    if ((NULL == global_inode_tree) || (NULL == attr)) {
        return EINVAL;
    }

    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL != ino) {
        *attr = ino->attr;
    } else {
        ret = ENOENT;
    }
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

    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
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

    return ret;
}

int unifyfs_inode_add_pending_extents(int gfid,
                                      client_rpc_req_t* client_req,
                                      int num_extents,
                                      extent_metadata* extents)
{
    int ret = UNIFYFS_SUCCESS;

    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        return ENOENT;
    }

    if (ino->attr.is_laminated) {
        LOGERR("trying to add extents to a laminated file (gfid=%d)",
               gfid);
        return EINVAL;
    }

    unifyfs_inode_wrlock(ino);
    {
        if (NULL == ino->pending_extents) {
            ino->pending_extents = arraylist_create(0);
            if (NULL == ino->pending_extents) {
                ret = ENOMEM;
                LOGERR("failed to allocate inode pending extents list");
                goto add_pending_unlock_inode;
            }
        }

        pending_extents_item* list_item = malloc(sizeof(*list_item));
        if (NULL == list_item) {
            ret = ENOMEM;
            LOGERR("failed to allocate inode pending extents list");
                goto add_pending_unlock_inode;

        }
        list_item->client_req = client_req;
        list_item->num_extents = num_extents;
        list_item->extents = extents;

        arraylist_add(ino->pending_extents, list_item);
    }
add_pending_unlock_inode:
    unifyfs_inode_unlock(ino);

    LOGINFO("added %d pending extents to inode (gfid=%d)",
            num_extents, gfid);


    return ret;
}

bool unifyfs_inode_has_pending_extents(int gfid)
{
    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        return false;
    }

    bool has_pending = false;
    unifyfs_inode_rdlock(ino);
    {
        if (NULL != ino->pending_extents) {
            has_pending = true;
        }
    }
    unifyfs_inode_unlock(ino);

    return has_pending;
}

int unifyfs_inode_get_pending_extents(int gfid,
                                      arraylist_t** pending_list)
{
    if (NULL == pending_list) {
        return EINVAL;
    }
    *pending_list = NULL;

    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        return ENOENT;
    }

    unifyfs_inode_wrlock(ino);
    {
        if (NULL != ino->pending_extents) {
            *pending_list = ino->pending_extents;
            ino->pending_extents = NULL;
            LOGINFO("returning pending list (sz=%d) from inode (gfid=%d)",
                    arraylist_size(*pending_list), gfid);
        }
    }
    unifyfs_inode_unlock(ino);

    return UNIFYFS_SUCCESS;
}

int unifyfs_inode_add_extents(int gfid,
                              int num_extents,
                              extent_metadata* extents)
{
    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        return ENOENT;
    }

    if (ino->attr.is_laminated) {
        LOGERR("trying to add extents to a laminated file (gfid=%d)",
               gfid);
        return EINVAL;
    }

    int ret = UNIFYFS_SUCCESS;
    unifyfs_inode_wrlock(ino);
    {
        struct extent_tree* tree = ino->extents;
        if (NULL == tree) {
            LOGERR("inode extent tree is missing");
            goto add_unlock_inode;
        }

        for (int i = 0; i < num_extents; i++) {
            extent_metadata* current = extents + i;
            ret = extent_tree_add(tree, current);
            if (ret) {
                LOGERR("failed to add extent [%lu, %lu] to gfid=%d",
                       current->start, current->end, gfid);
                goto add_unlock_inode;
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
add_unlock_inode:
    unifyfs_inode_unlock(ino);

    LOGINFO("added %d extents to inode (gfid=%d, filesize=%" PRIu64 ")",
            num_extents, gfid, ino->attr.size);


    return ret;
}

int unifyfs_inode_get_filesize(int gfid, size_t* outsize)
{
    int ret = UNIFYFS_SUCCESS;
    size_t filesize = 0;

    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        ret = ENOENT;
    } else {
        unifyfs_inode_rdlock(ino);
        {
            /* the size is updated each time we add extents or truncate,
             * so no need to recalculate */
            filesize = ino->attr.size;
        }
        unifyfs_inode_unlock(ino);
    }

    *outsize = filesize;
    LOGDBG("local file size (gfid=%d): %lu", gfid, filesize);

    return ret;
}

int unifyfs_inode_laminate(int gfid)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        ret = ENOENT;
    } else {
        unifyfs_inode_wrlock(ino);
        {
            ino->attr.is_laminated = 1;
        }
        unifyfs_inode_unlock(ino);
        LOGDBG("laminated file (gfid=%d)", gfid);
    }
    return ret;
}

int unifyfs_inode_get_extents(int gfid,
                              size_t* n,
                              extent_metadata** extents)
{
    if ((NULL == n) || (NULL == extents)) {
        return EINVAL;
    }

    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        ret = ENOENT;
    } else {
        unifyfs_inode_rdlock(ino);
        {
            struct extent_tree* tree = ino->extents;
            size_t n_extents = tree->count;
            extent_metadata* _extents = calloc(n_extents, sizeof(*_extents));
            if (NULL == _extents) {
                ret = ENOMEM;
            } else {
                int i = 0;
                struct extent_tree_node* curr = NULL;
                while ((curr = extent_tree_iter(tree, curr)) != NULL) {
                    _extents[i] = curr->extent;
                    i++;
                }

                *n = n_extents;
                *extents = _extents;
            }
        }
        unifyfs_inode_unlock(ino);
    }
    return ret;
}

int unifyfs_inode_get_extent_chunks(unifyfs_extent_t* extent,
                                    unsigned int* n_chunks,
                                    chunk_read_req_t** chunks,
                                    int* full_coverage)
{
    int ret = UNIFYFS_SUCCESS;
    int gfid = extent->gfid;
    int covered = 0;

    *full_coverage = 0;

    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
        ret = ENOENT;
    } else {
        unifyfs_inode_rdlock(ino);
        {
            if (NULL != ino->extents) {
                unsigned long offset = extent->offset;
                unsigned long len    = extent->length;
                ret = extent_tree_get_chunk_list(ino->extents, offset, len,
                                                 n_chunks, chunks,
                                                 &covered);
                if (ret) {
                    LOGERR("failed to get chunks for gfid=%d (rc=%d)",
                           gfid, ret);
                }
            }
        }
        unifyfs_inode_unlock(ino);
    }

    if (ret == UNIFYFS_SUCCESS) {
        /* extent_tree_get_chunk_list() does not populate the gfid field */
        for (unsigned int i = 0; i < *n_chunks; i++) {
            (*chunks)[i].gfid = gfid;
        }
        *full_coverage = covered;
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
                                        unifyfs_extent_t* extents,
                                        unsigned int* n_locs,
                                        chunk_read_req_t** chunklocs,
                                        int* full_coverage)
{
    int ret = UNIFYFS_SUCCESS;
    int fully_covered = 1;
    unsigned int i = 0;
    unsigned int j = 0;
    unsigned int n_chunks = 0;
    chunk_read_req_t* chunks = NULL;
    unsigned int* n_resolved = NULL;
    chunk_read_req_t** resolved = NULL;

    /* set default output parameter values */
    *n_locs = 0;
    *chunklocs = NULL;
    *full_coverage = 0;

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
        unifyfs_extent_t* current = &extents[i];

        LOGDBG("resolving extent request [gfid=%d, offset=%lu, length=%lu]",
               current->gfid, current->offset, current->length);

        int covered = 0;
        ret = unifyfs_inode_get_extent_chunks(current,
                                              &n_resolved[i], &resolved[i],
                                              &covered);
        if (ret) {
            LOGERR("failed to resolve extent request "
                   "[gfid=%d, offset=%lu, length=%lu] (ret=%d)",
                   current->gfid, current->offset, current->length, ret);
            goto out_fail;
        }
        if (!covered) {
            fully_covered = 0;
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
    *full_coverage = fully_covered;

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

int unifyfs_inode_dump(int gfid)
{
    int ret = UNIFYFS_SUCCESS;
    struct unifyfs_inode* ino = unifyfs_inode_lookup(gfid);
    if (NULL == ino) {
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

    return ret;
}


int unifyfs_get_gfids(int* num_gfids, int** gfid_list)
{
    int ret = UNIFYFS_SUCCESS;

    int _num_gfids = 0;
    int gfid_list_size = 64;
    int* _gfid_list = calloc(gfid_list_size, sizeof(int));
    if (NULL == _gfid_list) {
        return ENOMEM;
    }

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        struct unifyfs_inode* node =
            unifyfs_inode_tree_iter(global_inode_tree, NULL);
        while (node) {
            if (_num_gfids == gfid_list_size) {
                gfid_list_size *= 2;  // Double the list size each time
                _gfid_list = realloc(_gfid_list, sizeof(int)*gfid_list_size);
                if (!_gfid_list) {
                    unifyfs_inode_tree_unlock(global_inode_tree);
                    return ENOMEM;
                }
            }
            _gfid_list[_num_gfids++] = node->gfid;
            node = unifyfs_inode_tree_iter(global_inode_tree, node);
        }

    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    *num_gfids = _num_gfids;
    *gfid_list = _gfid_list;
    return ret;
}


int unifyfs_get_owned_files(unsigned int* num_files,
                            unifyfs_file_attr_t** attr_list)
{
    /* Iterate through the global_inode_tree and copy all the file
     * attr structs for the files this server owns.
     * Note: the file names in the unifyfs_file_attr_t are pointers to
     * separately allocated memory, so they will be created using strdup(). */

    unsigned int attr_list_size = 64;
    unsigned int num_files_int = 0;
    unifyfs_file_attr_t* attr_list_int;
    /* _int suffix is short for "internal".  If everything succeeds, then
     * before returning, we'll copy num_files_int to num_files and
     * attr_list_int to attr_list. */

    attr_list_int =  malloc(sizeof(unifyfs_file_attr_t) * attr_list_size);
    if (!attr_list_int) {
        return ENOMEM;
    }

    unifyfs_inode_tree_rdlock(global_inode_tree);
    {
        struct unifyfs_inode* node =
            unifyfs_inode_tree_iter(global_inode_tree, NULL);
        while (node) {
            if (num_files_int == attr_list_size) {
                attr_list_size *= 2;  // Double the list size each time
                attr_list_int =
                    realloc(attr_list_int,
                            sizeof(unifyfs_file_attr_t)*attr_list_size);
                if (!attr_list_int) {
                    unifyfs_inode_tree_unlock(global_inode_tree);
                    free(attr_list_int);
                    return ENOMEM;
                }
            }

            /* We only want to copy file attrs that we're the owner of */
            int owner_rank = hash_gfid_to_server(node->attr.gfid);
            if (owner_rank == glb_pmi_rank) {
                memcpy(&attr_list_int[num_files_int], &node->attr,
                    sizeof(unifyfs_file_attr_t));

                /* filename is a pointer to separately allocated memory.
                 *  We need to do a deep copy, so create a new string with
                 *  strdup(). */
                attr_list_int[num_files_int].filename =
                    strdup(node->attr.filename);

                num_files_int++;
            }
            node = unifyfs_inode_tree_iter(global_inode_tree, node);
        }
    }
    unifyfs_inode_tree_unlock(global_inode_tree);

    /* realloc() the array list space down to only what we need.
     *
     * Note that corner cases get a little odd here:
     * 1) If num_files_int is 0, this will conveniently free() the list for
     *    us.  The pointer we get back from realloc() will be NULL and that's
     *    exactly what we should return to the caller.
     * 2) If the realloc actually fails (and it's unclear how that could
     *    happen given that we're reducing the size of the allocation), then
     *    the original memory will be left untouched and we we can return the
     *    original pointer.   That wastes some space, but the pointer is
     *    valid and the memory will eventually be freed by the caller.  */
    unifyfs_file_attr_t* attr_list_int2 = realloc(
        attr_list_int, num_files_int * sizeof(unifyfs_file_attr_t));
    if (NULL == attr_list_int2) {
        if (0 != num_files_int) {
            /* realloc() actually failed. Wow. */
            attr_list_int2 = attr_list_int;
        }
    }

    /* If we made it here, then we walked the tree with no errors, so update
     * the return parameters and return. */
    *attr_list = attr_list_int2;
    *num_files = num_files_int;
    return UNIFYFS_SUCCESS;
}
