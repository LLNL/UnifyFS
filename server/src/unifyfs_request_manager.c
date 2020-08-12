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



// general support
#include "unifyfs_global.h"

// server components
#include "unifyfs_inode_tree.h"
#include "unifyfs_metadata_mdhim.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"

// margo rpcs
#include "unifyfs_group_rpc.h"
#include "unifyfs_server_rpcs.h"
#include "margo_server.h"

// system headers
#include <time.h> // nanosleep

#define RM_LOCK(rm) \
do { \
    LOGDBG("locking RM[%d:%d] state", rm->app_id, rm->client_id); \
    pthread_mutex_lock(&(rm->thrd_lock)); \
} while (0)

#define RM_UNLOCK(rm) \
do { \
    LOGDBG("unlocking RM[%d:%d] state", rm->app_id, rm->client_id); \
    pthread_mutex_unlock(&(rm->thrd_lock)); \
} while (0)

arraylist_t* rm_thrd_list;

/* One request manager thread is created for each client that a
 * server serves.  The margo rpc handler thread(s) assign work
 * to the request manager thread to retrieve data and send
 * it back to the client.
 *
 * To start, given a read request from the client (via rpc)
 * the handler function on the main server first queries the
 * key/value store using the given file id and byte range to obtain
 * the meta data on the physical location of the file data.  This
 * meta data provides the host server rank, the app/client
 * ids that specify the log file on the remote server, the
 * offset within the log file and the length of data.  The rpc
 * handler function sorts the meta data by host server rank,
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

    /* initialize lock for shared data structures of the
     * request manager */
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

    /* initialize condition variable to synchronize work
     * notifications for the request manager thread */
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

static void debug_print_read_req(server_read_req_t* req)
{
    if (NULL != req) {
        LOGDBG("server_read_req[%d] status=%d, num_remote=%d",
               req->req_ndx, req->status, req->num_remote_reads);
    }
}

server_read_req_t* rm_reserve_read_req(reqmgr_thrd_t* thrd_ctrl)
{
    server_read_req_t* rdreq = NULL;
    RM_LOCK(thrd_ctrl);
    if (thrd_ctrl->num_read_reqs < RM_MAX_ACTIVE_REQUESTS) {
        if (thrd_ctrl->next_rdreq_ndx < (RM_MAX_ACTIVE_REQUESTS - 1)) {
            rdreq = thrd_ctrl->read_reqs + thrd_ctrl->next_rdreq_ndx;
            assert((rdreq->req_ndx == 0) && (rdreq->in_use == 0));
            rdreq->req_ndx = thrd_ctrl->next_rdreq_ndx++;
        } else { // search for unused slot
            for (int i = 0; i < RM_MAX_ACTIVE_REQUESTS; i++) {
                rdreq = thrd_ctrl->read_reqs + i;
                if ((rdreq->req_ndx == 0) && (rdreq->in_use == 0)) {
                    rdreq->req_ndx = i;
                    break;
                }
            }
        }
        thrd_ctrl->num_read_reqs++;
        rdreq->in_use = 1;
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
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    int rc = (int)UNIFYFS_SUCCESS;

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
        rc = EINVAL;
        LOGERR("NULL read_req");
    }

    return rc;
}

static void signal_new_requests(reqmgr_thrd_t* thrd_ctrl)
{
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    /* wake up the request manager thread for the requesting client */
    if (!thrd_ctrl->has_waiting_delegator) {
        /* reqmgr thread is not waiting, but we are in critical
         * section, we just added requests so we must wait for reqmgr
         * to signal us that it's reached the critical section before
         * we escape so we don't overwrite these requests before it
         * has had a chance to process them */
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* reqmgr thread has signaled us that it's now waiting */
        thrd_ctrl->has_waiting_dispatcher = 0;
    }
    /* have a reqmgr thread waiting on condition variable,
     * signal it to begin processing the requests we just added */
    pthread_cond_signal(&thrd_ctrl->thrd_cond);
}

static void signal_new_responses(reqmgr_thrd_t* thrd_ctrl)
{
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    /* wake up the request manager thread */
    if (thrd_ctrl->has_waiting_delegator) {
        /* have a reqmgr thread waiting on condition variable,
         * signal it to begin processing the responses we just added */
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }
}

/* issue remote chunk read requests for extent chunks
 * listed within keyvals */
