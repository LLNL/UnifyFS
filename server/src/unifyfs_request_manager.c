/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2019, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contribuions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

// system headers
#include <assert.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// general support
#include "unifyfs_global.h"
#include "unifyfs_log.h"

// server components
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"
#include "unifyfs_metadata.h"

// margo rpcs
#include "unifyfs_server_rpcs.h"
#include "margo_server.h"
#include "ucr_read_builder.h"


#define RM_LOCK(rm) \
do { \
    LOGDBG("locking RM[%d] state", rm->thrd_ndx); \
    pthread_mutex_lock(&(rm->thrd_lock)); \
} while (0)

#define RM_UNLOCK(rm) \
do { \
    LOGDBG("unlocking RM[%d] state", rm->thrd_ndx); \
    pthread_mutex_unlock(&(rm->thrd_lock)); \
} while (0)

arraylist_t* rm_thrd_list;

/* One request manager thread is created for each client that a
 * delegator serves.  The main thread of the delegator assigns
 * work to the request manager thread to retrieve data and send
 * it back to the client.
 *
 * To start, given a read request from the client (via rpc)
 * the handler function on the main delegator first queries the
 * key/value store using the given file id and byte range to obtain
 * the meta data on the physical location of the file data.  This
 * meta data provides the host delegator rank, the app/client
 * ids that specify the log file on the remote delegator, the
 * offset within the log file and the length of data.  The rpc
 * handler function sorts the meta data by host delegator rank,
 * generates read requests, and inserts those into a list on a
 * data structure shared with the request manager (del_req_set).
 *
 * The request manager thread coordinates with the main thread
 * using a lock and a condition variable to protect the shared data
 * structure and impose flow control.  When assigned work, the
 * request manager thread packs and sends request messages to
 * service manager threads on remote delegators via MPI send.
 * It waits for data to be sent back, and unpacks the read replies
 * in each message into a shared memory buffer for the client.
 * When the shared memory is full or all data has been received,
 * it signals the client process to process the read replies.
 * It iterates with the client until all incoming read replies
 * have been transferred. */
/* create a request manager thread for the given app_id
 * and client_id, returns pointer to thread control structure
 * on success and NULL on failure */

/* Create Request Manager thread for application client */
reqmgr_thrd_t* unifyfs_rm_thrd_create(int app_id, int client_id)
{
    /* allocate a new thread control structure */
    reqmgr_thrd_t* thrd_ctrl = (reqmgr_thrd_t*)
        calloc(1, sizeof(reqmgr_thrd_t));
    if (thrd_ctrl == NULL) {
        LOGERR("Failed to allocate structure for request "
               "manager thread for app_id=%d client_id=%d",
               app_id, client_id);
        return NULL;
    }

    /* initialize lock for shared data structures between
     * main thread and request delegator thread */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rc = pthread_mutex_init(&(thrd_ctrl->thrd_lock), &attr);
    if (rc != 0) {
        LOGERR("pthread_mutex_init failed for request "
               "manager thread app_id=%d client_id=%d rc=%d (%s)",
               app_id, client_id, rc, strerror(rc));
        free(thrd_ctrl);
        return NULL;
    }

    /* initailize condition variable to flow control
     * work between main thread and request delegator thread */
    rc = pthread_cond_init(&(thrd_ctrl->thrd_cond), NULL);
    if (rc != 0) {
        LOGERR("pthread_cond_init failed for request "
               "manager thread app_id=%d client_id=%d rc=%d (%s)",
               app_id, client_id, rc, strerror(rc));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        free(thrd_ctrl);
        return NULL;
    }

    /* record app and client id this thread will be serving */
    thrd_ctrl->app_id    = app_id;
    thrd_ctrl->client_id = client_id;

    /* initialize flow control flags */
    thrd_ctrl->exit_flag              = 0;
    thrd_ctrl->exited                 = 0;
    thrd_ctrl->has_waiting_delegator  = 0;
    thrd_ctrl->has_waiting_dispatcher = 0;

    /* insert our thread control structure into our list of
     * active request manager threads, important to do this before
     * launching thread since it uses list to lookup its structure */
    rc = arraylist_add(rm_thrd_list, thrd_ctrl);
    if (rc != 0) {
        pthread_cond_destroy(&(thrd_ctrl->thrd_cond));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        free(thrd_ctrl);
        return NULL;
    }
    thrd_ctrl->thrd_ndx = arraylist_size(rm_thrd_list) - 1;

    /* launch request manager thread */
    rc = pthread_create(&(thrd_ctrl->thrd), NULL,
                        rm_delegate_request_thread, (void*)thrd_ctrl);
    if (rc != 0) {
        LOGERR("failed to create request manager thread for "
               "app_id=%d client_id=%d - rc=%d (%s)",
               app_id, client_id, rc, strerror(rc));
        pthread_cond_destroy(&(thrd_ctrl->thrd_cond));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        free(thrd_ctrl);
        return NULL;
    }

    return thrd_ctrl;
}

/* Lookup RM thread control structure */
reqmgr_thrd_t* rm_get_thread(int thrd_id)
{
    return (reqmgr_thrd_t*) arraylist_get(rm_thrd_list, thrd_id);
}

/* order keyvals by gfid, then host delegator rank */
static int compare_kv_gfid_rank(const void* a, const void* b)
{
    const unifyfs_keyval_t* kv_a = a;
    const unifyfs_keyval_t* kv_b = b;

    int gfid_a = kv_a->key.fid;
    int gfid_b = kv_b->key.fid;
    if (gfid_a == gfid_b) {
        int rank_a = kv_a->val.delegator_rank;
        int rank_b = kv_b->val.delegator_rank;
        if (rank_a == rank_b) {
            return 0;
        } else if (rank_a < rank_b) {
            return -1;
        } else {
            return 1;
        }
    } else if (gfid_a < gfid_b) {
        return -1;
    } else {
        return 1;
    }
}

unifyfs_key_t** alloc_key_array(int elems)
{
    int size = elems * (sizeof(unifyfs_key_t*) + sizeof(unifyfs_key_t));

    void* mem_block = calloc(size, sizeof(char));

    unifyfs_key_t** array_ptr = mem_block;
    unifyfs_key_t* key_ptr = (unifyfs_key_t*)(array_ptr + elems);

    for (int i = 0; i < elems; i++) {
        array_ptr[i] = &key_ptr[i];
    }

    return (unifyfs_key_t**)mem_block;
}

fattr_key_t** alloc_attr_key_array(int elems)
{
    int size = elems * (sizeof(fattr_key_t*) + sizeof(fattr_key_t));

    void* mem_block = calloc(size, sizeof(char));

    fattr_key_t** array_ptr = mem_block;
    fattr_key_t* key_ptr = (fattr_key_t*)(array_ptr + elems);

    for (int i = 0; i < elems; i++) {
        array_ptr[i] = &key_ptr[i];
    }

    return (fattr_key_t**)mem_block;
}

unifyfs_val_t** alloc_value_array(int elems)
{
    int size = elems * (sizeof(unifyfs_val_t*) + sizeof(unifyfs_val_t));

    void* mem_block = calloc(size, sizeof(char));

    unifyfs_val_t** array_ptr = mem_block;
    unifyfs_val_t* key_ptr = (unifyfs_val_t*)(array_ptr + elems);

    for (int i = 0; i < elems; i++) {
        array_ptr[i] = &key_ptr[i];
    }

    return (unifyfs_val_t**)mem_block;
}

