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
#include "margo_server.h"
#include "unifyfs_group_rpc.h"
#include "unifyfs_p2p_rpc.h"
#include "unifyfs_server_rpcs.h"


#define RM_LOCK(rm) \
do { \
    /*LOGDBG("locking RM[%d:%d] state", rm->app_id, rm->client_id);*/ \
    pthread_mutex_lock(&(rm->thrd_lock)); \
} while (0)

#define RM_UNLOCK(rm) \
do { \
    /*LOGDBG("unlocking RM[%d:%d] state", rm->app_id, rm->client_id);*/ \
    pthread_mutex_unlock(&(rm->thrd_lock)); \
} while (0)

#define RM_REQ_LOCK(rm) \
do { \
    /*LOGDBG("locking RM[%d:%d] requests", rm->app_id, rm->client_id);*/ \
    ABT_mutex_lock(rm->reqs_sync); \
} while (0)

#define RM_REQ_UNLOCK(rm) \
do { \
    /*LOGDBG("unlocking RM[%d:%d] requests", rm->app_id, rm->client_id);*/ \
    ABT_mutex_unlock(rm->reqs_sync); \
} while (0)

/* One request manager thread is created for each client of the
 * server. The margo rpc handler thread(s) assign work to the
 * request manager thread to handle data and metadata operations.
 *
 * The request manager thread coordinates with other threads
 * using a lock and a condition variable to protect the shared data
 * structure and impose flow control. When assigned work, the
 * request manager thread either handles the request directly, or
 * forwards requests to remote servers.
 *
 * For read requests, the request manager waits for data chunk
 * responses and places the data into a shared memory data buffer
 * specific to the client. When the shared memory is full or all
 * data has been received, the request manager signals the client
 * to process the read replies. It iterates with the client until
 * all incoming read replies have been transferred. */

/* Create a request manager thread for the application client
 * corresponding to the given app_id and client_id.
 * Returns pointer to thread control structure on success, or
 * NULL on failure */
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

    /* create the argobots mutex for synchronizing access to reqs state */
    ABT_mutex_create(&(thrd_ctrl->reqs_sync));

    /* allocate a list to track client rpc requests */
    thrd_ctrl->client_reqs =
        arraylist_create(UNIFYFS_CLIENT_MAX_ACTIVE_REQUESTS);
    if (thrd_ctrl->client_reqs == NULL) {
        LOGERR("failed to allocate request manager client_reqs!");
        ABT_mutex_free(&(thrd_ctrl->reqs_sync));
        pthread_cond_destroy(&(thrd_ctrl->thrd_cond));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        free(thrd_ctrl);
        return NULL;
    }

    /* allocate a list to track client rpc requests */
    thrd_ctrl->client_callbacks =
        arraylist_create(UNIFYFS_CLIENT_MAX_FILES);
    if (thrd_ctrl->client_callbacks == NULL) {
        LOGERR("failed to allocate request manager client_callbacks!");
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
    thrd_ctrl->waiting_for_work       = 0;

    /* launch request manager thread */
    rc = pthread_create(&(thrd_ctrl->thrd), NULL,
                        request_manager_thread, (void*)thrd_ctrl);
    if (rc != 0) {
        LOGERR("failed to create request manager thread for "
               "app_id=%d client_id=%d - rc=%d (%s)",
               app_id, client_id, rc, strerror(rc));
        ABT_mutex_free(&(thrd_ctrl->reqs_sync));
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
               req->req_ndx, req->status, req->num_server_reads);
    }
}