int rm_create_chunk_requests(reqmgr_thrd_t* thrd_ctrl,
                             server_read_req_t* rdreq,
                             int num_vals,
                             unifyfs_keyval_t* keyvals)
{
    /* allocate read request structures */
    chunk_read_req_t* all_chunk_reads = (chunk_read_req_t*)
        calloc((size_t)num_vals, sizeof(chunk_read_req_t));
    if (NULL == all_chunk_reads) {
        LOGERR("failed to allocate chunk-reads array");
        return ENOMEM;
    }

    /* wait on lock before we attach new array to global variable */
    RM_LOCK(thrd_ctrl);

    LOGDBG("creating chunk requests for rdreq %d", rdreq->req_ndx);

    /* attach read request array to global request mananger struct */
    rdreq->chunks = all_chunk_reads;

    /* iterate over write index values and create read requests
     * for each one, also count up number of delegators that we'll
     * forward read requests to */
    int i;
    int prev_del = -1;
    int num_del = 0;
    for (i = 0; i < num_vals; i++) {
        /* get target server for this request */
        int curr_del = keyvals[i].val.delegator_rank;

        /* if target server is different from last target,
         * increment our server count */
        if ((prev_del == -1) || (curr_del != prev_del)) {
            num_del++;
        }
        prev_del = curr_del;

        /* get pointer to next read request structure */
        debug_log_key_val(__func__, &keyvals[i].key, &keyvals[i].val);
        chunk_read_req_t* chk = all_chunk_reads + i;

        /* fill in chunk read request */
        chk->gfid          = keyvals[i].key.gfid;
        chk->nbytes        = keyvals[i].val.len;
        chk->offset        = keyvals[i].key.offset;
        chk->log_offset    = keyvals[i].val.addr;
        chk->log_app_id    = keyvals[i].val.app_id;
        chk->log_client_id = keyvals[i].val.rank;
    }

    /* allocate per-delgator chunk-reads */
    int num_dels = num_del;
    rdreq->num_remote_reads = num_dels;
    rdreq->remote_reads = (remote_chunk_reads_t*)
        calloc((size_t)num_dels, sizeof(remote_chunk_reads_t));
    if (NULL == rdreq->remote_reads) {
        LOGERR("failed to allocate remote-reads array");
        RM_UNLOCK(thrd_ctrl);
        return ENOMEM;
    }

    /* get pointer to start of chunk read request array */
    remote_chunk_reads_t* reads = rdreq->remote_reads;

    /* iterate over write index values again and now create
     * per-server chunk-reads info, for each server
     * that we'll request data from, this totals up the number
     * of read requests and total read data size from that
     * server  */
    prev_del = -1;
    size_t del_data_sz = 0;
    for (i = 0; i < num_vals; i++) {
        /* get target server for this request */
        int curr_del = keyvals[i].val.delegator_rank;

        /* if target server is different from last target,
         * close out the total number of bytes for the last
         * server, note this assumes our write index values are
         * sorted by server rank */
        if ((prev_del != -1) && (curr_del != prev_del)) {
            /* record total data for previous server */
            reads->total_sz = del_data_sz;

            /* advance to read request for next server */
            reads += 1;

            /* reset our running tally of bytes to 0 */
            del_data_sz = 0;
        }
        prev_del = curr_del;

        /* update total read data size for current server */
        del_data_sz += keyvals[i].val.len;

        /* if this is the first read request for this server,
         * initialize fields on the per-server read request
         * structure */
        if (0 == reads->num_chunks) {
            /* TODO: let's describe what these fields are for */
            reads->rank     = curr_del;
            reads->rdreq_id = rdreq->req_ndx;
            reads->reqs     = all_chunk_reads + i;
            reads->resp     = NULL;
        }

        /* increment number of read requests we're sending
         * to this server */
        reads->num_chunks++;
    }

    /* record total data size for final server (if any),
     * would have missed doing this in the above loop */
    if (num_vals > 0) {
        reads->total_sz = del_data_sz;
    }

    /* mark request as ready to be started */
    rdreq->status = READREQ_READY;

    /* wake up the request manager thread for the requesting client */
    signal_new_requests(thrd_ctrl);

    RM_UNLOCK(thrd_ctrl);

    return UNIFYFS_SUCCESS;
}