void free_key_array(unifyfs_key_t** array)
{
    free(array);
}

void free_value_array(unifyfs_val_t** array)
{
    free(array);
}

void free_attr_key_array(fattr_key_t** array)
{
    free(array);
}

static void debug_print_read_req(server_read_req_t* req)
{
    if (NULL != req) {
        LOGDBG("server_read_req[%d] status=%d, gfid=%d, num_remote=%d",
               req->req_ndx, req->status, req->extent.gfid,
               req->num_remote_reads);
    }
}

static server_read_req_t* reserve_read_req(reqmgr_thrd_t* thrd_ctrl)
{
    server_read_req_t* rdreq = NULL;
    RM_LOCK(thrd_ctrl);
    if (thrd_ctrl->num_read_reqs < RM_MAX_ACTIVE_REQUESTS) {
        if (thrd_ctrl->next_rdreq_ndx < (RM_MAX_ACTIVE_REQUESTS - 1)) {
            rdreq = thrd_ctrl->read_reqs + thrd_ctrl->next_rdreq_ndx;
            assert((rdreq->req_ndx == 0) && (rdreq->extent.gfid == 0));
            rdreq->req_ndx = thrd_ctrl->next_rdreq_ndx++;
        } else { // search for unused slot
            for (int i = 0; i < RM_MAX_ACTIVE_REQUESTS; i++) {
                rdreq = thrd_ctrl->read_reqs + i;
                if ((rdreq->req_ndx == 0) && (rdreq->extent.gfid == 0)) {
                    rdreq->req_ndx = i;
                    break;
                }
            }
        }
        thrd_ctrl->num_read_reqs++;
        LOGDBG("reserved read req %d (active=%d, next=%d)", rdreq->req_ndx,
               thrd_ctrl->num_read_reqs, thrd_ctrl->next_rdreq_ndx);
        debug_print_read_req(rdreq);
    } else {
        LOGERR("maxed-out request manager read_reqs array!!");
    }
    RM_UNLOCK(thrd_ctrl);
    return rdreq;
}

static int release_read_req(reqmgr_thrd_t* thrd_ctrl,
                            server_read_req_t* rdreq)
{
    int rc = (int)UNIFYFS_SUCCESS;
    RM_LOCK(thrd_ctrl);
    if (rdreq != NULL) {
        LOGDBG("releasing read req %d", rdreq->req_ndx);
        if (rdreq->req_ndx == (thrd_ctrl->next_rdreq_ndx - 1)) {
            thrd_ctrl->next_rdreq_ndx--;
        }
        if (NULL != rdreq->chunks) {
            free(rdreq->chunks);
        }
        if (NULL != rdreq->remote_reads) {
            free(rdreq->remote_reads);
        }
        memset((void*)rdreq, 0, sizeof(server_read_req_t));
        thrd_ctrl->num_read_reqs--;
        LOGDBG("after release (active=%d, next=%d)",
               thrd_ctrl->num_read_reqs, thrd_ctrl->next_rdreq_ndx);
        debug_print_read_req(rdreq);
    } else {
        rc = UNIFYFS_ERROR_INVAL;
        LOGERR("NULL read_req");
    }
    RM_UNLOCK(thrd_ctrl);
    return rc;
}

static void signal_new_requests(reqmgr_thrd_t* thrd_ctrl)
{
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    /* wake up the request manager thread for the requesting client */
    if (!thrd_ctrl->has_waiting_delegator) {
        /* delegator thread is not waiting, but we are in critical
         * section, we just added requests so we must wait for delegator
         * to signal us that it's reached the critical section before
         * we escape so we don't overwrite these requests before it
         * has had a chance to process them */
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* delegator thread has signaled us that it's now waiting */
        thrd_ctrl->has_waiting_dispatcher = 0;
    }
    /* have a delegator thread waiting on condition variable,
     * signal it to begin processing the requests we just added */
    pthread_cond_signal(&thrd_ctrl->thrd_cond);
}

static void signal_new_responses(reqmgr_thrd_t* thrd_ctrl)
{
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    /* wake up the request manager thread */
    if (thrd_ctrl->has_waiting_delegator) {
        /* have a delegator thread waiting on condition variable,
         * signal it to begin processing the responses we just added */
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }
}

/* issue remote chunk read requests for extent chunks
 * listed within keyvals */
int create_chunk_requests(reqmgr_thrd_t* thrd_ctrl,
                          server_read_req_t* rdreq,
                          int num_vals,
                          unifyfs_keyval_t* keyvals)
{
    int thrd_id = thrd_ctrl->thrd_ndx;
    int app_id = thrd_ctrl->app_id;
    int client_id = thrd_ctrl->client_id;

    chunk_read_req_t* all_chunk_reads = (chunk_read_req_t*)
        calloc((size_t)num_vals, sizeof(chunk_read_req_t));
    if (NULL == all_chunk_reads) {
        LOGERR("failed to allocate chunk-reads array");
        return UNIFYFS_ERROR_NOMEM;
    }

    RM_LOCK(thrd_ctrl);

    LOGDBG("creating chunk requests for rdreq %d", rdreq->req_ndx);

    rdreq->chunks = all_chunk_reads;

    int i, curr_del;
    int prev_del = -1;
    int del_ndx = 0;
    chunk_read_req_t* chk_read;
    for (i = 0; i < num_vals; i++) {
        /* count the delegators */
        curr_del = keyvals[i].val.delegator_rank;
        if ((prev_del != -1) && (curr_del != prev_del)) {
            del_ndx++;
        }
        prev_del = curr_del;

        /* create chunk-reads */
        debug_log_key_val(__func__, &keyvals[i].key, &keyvals[i].val);
        chk_read = all_chunk_reads + i;
        chk_read->nbytes = keyvals[i].val.len;
        chk_read->offset = keyvals[i].key.offset;
        chk_read->log_offset = keyvals[i].val.addr;
        chk_read->log_app_id = keyvals[i].val.app_id;
        chk_read->log_client_id = keyvals[i].val.rank;
    }

    /* allocate per-delgator chunk-reads */
    int num_dels = del_ndx + 1;
    rdreq->num_remote_reads = num_dels;
    rdreq->remote_reads = (remote_chunk_reads_t*)
        calloc((size_t)num_dels, sizeof(remote_chunk_reads_t));
    if (NULL == rdreq->remote_reads) {
        LOGERR("failed to allocate remote-reads array");
        RM_UNLOCK(thrd_ctrl);
        return UNIFYFS_ERROR_NOMEM;
    }

    /* populate per-delegator chunk-reads info */
    size_t del_data_sz = 0;
    remote_chunk_reads_t* del_reads;
    prev_del = -1;
    del_ndx = 0;
    for (i = 0; i < num_vals; i++) {
        curr_del = keyvals[i].val.delegator_rank;
        if ((prev_del != -1) && (curr_del != prev_del)) {
            /* record total data for previous delegator */
            del_reads = rdreq->remote_reads + del_ndx;
            del_reads->total_sz = del_data_sz;
            /* advance to next delegator */
            del_ndx++;
            del_data_sz = 0;
        }
        prev_del = curr_del;

        /* update total data size for current delegator */
        del_data_sz += keyvals[i].val.len;

        del_reads = rdreq->remote_reads + del_ndx;
        if (0 == del_reads->num_chunks) {
            /* initialize structure */
            del_reads->rank = curr_del;
            del_reads->rdreq_id = rdreq->req_ndx;
            del_reads->reqs = all_chunk_reads + i;
            del_reads->resp = NULL;
        }
        del_reads->num_chunks++;
    }
    del_reads = rdreq->remote_reads + del_ndx;
    del_reads->total_sz = del_data_sz;

    /* wake up the request manager thread for the requesting client */
    signal_new_requests(thrd_ctrl);
    RM_UNLOCK(thrd_ctrl);

    return UNIFYFS_SUCCESS;
}