server_read_req_t* rm_reserve_read_req(reqmgr_thrd_t* thrd_ctrl)
{
    server_read_req_t* rdreq = NULL;
    RM_REQ_LOCK(thrd_ctrl);
    if (thrd_ctrl->num_read_reqs < UNIFYFS_SERVER_MAX_READS) {
        if (thrd_ctrl->next_rdreq_ndx < (UNIFYFS_SERVER_MAX_READS - 1)) {
            rdreq = thrd_ctrl->read_reqs + thrd_ctrl->next_rdreq_ndx;
            assert((rdreq->req_ndx == 0) && (rdreq->in_use == 0));
            rdreq->req_ndx = thrd_ctrl->next_rdreq_ndx++;
        } else { // search for unused slot
            for (int i = 0; i < UNIFYFS_SERVER_MAX_READS; i++) {
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
    RM_REQ_UNLOCK(thrd_ctrl);
    return rdreq;
}

static int release_read_req(reqmgr_thrd_t* thrd_ctrl,
                            server_read_req_t* rdreq)
{
    int rc = (int)UNIFYFS_SUCCESS;

    if (rdreq != NULL) {
        RM_REQ_LOCK(thrd_ctrl);
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
        if (0 == thrd_ctrl->num_read_reqs) {
            thrd_ctrl->next_rdreq_ndx = 0;
        }
        LOGDBG("after release (active=%d, next=%d)",
               thrd_ctrl->num_read_reqs, thrd_ctrl->next_rdreq_ndx);
        RM_REQ_UNLOCK(thrd_ctrl);
    } else {
        rc = EINVAL;
        LOGERR("NULL read_req");
    }

    return rc;
}

int rm_release_read_req(reqmgr_thrd_t* thrd_ctrl,
                        server_read_req_t* rdreq)
{
    return release_read_req(thrd_ctrl, rdreq);
}

static void signal_new_requests(reqmgr_thrd_t* reqmgr)
{
    pid_t this_thread = unifyfs_gettid();
    if (this_thread != reqmgr->tid) {
        /* signal reqmgr to begin processing the requests we just added */
        LOGDBG("signaling new requests");
        pthread_cond_signal(&reqmgr->thrd_cond);
    }
}

static void signal_new_responses(reqmgr_thrd_t* reqmgr)
{
    pid_t this_thread = unifyfs_gettid();
    if (this_thread != reqmgr->tid) {
        /* wake up the request manager thread */
        RM_LOCK(reqmgr);
        if (reqmgr->waiting_for_work) {
            /* reqmgr thread is waiting on condition variable,
             * signal it to begin processing the responses we just added */
            LOGDBG("signaling new responses");
            pthread_cond_signal(&reqmgr->thrd_cond);
        }
        RM_UNLOCK(reqmgr);
    }
}

/* issue remote chunk read requests for extent chunks
 * listed within keyvals */
int rm_create_chunk_requests(reqmgr_thrd_t* thrd_ctrl,
                             server_read_req_t* rdreq,
                             int num_vals,
                             unifyfs_keyval_t* keyvals)
{
    LOGDBG("creating chunk requests for rdreq %d", rdreq->req_ndx);

    /* allocate read request structures */
    chunk_read_req_t* all_chunk_reads = (chunk_read_req_t*)
        calloc((size_t)num_vals, sizeof(chunk_read_req_t));
    if (NULL == all_chunk_reads) {
        LOGERR("failed to allocate chunk-reads array");
        return ENOMEM;
    }
    rdreq->chunks = all_chunk_reads;

    /* iterate over write index values and create read requests
     * for each one, also count up number of servers that we'll
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
    rdreq->num_server_reads = num_dels;
    rdreq->remote_reads = (server_chunk_reads_t*)
        calloc((size_t)num_dels, sizeof(server_chunk_reads_t));
    if (NULL == rdreq->remote_reads) {
        LOGERR("failed to allocate remote-reads array");
        return ENOMEM;
    }

    /* get pointer to start of chunk read request array */
    server_chunk_reads_t* reads = rdreq->remote_reads;

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

    /* get reqmgr for app-client */
    client = get_app_client(req->app_id, req->client_id);
    if (NULL == client) {
        return UNIFYFS_FAILURE;
    }
    thrd_ctrl = client->reqmgr;

    /* reserve an available reqmgr request slot */
    rdreq = rm_reserve_read_req(thrd_ctrl);
    if (!rdreq) {
        LOGERR("failed to allocate a request");
        return UNIFYFS_FAILURE;
    }

    /* get assigned slot index, then copy request parameters to reserved
     * req from input req. note we can't use memcpy or struct assignment
     * because there are other fields set by rm_reserve_read_req() */
    int rm_req_index = rdreq->req_ndx;
    rdreq->app_id = req->app_id;
    rdreq->client_id = req->client_id;
    rdreq->client_mread = req->client_mread;
    rdreq->client_read_ndx = req->client_read_ndx;
    rdreq->num_server_reads = req->num_server_reads;
    rdreq->chunks = req->chunks;
    rdreq->remote_reads = req->remote_reads;
    rdreq->extent = req->extent;

    for (i = 0; i < rdreq->num_server_reads; i++) {
        rdreq->remote_reads[i].rdreq_id = rm_req_index;
    }

    rdreq->status = READREQ_READY;
    signal_new_requests(thrd_ctrl);

    return ret;
}

/* function called by main thread to instruct
 * resource manager thread to exit,
 * returns UNIFYFS_SUCCESS on success */
int rm_request_exit(reqmgr_thrd_t* thrd_ctrl)
{
    if (thrd_ctrl->exited) {
        /* already done */
        return UNIFYFS_SUCCESS;
    }

    /* grab the lock */
    RM_LOCK(thrd_ctrl);

    /* inform reqmgr thread that it's time to exit */
    thrd_ctrl->exit_flag = 1;

    /* if reqmgr thread is waiting for work, wake it up */
    if (thrd_ctrl->waiting_for_work) {
         /* signal reqmgr thread */
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }

    /* release the lock */
    RM_UNLOCK(thrd_ctrl);

    /* wait for reqmgr thread to exit */
    int rc = pthread_join(thrd_ctrl->thrd, NULL);
    if (0 == rc) {
        pthread_cond_destroy(&(thrd_ctrl->thrd_cond));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        ABT_mutex_free(&(thrd_ctrl->reqs_sync));
        thrd_ctrl->exited = 1;
    }
    return UNIFYFS_SUCCESS;
}

/************************
 * These functions define the logic of the request manager thread
 ***********************/

/* send the chunk read requests to remote servers
 *
 * @param thrd_ctrl : reqmgr thread control structure
 * @return success/error code
 */
static int rm_request_remote_chunks(reqmgr_thrd_t* thrd_ctrl)
{
    int i, j, rc;
    int ret = (int)UNIFYFS_SUCCESS;

    /* iterate over each active read request */
    RM_REQ_LOCK(thrd_ctrl);
    for (i = 0; i < UNIFYFS_SERVER_MAX_READS; i++) {
        server_read_req_t* req = thrd_ctrl->read_reqs + i;
        if (!req->in_use) {
            continue;
        }
        if (req->num_server_reads > 0) {
            LOGDBG("read req %d is active", i);
            debug_print_read_req(req);
            if (req->status == READREQ_READY) {
                req->status = READREQ_STARTED;
                /* iterate over each server we need to send requests to */
                server_chunk_reads_t* remote_reads;
                for (j = 0; j < req->num_server_reads; j++) {
                    remote_reads = req->remote_reads + j;
                    remote_reads->status = READREQ_STARTED;

                    /* send requests */
                    int remote_rank = remote_reads->rank;
                    LOGDBG("[%d of %d] sending %d chunk requests to server[%d]",
                           j, req->num_server_reads,
                           remote_reads->num_chunks, remote_rank);
                    rc = invoke_chunk_read_request_rpc(remote_rank, req,
                                                       remote_reads);
                    if (rc != UNIFYFS_SUCCESS) {
                        ret = rc;
                        LOGERR("server request rpc to %d failed - %s",
                               remote_rank,
                               unifyfs_rc_enum_str((unifyfs_rc)rc));
                    }
                }
            } else {
                /* already started */
                LOGDBG("read req %d already processed", i);
            }
        } else if (req->num_server_reads == 0) {
            if (req->status == READREQ_READY) {
                req->status = READREQ_STARTED;
            }
        }
    }
    RM_REQ_UNLOCK(thrd_ctrl);

    return ret;
}

/* process chunk read responses from remote servers
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
    for (i = 0; i < UNIFYFS_SERVER_MAX_READS; i++) {
        server_read_req_t* req = thrd_ctrl->read_reqs + i;
        if (!req->in_use) {
            continue;
        }
        if (req->status == READREQ_STARTED) {
            if (req->num_server_reads > 0) {
                /* iterate over each server we sent requests to */
                server_chunk_reads_t* scr;
                for (j = 0; j < req->num_server_reads; j++) {
                    scr = req->remote_reads + j;
                    if (NULL == scr->resp) {
                        continue;
                    }
                    LOGDBG("found read req %d responses from server %d",
                           i, scr->rank);
                    rc = rm_handle_chunk_read_responses(thrd_ctrl, req, scr);
                    if (rc != (int)UNIFYFS_SUCCESS) {
                        LOGERR("failed to handle chunk read responses");
                        ret = rc;
                    }
                }
            }
        } else if (req->status == READREQ_COMPLETE) {
            /* cleanup completed server_read_req */
            rc = release_read_req(thrd_ctrl, req);
            if (rc != (int)UNIFYFS_SUCCESS) {
                LOGERR("failed to release server_read_req_t");
                ret = rc;
            }
        }
    }

    return ret;
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

    server_chunk_reads_t* server_chunks = NULL;

    /* find read req associated with req_id */
    if (src_rank != glb_pmi_rank) {
        /* only need to lock for posting responses from remote servers.
         * when response is local, we already have the lock */
        RM_REQ_LOCK(thrd_ctrl);
    }
    server_read_req_t* rdreq = thrd_ctrl->read_reqs + req_id;
    for (int i = 0; i < rdreq->num_server_reads; i++) {
        if (rdreq->remote_reads[i].rank == src_rank) {
            server_chunks = rdreq->remote_reads + i;
            break;
        }
    }

    if (NULL != server_chunks) {
        LOGDBG("posting chunk responses for req %d from server %d",
               req_id, src_rank);
        server_chunks->resp = (chunk_read_resp_t*)resp_buf;
        if (server_chunks->num_chunks != num_chks) {
            LOGERR("mismatch on request vs. response chunks");
            server_chunks->num_chunks = num_chks;
        }
        server_chunks->total_sz = bulk_sz;
        rc = (int)UNIFYFS_SUCCESS;
    } else {
        LOGERR("failed to find matching chunk-reads request");
        rc = (int)UNIFYFS_FAILURE;
    }
    if (src_rank != glb_pmi_rank) {
        RM_REQ_UNLOCK(thrd_ctrl);
    }

    /* inform the request manager thread we added responses */
    signal_new_responses(thrd_ctrl);

    return rc;
}