int rm_submit_read_request(server_read_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;
    int i = 0;
    app_client* client = NULL;
    reqmgr_thrd_t* thrd_ctrl = NULL;
    server_read_req_t* rdreq = NULL;

    if (!req || !req->chunks || !req->remote_reads) {
        return EINVAL;
    }

    client = get_app_client(req->app_id, req->client_id);
    if (NULL == client) {
        return UNIFYFS_FAILURE;
    }

    thrd_ctrl = client->reqmgr;

    rdreq = rm_reserve_read_req(thrd_ctrl);
    if (!rdreq) {
        LOGERR("failed to allocate a request");
        return UNIFYFS_FAILURE;
    }

    RM_LOCK(thrd_ctrl);

    rdreq->app_id = req->app_id;
    rdreq->client_id = req->client_id;
    rdreq->num_remote_reads = req->num_remote_reads;
    rdreq->chunks = req->chunks;
    rdreq->remote_reads = req->remote_reads;

    for (i = 0; i < rdreq->num_remote_reads; i++) {
        remote_chunk_reads_t* read = &rdreq->remote_reads[i];
        read->rdreq_id = rdreq->req_ndx;
    }

    rdreq->status = READREQ_READY;
    signal_new_requests(thrd_ctrl);

    RM_UNLOCK(thrd_ctrl);

    return ret;
}

/* signal the client process for it to start processing read
 * data in shared memory */
static int client_signal(shm_data_header* hdr,
                         shm_data_state_e flag)
{
    if (flag == SHMEM_REGION_DATA_READY) {
        LOGDBG("setting data-ready");
    } else if (flag == SHMEM_REGION_DATA_COMPLETE) {
        LOGDBG("setting data-complete");
    }

    /* we signal the client by setting a flag value within
     * a shared memory segment that the client is monitoring */
    hdr->state = flag;

    /* TODO: MEM_FLUSH */

    return UNIFYFS_SUCCESS;
}

/* wait until client has processed all read data in shared memory */
static int client_wait(shm_data_header* hdr)
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

/* function called by main thread to instruct
 * resource manager thread to exit,
 * returns UNIFYFS_SUCCESS on success */
int rm_cmd_exit(reqmgr_thrd_t* thrd_ctrl)
{
    if (thrd_ctrl->exited) {
        /* already done */
        return UNIFYFS_SUCCESS;
    }

    /* grab the lock */
    RM_LOCK(thrd_ctrl);

    /* if reqmgr thread is not waiting in critical
     * section, let's wait on it to come back */
    if (!thrd_ctrl->has_waiting_delegator) {
        /* reqmgr thread is not in critical section,
         * tell it we've got something and signal it */
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* we're no longer waiting */
        thrd_ctrl->has_waiting_dispatcher = 0;
    }

    /* inform reqmgr thread that it's time to exit */
    thrd_ctrl->exit_flag = 1;

    /* signal reqmgr thread */
    pthread_cond_signal(&thrd_ctrl->thrd_cond);

    /* release the lock */
    RM_UNLOCK(thrd_ctrl);

    /* wait for reqmgr thread to exit */
    int rc = pthread_join(thrd_ctrl->thrd, NULL);
    if (0 == rc) {
        pthread_cond_destroy(&(thrd_ctrl->thrd_cond));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        thrd_ctrl->exited = 1;
    }
    return UNIFYFS_SUCCESS;
}

/************************
 * These functions define the logic of the request manager thread
 ***********************/