/************************
 * These functions are called by the rpc handler to assign work
 * to the request manager thread
 ***********************/

/* given an app_id, client_id and global file id,
 * compute and return file size for specified file
 */
int rm_cmd_filesize(
    int app_id,    /* app_id for requesting client */
    int client_id, /* client_id for requesting client */
    int gfid,      /* global file id of read request */
    size_t* outsize) /* output file size */
{
    /* initialize output file size to something deterministic,
     * in case we drop out with an error */
    *outsize = 0;

    /* set offset and length to request *all* key/value pairs
     * for this file */
    size_t offset = 0;

    /* want to pick the highest integer offset value a file
     * could have here */
    size_t length = (SIZE_MAX >> 1) - 1;

    /* get the locations of all the read requests from the
     * key-value store*/
    unifyfs_key_t key1, key2;

    /* create key to describe first byte we'll read */
    key1.fid    = gfid;
    key1.offset = offset;

    /* create key to describe last byte we'll read */
    key2.fid    = gfid;
    key2.offset = offset + length - 1;

    /* set up input params to specify range lookup */
    unifyfs_key_t* unifyfs_keys[2] = {&key1, &key2};
    int key_lens[2] = {sizeof(unifyfs_key_t), sizeof(unifyfs_key_t)};

    /* look up all entries in this range */
    int num_vals = 0;
    unifyfs_keyval_t* keyvals = NULL;
    int rc = unifyfs_get_file_extents(2, unifyfs_keys, key_lens,
                                      &num_vals, &keyvals);
    if (UNIFYFS_SUCCESS != rc) {
        /* failed to look up extents, bail with error */
        return UNIFYFS_FAILURE;
    }

    /* compute our file size by iterating over each file
     * segment and taking the max logical offset */
    int i;
    size_t filesize = 0;
    for (i = 0; i < num_vals; i++) {
        /* get pointer to next key value pair */
        unifyfs_keyval_t* kv = &keyvals[i];

        /* get last byte offset for this segment of the file */
        size_t last_offset = kv->key.offset + kv->val.len;

        /* update our filesize if this offset is bigger than the current max */
        if (last_offset > filesize) {
            filesize = last_offset;
        }
    }

    /* free off key/value buffer returned from get_file_extents */
    if (NULL != keyvals) {
        free(keyvals);
        keyvals = NULL;
    }

    *outsize = filesize;
    return rc;
}

int create_gfid_chunk_reads(reqmgr_thrd_t* thrd_ctrl,
                            int gfid, int app_id, int client_id,
                            int num_keys, unifyfs_key_t** keys, int* keylens)
{
    /* lookup all key/value pairs for given range */
    int num_vals = 0;
    unifyfs_keyval_t* keyvals = NULL;
    int rc = unifyfs_get_file_extents(num_keys, keys, keylens,
                                      &num_vals, &keyvals);

    /* this is to maintain limits imposed in previous code
     * that would throw fatal errors */
    if (num_vals >= UNIFYFS_MAX_SPLIT_CNT ||
        num_vals >= MAX_META_PER_SEND) {
        LOGERR("too many key/values returned in range lookup");
        if (NULL != keyvals) {
            free(keyvals);
            keyvals = NULL;
        }
        return UNIFYFS_ERROR_NOMEM;
    }

    /* TODO: if there are file extents not accounted for we should
     * either return 0 for that data (holes) or EOF if reading past
     * the end of the file */
    if (UNIFYFS_SUCCESS != rc || num_vals == 0) {
        /* failed to find any key / value pairs */
        rc = UNIFYFS_FAILURE;
    } else {
        if (num_vals > 1) {
            /* sort keyvals by delegator */
            qsort(keyvals, (size_t)num_vals, sizeof(unifyfs_keyval_t),
                  compare_kv_gfid_rank);
        }

        server_read_req_t* rdreq = reserve_read_req(thrd_ctrl);
        if (NULL == rdreq) {
            rc = UNIFYFS_FAILURE;
        } else {
            rdreq->app_id         = app_id;
            rdreq->client_id      = client_id;
            rdreq->extent.gfid    = gfid;
            rdreq->extent.errcode = EINPROGRESS;
            rc = create_chunk_requests(thrd_ctrl, rdreq,
                                       num_vals, keyvals);
            if (rc != (int)UNIFYFS_SUCCESS) {
                release_read_req(thrd_ctrl, rdreq);
            }
        }
    }

    /* free off key/value buffer returned from get_file_extents */
    if (NULL != keyvals) {
        free(keyvals);
        keyvals = NULL;
    }

    return rc;
}

/* read function for one requested extent,
 * called from rpc handler to fill shared data structures
 * with read requests to be handled by the delegator thread
 * returns before requests are handled
 */
int rm_cmd_read(
    int app_id,    /* app_id for requesting client */
    int client_id, /* client_id for requesting client */
    int gfid,      /* global file id of read request */
    size_t offset, /* logical file offset of read request */
    size_t length) /* number of bytes to read */
{
    /* get pointer to app structure for this app id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* get thread id for this client */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* look up thread control structure */
    reqmgr_thrd_t* thrd_ctrl = rm_get_thread(thrd_id);

    /* get chunks corresponding to requested client read extent
     *
     * Generate a pair of keys for the read request, representing the start
     * and end offset. MDHIM returns all key-value pairs that fall within
     * the offset range.
     *
     * TODO: this is specific to the MDHIM in the source tree and not portable
     *       to other KV-stores. This needs to be revisited to utilize some
     *       other mechanism to retrieve all relevant key-value pairs from the
     *       KV-store.
     */
    unifyfs_key_t key1, key2;

    /* create key to describe first byte we'll read */
    key1.fid    = gfid;
    key1.offset = offset;

    /* create key to describe last byte we'll read */
    key2.fid    = gfid;
    key2.offset = offset + length - 1;

    unifyfs_key_t* unifyfs_keys[2] = {&key1, &key2};
    int key_lens[2] = {sizeof(unifyfs_key_t), sizeof(unifyfs_key_t)};

    return create_gfid_chunk_reads(thrd_ctrl, gfid, app_id, client_id,
                                   2, unifyfs_keys, key_lens);
}

/* send the read requests to the remote delegators
 *
 * @param app_id: application id
 * @param client_id: client id for requesting process
 * @param gfid: global file id
 * @param req_num: number of read requests
 * @param reqbuf: read requests buffer
 * @return success/error code */