static
int send_data_to_client(server_read_req_t* rdreq,
                        chunk_read_resp_t* resp,
                        char* data,
                        size_t* bytes_processed)
{
    int ret = UNIFYFS_SUCCESS;
    int errcode;
    int app_id = rdreq->app_id;
    int client_id = rdreq->client_id;
    int mread_id = rdreq->client_mread;
    int read_ndx = rdreq->client_read_ndx;

    if (resp->read_rc < 0) {
        /* server read returned error */
        errcode = (int) -(resp->read_rc);
        *bytes_processed = 0;
        return invoke_client_mread_req_complete_rpc(app_id, client_id,
                                                    mread_id, read_ndx,
                                                    errcode);
    }

    size_t data_size = (size_t) resp->read_rc;
    size_t send_sz = UNIFYFS_SERVER_MAX_DATA_TX_SIZE;
    char* bufpos = data;

    size_t resp_file_offset = resp->offset;
    size_t req_file_offset = (size_t) rdreq->extent.offset;
    assert(resp_file_offset >= req_file_offset);

    size_t read_byte_offset = resp_file_offset - req_file_offset;
    errcode = 0;

    /* data can be larger than the shmem buffer size. split the data into
     * pieces and send them */
    size_t bytes_left = data_size;
    for ( ; bytes_left > 0; bytes_left -= send_sz) {
        if (bytes_left < send_sz) {
            send_sz = bytes_left;
        }

        LOGDBG("sending data for client[%d:%d] mread[%d] request %d "
               "(gfid=%d, offset=%zu, length=%zu, remaining=%zu)",
               app_id, client_id, mread_id, read_ndx,
               resp->gfid, req_file_offset + read_byte_offset,
               send_sz, bytes_left);

        int rc = invoke_client_mread_req_data_rpc(app_id, client_id, mread_id,
                                                  read_ndx, read_byte_offset,
                                                  send_sz, bufpos);
        if (rc != UNIFYFS_SUCCESS) {
            ret = rc;
            LOGERR("failed data rpc for mread[%d] request %d "
                   "(gfid=%d, offset=%zu, length=%zu)",
                   mread_id, read_ndx, resp->gfid,
                   req_file_offset + read_byte_offset, send_sz);
        }

        bufpos += send_sz;
        read_byte_offset += send_sz;
    }

    *bytes_processed = data_size - bytes_left;

    return ret;
}