/* pack the chunk read requests for a single remote server.
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

    assert(req_cnt <= MAX_META_PER_SEND);

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
            if (req->status == READREQ_READY) {
                req->status = READREQ_STARTED;
                /* iterate over each server we need to send requests to */
                remote_chunk_reads_t* remote_reads;
                size_t packed_sz;
                for (j = 0; j < req->num_remote_reads; j++) {
                    remote_reads = req->remote_reads + j;
                    remote_reads->status = READREQ_STARTED;

                    /* pack requests into send buffer, get packed size */
                    packed_sz = rm_pack_chunk_requests(sendbuf, remote_reads);

                    /* get rank of target server */
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
                               unifyfs_rc_enum_str((unifyfs_rc)rc));
                    }
                }
            } else {
                /* already started */
                LOGDBG("read req %d already processed", i);
            }
        } else if (req->num_remote_reads == 0) {
            if (req->status == READREQ_READY) {
                req->status = READREQ_STARTED;
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
            /* iterate over each server we need to send requests to */
            remote_chunk_reads_t* rcr;
            for (j = 0; j < req->num_remote_reads; j++) {
                rcr = req->remote_reads + j;
                if (NULL == rcr->resp) {
                    continue;
                }
                LOGDBG("found read req %d responses from server %d",
                       i, rcr->rank);
                rc = rm_handle_chunk_read_responses(thrd_ctrl, req, rcr);
                if (rc != (int)UNIFYFS_SUCCESS) {
                    LOGERR("failed to handle chunk read responses");
                    ret = rc;
                }
            }
        } else if ((req->num_remote_reads == 0) &&
                   (req->status == READREQ_STARTED)) {
            /* look up client shared memory region */
            int app_id = req->app_id;
            int client_id = req->client_id;
            app_client* client = get_app_client(app_id, client_id);
            if (NULL != client) {
                shm_context* client_shm = client->shmem_data;
                assert(NULL != client_shm);
                shm_data_header* shm_hdr = (shm_data_header*) client_shm->addr;

                /* mark request as complete */
                req->status = READREQ_COMPLETE;

                /* signal client that we're now done writing data */
                client_signal(shm_hdr, SHMEM_REGION_DATA_COMPLETE);

                /* wait for client to read data */
                client_wait(shm_hdr);
            }

            rc = release_read_req(thrd_ctrl, req);
            if (rc != (int)UNIFYFS_SUCCESS) {
                LOGERR("failed to release server_read_req_t");
                ret = rc;
            }
        }
    }

    return ret;
}