int rm_cmd_mread(int app_id, int client_id,
                 size_t req_num, void* reqbuf)
{
    /* get pointer to app structure for this app id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* get thread id for this client */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* look up thread control structure */
    reqmgr_thrd_t* thrd_ctrl = rm_get_thread(thrd_id);

    /* get debug rank for this client */
    int cli_rank = app_config->dbg_ranks[client_id];

     /* get the locations of all the read requests from the key-value store */
    unifyfs_ReadRequest_table_t readRequest =
        unifyfs_ReadRequest_as_root(reqbuf);
    unifyfs_Extent_vec_t extents = unifyfs_ReadRequest_extents(readRequest);
    size_t extents_len = unifyfs_Extent_vec_len(extents);
    assert(extents_len == req_num);

    // allocate key storage
    unifyfs_key_t** unifyfs_keys;
    int* key_lens;
    size_t key_cnt = req_num * 2;
    unifyfs_keys = alloc_key_array(key_cnt);
    key_lens = (int*) calloc(key_cnt, sizeof(int));
    if ((NULL == unifyfs_keys) ||
        (NULL == key_lens)) {
        // this is a fatal error
        // TODO: we need better error handling
        LOGERR("Error allocating buffers");
        return (int)UNIFYFS_ERROR_NOMEM;
    }

    /* get chunks corresponding to requested client read extents */
    int rc, num_keys;
    int fid = -1;
    int last_fid = -1;
    int ndx = 0;
    size_t j, eoff, elen;
    for (j = 0; j < req_num; j++) {
        fid = unifyfs_Extent_fid(unifyfs_Extent_vec_at(extents, j));
        if (j && (fid != last_fid)) {
            // create requests for all extents of last_fid
            num_keys = ndx;
            rc = create_gfid_chunk_reads(thrd_ctrl, last_fid, app_id,
                                         client_id, num_keys,
                                         unifyfs_keys, key_lens);
            if (rc != UNIFYFS_SUCCESS) {
                LOGERR("Error creating chunk reads for gfid=%d", last_fid);
            }
            // reset ndx for current fid
            ndx = 0;
        }

        eoff = unifyfs_Extent_offset(unifyfs_Extent_vec_at(extents, j));
        elen = unifyfs_Extent_length(unifyfs_Extent_vec_at(extents, j));
        LOGDBG("gfid:%d, offset:%zu, length:%zu", fid, eoff, elen);

        /* Generate a pair of keys for each read request, representing
         * the start and end offsets. MDHIM returns all key-value pairs that
         * fall within the offset range.
         *
         * TODO: this is specific to the MDHIM in the source tree and not
         *       portable to other KV-stores. This needs to be revisited to
         *       utilize some other mechanism to retrieve all relevant KV
         *       pairs from the KV-store.
         */
        key_lens[ndx] = sizeof(unifyfs_key_t);
        key_lens[ndx + 1] = sizeof(unifyfs_key_t);

        /* create key to describe first byte we'll read */
        unifyfs_keys[ndx]->fid = fid;
        unifyfs_keys[ndx]->offset = eoff;

        /* create key to describe last byte we'll read */
        unifyfs_keys[ndx + 1]->fid = fid;
        unifyfs_keys[ndx + 1]->offset = eoff + elen - 1;

        ndx += 2;
        last_fid = fid;
    }

    // create requests for all extents of last_fid
    num_keys = ndx;
    rc = create_gfid_chunk_reads(thrd_ctrl, last_fid, app_id,
                                 client_id, num_keys,
                                 unifyfs_keys, key_lens);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("Error creating chunk reads for gfid=%d", last_fid);
    }

    // cleanup
    free_key_array(unifyfs_keys);
    free(key_lens);

    return rc;
}

/* function called by main thread to instruct
 * resource manager thread to exit,
 * returns UNIFYFS_SUCCESS on success */
int rm_cmd_exit(reqmgr_thrd_t* thrd_ctrl)
{
    /* grab the lock */
    RM_LOCK(thrd_ctrl);

    if (thrd_ctrl->exited) {
        /* already done */
        RM_UNLOCK(thrd_ctrl);
        return UNIFYFS_SUCCESS;
    }

    /* if delegator thread is not waiting in critical
     * section, let's wait on it to come back */
    if (!thrd_ctrl->has_waiting_delegator) {
        /* delegator thread is not in critical section,
         * tell it we've got something and signal it */
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* we're no longer waiting */
        thrd_ctrl->has_waiting_dispatcher = 0;
    }

    /* inform delegator thread that it's time to exit */
    thrd_ctrl->exit_flag = 1;

    /* signal delegator thread */
    pthread_cond_signal(&thrd_ctrl->thrd_cond);

    /* release the lock */
    RM_UNLOCK(thrd_ctrl);

    /* wait for delegator thread to exit */
    void* status;
    pthread_join(thrd_ctrl->thrd, &status);
    thrd_ctrl->exited = 1;

    return UNIFYFS_SUCCESS;
}

/*
 * synchronize all the indices and file attributes
 * to the key-value store
 *
 * @param app_id: the application id
 * @param client_side_id: client rank in app
 * @param gfid: global file id
 * @return success/error code
 */