/**
 * process the requested chunk data returned from service managers
 *
 * @param thrd_ctrl      request manager thread state
 * @param rdreq          server read request
 * @param server_chunks  remote server chunk reads
 * @return success/error code
 */
int rm_handle_chunk_read_responses(reqmgr_thrd_t* thrd_ctrl,
                                   server_read_req_t* rdreq,
                                   server_chunk_reads_t* server_chunks)
{
    // NOTE: this fn assumes thrd_ctrl->thrd_lock is locked

    int i, num_chks, rc;
    int ret = (int)UNIFYFS_SUCCESS;
    chunk_read_resp_t* responses = NULL;
    char* data_buf = NULL;

    assert((NULL != thrd_ctrl) &&
           (NULL != rdreq) &&
           (NULL != server_chunks) &&
           (NULL != server_chunks->resp));

    num_chks = server_chunks->num_chunks;
    if (server_chunks->status != READREQ_STARTED) {
        LOGERR("chunk read response for non-started req @ index=%d",
               rdreq->req_ndx);
        ret = (int32_t)EINVAL;
    } else if (0 == server_chunks->total_sz) {
        LOGERR("empty chunk read response from server %d",
               server_chunks->rank);
        ret = (int32_t)EINVAL;
    } else {
        LOGDBG("handling chunk read responses from server %d: "
               "num_chunks=%d buf_size=%zu",
               server_chunks->rank, num_chks, server_chunks->total_sz);
        responses = server_chunks->resp;
        data_buf = (char*)(responses + num_chks);

        for (i = 0; i < num_chks; i++) {
            chunk_read_resp_t* resp = responses + i;
            size_t processed = 0;

            rc = send_data_to_client(rdreq, resp, data_buf, &processed);
            if (rc != UNIFYFS_SUCCESS) {
                LOGERR("failed to send data to client (ret=%d)", rc);
                ret = rc;
            }

            data_buf += processed;
        }

        /* cleanup */
        free((void*)responses);
        server_chunks->resp = NULL;

        /* update request status */
        server_chunks->status = READREQ_COMPLETE;

        /* if all remote reads are complete, mark the request as complete */
        int completed_remote_reads = 0;
        for (i = 0; i < rdreq->num_server_reads; i++) {
            if (rdreq->remote_reads[i].status != READREQ_COMPLETE) {
                break;
            }
            completed_remote_reads++;
        }
        if (completed_remote_reads == rdreq->num_server_reads) {
            rdreq->status = READREQ_COMPLETE;

            int app_id = rdreq->app_id;
            int client_id = rdreq->client_id;
            int mread_id = rdreq->client_mread;
            int read_ndx = rdreq->client_read_ndx;
            int errcode = 0;
            if (ret != UNIFYFS_SUCCESS) {
                errcode = ret;
            }
            rc = invoke_client_mread_req_complete_rpc(app_id, client_id,
                                                      mread_id, read_ndx,
                                                      errcode);
            if (rc != UNIFYFS_SUCCESS) {
                LOGERR("mread[%d] request %d completion rpc failed (rc=%d)",
                       mread_id, read_ndx, rc);
                ret = rc;
            }
        }
    }

    return ret;
}

