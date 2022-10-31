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

#include "client_read.h"


static void debug_print_read_req(read_req_t* req)
{
    if (NULL != req) {
        LOGDBG("read_req[%p] gfid=%d, file offset=%zu, length=%zu, buf=%p"
               " - nread=%zu, errcode=%d (%s), byte coverage=[%zu,%zu]",
               req, req->gfid, req->offset, req->length, req->buf, req->nread,
               req->errcode, unifyfs_rc_enum_description(req->errcode),
               req->cover_begin_offset, req->cover_end_offset);
    }
}



/* compute the arraylist index for the given request id. we use
 * modulo operator to reuse slots in the list */
static inline
unsigned int id_to_list_index(unifyfs_client* client,
                              unsigned int id)
{
    unsigned int capacity = (unsigned int)
        arraylist_capacity(client->active_mreads);
    return id % capacity;
}

/* Create a new mread request containing the n_reads requests provided
 * in read_reqs array */
client_mread_status* client_create_mread_request(unifyfs_client* client,
                                                 int n_reads,
                                                 read_req_t* read_reqs)
{
    if ((NULL == client) || (NULL == client->active_mreads)) {
        LOGERR("client->active_mreads is NULL");
        return NULL;
    }

    pthread_mutex_lock(&(client->sync));

    int active_count = arraylist_size(client->active_mreads);
    if (active_count == arraylist_capacity(client->active_mreads)) {
        /* already at full capacity for outstanding reads */
        LOGWARN("too many outstanding client reads");
        pthread_mutex_unlock(&(client->sync));
        return NULL;
    }

    /* generate an id that doesn't conflict with another active mread */
    unsigned int mread_id, req_ndx;
    void* existing;
    do {
        mread_id = client->mread_id_generator++;
        req_ndx = id_to_list_index(client, mread_id);
        existing = arraylist_get(client->active_mreads, req_ndx);
    } while (existing != NULL);

    client_mread_status* mread = calloc(1, sizeof(client_mread_status));
    if (NULL == mread) {
        LOGERR("failed to allocate client mread status");
    } else {
        mread->client = client;
        mread->id = mread_id;
        mread->reqs = read_reqs;
        mread->n_reads = (unsigned int) n_reads;
        ABT_mutex_create(&(mread->sync));

        int rc = arraylist_insert(client->active_mreads,
                                (int)req_ndx, (void*)mread);
        if (rc != 0) {
            ABT_mutex_free(&(mread->sync));
            free(mread);
            mread = NULL;
        }
    }

    pthread_mutex_unlock(&(client->sync));

    return mread;
}

/* Remove the mread status */
int client_remove_mread_request(client_mread_status* mread)
{
    if (NULL == mread) {
        LOGERR("mread is NULL");
        return EINVAL;
    }

    unifyfs_client* client = mread->client;
    if (NULL == client->active_mreads) {
        LOGERR("client->active_mreads is NULL");
        return EINVAL;
    }

    int ret = UNIFYFS_SUCCESS;

    pthread_mutex_lock(&(client->sync));

    int list_index = (int) id_to_list_index(client, mread->id);
    void* list_item = arraylist_remove(client->active_mreads, list_index);
    if (list_item == (void*)mread) {
        ABT_mutex_free(&(mread->sync));
        free(mread);
    } else {
        LOGERR("mismatch on client->active_mreads index=%d", list_index);
        ret = UNIFYFS_FAILURE;
    }

    pthread_mutex_unlock(&(client->sync));

    return ret;
}

/* Retrieve the mread request corresponding to the given mread_id. */
client_mread_status* client_get_mread_status(unifyfs_client* client,
                                             unsigned int mread_id)
{
    if ((NULL == client) || (NULL == client->active_mreads)) {
        LOGERR("client->active_mreads is NULL");
        return NULL;
    }

    pthread_mutex_lock(&(client->sync));

    int list_index = (int) id_to_list_index(client, mread_id);
    void* list_item = arraylist_get(client->active_mreads, list_index);
    client_mread_status* status = (client_mread_status*)list_item;
    if (NULL != status) {
        if (status->id != mread_id) {
            LOGERR("mismatch on mread id=%u - status at index %d has id=%u",
                   mread_id, list_index, status->id);
            status = NULL;
        }
    } else {
        LOGERR("lookup of mread status for id=%u failed", mread_id);
    }

    pthread_mutex_unlock(&(client->sync));

    return status;
}