int rm_cmd_fsync(int app_id, int client_side_id, int gfid)
{
    size_t i;

    /* assume we'll succeed */
    int ret = (int)UNIFYFS_SUCCESS;

    /* pointers to memory we'll dynamically allocate for file extents */
    unifyfs_key_t** unifyfs_keys = NULL;
    unifyfs_val_t** unifyfs_vals = NULL;
    int* unifyfs_key_lens        = NULL;
    int* unifyfs_val_lens        = NULL;

    /* pointers to memory we'll dynamically allocate for file attributes */
    fattr_key_t** fattr_keys         = NULL;
    unifyfs_file_attr_t** fattr_vals = NULL;
    int* fattr_key_lens              = NULL;
    int* fattr_val_lens              = NULL;

    /* get memory page size on this machine */
    int page_sz = getpagesize();

    /* get app config struct for this client */
    app_config_t* app_config = (app_config_t*)
        arraylist_get(app_config_list, app_id);

    /* get pointer to superblock for this client and app */
    char* superblk = app_config->shm_superblocks[client_side_id];

    /* get pointer to start of key/value region in superblock */
    char* meta = superblk + app_config->meta_offset;

    /* get pointer to start of file attribute region in superblock */
    char* fmeta = superblk + app_config->fmeta_offset;

    /* get number of file extent index values client has for us,
     * stored as a size_t value in meta region of shared memory */
    size_t extent_num_entries = *(size_t*)(meta);

    /* indices are stored in the superblock shared memory
     * created by the client, these are stored as index_t
     * structs starting one page size offset into meta region */
    char* ptr_extents = meta + page_sz;
    unifyfs_index_t* meta_payload = (unifyfs_index_t*)(ptr_extents);

    /* allocate storage for file extent key/values */
    /* TODO: possibly get this from memory pool */
    unifyfs_keys     = alloc_key_array(extent_num_entries);
    unifyfs_vals     = alloc_value_array(extent_num_entries);
    unifyfs_key_lens = calloc(extent_num_entries, sizeof(int));
    unifyfs_val_lens = calloc(extent_num_entries, sizeof(int));
    if ((NULL == unifyfs_keys) ||
        (NULL == unifyfs_vals) ||
        (NULL == unifyfs_key_lens) ||
        (NULL == unifyfs_val_lens)) {
        LOGERR("failed to allocate memory for file extents");
        ret = (int)UNIFYFS_ERROR_NOMEM;
        goto rm_cmd_fsync_exit;
    }

    /* create file extent key/values for insertion into MDHIM */
    for (i = 0; i < extent_num_entries; i++) {
        /* for a key, we store the global file id and logical file offset */
        unifyfs_key_t* key = unifyfs_keys[i];
        key->fid    = meta_payload[i].fid;
        key->offset = meta_payload[i].file_pos;

        /* for the value, we store the log position, the length,
         * the host server (delegator rank), the mount point id (app id),
         * and the client id (rank) */
        unifyfs_val_t* val = unifyfs_vals[i];
        val->addr           = meta_payload[i].log_pos;
        val->len            = meta_payload[i].length;
        val->delegator_rank = glb_pmi_rank;
        val->app_id         = app_id;
        val->rank           = client_side_id;

        LOGDBG("extent - fid:%d, offset:%zu, length:%zu, app:%d, clid:%d",
               key->fid, key->offset,
               val->len, val->app_id, val->rank);

        /* MDHIM needs to know the byte size of each key and value */
        unifyfs_key_lens[i] = sizeof(unifyfs_key_t);
        unifyfs_val_lens[i] = sizeof(unifyfs_val_t);
    }

    /* batch insert file extent key/values into MDHIM */
    ret = unifyfs_set_file_extents((int)extent_num_entries,
                                   unifyfs_keys, unifyfs_key_lens,
                                   unifyfs_vals, unifyfs_val_lens);
    if (ret != UNIFYFS_SUCCESS) {
        /* TODO: need proper error handling */
        LOGERR("unifyfs_set_file_extents() failed");
        goto rm_cmd_fsync_exit;
    }

    /* get number of file attribute values client has for us,
     * stored as a size_t value in fmeta region of shared memory */
    size_t attr_num_entries = *(size_t*)(fmeta);

    /* file attributes are stored in the superblock shared memory
     * created by the client */
    char* ptr_fattr = fmeta + page_sz;
    unifyfs_file_attr_t* attr_payload = (unifyfs_file_attr_t*)(ptr_fattr);

    /* allocate storage for file attribute key/values */
    /* TODO: possibly get this from memory pool */
    fattr_keys     = alloc_attr_key_array(attr_num_entries);
    fattr_vals     = calloc(attr_num_entries, sizeof(unifyfs_file_attr_t*));
    fattr_key_lens = calloc(attr_num_entries, sizeof(int));
    fattr_val_lens = calloc(attr_num_entries, sizeof(int));
    if ((NULL == fattr_keys) ||
        (NULL == fattr_vals) ||
        (NULL == fattr_key_lens) ||
        (NULL == fattr_val_lens)) {
        LOGERR("failed to allocate memory for file attributes");
        ret = (int)UNIFYFS_ERROR_NOMEM;
        goto rm_cmd_fsync_exit;
    }

    /* create file attribute key/values for insertion into MDHIM */
    for (i = 0; i < attr_num_entries; i++) {
        /* for a key, we use the global file id */
        *fattr_keys[i] = attr_payload[i].gfid;

        /* for the value, we'll store a file_attr structure */
        fattr_vals[i] = &(attr_payload[i]);

        /* MDHIM needs to know the byte size of each key and value */
        fattr_key_lens[i] = sizeof(fattr_key_t);
        fattr_val_lens[i] = sizeof(fattr_val_t);
    }

    /* batch insert file attribute key/values into MDHIM */
    ret = unifyfs_set_file_attributes((int)attr_num_entries,
                                      fattr_keys, fattr_key_lens,
                                      fattr_vals, fattr_val_lens);
    if (ret != UNIFYFS_SUCCESS) {
        /* TODO: need proper error handling */
        LOGERR("unifyfs_set_file_attributes() failed");
        goto rm_cmd_fsync_exit;
    }

rm_cmd_fsync_exit:
    /* clean up memory */

    if (NULL != unifyfs_keys) {
        free_key_array(unifyfs_keys);
    }

    if (NULL != unifyfs_vals) {
        free_value_array(unifyfs_vals);
    }

    if (NULL != unifyfs_key_lens) {
        free(unifyfs_key_lens);
    }

    if (NULL != unifyfs_val_lens) {
        free(unifyfs_val_lens);
    }

    if (NULL != fattr_keys) {
        free_attr_key_array(fattr_keys);
    }

    if (NULL != fattr_vals) {
        free(fattr_vals);
    }

    if (NULL != fattr_key_lens) {
        free(fattr_key_lens);
    }

    if (NULL != fattr_val_lens) {
        free(fattr_val_lens);
    }

    return ret;
}

/************************
 * These functions define the logic of the request manager thread
 ***********************/

/* pack the chunk read requests for a single remote delegator.
 *
 * @param req_msg_buf: request buffer used for packing
 * @param req_num: number of read requests
 * @return size of packed buffer (or error code)
 */
static size_t rm_pack_chunk_requests(char* req_msg_buf,
                                     remote_chunk_reads_t* remote_reads)
{
    /* send format:
     *   (int) cmd     - specifies type of message (SVC_CMD_RDREQ_CHK)
     *   (int) req_cnt - number of requests in message
     *   {sequence of chunk_read_req_t} */
    int req_cnt = remote_reads->num_chunks;
    size_t reqs_sz = req_cnt * sizeof(chunk_read_req_t);
    size_t packed_size = (2 * sizeof(int)) + sizeof(size_t) + reqs_sz;

    assert(req_cnt < MAX_META_PER_SEND);

    /* get pointer to start of send buffer */
    char* ptr = req_msg_buf;
    memset(ptr, 0, packed_size);

    /* pack command */
    int cmd = (int)SVC_CMD_RDREQ_CHK;
    *((int*)ptr) = cmd;
    ptr += sizeof(int);

    /* pack request count */
    *((int*)ptr) = req_cnt;
    ptr += sizeof(int);

    /* pack total requested data size */
    *((size_t*)ptr) = remote_reads->total_sz;
    ptr += sizeof(size_t);

    /* copy requests into buffer */
    memcpy(ptr, remote_reads->reqs, reqs_sz);
    ptr += reqs_sz;

    /* return number of bytes used to pack requests */
    return packed_size;
}

/* send the chunk read requests to remote delegators
 *
 * @param thrd_ctrl : reqmgr thread control structure
 * @return success/error code
 */