/* submit a client callback request to the request manager thread */
int rm_submit_client_callback_request(client_callback_req* req)
{
    assert(req != NULL);

    /* get application client */
    app_client* client = get_app_client(req->app_id, req->client_id);
    if (NULL == client) {
        LOGERR("app client [%d:%d] lookup failed",
               req->app_id, req->client_id);
        return EINVAL;
    }

    /* LOGDBG("client callback: client=[%d:%d], type=%d, gfid=%d",
     *      req->app_id, req->client_id, req->req_type, req->gfid); */

    /* get thread control structure */
    reqmgr_thrd_t* reqmgr = client->reqmgr;
    assert(NULL != reqmgr);
    RM_REQ_LOCK(reqmgr);
    arraylist_add(reqmgr->client_callbacks, req);
    RM_REQ_UNLOCK(reqmgr);

    signal_new_requests(reqmgr);

    return UNIFYFS_SUCCESS;
}

/* this qsort() comparison function groups callbacks by type, then
 * client + gfid. It also pushes any NULL elements to the end of the array */
static int cb_arraylist_compare(const void* a, const void* b)
{
    const void* elema = *(const void**)a;
    const void* elemb = *(const void**)b;

    /* first handle the NULL cases (use 'NULL > ptr' to push NULLs to end) */
    if (NULL == elema) {
        if (NULL == elemb) {
            return 0;
        } else {
            return 1;
        }
    } else if (NULL == elemb) {
        return -1;
    }

    /* now compare the callback requests */
    const client_callback_req* reqa = elema;
    const client_callback_req* reqb = elemb;

    if (reqa->req_type < reqb->req_type) {
        return -1;
    } else if (reqa->req_type > reqb->req_type) {
        return 1;
    } else { // request types are equal
        if (reqa->app_id < reqb->app_id) {
            return -1;
        } else if (reqa->app_id > reqb->app_id) {
            return 1;
        } else { // app_ids are equal
            if (reqa->client_id < reqb->client_id) {
                return -1;
            } else if (reqa->client_id > reqb->client_id) {
                return 1;
            } else { // client_ids are equal
                if (reqa->gfid < reqb->gfid) {
                    return -1;
                } else if (reqa->gfid > reqb->gfid) {
                    return 1;
                } else { // gfids are equal
                    return 0;
                }
            }
        }
    }
}

/* iterate over list of callbacks and invoke rpcs */
static int rm_process_client_callbacks(reqmgr_thrd_t* reqmgr)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* this will hold a list of client requests if we find any */
    arraylist_t* client_cbs = NULL;

    /* lock to access requests */
    RM_REQ_LOCK(reqmgr);

    /* if we have any requests, take pointer to the list
     * of requests and replace it with a newly allocated
     * list on the request manager structure */
    int num_client_cbs = arraylist_size(reqmgr->client_callbacks);
    if (num_client_cbs) {
        /* got some client requets, take the list and replace
         * it with an empty list */
        LOGDBG("processing %d client callback requests", num_client_cbs);
        client_cbs = reqmgr->client_callbacks;
        reqmgr->client_callbacks =
            arraylist_create(UNIFYFS_CLIENT_MAX_FILES);
    }

    /* release lock on reqmgr requests */
    RM_REQ_UNLOCK(reqmgr);

    if (0 == num_client_cbs) {
        return UNIFYFS_SUCCESS;
    }

    /* sort the requests to make it easy to find duplicates */
    int rc = arraylist_sort(client_cbs, cb_arraylist_compare);
    if (rc) {
        LOGERR("failed to sort client callback arraylist");
    }

    /* iterate over each client request */
    int last_req_type = -1;
    int last_req_app  = -1;
    int last_req_cli  = -1;
    int last_req_gfid = -1;
    client_callback_req* req;
    for (int i = 0; i < num_client_cbs; i++) {
        /* process next request */
        int rret;
        req = (client_callback_req*) arraylist_get(client_cbs, i);
        if (NULL == req) {
            continue;
        }

        /* ignore duplicate callback reqs */
        if ((last_req_type == req->req_type)  &&
            (last_req_app  == req->app_id)    &&
            (last_req_cli  == req->client_id) &&
            (last_req_gfid == req->gfid)) {
            continue;
        }
        last_req_type = req->req_type;
        last_req_app  = req->app_id;
        last_req_cli  = req->client_id;
        last_req_gfid = req->gfid;

        switch (req->req_type) {
        case UNIFYFS_CLIENT_CALLBACK_LAMINATE:
            LOGERR("laminate callback not yet implemented");
            rret = UNIFYFS_ERROR_NYI;
            break;
        case UNIFYFS_CLIENT_CALLBACK_TRUNCATE:
            LOGERR("truncate callback not yet implemented");
            rret = UNIFYFS_ERROR_NYI;
            break;
        case UNIFYFS_CLIENT_CALLBACK_UNLINK:
            LOGDBG("unlink callback - client[%d:%d] gfid=%d",
                   req->app_id, req->client_id, req->gfid);
            rret = invoke_client_unlink_callback_rpc(req->app_id,
                                                     req->client_id,
                                                     req->gfid);
            break;
        default:
            LOGERR("unsupported client rpc request type %d", req->req_type);
            rret = UNIFYFS_ERROR_NYI;
            break;
        }
        if (rret != UNIFYFS_SUCCESS) {
            if ((rret != ENOENT) && (rret != EEXIST)) {
                LOGERR("client rpc request %d failed (%s)",
                       i, unifyfs_rc_enum_description(rret));
            }
            ret = rret;
        }
    }

    /* free the list if we have one */
    if (NULL != client_cbs) {
        /* NOTE: this will call free() on each req in the arraylist */
        arraylist_free(client_cbs);
    }

    return ret;
}