static shm_data_meta* reserve_shmem_meta(shm_context* shmem_data,
                                         size_t data_sz)
{
    shm_data_meta* meta = NULL;
    shm_data_header* hdr = (shm_data_header*) shmem_data->addr;

    if (NULL == hdr) {
        LOGERR("invalid header");
    } else {
        pthread_mutex_lock(&(hdr->sync));
        LOGDBG("shm_data_header(cnt=%zu, bytes=%zu)",
               hdr->meta_cnt, hdr->bytes);
        size_t remain_size = shmem_data->size -
                             (sizeof(shm_data_header) + hdr->bytes);
        size_t meta_size = sizeof(shm_data_meta) + data_sz;
        if (meta_size > remain_size) {
            /* client-side receive buffer is full,
             * inform client to start reading */
            LOGDBG("need more space in client recv buffer");
            client_signal(hdr, SHMEM_REGION_DATA_READY);

            /* wait for client to read data */
            int rc = client_wait(hdr);
            if (rc != (int)UNIFYFS_SUCCESS) {
                LOGERR("wait for client recv buffer space failed");
                pthread_mutex_unlock(&(hdr->sync));
                return NULL;
            }
        }
        size_t shm_offset = hdr->bytes;
        char* shm_buf = ((char*)hdr) + sizeof(shm_data_header);
        meta = (shm_data_meta*)(shm_buf + shm_offset);
        LOGDBG("reserved shm_data_meta[%zu] and %zu payload bytes",
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
    int rc;

    /* get application client */
    app_client* client = get_app_client(app_id, client_id);
    if (NULL == client) {
        return (int)UNIFYFS_FAILURE;
    }

    /* get thread control structure */
    reqmgr_thrd_t* thrd_ctrl = client->reqmgr;
    assert(NULL != thrd_ctrl);

    RM_LOCK(thrd_ctrl);

    remote_chunk_reads_t* del_reads = NULL;

    /* find read req associated with req_id */
    server_read_req_t* rdreq = thrd_ctrl->read_reqs + req_id;
    for (int i = 0; i < rdreq->num_remote_reads; i++) {
        if (rdreq->remote_reads[i].rank == src_rank) {
            del_reads = rdreq->remote_reads + i;
            break;
        }
    }

    if (NULL != del_reads) {
        LOGDBG("posting chunk responses for req %d from server %d",
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

static int send_data_to_client(shm_context* shm, chunk_read_resp_t* resp,
                               char* data, size_t* bytes_processed)
{
    int ret = UNIFYFS_SUCCESS;
    int errcode = 0;
    size_t offset = 0;
    size_t data_size = 0;
    size_t bytes_left = 0;
    size_t tx_size = MAX_DATA_TX_SIZE;
    char* bufpos = data;
    shm_data_meta* meta = NULL;

    if (resp->read_rc < 0) {
        errcode = (int) -(resp->read_rc);
        data_size = 0;
    } else {
        data_size = resp->nbytes;
    }

    /* data can be larger than the shmem buffer size. split the data into
     * pieces and send them */
    bytes_left = data_size;
    offset = resp->offset;

    for (bytes_left = data_size; bytes_left > 0; bytes_left -= tx_size) {
        if (bytes_left < tx_size) {
            tx_size = bytes_left;
        }

        meta = reserve_shmem_meta(shm, tx_size);
        if (meta) {
            meta->gfid = resp->gfid;
            meta->errcode = errcode;
            meta->offset = offset;
            meta->length = tx_size;

            LOGDBG("sending data to client (gfid=%d, offset=%zu, length=%zu) "
                   "%zu bytes left",
                   resp->gfid, offset, tx_size, bytes_left);

            if (tx_size) {
                void* shm_buf = (void*) ((char*) meta + sizeof(shm_data_meta));
                memcpy(shm_buf, bufpos, tx_size);
            }
        } else {
            /* do we need to stop processing and exit loop here? */
            LOGERR("failed to reserve shmem space for read reply");
            ret = UNIFYFS_ERROR_SHMEM;
        }

        bufpos += tx_size;
        offset += tx_size;
    }

    *bytes_processed = data_size - bytes_left;

    return ret;
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
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    int i, num_chks, rc;
    int ret = (int)UNIFYFS_SUCCESS;
    chunk_read_resp_t* responses = NULL;
    shm_context* client_shm = NULL;
    shm_data_header* shm_hdr = NULL;
    char* data_buf = NULL;

    assert((NULL != thrd_ctrl) &&
           (NULL != rdreq) &&
           (NULL != del_reads) &&
           (NULL != del_reads->resp));

    /* look up client shared memory region */
    app_client* clnt = get_app_client(rdreq->app_id, rdreq->client_id);
    if (NULL == clnt) {
        return (int)UNIFYFS_FAILURE;
    }
    client_shm = clnt->shmem_data;
    shm_hdr = (shm_data_header*) client_shm->addr;

    num_chks = del_reads->num_chunks;
    if (del_reads->status != READREQ_STARTED) {
        LOGERR("chunk read response for non-started req @ index=%d",
               rdreq->req_ndx);
        ret = (int32_t)EINVAL;
    } else if (0 == del_reads->total_sz) {
        LOGERR("empty chunk read response from server %d", del_reads->rank);
        ret = (int32_t)EINVAL;
    } else {
        LOGDBG("handling chunk read responses from server %d: "
               "num_chunks=%d buf_size=%zu",
               del_reads->rank, num_chks, del_reads->total_sz);
        responses = del_reads->resp;
        data_buf = (char*)(responses + num_chks);

        for (i = 0; i < num_chks; i++) {
            chunk_read_resp_t* resp = responses + i;
            size_t processed = 0;

            ret = send_data_to_client(client_shm, resp, data_buf, &processed);
            if (ret != UNIFYFS_SUCCESS) {
                LOGERR("failed to send data to client (ret=%d)", ret);
            }

            data_buf += processed;
        }

        /* cleanup */
        free((void*)responses);
        del_reads->resp = NULL;

        /* update request status */
        del_reads->status = READREQ_COMPLETE;

        /* if all remote reads are complete, mark the request as complete */
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
            client_signal(shm_hdr, SHMEM_REGION_DATA_COMPLETE);

            /* wait for client to read data */
            client_wait(shm_hdr);

            rc = release_read_req(thrd_ctrl, rdreq);
            if (rc != (int)UNIFYFS_SUCCESS) {
                LOGERR("failed to release server_read_req_t");
            }
        }
    }

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
        LOGDBG("RM[%d:%d] waiting for work",
               thrd_ctrl->app_id, thrd_ctrl->client_id);
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* set flag to indicate we're no longer waiting */
        thrd_ctrl->has_waiting_delegator = 0;

        /* go do work ... */
        LOGDBG("RM[%d:%d] got work", thrd_ctrl->app_id, thrd_ctrl->client_id);

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
    if (hret != HG_SUCCESS) {
        LOGERR("margo_create() failed");
        return UNIFYFS_ERROR_MARGO;
    }

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
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        rc = UNIFYFS_ERROR_MARGO;
    } else {
        LOGDBG("invoking the chunk-read-request rpc function");
        hret = margo_forward(handle, &in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_forward() failed");
            rc = UNIFYFS_ERROR_MARGO;
        } else {
            /* decode response */
            hret = margo_get_output(handle, &out);
            if (hret == HG_SUCCESS) {
                rc = (int)out.ret;
                LOGDBG("Got request rpc response from %d - ret=%d",
                    dst_srvr_rank, rc);
                margo_free_output(handle, &out);
            } else {
                LOGERR("margo_get_output() failed");
                rc = UNIFYFS_ERROR_MARGO;
            }
        }

        margo_bulk_free(in.bulk_handle);
    }
    margo_destroy(handle);

    return rc;
}

/* BEGIN MARGO SERVER-SERVER RPC HANDLER FUNCTIONS */

/* handler for remote read request response */
static void chunk_read_response_rpc(hg_handle_t handle)
{
    int32_t ret;
    chunk_read_response_out_t out;

    /* get input params */
    chunk_read_response_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = (int32_t) UNIFYFS_ERROR_MARGO;
    } else {
        /* extract params from input struct */
        int src_rank   = (int)in.src_rank;
        int app_id     = (int)in.app_id;
        int client_id  = (int)in.client_id;
        int req_id     = (int)in.req_id;
        int num_chks   = (int)in.num_chks;
        size_t bulk_sz = (size_t)in.bulk_size;

        LOGDBG("received chunk read response from server %d (%d chunks)",
            src_rank, num_chks);

        /* The input parameters specify the info for a bulk transfer
         * buffer on the sending process.  We use that info to pull data
         * from the sender into a local buffer.  This buffer contains
         * the read reply headers and associated read data for requests
         * we had sent earlier. */

        /* pull the remote data via bulk transfer */
        if (0 == bulk_sz) {
            /* sender is trying to send an empty buffer,
             * don't think that should happen unless maybe
             * we had sent a read request list that was empty? */
            LOGERR("empty response buffer");
            ret = (int32_t)EINVAL;
        } else {
            /* allocate a buffer to hold the incoming data */
            char* resp_buf = (char*) malloc(bulk_sz);
            if (NULL == resp_buf) {
                /* allocation failed, that's bad */
                LOGERR("failed to allocate chunk read responses buffer");
                ret = (int32_t)ENOMEM;
            } else {
                /* got a buffer, now pull response data */
                ret = (int32_t)UNIFYFS_SUCCESS;

                /* get margo info */
                const struct hg_info* hgi = margo_get_info(handle);
                assert(NULL != hgi);

                margo_instance_id mid = margo_hg_info_get_instance(hgi);
                assert(mid != MARGO_INSTANCE_NULL);

                /* pass along address of buffer we want to transfer
                 * data into to prepare it for a bulk write,
                 * get resulting margo handle */
                hg_bulk_t bulk_handle;
                hret = margo_bulk_create(mid, 1,
                                         (void**)&resp_buf, &in.bulk_size,
                                         HG_BULK_WRITE_ONLY, &bulk_handle);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_bulk_create() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                    goto out_respond;
                }

                /* execute the transfer to pull data from remote side
                 * into our local bulk transfer buffer.
                 * NOTE: mercury/margo bulk transfer does not check the maximum
                 * transfer size that the underlying transport supports, and a
                 * large bulk transfer may result in failure. */
                int i = 0;
                hg_size_t remain = in.bulk_size;
                do {
                    hg_size_t offset = i * MAX_BULK_TX_SIZE;
                    hg_size_t len = remain < MAX_BULK_TX_SIZE
                                    ? remain : MAX_BULK_TX_SIZE;

                    hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                                               in.bulk_handle, offset,
                                               bulk_handle, offset, len);
                    if (hret != HG_SUCCESS) {
                        LOGERR("margo_bulk_transfer(off=%zu, sz=%zu) failed",
                               (size_t)offset, (size_t)len);
                        ret = UNIFYFS_ERROR_MARGO;
                        break;
                    }

                    remain -= len;
                    i++;
                } while (remain > 0);

                if (hret == HG_SUCCESS) {
                    LOGDBG("successful bulk transfer (%zu bytes)", bulk_sz);

                    /* process read replies we just received */
                    int rc = rm_post_chunk_read_responses(app_id, client_id,
                                                          src_rank, req_id,
                                                          num_chks, bulk_sz,
                                                          resp_buf);
                    if (rc != UNIFYFS_SUCCESS) {
                        LOGERR("failed to handle chunk read responses");
                        ret = rc;
                    }
                } else {
                    LOGERR("failed to perform bulk transfer");
                }

                /* deregister our bulk transfer buffer */
                margo_bulk_free(bulk_handle);
            }
        }
        margo_free_input(handle, &in);
    }

out_respond:
    /* return to caller */
    out.ret = ret;
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(chunk_read_response_rpc)