static int rm_request_remote_chunks(reqmgr_thrd_t* thrd_ctrl)
{
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    int i, j, rc;
    int ret = (int)UNIFYFS_SUCCESS;

    /* get pointer to send buffer */
    char* sendbuf = thrd_ctrl->del_req_msg_buf;

    /* iterate over each active read request */
    for (i = 0; i < RM_MAX_ACTIVE_REQUESTS; i++) {
        server_read_req_t* req = thrd_ctrl->read_reqs + i;
        if (req->num_remote_reads > 0) {
            LOGDBG("read req %d is active", i);
            debug_print_read_req(req);
            if (req->status == READREQ_INIT) {
                req->status = READREQ_STARTED;
                /* iterate over each delegator we need to send requests to */
                remote_chunk_reads_t* remote_reads;
                size_t packed_sz;
                for (j = 0; j < req->num_remote_reads; j++) {
                    remote_reads = req->remote_reads + j;
                    remote_reads->status = READREQ_STARTED;

                    /* pack requests into send buffer, get packed size */
                    packed_sz = rm_pack_chunk_requests(sendbuf, remote_reads);

                    /* get rank of target delegator */
                    int del_rank = remote_reads->rank;

                    /* send requests */
                    LOGDBG("[%d of %d] sending %d chunk requests to server %d",
                           j, req->num_remote_reads,
                           remote_reads->num_chunks, del_rank);
                    rc = invoke_chunk_read_request_rpc(del_rank, req,
                                                       remote_reads->num_chunks,
                                                       sendbuf, packed_sz);
                    if (rc != (int)UNIFYFS_SUCCESS) {
                        ret = rc;
                        LOGERR("server request rpc to %d failed - %s",
                               del_rank,
                               unifyfs_error_enum_str((unifyfs_error_e)rc));
                    }
                }
            } else {
                /* already started */
                LOGDBG("read req %d already processed", i);
            }
        }
    }

    return ret;
}

/* send the chunk read requests to remote delegators
 *
 * @param thrd_ctrl : reqmgr thread control structure
 * @return success/error code
 */
static int rm_process_remote_chunk_responses(reqmgr_thrd_t* thrd_ctrl)
{
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    int i, j, rc;
    int ret = (int)UNIFYFS_SUCCESS;

    /* iterate over each active read request */
    for (i = 0; i < RM_MAX_ACTIVE_REQUESTS; i++) {
        server_read_req_t* req = thrd_ctrl->read_reqs + i;
        if ((req->num_remote_reads > 0) &&
            (req->status == READREQ_STARTED)) {
            /* iterate over each delegator we need to send requests to */
            remote_chunk_reads_t* rcr;
            for (j = 0; j < req->num_remote_reads; j++) {
                rcr = req->remote_reads + j;
                if (NULL == rcr->resp) {
                    continue;
                }
                LOGDBG("found read req %d responses from delegator %d",
                       i, rcr->rank);
                rc = rm_handle_chunk_read_responses(thrd_ctrl, req, rcr);
                if (rc != (int)UNIFYFS_SUCCESS) {
                    LOGERR("failed to handle chunk read responses");
                    ret = rc;
                }
            }
        }
    }

    return ret;
}

/* signal the client process for it to start processing read
 * data in shared memory */
static int client_signal(shm_header_t* hdr,
                         shm_region_state_e flag)
{
    if (flag == SHMEM_REGION_DATA_READY) {
        LOGDBG("setting data-ready");
    } else if (flag == SHMEM_REGION_DATA_COMPLETE) {
        LOGDBG("setting data-complete");
    }
    hdr->state = flag;
    /* TODO: MEM_FLUSH */
    return UNIFYFS_SUCCESS;
}

/* wait until client has processed all read data in shared memory */
static int client_wait(shm_header_t* hdr)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* specify time to sleep between checking flag in shared
     * memory indicating client has processed data */
    struct timespec shm_wait_tm;
    shm_wait_tm.tv_sec  = 0;
    shm_wait_tm.tv_nsec = SHM_WAIT_INTERVAL;

    /* wait for client to set flag to 0 */
    int max_sleep = 10000000; // 10s
    volatile int* vip = (volatile int*)&(hdr->state);
    while (*vip != SHMEM_REGION_EMPTY) {
        /* not there yet, sleep for a while */
        nanosleep(&shm_wait_tm, NULL);
        /* TODO: MEM_FETCH */
        max_sleep--;
        if (0 == max_sleep) {
            LOGERR("timed out waiting for empty");
            rc = (int)UNIFYFS_ERROR_SHMEM;
            break;
        }
    }

    /* reset header to reflect empty state */
    hdr->meta_cnt = 0;
    hdr->bytes = 0;
    return rc;
}

static shm_meta_t* reserve_shmem_meta(app_config_t* app_config,
                                      shm_header_t* hdr,
                                      size_t data_sz)
{
    shm_meta_t* meta = NULL;
    if (NULL == hdr) {
        LOGERR("invalid header");
    } else {
        pthread_mutex_lock(&(hdr->sync));
        LOGDBG("shm_header(cnt=%zu, bytes=%zu)", hdr->meta_cnt, hdr->bytes);
        size_t remain_size = app_config->recv_buf_sz -
                             (sizeof(shm_header_t) + hdr->bytes);
        size_t meta_size = sizeof(shm_meta_t) + data_sz;
        if (meta_size > remain_size) {
            /* client-side receive buffer is full,
             * inform client to start reading */
            LOGDBG("need more space in client recv buffer");
            client_signal(hdr, SHMEM_REGION_DATA_READY);

            /* wait for client to read data */
            int rc = client_wait(hdr);
            if (rc != (int)UNIFYFS_SUCCESS) {
                LOGERR("wait for client recv buffer space failed");
                return NULL;
            }
        }
        size_t shm_offset = hdr->bytes;
        char* shm_buf = ((char*)hdr) + sizeof(shm_header_t);
        meta = (shm_meta_t*)(shm_buf + shm_offset);
        LOGDBG("reserved shm_meta[%zu] and %zu payload bytes",
               hdr->meta_cnt, data_sz);
        hdr->meta_cnt++;
        hdr->bytes += meta_size;
        pthread_mutex_unlock(&(hdr->sync));
    }
    return meta;
}

int rm_post_chunk_read_responses(int app_id,
                                 int client_id,
                                 int src_rank,
                                 int req_id,
                                 int num_chks,
                                 size_t bulk_sz,
                                 char* resp_buf)
{
    int rc, thrd_id;
    app_config_t* app_config = NULL;
    reqmgr_thrd_t* thrd_ctrl = NULL;
    server_read_req_t* rdreq = NULL;
    remote_chunk_reads_t* del_reads = NULL;

    /* lookup RM thread control structure for this app id */
    app_config = (app_config_t*) arraylist_get(app_config_list, app_id);
    assert(NULL != app_config);
    thrd_id = app_config->thrd_idxs[client_id];
    thrd_ctrl = rm_get_thread(thrd_id);
    assert(NULL != thrd_ctrl);

    RM_LOCK(thrd_ctrl);

    /* find read req associated with req_id */
    rdreq = thrd_ctrl->read_reqs + req_id;
    for (int i = 0; i < rdreq->num_remote_reads; i++) {
        if (rdreq->remote_reads[i].rank == src_rank) {
            del_reads = rdreq->remote_reads + i;
            break;
        }
    }
    if (NULL != del_reads) {
        LOGDBG("posting chunk responses for req %d from delegator %d",
               req_id, src_rank);
        del_reads->resp = (chunk_read_resp_t*)resp_buf;
        if (del_reads->num_chunks != num_chks) {
            LOGERR("mismatch on request vs. response chunks");
            del_reads->num_chunks = num_chks;
        }
        del_reads->total_sz = bulk_sz;
        rc = (int)UNIFYFS_SUCCESS;
    } else {
        LOGERR("failed to find matching chunk-reads request");
        rc = (int)UNIFYFS_FAILURE;
    }

    /* inform the request manager thread we added responses */
    signal_new_responses(thrd_ctrl);

    RM_UNLOCK(thrd_ctrl);

    return rc;
}