/* Update the mread status for the request at the given req_index.
 * If the request is now complete, update the request's completion state
 * (i.e., errcode and nread) */
int client_update_mread_request(client_mread_status* mread,
                                unsigned int req_index,
                                int req_complete,
                                int req_error)
{
    int ret = UNIFYFS_SUCCESS;

    if (NULL == mread) {
        LOGERR("mread is NULL");
        return EINVAL;
    }

    ABT_mutex_lock(mread->sync);
    if (req_index < mread->n_reads) {
        read_req_t* rdreq = mread->reqs + req_index;
        if (req_complete) {
            mread->n_complete++;
            if (req_error != 0) {
                mread->n_error++;
                rdreq->nread = 0;
                rdreq->errcode = req_error;
            }
            LOGINFO("updating mread[%u] status for request %u of %u "
                    "(n_complete=%u, n_error=%u)",
                    mread->id, req_index, mread->n_reads,
                    mread->n_complete, mread->n_error);
        }
    } else {
        LOGERR("invalid read request index %u (mread[%u] has %u reqs)",
               req_index, mread->id, mread->n_reads);
        ret = EINVAL;
    }

    int complete = (mread->n_complete == mread->n_reads);
    ABT_mutex_unlock(mread->sync);

    if (complete) {
        LOGDBG("mread[%u] completed %u requests",
               mread->id, mread->n_reads);
    }

    return ret;
}


/* For the given read request and extent (file offset, length), calculate
 * the coverage including offsets from the beginning of the request and extent
 * and the coverage length. Return a pointer to the segment within the request
 * buffer where read data should be placed. */
char* get_extent_coverage(read_req_t* req,
                          size_t extent_file_offset,
                          size_t extent_length,
                          size_t* out_req_offset,
                          size_t* out_ext_offset,
                          size_t* out_length)
{
    assert(NULL != req);

    /* start and end file offset of this request */
    size_t req_start = req->offset;
    size_t req_end   = (req->offset + req->length) - 1;

    /* start and end file offset of the extent */
    size_t ext_start = extent_file_offset;
    size_t ext_end   = (ext_start + extent_length) - 1;

    if ((ext_end < req_start) || (ext_start > req_end)) {
        /* no overlap between request and extent */
        if (NULL != out_length) {
            *out_length = 0;
        }
        return NULL;
    }

    /* starting file offset of covered segment is maximum of extent and request
     * start offsets */
    size_t start = ext_start;
    if (req_start > start) {
        start = req_start;
    }

    /* ending file offset of covered segment is mimimum of extent and request
     * end offsets */
    size_t end = ext_end;
    if (req_end < end) {
        end = req_end;
    }

    /* compute length of covered segment */
    size_t cover_length = (end - start) + 1;

    /* compute byte offsets of covered segment start from request
     * and extent */
    size_t req_byte_offset = start - req_start;
    size_t ext_byte_offset = start - ext_start;

    /* fill output values for request and extent byte offsets for covered
     * segment and covered length */
    if (NULL != out_req_offset) {
        *out_req_offset = req_byte_offset;
    }
    if (NULL != out_ext_offset) {
        *out_ext_offset = ext_byte_offset;
    }
    if (NULL != out_length) {
        *out_length = cover_length;
    }

    /* return pointer to request buffer where extent data should be placed */
    assert((req_byte_offset + cover_length) <= req->length);
    return (req->buf + req_byte_offset);
}

void update_read_req_coverage(read_req_t* req,
                              size_t extent_byte_offset,
                              size_t extent_length)
{
    size_t end_byte_offset = (extent_byte_offset + extent_length) - 1;

    /* update bytes we have filled in the request buffer */
    if ((req->cover_begin_offset == (size_t)-1) ||
        (extent_byte_offset < req->cover_begin_offset)) {
        req->cover_begin_offset = extent_byte_offset;
    }

    if ((req->cover_end_offset == (size_t)-1) ||
        (end_byte_offset > req->cover_end_offset)) {
        req->cover_end_offset = end_byte_offset;
        req->nread = req->cover_end_offset + 1;
    }
}


/* This uses information in the extent map for a file on the client to
 * complete any read requests.  It only complets a request if it contains
 * all of the data.  Otherwise the request is copied to the list of
 * requests to be handled by the server. */