/* submit a client rpc request to the request manager thread */
int rm_submit_client_rpc_request(unifyfs_fops_ctx_t* ctx,
                                 client_rpc_req_t* req)
{
    assert((ctx != NULL) && (req != NULL));

    /* get application client */
    app_client* client = get_app_client(ctx->app_id, ctx->client_id);
    if (NULL == client) {
        LOGERR("app client [%d:%d] lookup failed",
               ctx->app_id, ctx->client_id);
        return EINVAL;
    }

    /* get thread control structure */
    reqmgr_thrd_t* reqmgr = client->reqmgr;
    assert(NULL != reqmgr);
    RM_REQ_LOCK(reqmgr);
    arraylist_add(reqmgr->client_reqs, req);
    RM_REQ_UNLOCK(reqmgr);

    signal_new_requests(reqmgr);

    return UNIFYFS_SUCCESS;
}

static int process_attach_rpc(reqmgr_thrd_t* reqmgr,
                              client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_attach_in_t* in = req->input;
    assert(in != NULL);

    /* lookup client structure and attach it */
    int app_id = reqmgr->app_id;
    int client_id = reqmgr->client_id;
    app_client* client = get_app_client(app_id, client_id);
    if (NULL != client) {
        LOGDBG("attaching client %d:%d", app_id, client_id);
        ret = attach_app_client(client,
                                in->logio_spill_dir,
                                in->logio_spill_size,
                                in->logio_mem_size,
                                in->shmem_super_size,
                                in->meta_offset,
                                in->meta_size);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("attach_app_client() failed");
        }
    } else {
        LOGERR("client not found (app_id=%d, client_id=%d)",
            app_id, client_id);
        ret = (int)UNIFYFS_FAILURE;
    }

    margo_free_input(req->handle, in);
    free(in);

    /* send rpc response */
    unifyfs_attach_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_filesize_rpc(reqmgr_thrd_t* reqmgr,
                                client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;
    size_t filesize = 0;

    unifyfs_filesize_in_t* in = req->input;
    assert(in != NULL);
    int gfid = in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    LOGDBG("getting filesize for gfid=%d", gfid);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    ret = unifyfs_fops_filesize(&ctx, gfid, &filesize);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unifyfs_fops_filesize() failed");
    }

    /* send rpc response */
    unifyfs_filesize_out_t out;
    out.ret = (int32_t) ret;
    out.filesize = filesize;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_fsync_rpc(reqmgr_thrd_t* reqmgr,
                             client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_fsync_in_t* in = req->input;
    assert(in != NULL);
    int gfid = in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    LOGINFO("syncing gfid=%d", gfid);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    ret = unifyfs_fops_fsync(&ctx, gfid);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unifyfs_fops_fsync() failed");
    }

    /* send rpc response */
    unifyfs_fsync_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_laminate_rpc(reqmgr_thrd_t* reqmgr,
                                client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_laminate_in_t* in = req->input;
    assert(in != NULL);
    int gfid = in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    LOGDBG("laminating gfid=%d", gfid);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    ret = unifyfs_fops_laminate(&ctx, gfid);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unifyfs_fops_laminate() failed");
    }

    /* send rpc response */
    unifyfs_laminate_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_metaget_rpc(reqmgr_thrd_t* reqmgr,
                               client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_metaget_in_t* in = req->input;
    assert(in != NULL);
    int gfid = in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    LOGDBG("getting metadata for gfid=%d", gfid);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    unifyfs_file_attr_t fattr;
    memset(&fattr, 0, sizeof(fattr));
    ret = unifyfs_fops_metaget(&ctx, gfid, &fattr);
    if (ret != UNIFYFS_SUCCESS) {
        LOGDBG("unifyfs_fops_metaget() failed");
    }

    /* send rpc response */
    unifyfs_metaget_out_t out;
    out.ret = (int32_t) ret;
    out.attr = fattr;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_metaset_rpc(reqmgr_thrd_t* reqmgr,
                               client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_metaset_in_t* in = req->input;
    assert(in != NULL);
    int gfid = in->attr.gfid;
    int attr_op = (int) in->attr_op;
    unifyfs_file_attr_t fattr = in->attr;
    if (NULL != in->attr.filename) {
        fattr.filename = strdup(in->attr.filename);
    }

    /* This is somewhat ugly: if the input came via a standard Mercury RPC,
     * then req->handle will exist and margo_free_input() will clean up 'in'
     * correctly.
     *
     * *HOWEVER*, there's one case where we'll end up here without going
     * through an RPC: the request created by create_mountpoint_dir() is
     * created locally.  More specifically,  margo_get_input() is not used to
     * create the 'in' struct and thus margo_free_input() should not be
     * called.  That said, in->attr.filename is allocated with strdup(), and
     * must therefore be freed before we free 'in'.
     */
    if (HG_HANDLE_NULL != req->handle) {
        margo_free_input(req->handle, in);
    } else {
        if (NULL != in->attr.filename) {
            free(in->attr.filename);
        }
    }
    free(in);

    LOGDBG("setting metadata for gfid=%d", gfid);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    ret = unifyfs_fops_metaset(&ctx, gfid, attr_op, &fattr);
    if (ret != UNIFYFS_SUCCESS) {
        LOGDBG("unifyfs_fops_metaset() failed");
    }

    if (NULL != fattr.filename) {
        free(fattr.filename);
    }

    if (HG_HANDLE_NULL != req->handle) {
        /* send rpc response */
        unifyfs_metaset_out_t out;
        out.ret = (int32_t) ret;
        hg_return_t hret = margo_respond(req->handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* cleanup req */
        margo_destroy(req->handle);
    }
    return ret;
}

static int process_read_rpc(reqmgr_thrd_t* reqmgr,
                            client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_mread_in_t* in = req->input;
    assert(in != NULL);
    int mread_id = in->mread_id;
    size_t read_count = in->read_count;
    margo_free_input(req->handle, in);
    free(in);

    LOGDBG("processing mread[%d] with %zu requests", mread_id, read_count);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
        .mread_id = mread_id
    };
    ret = unifyfs_fops_mread(&ctx, read_count, req->bulk_buf);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unifyfs_fops_read() failed");
    }
    free(req->bulk_buf);

    /* send rpc response */
    unifyfs_mread_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_transfer_rpc(reqmgr_thrd_t* reqmgr,
                                client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_transfer_in_t* in = req->input;
    assert(in != NULL);
    int transfer_id = in->transfer_id;
    int gfid = in->gfid;
    int mode = in->mode;
    const char* dest_file = strdup(in->dst_file);
    margo_free_input(req->handle, in);
    free(in);

    LOGDBG("transferring gfid=%d to file %s", gfid, dest_file);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    ret = unifyfs_fops_transfer(&ctx, transfer_id, gfid, mode, dest_file);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unifyfs_fops_transfer() failed");
    }

    /* send rpc response */
    unifyfs_transfer_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_truncate_rpc(reqmgr_thrd_t* reqmgr,
                                client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_truncate_in_t* in = req->input;
    assert(in != NULL);
    int gfid = in->gfid;
    size_t filesize = in->filesize;
    margo_free_input(req->handle, in);
    free(in);

    LOGDBG("truncating gfid=%d, sz=%zu", gfid, filesize);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    ret = unifyfs_fops_truncate(&ctx, gfid, filesize);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unifyfs_fops_truncate() failed");
    }

    /* send rpc response */
    unifyfs_truncate_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_unlink_rpc(reqmgr_thrd_t* reqmgr,
                              client_rpc_req_t* req)
{
    int ret = UNIFYFS_SUCCESS;

    unifyfs_unlink_in_t* in = req->input;
    assert(in != NULL);
    int gfid = in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    LOGDBG("unlinking gfid=%d", gfid);

    unifyfs_fops_ctx_t ctx = {
        .app_id = reqmgr->app_id,
        .client_id = reqmgr->client_id,
    };
    ret = unifyfs_fops_unlink(&ctx, gfid);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("unifyfs_fops_unlink() failed");
    }

    /* send rpc response */
    unifyfs_unlink_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

/* iterate over list of chunk reads and send responses */
static int rm_process_client_requests(reqmgr_thrd_t* reqmgr)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* this will hold a list of client requests if we find any */
    arraylist_t* client_reqs = NULL;

    /* lock to access requests */
    RM_REQ_LOCK(reqmgr);

    /* if we have any requests, take pointer to the list
     * of requests and replace it with a newly allocated
     * list on the request manager structure */
    int num_client_reqs = arraylist_size(reqmgr->client_reqs);
    if (num_client_reqs) {
        /* got some client requets, take the list and replace
         * it with an empty list */
        LOGDBG("processing %d client requests", num_client_reqs);
        client_reqs = reqmgr->client_reqs;
        reqmgr->client_reqs =
            arraylist_create(UNIFYFS_CLIENT_MAX_ACTIVE_REQUESTS);
    }

    /* release lock on reqmgr requests */
    RM_REQ_UNLOCK(reqmgr);

    /* iterate over each client request */
    for (int i = 0; i < num_client_reqs; i++) {
        /* process next request */
        int rret;
        client_rpc_req_t* req = (client_rpc_req_t*)
            arraylist_get(client_reqs, i);
        switch (req->req_type) {
        case UNIFYFS_CLIENT_RPC_ATTACH:
            rret = process_attach_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_FILESIZE:
            rret = process_filesize_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_LAMINATE:
            rret = process_laminate_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_METAGET:
            rret = process_metaget_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_METASET:
            rret = process_metaset_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_READ:
            rret = process_read_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_SYNC:
            rret = process_fsync_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_TRANSFER:
            rret = process_transfer_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_TRUNCATE:
            rret = process_truncate_rpc(reqmgr, req);
            break;
        case UNIFYFS_CLIENT_RPC_UNLINK:
            rret = process_unlink_rpc(reqmgr, req);
            break;
        default:
            LOGERR("unsupported client rpc request type %d", req->req_type);
            rret = UNIFYFS_ERROR_NYI;
            break;
        }
        if (rret != UNIFYFS_SUCCESS) {
            if ((rret != ENOENT) && (rret != EEXIST)) {
                LOGERR("client rpc request %d failed (%s)",
                       i, unifyfs_rc_enum_description(rret));
            }
            ret = rret;
        }
    }

    /* free the list if we have one */
    if (NULL != client_reqs) {
        /* NOTE: this will call free() on each req in the arraylist */
        arraylist_free(client_reqs);
    }

    return ret;
}