/* process the requested chunk data returned from service managers
 *
 * @param thrd_ctrl  : request manager thread state
 * @param rdreq      : server read request
 * @param del_reads  : remote server chunk reads
 * @return success/error code
 */
int rm_handle_chunk_read_responses(reqmgr_thrd_t* thrd_ctrl,
                                   server_read_req_t* rdreq,
                                   remote_chunk_reads_t* del_reads)
{
    int errcode, gfid, i, num_chks, rc, thrd_id;
    int ret = (int)UNIFYFS_SUCCESS;
    app_config_t* app_config = NULL;
    chunk_read_resp_t* responses = NULL;
    shm_header_t* client_shm = NULL;
    shm_meta_t* shm_meta = NULL;
    void* shm_buf = NULL;
    char* data_buf = NULL;
    size_t data_sz, offset;

    assert((NULL != thrd_ctrl) &&
           (NULL != rdreq) &&
           (NULL != del_reads) &&
           (NULL != del_reads->resp));

    /* look up client shared memory region */
    app_config = (app_config_t*) arraylist_get(app_config_list, rdreq->app_id);
    assert(NULL != app_config);
    client_shm = (shm_header_t*) app_config->shm_recv_bufs[rdreq->client_id];

    RM_LOCK(thrd_ctrl);

    num_chks = del_reads->num_chunks;
    gfid = rdreq->extent.gfid;
    if (del_reads->status != READREQ_STARTED) {
        LOGERR("chunk read response for non-started req @ index=%d",
               rdreq->req_ndx);
        ret = (int32_t)UNIFYFS_ERROR_INVAL;
    } else if (0 == del_reads->total_sz) {
        LOGERR("empty chunk read response for gfid=%d", gfid);
        ret = (int32_t)UNIFYFS_ERROR_INVAL;
    } else {
        LOGDBG("handling chunk read responses from server %d: "
               "gfid=%d num_chunks=%d buf_size=%zu",
               del_reads->rank, gfid, num_chks,
               del_reads->total_sz);
        responses = del_reads->resp;
        data_buf = (char*)(responses + num_chks);
        for (i = 0; i < num_chks; i++) {
            chunk_read_resp_t* resp = responses + i;
            if (resp->read_rc < 0) {
                errcode = (int)-(resp->read_rc);
                data_sz = 0;
            } else {
                errcode = 0;
                data_sz = resp->nbytes;
            }
            offset = resp->offset;
            LOGDBG("chunk response for offset=%zu: sz=%zu", offset, data_sz);

            /* allocate and register local target buffer for bulk access */
            shm_meta = reserve_shmem_meta(app_config, client_shm, data_sz);
            if (NULL != shm_meta) {
                shm_meta->offset = offset;
                shm_meta->length = data_sz;
                shm_meta->gfid = gfid;
                shm_meta->errcode = errcode;
                shm_buf = (void*)((char*)shm_meta + sizeof(shm_meta_t));
                if (data_sz) {
                    memcpy(shm_buf, data_buf, data_sz);
                }
            } else {
                LOGERR("failed to reserve shmem space for read reply")
                ret = (int32_t)UNIFYFS_ERROR_SHMEM;
            }
            data_buf += data_sz;
        }
        /* cleanup */
        free((void*)responses);
        del_reads->resp = NULL;

        /* update request status */
        del_reads->status = READREQ_COMPLETE;
        if (rdreq->status == READREQ_STARTED) {
            rdreq->status = READREQ_PARTIAL_COMPLETE;
        }
        int completed_remote_reads = 0;
        for (i = 0; i < rdreq->num_remote_reads; i++) {
            if (rdreq->remote_reads[i].status != READREQ_COMPLETE) {
                break;
            }
            completed_remote_reads++;
        }
        if (completed_remote_reads == rdreq->num_remote_reads) {
            rdreq->status = READREQ_COMPLETE;

            /* signal client that we're now done writing data */
            client_signal(client_shm, SHMEM_REGION_DATA_COMPLETE);

            /* wait for client to read data */
            client_wait(client_shm);

            rc = release_read_req(thrd_ctrl, rdreq);
            if (rc != (int)UNIFYFS_SUCCESS) {
                LOGERR("failed to release server_read_req_t");
            }
        }
    }

    RM_UNLOCK(thrd_ctrl);

    return ret;
}

/* Entry point for request manager thread. One thread is created
 * for each client process to retrieve remote data and notify the
 * client when data is ready.
 *
 * @param arg: pointer to RM thread control structure
 * @return NULL */
void* rm_delegate_request_thread(void* arg)
{
    /* get pointer to our thread control structure */
    reqmgr_thrd_t* thrd_ctrl = (reqmgr_thrd_t*) arg;

    LOGDBG("I am request manager thread!");

    /* loop forever to handle read requests from the client,
     * new requests are added to a list on a shared data structure
     * with main thread, new items inserted by the rpc handler */
    int rc;
    while (1) {
        /* grab lock */
        RM_LOCK(thrd_ctrl);

        /* process any chunk read responses */
        rc = rm_process_remote_chunk_responses(thrd_ctrl);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to process remote chunk responses");
        }

        /* inform dispatcher that we're waiting for work
         * inside the critical section */
        thrd_ctrl->has_waiting_delegator = 1;

        /* if dispatcher is waiting on us, signal it to go ahead,
         * this coordination ensures that we'll be the next thread
         * to grab the lock after the dispatcher has assigned us
         * some work (rather than the dispatcher grabbing the lock
         * and assigning yet more work) */
        if (thrd_ctrl->has_waiting_dispatcher == 1) {
            pthread_cond_signal(&thrd_ctrl->thrd_cond);
        }

        /* release lock and wait to be signaled by dispatcher */
        LOGDBG("RM[%d] waiting for work", thrd_ctrl->thrd_ndx);
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* set flag to indicate we're no longer waiting */
        thrd_ctrl->has_waiting_delegator = 0;

        /* go do work ... */
        LOGDBG("RM[%d] got work", thrd_ctrl->thrd_ndx);

        /* release lock and bail out if we've been told to exit */
        if (thrd_ctrl->exit_flag == 1) {
            RM_UNLOCK(thrd_ctrl);
            break;
        }

        /* send chunk read requests to remote servers */
        rc = rm_request_remote_chunks(thrd_ctrl);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to request remote chunks");
        }

        /* release lock */
        RM_UNLOCK(thrd_ctrl);
    }

    LOGDBG("request manager thread exiting");

    return NULL;
}

/* BEGIN MARGO SERVER-SERVER RPC INVOCATION FUNCTIONS */