static
void service_local_reqs(
    unifyfs_client* client,
    read_req_t* read_reqs,   /* list of input read requests */
    int count,               /* number of input read requests */
    read_req_t* local_reqs,  /* output list of requests completed by client */
    read_req_t* server_reqs, /* output list of requests to forward to server */
    int* out_count)          /* number of items copied to server list */
{
    /* this will track the total number of requests we're passing
     * on to the server */
    int local_count  = 0;
    int server_count = 0;

    /* iterate over each input read request, satisfy it locally if we can
     * otherwise copy request into output list that the server will handle
     * for us */
    int i;
    for (i = 0; i < count; i++) {
        /* get current read request */
        read_req_t* req = &read_reqs[i];
        int gfid = req->gfid;

        /* lookup local extents if we have them */
        int fid = unifyfs_fid_from_gfid(client, gfid);

        /* move to next request if we can't find the matching fid */
        if (fid < 0) {
            /* copy current request into list of requests
             * that we'll ask server for */
            memcpy(&server_reqs[server_count], req, sizeof(read_req_t));
            server_count++;
            continue;
        }

        /* start and length of this request */
        size_t req_start = req->offset;
        size_t req_end   = req->offset + req->length;

        /* get pointer to extents for this file */
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
        assert(meta != NULL);
        struct seg_tree* extents = &meta->extents;

        /* lock the extent tree for reading */
        seg_tree_rdlock(extents);

        /* can we fully satisfy this request? assume we can */
        int have_local = 1;

        /* this will point to the offset of the next byte we
         * need to account for */
        size_t expected_start = req_start;

        /* iterate over extents we have for this file,
         * and check that there are no holes in coverage.
         * we search for a starting extent using a range
         * of just the very first byte that we need */
        struct seg_tree_node* first;
        first = seg_tree_find_nolock(extents, req_start, req_start);
        struct seg_tree_node* next = first;
        while (next != NULL && next->start < req_end) {
            if (expected_start >= next->start) {
                /* this extent has the next byte we expect,
                 * bump up to the first byte past the end
                 * of this extent */
                expected_start = next->end + 1;
            } else {
                /* there is a gap between extents so we're missing
                 * some bytes */
                have_local = 0;
                break;
            }

            /* get the next element in the tree */
            next = seg_tree_iter(extents, next);
        }

        /* check that we account for the full request
         * up until the last byte */
        if (expected_start < req_end) {
            /* missing some bytes at the end of the request */
            have_local = 0;
        }

        /* if we can't fully satisfy the request, copy request to
         * output array, so it can be passed on to server */
        if (!have_local) {
            /* copy current request into list of requests
             * that we'll ask server for */
            memcpy(&server_reqs[server_count], req, sizeof(read_req_t));
            server_count++;

            /* release lock before we go to next request */
            seg_tree_unlock(extents);

            continue;
        }

        /* otherwise we can copy the data locally, iterate
         * over the extents and copy data into request buffer.
         * again search for a starting extent using a range
         * of just the very first byte that we need */
        next = first;
        while ((next != NULL) && (next->start < req_end)) {
            /* get start and length of this extent */
            size_t ext_start = next->start;
            size_t ext_length = (next->end + 1) - ext_start;

            /* get the offset into the log */
            size_t ext_log_pos = next->ptr;

            /* get number of bytes from start of extent and request
             * buffers to the start of the overlap region */
            size_t ext_byte_offset, req_byte_offset, cover_length;
            char* req_ptr = get_extent_coverage(req, ext_start, ext_length,
                                                &req_byte_offset,
                                                &ext_byte_offset,
                                                &cover_length);
            assert(req_ptr != NULL);

            /* copy data from local write log into user buffer */
            off_t log_offset = ext_log_pos + ext_byte_offset;
            size_t nread = 0;
            /* we need to use the logio_ctx from correct client */
            logio_context* logio_ctx = NULL;
            if (next->client_id == client->state.client_id) {
                logio_ctx = client->state.logio_ctx;
            } else if (client->logio_ctx_ptrs[next->client_id] != NULL) {
                logio_ctx = client->logio_ctx_ptrs[next->client_id];
            } else {
                size_t shmem_size = 0;
                if (client->state.logio_ctx->shmem != NULL) {
                    shmem_size = client->state.logio_ctx->shmem->size;
                }
                char* spill_dir = NULL;
                if (client->state.logio_ctx->spill_sz > 0) {
                    spill_dir = client->cfg.logio_spill_dir;
                }
                unifyfs_logio_init(client->state.app_id,
                                   next->client_id,
                                   shmem_size,
                                   client->state.logio_ctx->spill_sz,
                                   spill_dir,
                                   &client->logio_ctx_ptrs[next->client_id]);
                logio_ctx = client->logio_ctx_ptrs[next->client_id];
            }
            if (NULL != logio_ctx) {
                int rc = unifyfs_logio_read(logio_ctx, log_offset,
                                            cover_length, req_ptr, &nread);
                if (rc == UNIFYFS_SUCCESS) {
                    /* update bytes we have filled in the request buffer */
                    update_read_req_coverage(req, req_byte_offset, nread);
                } else {
                    LOGERR("local log read failed for offset=%zu size=%zu",
                           (size_t) log_offset, cover_length);
                    req->errcode = rc;
                }
            }
            /* get the next element in the tree */
            next = seg_tree_iter(extents, next);
        }

        /* copy request data to list we completed locally */
        memcpy(&local_reqs[local_count], req, sizeof(read_req_t));
        local_count++;

        /* done reading the tree */
        seg_tree_unlock(extents);
    }

    /* return to user the number of key/values we set */
    *out_count = server_count;

    return;
}