static int rm_heartbeat(reqmgr_thrd_t* reqmgr)
{
    static time_t last_check; // = 0
    static int check_interval = 30; /* seconds */

    int ret = UNIFYFS_SUCCESS;

    /* send a heartbeat rpc to associated client every 30 seconds */
    time_t now = time(NULL);
    if (0 == last_check) {
        last_check = now;
    }

    time_t elapsed = now - last_check;
    if (elapsed >= check_interval) {
        last_check = now;

        /* invoke heartbeat rpc */
        LOGDBG("sending heartbeat rpc");
        int app = reqmgr->app_id;
        int clid = reqmgr->client_id;
        int rc = invoke_client_heartbeat_rpc(app, clid);
        if (rc != UNIFYFS_SUCCESS) {
            ret = rc;
            LOGDBG("heartbeat rpc for client[%d:%d] failed", app, clid);
            add_failed_client(app, clid);
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
void* request_manager_thread(void* arg)
{
    /* get pointer to our thread control structure */
    reqmgr_thrd_t* thrd_ctrl = (reqmgr_thrd_t*) arg;
    int appid = thrd_ctrl->app_id;
    int clid  = thrd_ctrl->client_id;

    thrd_ctrl->tid = unifyfs_gettid();
    LOGINFO("I am request manager [app=%d:client=%d] thread!", appid, clid);

    /* loop forever to handle read requests from the client,
     * new requests are added to a list on a shared data structure
     * with main thread, new items inserted by the rpc handler */
    int rc;
    while (1) {
        /* process any client callback requests */
        rc = rm_process_client_callbacks(thrd_ctrl);
        if (rc != UNIFYFS_SUCCESS) {
            LOGWARN("failed to process client rpc requests");
        }

        /* process any client requests */
        rc = rm_process_client_requests(thrd_ctrl);
        if (rc != UNIFYFS_SUCCESS) {
            LOGWARN("failed to process client rpc requests");
        }

         /* send chunk read requests to remote servers */
        rc = rm_request_remote_chunks(thrd_ctrl);
        if (rc != UNIFYFS_SUCCESS) {
            LOGWARN("failed to request remote chunks");
        }

        /* process any chunk read responses */
        rc = rm_process_remote_chunk_responses(thrd_ctrl);
        if (rc != UNIFYFS_SUCCESS) {
            LOGWARN("failed to process remote chunk responses");
        }

        /* grab lock */
        RM_LOCK(thrd_ctrl);

        /* set flag to indicate that we're waiting for work */
        thrd_ctrl->waiting_for_work = 1;

        /* release lock and wait to be signaled by dispatcher */
        //LOGDBG("RM[%d:%d] waiting for work", appid, clid);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 10000000; /* 10 ms */
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_nsec -= 1000000000;
            timeout.tv_sec++;
        }
        int wait_rc = pthread_cond_timedwait(&thrd_ctrl->thrd_cond,
                                             &thrd_ctrl->thrd_lock,
                                             &timeout);
        if (0 == wait_rc) {
            LOGDBG("RM[%d:%d] got work", appid, clid);
        } else if (ETIMEDOUT != wait_rc) {
            LOGERR("RM[%d:%d] work condition wait failed (rc=%d)",
                   appid, clid, wait_rc);
        }

        /* set flag to indicate we're no longer waiting */
        thrd_ctrl->waiting_for_work = 0;
        RM_UNLOCK(thrd_ctrl);

        rc = rm_heartbeat(thrd_ctrl);
        if (rc != UNIFYFS_SUCCESS) {
            /* detected failure of our client, time to exit */
            break;
        }

        /* bail out if we've been told to exit */
        if (thrd_ctrl->exit_flag == 1) {
            break;
        }
    }

    LOGDBG("RM[%d:%d] thread exiting", appid, clid);

    return NULL;
}