#if 0 // DISABLE UNUSED RPCS
/* invokes the server_hello rpc */
int invoke_server_hello_rpc(int dst_srvr_rank)
{
    int rc = (int)UNIFYFS_SUCCESS;
    hg_handle_t handle;
    server_hello_in_t in;
    server_hello_out_t out;
    hg_return_t hret;
    hg_addr_t dst_srvr_addr;
    char hello_msg[UNIFYFS_MAX_HOSTNAME];

    assert(dst_srvr_rank < (int)glb_num_servers);
    dst_srvr_addr = glb_servers[dst_srvr_rank].margo_svr_addr;

    hret = margo_create(unifyfsd_rpc_context->svr_mid, dst_srvr_addr,
                        unifyfsd_rpc_context->rpcs.hello_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    snprintf(hello_msg, sizeof(hello_msg), "hello from %s", glb_host);
    in.src_rank = (int32_t)glb_pmi_rank;
    in.message_str = strdup(hello_msg);

    LOGDBG("invoking the server-hello rpc function");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        rc = (int)UNIFYFS_FAILURE;
    } else {
        /* decode response */
        hret = margo_get_output(handle, &out);
        if (hret == HG_SUCCESS) {
            int32_t ret = out.ret;
            LOGDBG("Got hello rpc response from %d - ret=%" PRIi32,
                   dst_srvr_rank, ret);
            margo_free_output(handle, &out);
        } else {
            rc = (int)UNIFYFS_FAILURE;
        }
    }

    free((void*)in.message_str);
    margo_destroy(handle);

    return rc;
}

/* invokes the server_request rpc */
int invoke_server_request_rpc(int dst_srvr_rank, int req_id, int tag,
                              void* data_buf, size_t buf_sz)
{
    int rc = (int)UNIFYFS_SUCCESS;
    hg_handle_t handle;
    server_request_in_t in;
    server_request_out_t out;
    hg_return_t hret;
    hg_addr_t dst_srvr_addr;
    hg_size_t bulk_sz = buf_sz;

    if (dst_srvr_rank == glb_pmi_rank) {
        // short-circuit for local requests
        return rc;
    }

    assert(dst_srvr_rank < (int)glb_num_servers);
    dst_srvr_addr = glb_servers[dst_srvr_rank].margo_svr_addr;

    hret = margo_create(unifyfsd_rpc_context->svr_mid, dst_srvr_addr,
                        unifyfsd_rpc_context->rpcs.request_id, &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.src_rank = (int32_t)glb_pmi_rank;
    in.req_id = (int32_t)req_id;
    in.req_tag = (int32_t)tag;
    in.bulk_size = bulk_sz;

    /* register request buffer for bulk remote access */
    hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                             &data_buf, &bulk_sz,
                             HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    LOGDBG("invoking the server-request rpc function");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        rc = (int)UNIFYFS_FAILURE;
    } else {
        /* decode response */
        hret = margo_get_output(handle, &out);
        if (hret == HG_SUCCESS) {
            rc = (int)out.ret;
            LOGDBG("Got request rpc response from %d - ret=%d",
                   dst_srvr_rank, rc);
            margo_free_output(handle, &out);
        } else {
            rc = (int)UNIFYFS_FAILURE;
        }
    }

    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);

    return rc;
}
#endif // DISABLE UNUSED RPCS

/* invokes the server_request rpc */
int invoke_chunk_read_request_rpc(int dst_srvr_rank,
                                  server_read_req_t* rdreq,
                                  int num_chunks,
                                  void* data_buf, size_t buf_sz)
{
    int rc = (int)UNIFYFS_SUCCESS;
    hg_handle_t handle;
    chunk_read_request_in_t in;
    chunk_read_request_out_t out;
    hg_return_t hret;
    hg_addr_t dst_srvr_addr;
    hg_size_t bulk_sz = buf_sz;

    if (dst_srvr_rank == glb_pmi_rank) {
        // short-circuit for local requests
        return sm_issue_chunk_reads(glb_pmi_rank,
                                    rdreq->app_id,
                                    rdreq->client_id,
                                    rdreq->req_ndx,
                                    num_chunks,
                                    (char*)data_buf);
    }

    assert(dst_srvr_rank < (int)glb_num_servers);
    dst_srvr_addr = glb_servers[dst_srvr_rank].margo_svr_addr;

    hret = margo_create(unifyfsd_rpc_context->svr_mid, dst_srvr_addr,
                        unifyfsd_rpc_context->rpcs.chunk_read_request_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.src_rank = (int32_t)glb_pmi_rank;
    in.app_id = (int32_t)rdreq->app_id;
    in.client_id = (int32_t)rdreq->client_id;
    in.req_id = (int32_t)rdreq->req_ndx;
    in.num_chks = (int32_t)num_chunks;
    in.bulk_size = bulk_sz;

    /* register request buffer for bulk remote access */
    hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                             &data_buf, &bulk_sz,
                             HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    LOGDBG("invoking the chunk-read-request rpc function");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        rc = (int)UNIFYFS_FAILURE;
    } else {
        /* decode response */
        hret = margo_get_output(handle, &out);
        if (hret == HG_SUCCESS) {
            rc = (int)out.ret;
            LOGDBG("Got request rpc response from %d - ret=%d",
                   dst_srvr_rank, rc);
            margo_free_output(handle, &out);
        } else {
            rc = (int)UNIFYFS_FAILURE;
        }
    }

    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);

    return rc;
}

/* BEGIN MARGO SERVER-SERVER RPC HANDLER FUNCTIONS */

/* handler for remote read request response */
static void chunk_read_response_rpc(hg_handle_t handle)
{
    int rc, src_rank, req_id;
    int app_id, client_id, thrd_id;
    int i, num_chks;
    int32_t ret;
    hg_return_t hret;
    hg_bulk_t bulk_handle;
    size_t bulk_sz;
    chunk_read_response_in_t in;
    chunk_read_response_out_t out;
    void* resp_buf = NULL;

    /* get input params */
    rc = margo_get_input(handle, &in);
    assert(rc == HG_SUCCESS);
    src_rank = (int)in.src_rank;
    app_id = (int)in.app_id;
    client_id = (int)in.client_id;
    req_id = (int)in.req_id;
    num_chks = (int)in.num_chks;
    bulk_sz = (size_t)in.bulk_size;

    if (0 == bulk_sz) {
        LOGERR("empty response buffer");
        ret = (int32_t)UNIFYFS_ERROR_INVAL;
    } else {
        resp_buf = malloc(bulk_sz);
        if (NULL == resp_buf) {
            LOGERR("failed to allocate chunk read responses buffer");
            ret = (int32_t)UNIFYFS_ERROR_NOMEM;
        } else {
            /* pull response data */
            ret = (int32_t)UNIFYFS_SUCCESS;
            const struct hg_info* hgi = margo_get_info(handle);
            assert(NULL != hgi);
            margo_instance_id mid = margo_hg_info_get_instance(hgi);
            assert(mid != MARGO_INSTANCE_NULL);
            hret = margo_bulk_create(mid, 1, &resp_buf, &in.bulk_size,
                                     HG_BULK_WRITE_ONLY, &bulk_handle);
            assert(hret == HG_SUCCESS);
            hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                                       in.bulk_handle, 0,
                                       bulk_handle, 0, in.bulk_size);
            assert(hret == HG_SUCCESS);

            rc = rm_post_chunk_read_responses(app_id, client_id,
                                              src_rank, req_id, num_chks,
                                              bulk_sz, (char*)resp_buf);
            if (rc != (int)UNIFYFS_SUCCESS) {
                LOGERR("failed to handle chunk read responses")
                ret = rc;
            }
            margo_bulk_free(bulk_handle);
        }
    }

    /* fill output structure and return to caller */
    out.ret = ret;
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(chunk_read_response_rpc)