/* order by file id then by offset */
static
int compare_read_req(const void* a, const void* b)
{
    const read_req_t* rra = a;
    const read_req_t* rrb = b;

    if (rra->gfid != rrb->gfid) {
        if (rra->gfid < rrb->gfid) {
            return -1;
        } else {
            return 1;
        }
    }

    if (rra->offset == rrb->offset) {
        return 0;
    } else if (rra->offset < rrb->offset) {
        return -1;
    } else {
        return 1;
    }
}

static void update_read_req_result(unifyfs_client* client,
                                   read_req_t* req)
{
    debug_print_read_req(req);

    /* no error message was set, assume success */
    if (req->errcode == EINPROGRESS) {
        req->errcode = UNIFYFS_SUCCESS;
    }

    /* if we hit an error on our read, nothing else to do */
    if ((req->errcode != UNIFYFS_SUCCESS) &&
        (req->errcode != ENODATA)) {
        return;
    }

    /* if we read all of the bytes, request is satisfied */
    if (req->nread == req->length) {
        /* check for read hole at beginning of request */
        if (req->cover_begin_offset != 0) {
            /* fill read hole at beginning of request */
            LOGDBG("zero-filling hole at offset %zu of length %zu",
                   req->offset, req->cover_begin_offset);
            memset(req->buf, 0, req->cover_begin_offset);
            req->errcode = UNIFYFS_SUCCESS;
        }
        return;
    }

    /* get file size for this file */
    off_t filesize_offt = unifyfs_gfid_filesize(client, req->gfid);
    if (filesize_offt == (off_t)-1) {
        /* failed to get file size */
        req->errcode = ENOENT;
        return;
    }
    size_t filesize = (size_t) filesize_offt;

    if (filesize <= req->offset) {
        /* request start offset is at or after EOF */
        req->nread = 0;
        req->errcode = UNIFYFS_SUCCESS;
    } else {
        /* otherwise, we have a short read, check whether there
         * would be a hole after us, in which case we fill the
         * request buffer with zeros */

        /* get offset of where hole starts */
        size_t gap_start = req->offset + req->nread;

        /* get last offset of the read request */
        size_t req_end = req->offset + req->length;

        /* if file size is larger than last offset we wrote to in
         * read request, then there is a hole we can fill */
        if (filesize > gap_start) {
            /* assume we can fill the full request with zero */
            size_t gap_length = req_end - gap_start;
            if (req_end > filesize) {
                /* request is trying to read past end of file,
                 * so only fill zeros up to end of file */
                gap_length = filesize - gap_start;
            }

            /* copy zeros into request buffer */
            LOGDBG("zero-filling hole at offset %zu of length %zu",
                   gap_start, gap_length);
            char* req_ptr = req->buf + req->nread;
            memset(req_ptr, 0, gap_length);

            /* update number of bytes read and request status */
            req->nread += gap_length;
            req->errcode = UNIFYFS_SUCCESS;
        }
    }
}

/**
 * Service a list of client read requests using either local
 * data or forwarding requests to the server.
 *
 * @param in_reqs     a list of read requests
 * @param in_count    number of read requests
 *
 * @return error code
 */
int process_gfid_reads(unifyfs_client* client,
                       read_req_t* in_reqs,
                       size_t in_count)
{
    if (0 == in_count) {
        return UNIFYFS_SUCCESS;
    }

    if (NULL == in_reqs) {
        return EINVAL;
    }

    int i, rc, read_rc;

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* assume we'll service all requests from the server */
    int local_count = 0;
    int server_count = in_count;
    read_req_t* server_reqs = in_reqs;
    read_req_t* local_reqs = NULL;

    /* TODO: if the file is laminated so that we know the file size,
     * we can adjust read requests to not read past the EOF */

    /* mark all read requests as in-progress */
    for (i = 0; i < in_count; i++) {
        in_reqs[i].errcode = EINPROGRESS;
    }

    /* if the option is enabled to service requests locally, try it,
     * in this case we'll allocate a large array which we split into
     * two, the first half will record requests we completed locally
     * and the second half will store requests to be sent to the server */

    /* this records the pointer to the temp request array if
     * we allocate one, we should free this later if not NULL */
    read_req_t* reqs = NULL;
    if (client->use_node_local_extents) {
        extents_list_t* list = calloc(1, sizeof(struct extents_list));
        struct extents_list* cur = list;
        int num_request_selected = 0;
        for (int i = 0; i < in_count; ++i) {
            int fid = unifyfs_fid_from_gfid(client, in_reqs[i].gfid);
            /* get meta for this file id */
            unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
            if (meta != NULL) {
                if (!meta->attrs.is_laminated || !meta->needs_reads_sync) {
                    /* do not proceed for this request as
                     * it is not a laminated file or has already been synced.*/
                    continue;
                }
                num_request_selected++;
                off_t filesize_offt = unifyfs_gfid_filesize(client,
                                                            in_reqs[i].gfid);
                cur->value.file_pos = 0;
                cur->value.length = filesize_offt - 1;
                cur->value.gfid = in_reqs[i].gfid;
                if (i < in_count - 1) {
                    cur->next = calloc(1, sizeof(struct extents_list));
                    cur->next->next = NULL;
                    cur = cur->next;
                } else {
                    cur->next = NULL;
                }
                meta->needs_reads_sync = 0;
            }
        }
        if (num_request_selected > 0) {
            /* There are files which are laminated and
             * require sync of extents */
            size_t extent_count = 0;
            unifyfs_client_index_t* extents = NULL;
            int rc =
                  invoke_client_node_local_extents_get_rpc(client,
                                                           num_request_selected,
                                                           list,
                                                           &extent_count,
                                                           &extents);
            if (rc == UNIFYFS_SUCCESS && extent_count != 0) {
                for (int j = 0; j < extent_count; ++j) {
                    if (extents[j].log_app_id ==
                        client->state.app_id) {
                        int fid = unifyfs_fid_from_gfid(client,
                                                        extents[j].gfid);
                        /* get meta for this file id */
                        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(
                                client,
                                fid);
                        if (meta != NULL) {
                            seg_tree_add(&meta->extents,
                                         extents[j].file_pos,
                                         extents[j].file_pos +
                                         extents[j].length - 1,
                                         extents[j].log_pos,
                                         extents[j].log_client_id);
                        }
                    }
                }
            }
            if (extents != NULL) {
                free(extents);
            }
        }
    }

    /* attempt to complete requests locally if enabled */
    if (client->use_local_extents || client->use_node_local_extents) {
        /* allocate space to make local and server copies of the requests,
         * each list will be at most in_count long */
        size_t reqs_size = 2 * in_count;
        reqs = (read_req_t*) calloc(reqs_size, sizeof(read_req_t));
        if (reqs == NULL) {
            return ENOMEM;
        }

        /* define pointers to space where we can build our list
         * of requests handled on the client and those left
         * for the server */
        local_reqs = reqs;
        server_reqs = reqs + in_count;

        /* service reads from local extent info if we can, this copies
         * completed requests from in_reqs into local_reqs, and it copies
         * any requests that can't be completed locally into the server_reqs
         * to be processed by the server */
        service_local_reqs(client, in_reqs, in_count,
                           local_reqs, server_reqs, &server_count);
        local_count = in_count - server_count;
        for (i = 0; i < local_count; i++) {
            /* get pointer to next read request */
            read_req_t* req = local_reqs + i;
            LOGDBG("local request %d:", i);
            update_read_req_result(client, req);
        }

        /* return early if we satisfied all requests locally */
        if (server_count == 0) {
            /* copy completed requests back into user's array */
            memcpy(in_reqs, local_reqs, in_count * sizeof(read_req_t));

            /* free the temporary array */
            free(reqs);
            return ret;
        }
    }

    /* check that we have enough slots for all read requests */
    if (server_count > UNIFYFS_CLIENT_MAX_READ_COUNT) {
        /* TODO: When the number of read requests exceeds the
         * maximum count, handle by issuing multiple mreads */
        LOGERR("Too many requests to pass to server");
        if (reqs != NULL) {
            free(reqs);
        }
        return ENOSPC;
    }

    /* order read request by increasing file id, then increasing offset */
    qsort(server_reqs, server_count, sizeof(read_req_t), compare_read_req);

    /* create mread status for tracking completion */
    client_mread_status* mread = client_create_mread_request(client,
                                                             server_count,
                                                             server_reqs);
    if (NULL == mread) {
        return ENOMEM;
    }
    unsigned int mread_id = mread->id;

    /* create buffer of extent requests */
    size_t size = (size_t)server_count * sizeof(unifyfs_extent_t);
    void* buffer = malloc(size);
    if (NULL == buffer) {
        return ENOMEM;
    }
    unifyfs_extent_t* extents = (unifyfs_extent_t*)buffer;
    for (i = 0; i < server_count; i++) {
        unifyfs_extent_t* ext = extents + i;
        read_req_t* req = server_reqs + i;
        ext->gfid = req->gfid;
        ext->offset = req->offset;
        ext->length = req->length;
    }

    LOGDBG("mread[%u]: n_reqs=%d, reqs(%p)",
           mread_id, server_count, server_reqs);

    /* invoke multi-read rpc on server */
    read_rc = invoke_client_mread_rpc(client, mread_id, server_count,
                                      size, buffer);
    free(buffer);

    if (read_rc != UNIFYFS_SUCCESS) {
        /* mark requests as failed if we couldn't even start the read(s) */
        LOGDBG("mread RPC to server failed (rc=%d)", read_rc);
        for (i = 0; i < server_count; i++) {
            server_reqs[i].errcode = read_rc;
        }
        if (read_rc != ENODATA) {
            ret = read_rc;
        }
    } else {
        /* wait for all requests to finish by blocking on mread
         * completion condition (with a reasonable timeout) */
        LOGDBG("waiting for completion of mread[%u]", mread->id);

        /* this loop uses usleep() instead of pthread_cond_timedwait()
         * because that method caused unexplained read timeouts */
        int wait_time_ms = 0;
        int complete = 0;
        while (1) {
            ABT_mutex_lock(mread->sync);
            complete = (mread->n_complete == mread->n_reads);
            ABT_mutex_unlock(mread->sync);
            if (complete) {
                break;
            }

            if ((wait_time_ms / 1000) >= UNIFYFS_CLIENT_READ_TIMEOUT_SECONDS) {
                LOGERR("mread[%u] timed out", mread->id);
                break;
            }

            usleep(50000); /* sleep 50 ms */
            wait_time_ms += 50;
        }
        if (!complete) {
            for (i = 0; i < server_count; i++) {
                if (EINPROGRESS == server_reqs[i].errcode) {
                    server_reqs[i].errcode = ETIMEDOUT;
                    mread->n_error++;
                }
            }
        }
        LOGDBG("mread[%u] wait completed - %u requests, %u errors",
               mread->id, mread->n_reads, mread->n_error);
    }

    /* got all of the data we'll get from the server, check for short reads
     * and whether those short reads are from errors, holes, or end of file */
    for (i = 0; i < server_count; i++) {
        /* get pointer to next read request */
        read_req_t* req = server_reqs + i;
        LOGDBG("mread[%u] server request %d:", mread->id, i);
        update_read_req_result(client, req);
    }

    /* if we attempted to service requests from our local extent map,
     * then we need to copy the resulting read requests from the local
     * and server arrays back into the user's original array */
    if (client->use_local_extents || client->use_node_local_extents) {
        /* TODO: would be nice to copy these back into the same order
         * in which we received them. */

        /* copy locally completed requests back into user's array */
        if (local_count > 0) {
            memcpy(in_reqs, local_reqs, local_count * sizeof(read_req_t));
        }

        /* copy sever completed requests back into user's array */
        if (server_count > 0) {
            /* skip past any items we copied in from the local requests */
            read_req_t* in_ptr = in_reqs + local_count;
            memcpy(in_ptr, server_reqs, server_count * sizeof(read_req_t));
        }

        /* free storage we used for copies of requests */
        if (reqs != NULL) {
            free(reqs);
            reqs = NULL;
        }
    }

    rc = client_remove_mread_request(mread);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("mread[%u] cleanup failed", mread_id);
    }

    return ret;
}


