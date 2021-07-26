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
 * Copyright (c) 2017, Florida State University. Contributions from
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

#include "unifyfs_global.h"
#include "unifyfs_group_rpc.h"
#include "unifyfs_p2p_rpc.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"
#include "unifyfs_server_rpcs.h"
#include "unifyfs_transfer.h"
#include "margo_server.h"

/* Service Manager (SM) state */
typedef struct {
    /* the SM thread */
    pthread_t thrd;
    pid_t tid;

    /* pthread mutex and condition variable for work notification */
    pthread_mutex_t thrd_lock;
    pthread_cond_t thrd_cond;

    /* thread status */
    int initialized;
    int waiting_for_work;
    volatile int time_to_exit;

    /* thread return status code */
    int sm_exit_rc;

    /* argobots mutex for synchronizing access to request state between
     * margo rpc handler ULTs and SM thread */
    ABT_mutex reqs_sync;

    /* list of chunk read requests from remote servers */
    arraylist_t* chunk_reads;

    /* list of local transfer requests */
    arraylist_t* local_transfers;
    arraylist_t* completed_transfers;

    /* list of service requests (server_rpc_req_t*) */
    arraylist_t* svc_reqs;

} svcmgr_state_t;
svcmgr_state_t* sm; // = NULL

#define SM_LOCK() \
do { \
    if ((NULL != sm) && sm->initialized) { \
        /*LOGDBG("locking SM state");*/ \
        pthread_mutex_lock(&(sm->thrd_lock)); \
    } \
} while (0)

#define SM_UNLOCK() \
do { \
    if ((NULL != sm) && sm->initialized) { \
        /*LOGDBG("unlocking SM state");*/ \
        pthread_mutex_unlock(&(sm->thrd_lock)); \
    } \
} while (0)

#define SM_REQ_LOCK() \
do { \
    if ((NULL != sm) && sm->initialized) { \
        /*LOGDBG("locking SM requests");*/ \
        ABT_mutex_lock(sm->reqs_sync); \
    } \
} while (0)

#define SM_REQ_UNLOCK() \
do { \
    if ((NULL != sm) && sm->initialized) { \
        /*LOGDBG("unlocking SM requests");*/ \
        ABT_mutex_unlock(sm->reqs_sync); \
    } \
} while (0)


static inline void signal_svcmgr(void)
{
    pid_t this_thread = unifyfs_gettid();
    if (this_thread != sm->tid) {
        /* signal svcmgr to begin processing the requests we just added */
        LOGDBG("signaling new service requests");
        pthread_cond_signal(&(sm->thrd_cond));
    }
}

/* submit a request to the service manager thread */
int sm_submit_service_request(server_rpc_req_t* req)
{
    if ((NULL == sm) || (NULL == sm->svc_reqs)) {
        return UNIFYFS_FAILURE;
    }

    SM_REQ_LOCK();
    arraylist_add(sm->svc_reqs, req);
    SM_REQ_UNLOCK();

    signal_svcmgr();

    return UNIFYFS_SUCCESS;
}

/* submit a transfer request to the service manager thread */
int sm_submit_transfer_request(transfer_thread_args* tta)
{
    if ((NULL == sm) || (NULL == sm->local_transfers)) {
        return UNIFYFS_FAILURE;
    }

    SM_REQ_LOCK();
    arraylist_add(sm->local_transfers, tta);
    SM_REQ_UNLOCK();

    signal_svcmgr();

    return UNIFYFS_SUCCESS;
}

/* tell service manager thread transfer has completed */
int sm_complete_transfer_request(transfer_thread_args* tta)
{
    if ((NULL == sm) || (NULL == sm->completed_transfers)) {
        return UNIFYFS_FAILURE;
    }

    SM_REQ_LOCK();
    arraylist_add(sm->completed_transfers, tta);
    SM_REQ_UNLOCK();

    signal_svcmgr();

    return UNIFYFS_SUCCESS;
}

/* initialize and launch service manager thread */
int svcmgr_init(void)
{
    /* allocate a struct to maintain service manager state.
     * store pointer to struct in a global variable */
    sm = (svcmgr_state_t*)calloc(1, sizeof(svcmgr_state_t));
    if (NULL == sm) {
        LOGERR("failed to allocate service manager state!");
        return ENOMEM;
    }

    /* initialize lock for shared data structures of the
     * service manager */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rc = pthread_mutex_init(&(sm->thrd_lock), &attr);
    if (rc != 0) {
        LOGERR("pthread_mutex_init failed for service manager rc=%d (%s)",
               rc, strerror(rc));
        svcmgr_fini();
        return rc;
    }

    /* initialize condition variable to synchronize work
     * notifications for the request manager thread */
    rc = pthread_cond_init(&(sm->thrd_cond), NULL);
    if (rc != 0) {
        LOGERR("pthread_cond_init failed for service manager rc=%d (%s)",
               rc, strerror(rc));
        pthread_mutex_destroy(&(sm->thrd_lock));
        svcmgr_fini();
        return rc;
    }

    ABT_mutex_create(&(sm->reqs_sync));

    /* allocate a list to track chunk reads */
    sm->chunk_reads = arraylist_create(0);
    if (sm->chunk_reads == NULL) {
        LOGERR("failed to allocate service manager chunk_reads!");
        svcmgr_fini();
        return ENOMEM;
    }

    /* allocate lists to track local transfer requests */
    sm->local_transfers = arraylist_create(0);
    if (sm->local_transfers == NULL) {
        LOGERR("failed to allocate service manager local_transfers!");
        svcmgr_fini();
        return ENOMEM;
    }
    sm->completed_transfers = arraylist_create(0);
    if (sm->completed_transfers == NULL) {
        LOGERR("failed to allocate service manager completed_transfers!");
        svcmgr_fini();
        return ENOMEM;
    }

    /* allocate a list to track service requests */
    sm->svc_reqs = arraylist_create(0);
    if (sm->svc_reqs == NULL) {
        LOGERR("failed to allocate service manager svc_reqs!");
        svcmgr_fini();
        return ENOMEM;
    }

    sm->tid = -1;
    sm->initialized = 1;

    rc = pthread_create(&(sm->thrd), NULL, service_manager_thread, (void*)sm);
    if (rc != 0) {
        LOGERR("failed to create service manager thread");
        svcmgr_fini();
        return UNIFYFS_ERROR_THREAD;
    }

    return UNIFYFS_SUCCESS;
}

/* join service manager thread (if created) and clean up state */
int svcmgr_fini(void)
{
    if (NULL != sm) {
        if (sm->initialized) {
            /* join thread before cleaning up state */
            if (sm->tid != -1) {
                pthread_mutex_lock(&(sm->thrd_lock));
                sm->time_to_exit = 1;
                pthread_cond_signal(&(sm->thrd_cond));
                pthread_mutex_unlock(&(sm->thrd_lock));
                pthread_join(sm->thrd, NULL);
            }
        }

        if (NULL != sm->chunk_reads) {
            arraylist_free(sm->chunk_reads);
        }

        if (NULL != sm->local_transfers) {
            arraylist_free(sm->local_transfers);
        }

        if (NULL != sm->completed_transfers) {
            arraylist_free(sm->completed_transfers);
        }

        if (NULL != sm->svc_reqs) {
            arraylist_free(sm->svc_reqs);
        }

        if (sm->initialized) {
            pthread_mutex_destroy(&(sm->thrd_lock));
            pthread_cond_destroy(&(sm->thrd_cond));
        }

        /* free the service manager struct allocated during init */
        free(sm);
        sm = NULL;
    }
    return UNIFYFS_SUCCESS;
}

/* Decode and issue chunk-reads received from request manager.
 * We get a list of read requests for data on our node.  Read
 * data for each request and construct a set of read replies
 * that will be sent back to the request manager.
 *
 * @param src_rank      : source server rank
 * @param src_app_id    : app id at source server
 * @param src_client_id : client id at source server
 * @param src_req_id    : request id at source server
 * @param num_chks      : number of chunk requests
 * @param msg_buf       : message buffer containing request(s)
 * @return success/error code
 */
int sm_issue_chunk_reads(int src_rank,
                         int src_app_id,
                         int src_client_id,
                         int src_req_id,
                         int num_chks,
                         size_t total_data_sz,
                         char* msg_buf)
{
    /* get pointer to read request array */
    chunk_read_req_t* reqs = (chunk_read_req_t*)msg_buf;

    /* we'll allocate a buffer to hold a list of chunk read response
     * structures, one for each chunk, followed by a data buffer
     * to hold all data for all reads */

    /* compute the size of that buffer */
    size_t resp_sz = sizeof(chunk_read_resp_t) * num_chks;
    size_t buf_sz  = resp_sz + total_data_sz;

    /* allocate the buffer */
    // NOTE: calloc() is required here, don't use malloc
    char* crbuf = (char*) calloc(1, buf_sz);
    if (NULL == crbuf) {
        LOGERR("failed to allocate chunk_read_reqs (buf_sz=%zu)", buf_sz);
        return ENOMEM;
    }

    /* the chunk read response array starts as the first
     * byte in our buffer and the data buffer follows
     * the read response array */
    chunk_read_resp_t* resp = (chunk_read_resp_t*)crbuf;
    char* databuf = crbuf + resp_sz;

    /* allocate a struct for the chunk read request */
    server_chunk_reads_t* scr = (server_chunk_reads_t*)
        calloc(1, sizeof(server_chunk_reads_t));
    if (NULL == scr) {
        LOGERR("failed to allocate remote_chunk_reads");
        return ENOMEM;
    }

    /* fill in chunk read request */
    scr->rank       = src_rank;
    scr->app_id     = src_app_id;
    scr->client_id  = src_client_id;
    scr->rdreq_id   = src_req_id;
    scr->num_chunks = num_chks;
    scr->reqs       = NULL;
    scr->total_sz   = buf_sz;
    scr->resp       = resp;

    LOGDBG("issuing %d requests for req=%d, total data size = %zu",
           num_chks, src_req_id, total_data_sz);

    /* points to offset in read reply buffer to place
     * data for next read */
    size_t buf_cursor = 0;

    int i;
    app_client* app_clnt = NULL;
    for (i = 0; i < num_chks; i++) {
        /* pointer to next read request */
        chunk_read_req_t* rreq = reqs + i;
        debug_print_chunk_read_req(rreq);

        /* pointer to next read response */
        chunk_read_resp_t* rresp = resp + i;

        /* get size and log offset of data we are to read */
        size_t nbytes = rreq->nbytes;
        size_t log_offset = rreq->log_offset;

        /* record request metadata in response */
        rresp->gfid    = rreq->gfid;
        rresp->read_rc = 0;
        rresp->nbytes  = nbytes;
        rresp->offset  = rreq->offset;

        /* get pointer to next position in buffer to store read data */
        char* buf_ptr = databuf + buf_cursor;

        /* read data from client log */
        int app_id = rreq->log_app_id;
        int cli_id = rreq->log_client_id;
        app_clnt = get_app_client(app_id, cli_id);
        if (NULL != app_clnt) {
            logio_context* logio_ctx = app_clnt->state.logio_ctx;
            if (NULL != logio_ctx) {
                size_t nread = 0;
                int rc = unifyfs_logio_read(logio_ctx, log_offset, nbytes,
                                            buf_ptr, &nread);
                if (UNIFYFS_SUCCESS == rc) {
                    rresp->read_rc = nread;
                } else {
                    rresp->read_rc = (ssize_t)(-rc);
                }
            } else {
                LOGERR("app client [%d:%d] has NULL logio context",
                       app_id, cli_id);
                rresp->read_rc = (ssize_t)(-EINVAL);
            }
        } else {
            LOGERR("failed to get application client [%d:%d] state",
                   app_id, cli_id);
            rresp->read_rc = (ssize_t)(-EINVAL);
        }

        /* update to point to next slot in read reply buffer */
        buf_cursor += nbytes;
    }

    if (src_rank != glb_pmi_rank) {
        /* we need to send these read responses to another rank,
         * add chunk_reads to svcmgr response list */
        LOGDBG("adding to svcmgr chunk_reads");
        assert(NULL != sm);

        SM_REQ_LOCK();
        arraylist_add(sm->chunk_reads, scr);
        SM_REQ_UNLOCK();

        /* scr will be freed later by the sending thread */

        LOGDBG("done adding to svcmgr chunk_reads");
        return UNIFYFS_SUCCESS;
    } else {
        /* response is for myself, post it directly */
        LOGDBG("responding to myself");
        int rc = rm_post_chunk_read_responses(src_app_id, src_client_id,
                                              src_rank, src_req_id,
                                              num_chks, buf_sz, crbuf);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to handle chunk read responses");
        }

        /* clean up allocated buffers */
        free(scr);

        return rc;
    }
}

int sm_laminate(int gfid)
{
    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    int ret = unifyfs_inode_laminate(gfid);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to laminate gfid=%d (rc=%d, is_owner=%d)",
               gfid, ret, is_owner);
    } else if (is_owner) {
        /* I'm the owner, tell the rest of the servers */
        ret = unifyfs_invoke_broadcast_laminate(gfid);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("laminate broadcast failed");
        }
    }
    return ret;
}

int sm_get_fileattr(int gfid,
                    unifyfs_file_attr_t* attrs)
{
    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    /* do local inode metadata lookup */
    int ret = unifyfs_inode_metaget(gfid, attrs);
    if (ret) {
        if (ret != ENOENT) {
            LOGERR("failed to get attributes for gfid=%d (rc=%d, is_owner=%d)",
                    gfid, ret, is_owner);
        }
    }
    return ret;
}

int sm_set_fileattr(int gfid,
                    int file_op,
                    unifyfs_file_attr_t* attrs)
{
    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    /* set local metadata for target file */
    int ret = unifyfs_inode_metaset(gfid, file_op, attrs);
    if (ret) {
        if ((ret == EEXIST) && (file_op == UNIFYFS_FILE_ATTR_OP_CREATE)) {
            LOGWARN("create requested for existing gfid=%d", gfid);
        } else {
            LOGERR("failed to set attributes for gfid=%d (rc=%d, is_owner=%d)",
                   gfid, ret, is_owner);
        }
    }
    return ret;
}

int sm_add_extents(int gfid,
                   size_t num_extents,
                   struct extent_tree_node* extents)
{
    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    unsigned int n_extents = (unsigned int)num_extents;
    int ret = unifyfs_inode_add_extents(gfid, n_extents, extents);
    if (ret) {
        LOGERR("failed to add %u extents to gfid=%d (rc=%d, is_owner=%d)",
               n_extents, gfid, ret, is_owner);
    }
    return ret;
}

int sm_find_extents(int gfid,
                    size_t num_extents,
                    unifyfs_inode_extent_t* extents,
                    unsigned int* out_num_chunks,
                    chunk_read_req_t** out_chunks)
{
    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    /* do local inode metadata lookup to check for laminated */
    unifyfs_file_attr_t attrs;
    int ret = unifyfs_inode_metaget(gfid, &attrs);
    if (ret == UNIFYFS_SUCCESS) {
        /* do local lookup */
        if (is_owner || attrs.is_laminated) {
            unsigned int n_extents = (unsigned int)num_extents;
            ret = unifyfs_inode_resolve_extent_chunks(n_extents, extents,
                                                      out_num_chunks,
                                                      out_chunks);
            if (ret) {
                LOGERR("failed to find extents for gfid=%d (rc=%d)",
                    gfid, ret);
            } else if (*out_num_chunks == 0) {
                LOGDBG("extent lookup found no matching chunks");
            }
        } else {
            LOGWARN("cannot find extents for unlaminated file at non-owner");
            ret = UNIFYFS_FAILURE;
        }
    }
    return ret;
}

int sm_transfer(int client_server,
                int client_app,
                int client_id,
                int transfer_id,
                int gfid,
                int transfer_mode,
                const char* dest_file,
                server_rpc_req_t* bcast_req)
{
    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    unifyfs_file_attr_t attrs;
    int ret = unifyfs_inode_metaget(gfid, &attrs);
    if (ret == UNIFYFS_SUCCESS) {
        /* we have local file state */
        LOGDBG("transfer - gfid=%d mode=%d file=%s",
               gfid, transfer_mode, dest_file);
        transfer_thread_args* tta = calloc(1, sizeof(*tta));
        if (transfer_mode == TRANSFER_MODE_LOCAL) {
            /* each server transfers local data to the destination file */
            int rc = create_local_transfers(gfid, dest_file, tta);
            if (rc != UNIFYFS_SUCCESS) {
                ret = rc;
            } else {
                /* submit transfer request for processing */
                tta->bcast_req = bcast_req;
                tta->client_server = client_server;
                tta->client_app = client_app;
                tta->client_id = client_id;
                tta->transfer_id = transfer_id;
                rc = sm_submit_transfer_request(tta);
                if (rc != UNIFYFS_SUCCESS) {
                    ret = rc;
                }
            }
        } else if (is_owner && (transfer_mode == TRANSFER_MODE_OWNER)) {
            // TODO: support TRANSFER_MODE_OWNER
            ret = UNIFYFS_ERROR_NYI;
        }
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("transfer(gfid=%d, mode=%d, file=%s) failed",
                   gfid, transfer_mode, dest_file);
        }
    }
    return ret;
}

int sm_truncate(int gfid, size_t filesize)
{
    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    unifyfs_file_attr_t attrs;
    int ret = unifyfs_inode_metaget(gfid, &attrs);
    if (ret == UNIFYFS_SUCCESS) {
        /* apply truncation to local file state */
        size_t old_size = (size_t) attrs.size;
        LOGDBG("truncate - gfid=%d size=%zu old-size=%zu",
               gfid, filesize, old_size);
        ret = unifyfs_inode_truncate(gfid, (unsigned long)filesize);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("truncate(gfid=%d, size=%zu) failed",
                   gfid, filesize);
        } else if (is_owner && (filesize < old_size)) {
            /* truncate the target file at other servers */
            ret = unifyfs_invoke_broadcast_truncate(gfid, filesize);
            if (ret != UNIFYFS_SUCCESS) {
                LOGERR("truncate broadcast failed");
            }
        }
    }
    return ret;
}


/* iterate over list of chunk reads and send responses */
static int send_chunk_read_responses(void)
{
    /* assume we'll succeed */
    int rc = UNIFYFS_SUCCESS;

    /* this will hold a list of chunk read requests if we find any */
    arraylist_t* chunk_reads = NULL;

    /* lock to access global service manager object */
    SM_REQ_LOCK();

    /* if we have any chunk reads, take pointer to the list
     * of chunk read requests and replace it with a newly allocated
     * list on the service manager structure */
    int num_chunk_reads = arraylist_size(sm->chunk_reads);
    if (num_chunk_reads) {
        /* got some chunk read requests, take the list and replace
         * it with an empty list */
        LOGDBG("processing %d chunk read responses", num_chunk_reads);
        chunk_reads = sm->chunk_reads;
        sm->chunk_reads = arraylist_create(0);
    }

    /* release lock on service manager object */
    SM_REQ_UNLOCK();

    /* iterate over each chunk read request */
    for (int i = 0; i < num_chunk_reads; i++) {
        /* get next chunk read request */
        server_chunk_reads_t* scr = (server_chunk_reads_t*)
            arraylist_get(chunk_reads, i);

        rc = invoke_chunk_read_response_rpc(scr);
    }

    /* free the list if we have one */
    if (NULL != chunk_reads) {
        arraylist_free(chunk_reads);
    }

    return rc;
}

static int spawn_local_transfers(void)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* this will hold a list of local transfers if we find any */
    arraylist_t* transfers = NULL;

    /* lock to access global service manager object */
    SM_REQ_LOCK();

    /* if we have any local transfers, take pointer to the list
     * of transfer args and replace it with a newly allocated
     * list on the service manager structure */
    int num_transfers = arraylist_size(sm->local_transfers);
    if (num_transfers) {
        /* got some transfer requests, take the list and replace
         * it with an empty list */
        LOGDBG("processing %d local transfers", num_transfers);
        transfers = sm->local_transfers;
        sm->local_transfers = arraylist_create(0);
    }

    /* release lock on service manager object */
    SM_REQ_UNLOCK();

    /* iterate over each transfer and spawn helper thread */
    transfer_thread_args* tta;
    for (int i = 0; i < num_transfers; i++) {
        /* get next transfer */
        tta = (transfer_thread_args*) arraylist_remove(transfers, i);

        /* spawn transfer helper thread */
        int rc = pthread_create(&(tta->thrd), NULL,
                                transfer_helper_thread, (void*)tta);
        if (rc != 0) {
            LOGERR("failed to spawn transfer helper thread for tta=%p", tta);
            ret = UNIFYFS_ERROR_THREAD;
            release_transfer_thread_args(tta);
        }
    }

    return ret;
}

static int complete_local_transfers(void)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* this will hold a list of local transfers if we find any */
    arraylist_t* transfers = NULL;

    /* lock to access global service manager object */
    SM_REQ_LOCK();

    /* if we have any local transfers, take pointer to the list
     * of transfer args and replace it with a newly allocated
     * list on the service manager structure */
    int num_transfers = arraylist_size(sm->completed_transfers);
    if (num_transfers) {
        /* got some transfer requests, take the list and replace
         * it with an empty list */
        LOGDBG("completing %d local transfers", num_transfers);
        transfers = sm->completed_transfers;
        sm->completed_transfers = arraylist_create(0);
    }

    /* release lock on service manager object */
    SM_REQ_UNLOCK();

    /* iterate over each transfer and spawn helper thread */
    transfer_thread_args* tta;
    for (int i = 0; i < num_transfers; i++) {
        /* get next transfer */
        tta = (transfer_thread_args*) arraylist_remove(transfers, i);

        /* spawn transfer helper thread */
        int rc = pthread_join(tta->thrd, NULL);
        if (rc != 0) {
            LOGERR("failed to join transfer helper thread for tta=%p", tta);
            ret = UNIFYFS_ERROR_THREAD;
        }

        if (glb_pmi_rank == tta->client_server) {
            rc = invoke_client_transfer_complete_rpc(tta->client_app,
                                                     tta->client_id,
                                                     tta->transfer_id,
                                                     tta->status);
            if (rc != 0) {
                LOGERR("failed transfer(id=%d) complete rpc to client[%d:%d]",
                       tta->transfer_id, tta->client_app, tta->client_id);
                ret = rc;
            }
        }

        release_transfer_thread_args(tta);
    }

    return ret;
}

static int process_chunk_read_rpc(server_rpc_req_t* req)
{
    int ret;
    chunk_read_request_in_t* in = req->input;

    /* issue chunk read requests */
    int src_rank    = (int)in->src_rank;
    int app_id      = (int)in->app_id;
    int client_id   = (int)in->client_id;
    int req_id      = (int)in->req_id;
    int num_chks    = (int)in->num_chks;
    size_t total_sz = (size_t)in->total_data_size;

    LOGDBG("handling chunk read requests from server[%d]: "
           "req=%d num_chunks=%d data_sz=%zu bulk_sz=%zu",
           src_rank, req_id, num_chks, total_sz, req->bulk_sz);

    ret = sm_issue_chunk_reads(src_rank, app_id, client_id,
                               req_id, num_chks, total_sz,
                               (char*)req->bulk_buf);

    margo_free_input(req->handle, in);
    free(in);
    free(req->bulk_buf);

    /* send rpc response */
    chunk_read_request_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_add_extents_rpc(server_rpc_req_t* req)
{
    /* get input parameters */
    add_extents_in_t* in = req->input;
    int sender = (int) in->src_rank;
    int gfid = (int) in->gfid;
    size_t num_extents = (size_t) in->num_extents;
    struct extent_tree_node* extents = req->bulk_buf;

    /* add extents */
    LOGDBG("adding %zu extents to gfid=%d from server[%d]",
           num_extents, gfid, sender);
    int ret = sm_add_extents(gfid, num_extents, extents);
    if (ret) {
        LOGERR("failed to add extents from %d (ret=%d)", sender, ret);
    }

    margo_free_input(req->handle, in);
    free(in);
    free(req->bulk_buf);

    /* send rpc response */
    add_extents_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_find_extents_rpc(server_rpc_req_t* req)
{
    /* get input parameters */
    find_extents_in_t* in = req->input;
    int sender = (int) in->src_rank;
    int gfid = (int) in->gfid;
    size_t num_extents = (size_t) in->num_extents;
    unifyfs_inode_extent_t* extents = req->bulk_buf;

    LOGDBG("received %zu extent lookups for gfid=%d from server[%d]",
           num_extents, gfid, sender);

    /* find chunks for given extents */
    unsigned int num_chunks = 0;
    chunk_read_req_t* chunk_locs = NULL;
    int ret = sm_find_extents(gfid, num_extents, extents,
                              &num_chunks, &chunk_locs);

    margo_free_input(req->handle, in);
    free(in);
    free(req->bulk_buf);

    /* define a bulk handle to transfer chunk address info */
    hg_bulk_t bulk_resp_handle = HG_BULK_NULL;
    if (ret == UNIFYFS_SUCCESS) {
        if (num_chunks > 0) {
            const struct hg_info* hgi = margo_get_info(req->handle);
            assert(hgi);
            margo_instance_id mid = margo_hg_info_get_instance(hgi);
            assert(mid != MARGO_INSTANCE_NULL);

            void* buf = (void*) chunk_locs;
            size_t buf_sz = (size_t)num_chunks * sizeof(chunk_read_req_t);
            hg_return_t hret = margo_bulk_create(mid, 1, &buf, &buf_sz,
                                                 HG_BULK_READ_ONLY,
                                                 &bulk_resp_handle);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_bulk_create() failed");
                ret = UNIFYFS_ERROR_MARGO;
            }
        }
    }

    /* send rpc response */
    find_extents_out_t out;
    out.ret           = (int32_t) ret;
    out.num_locations = (int32_t) num_chunks;
    out.locations     = bulk_resp_handle;

    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    if (HG_BULK_NULL != bulk_resp_handle) {
        margo_bulk_free(bulk_resp_handle);
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_filesize_rpc(server_rpc_req_t* req)
{
    /* get target file */
    filesize_in_t* in = req->input;
    int gfid = (int) in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    /* get size of target file */
    size_t filesize;
    int ret = unifyfs_inode_get_filesize(gfid, &filesize);

    /* send rpc response */
    filesize_out_t out;
    out.ret = (int32_t) ret;
    out.filesize = (hg_size_t) filesize;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_laminate_rpc(server_rpc_req_t* req)
{
    /* get target file */
    laminate_in_t* in = req->input;
    int gfid  = (int)in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    /* do file lamination */
    int ret = sm_laminate(gfid);

    /* send rpc response */
    laminate_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_metaget_rpc(server_rpc_req_t* req)
{
    /* get target file */
    metaget_in_t* in = req->input;
    int gfid  = (int) in->gfid;
    margo_free_input(req->handle, in);
    free(in);

    /* initialize invalid attributes */
    unifyfs_file_attr_t attrs;
    unifyfs_file_attr_set_invalid(&attrs);

    /* get metadata for target file */
    int ret = sm_get_fileattr(gfid, &attrs);

    /* send rpc response */
    metaget_out_t out;
    out.ret = (int32_t) ret;
    out.attr = attrs;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
    return UNIFYFS_ERROR_NYI;
}

static int process_metaset_rpc(server_rpc_req_t* req)
{
    /* update target file metadata */
    metaset_in_t* in = req->input;
    int gfid = (int) in->gfid;
    int attr_op = (int) in->fileop;
    unifyfs_file_attr_t* attrs = &(in->attr);
    int ret = sm_set_fileattr(gfid, attr_op, attrs);
    margo_free_input(req->handle, in);
    free(in);

    /* send rpc response */
    metaset_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_server_pid_rpc(server_rpc_req_t* req)
{
    /* get input parameters */
    server_pid_in_t* in = req->input;
    int src_rank = (int) in->rank;
    int pid = (int) in->pid;
    margo_free_input(req->handle, in);
    free(in);

    /* do pid report */
    int ret = unifyfs_report_server_pid(src_rank, pid);

    /* send rpc response */
    server_pid_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_transfer_rpc(server_rpc_req_t* req)
{
    /* get target file and requested file size */
    transfer_in_t* in = req->input;
    int src_rank      = (int) in->src_rank;
    int client_app    = (int) in->client_app;
    int client_id     = (int) in->client_id;
    int transfer_id   = (int) in->transfer_id;
    int gfid          = (int) in->gfid;
    int transfer_mode = (int) in->mode;
    char* dest_file = strdup(in->dst_file);
    margo_free_input(req->handle, in);
    free(in);

    /* do file transfer */
    int ret = sm_transfer(src_rank, client_app, client_id, transfer_id,
                          gfid, transfer_mode, dest_file, NULL);
    free(dest_file);

    /* send rpc response */
    transfer_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_truncate_rpc(server_rpc_req_t* req)
{
    /* get target file and requested file size */
    truncate_in_t* in = req->input;
    int gfid = (int) in->gfid;
    size_t fsize = (size_t) in->filesize;
    margo_free_input(req->handle, in);
    free(in);

    /* do file truncation */
    int ret = sm_truncate(gfid, fsize);

    /* send rpc response */
    truncate_out_t out;
    out.ret = (int32_t) ret;
    hg_return_t hret = margo_respond(req->handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* cleanup req */
    margo_destroy(req->handle);

    return ret;
}

static int process_extents_bcast_rpc(server_rpc_req_t* req)
{
    /* get target file and extents */
    extent_bcast_in_t* in = req->input;
    int gfid = (int) in->gfid;
    size_t num_extents = (size_t) in->num_extents;
    struct extent_tree_node* extents = req->bulk_buf;

    LOGDBG("gfid=%d num_extents=%zu", gfid, num_extents);

    /* add extents */
    int ret = sm_add_extents(gfid, num_extents, extents);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("add_extents(gfid=%d) failed - rc=%d", gfid, ret);
    }
    collective_set_local_retval(req->coll, ret);

    /* create a ULT to finish broadcast operation */
    ret = invoke_bcast_progress_rpc(req->coll);

    return ret;
}

static int process_fileattr_bcast_rpc(server_rpc_req_t* req)
{
    /* get target file and attributes */
    fileattr_bcast_in_t* in = req->input;
    int gfid = (int) in->gfid;
    int attr_op = (int) in->attrop;
    unifyfs_file_attr_t* attrs = &(in->attr);

    LOGDBG("gfid=%d", gfid);

    /* update file attributes */
    int ret = sm_set_fileattr(gfid, attr_op, attrs);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("set_fileattr(gfid=%d, op=%d) failed - rc=%d",
               gfid, attr_op, ret);
    }
    collective_set_local_retval(req->coll, ret);

    /* create a ULT to finish broadcast operation */
    ret = invoke_bcast_progress_rpc(req->coll);

    return ret;
}

static int process_laminate_bcast_rpc(server_rpc_req_t* req)
{
    /* get target file and extents */
    laminate_bcast_in_t* in = req->input;
    int gfid = (int) in->gfid;
    size_t num_extents = (size_t) in->num_extents;
    unifyfs_file_attr_t* fattr = &(in->attr);
    struct extent_tree_node* extents = req->bulk_buf;

    LOGDBG("gfid=%d num_extents=%zu", gfid, num_extents);

    /* update inode file attributes. first check to make sure
     * inode for the gfid exists. if it doesn't, create it with
     * given attrs. otherwise, just do a metadata update. */
    unifyfs_file_attr_t existing_fattr;
    int ret = unifyfs_inode_metaget(gfid, &existing_fattr);
    if (ret == ENOENT) {
        /* create with is_laminated=0 so we can add extents */
        fattr->is_laminated = 0;
        ret = unifyfs_inode_create(gfid, fattr);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("inode create during laminate(gfid=%d) failed  - rc=%d",
                   gfid, ret);
        }
        fattr->is_laminated = 1;
    }

    /* add extents */
    ret = sm_add_extents(gfid, num_extents, extents);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("extent add during laminate(gfid=%d) failed - rc=%d",
               gfid, ret);
        collective_set_local_retval(req->coll, ret);
    }

    /* mark as laminated with passed attributes */
    int attr_op = UNIFYFS_FILE_ATTR_OP_LAMINATE;
    ret = sm_set_fileattr(gfid, attr_op, fattr);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("metaset during laminate(gfid=%d) failed - rc=%d",
               gfid, ret);
        collective_set_local_retval(req->coll, ret);
    }

    /* create a ULT to finish broadcast operation */
    ret = invoke_bcast_progress_rpc(req->coll);

    return ret;
}

static int process_transfer_bcast_rpc(server_rpc_req_t* req)
{
    /* get target file and requested file size */
    transfer_bcast_in_t* in = req->input;
    int src_rank      = (int) in->root;
    int gfid          = (int) in->gfid;
    int transfer_mode = (int) in->mode;
    const char* dest_file = (const char*) in->dst_file;

    LOGDBG("gfid=%d file=%s", gfid, dest_file);

    /* do file transfer */
    int ret = sm_transfer(src_rank, -1, -1, -1, gfid, transfer_mode,
                          dest_file, req);
    if (UNIFYFS_SUCCESS != ret) {
        /* submission of transfer request failed */
        collective_set_local_retval(req->coll, ret);

         /* create a ULT to finish broadcast operation */
        ret = invoke_bcast_progress_rpc(req->coll);
    }

    return ret;
}

static int process_truncate_bcast_rpc(server_rpc_req_t* req)
{
    /* get target file and requested file size */
    truncate_bcast_in_t* in = req->input;
    int gfid = (int) in->gfid;
    size_t fsize = (size_t) in->filesize;

    LOGDBG("gfid=%d size=%zu", gfid, fsize);

    /* apply truncation to local file state */
    int ret = unifyfs_inode_truncate(gfid, (unsigned long)fsize);
    if (ret != UNIFYFS_SUCCESS) {
        /* owner is root of broadcast tree */
        int is_owner = ((int)(in->root) == glb_pmi_rank);
        if ((ret == ENOENT) && !is_owner) {
            /* it's ok if inode doesn't exist at non-owners */
            ret = UNIFYFS_SUCCESS;
        } else {
            LOGERR("truncate(gfid=%d, size=%zu) failed - rc=%d",
                   gfid, fsize, ret);
        }
    }
    collective_set_local_retval(req->coll, ret);

    /* create a ULT to finish broadcast operation */
    ret = invoke_bcast_progress_rpc(req->coll);

    return ret;
}

static int process_unlink_bcast_rpc(server_rpc_req_t* req)
{
    /* get target file and requested file size */
    unlink_bcast_in_t* in = req->input;
    int gfid = (int) in->gfid;

    LOGDBG("gfid=%d", gfid);

    /* apply truncation to local file state */
    int ret = unifyfs_inode_unlink(gfid);
    if (ret != UNIFYFS_SUCCESS) {
        /* owner is root of broadcast tree */
        int is_owner = ((int)(in->root) == glb_pmi_rank);
        if ((ret == ENOENT) && !is_owner) {
            /* it's ok if inode doesn't exist at non-owners */
            ret = UNIFYFS_SUCCESS;
        } else {
            LOGERR("unlink(gfid=%d) failed - rc=%d", gfid, ret);
        }
    }
    collective_set_local_retval(req->coll, ret);

    /* create a ULT to finish broadcast operation */
    ret = invoke_bcast_progress_rpc(req->coll);

    return ret;
}

static int process_service_requests(void)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* this will hold a list of client requests if we find any */
    arraylist_t* svc_reqs = NULL;

    /* lock to access requests */
    SM_REQ_LOCK();

    /* if we have any requests, take pointer to the list
     * of requests and replace it with a newly allocated
     * list on the request manager structure */
    int num_svc_reqs = arraylist_size(sm->svc_reqs);
    if (num_svc_reqs) {
        /* got some client requets, take the list and replace
         * it with an empty list */
        LOGDBG("processing %d service requests", num_svc_reqs);
        svc_reqs = sm->svc_reqs;
        sm->svc_reqs = arraylist_create(0);
    }

    /* release lock on sm requests */
    SM_REQ_UNLOCK();

    /* iterate over each client request */
    for (int i = 0; i < num_svc_reqs; i++) {
        /* process next request */
        int rret;
        server_rpc_req_t* req = (server_rpc_req_t*)
            arraylist_get(svc_reqs, i);
        switch (req->req_type) {
        case UNIFYFS_SERVER_RPC_CHUNK_READ:
            rret = process_chunk_read_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_EXTENTS_ADD:
            rret = process_add_extents_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_EXTENTS_FIND:
            rret = process_find_extents_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_FILESIZE:
            rret = process_filesize_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_LAMINATE:
            rret = process_laminate_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_METAGET:
            rret = process_metaget_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_METASET:
            rret = process_metaset_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_PID_REPORT:
            rret = process_server_pid_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_TRANSFER:
            rret = process_transfer_rpc(req);
            break;
        case UNIFYFS_SERVER_RPC_TRUNCATE:
            rret = process_truncate_rpc(req);
            break;
        case UNIFYFS_SERVER_BCAST_RPC_EXTENTS:
            rret = process_extents_bcast_rpc(req);
            break;
        case UNIFYFS_SERVER_BCAST_RPC_FILEATTR:
            rret = process_fileattr_bcast_rpc(req);
            break;
        case UNIFYFS_SERVER_BCAST_RPC_LAMINATE:
            rret = process_laminate_bcast_rpc(req);
            break;
        case UNIFYFS_SERVER_BCAST_RPC_TRANSFER:
            rret = process_transfer_bcast_rpc(req);
            break;
        case UNIFYFS_SERVER_BCAST_RPC_TRUNCATE:
            rret = process_truncate_bcast_rpc(req);
            break;
        case UNIFYFS_SERVER_BCAST_RPC_UNLINK:
            rret = process_unlink_bcast_rpc(req);
            break;
        default:
            LOGERR("unsupported server rpc request type %d", req->req_type);
            rret = UNIFYFS_ERROR_NYI;
            break;
        }
        if (rret != UNIFYFS_SUCCESS) {
            if ((rret != ENOENT) && (rret != EEXIST)) {
                LOGERR("server rpc request %d failed (%s)",
                       i, unifyfs_rc_enum_description(rret));
            }
            ret = rret;
        }
    }

    /* free the list if we have one */
    if (NULL != svc_reqs) {
        /* NOTE: this will call free() on each req in the arraylist */
        arraylist_free(svc_reqs);
    }

    return ret;
}

/* Entry point for service manager thread. The SM thread
 * runs in a loop processing read request replies until
 * the main server thread asks it to exit. The read requests
 * themselves are handled by Margo RPC threads.
 *
 * @param arg: pointer to SM thread control structure
 * @return NULL */
void* service_manager_thread(void* arg)
{
    int rc;

    sm->tid = unifyfs_gettid();
    LOGINFO("I am the service manager thread!");
    assert(sm == (svcmgr_state_t*)arg);

#if defined(USE_SVCMGR_PROGRESS_TIMER)
    int have_progress_timer = 0;
    timer_t progress_timer;
    struct itimerspec alarm_set = { {0}, {0} };
    struct itimerspec alarm_reset = { {0}, {0} };
    rc = timer_create(CLOCK_REALTIME, NULL, &progress_timer);
    if (rc != 0) {
        LOGERR("failed to create progress timer");
    } else {
        have_progress_timer = 1;
        alarm_set.it_value.tv_sec = 60;
    }
#endif

    /* handle requests until told to exit */
    while (1) {

#if defined(USE_SVCMGR_PROGRESS_TIMER)
        if (have_progress_timer) {
            /* set a progress alarm for one minute */
            rc = timer_settime(progress_timer, 0, &alarm_set, NULL);
        }
#endif

        rc = process_service_requests();
        if (rc != UNIFYFS_SUCCESS) {
            LOGWARN("failed to process service requests");
        }

        rc = send_chunk_read_responses();
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to send chunk read responses");
        }

        rc = spawn_local_transfers();
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to send chunk read responses");
        }

#if defined(USE_SVCMGR_PROGRESS_TIMER)
        if (have_progress_timer) {
            /* cancel progress alarm */
            rc = timer_settime(progress_timer, 0, &alarm_reset, NULL);
        }
#endif

        /* inform dispatcher that we're waiting for work
         * inside the critical section */
        SM_LOCK();
        sm->waiting_for_work = 1;

        /* release lock and wait to be signaled by dispatcher */
        //LOGDBG("SM waiting for work");
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 50000000; /* 50 ms */
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_nsec -= 1000000000;
            timeout.tv_sec++;
        }
        int wait_rc = pthread_cond_timedwait(&(sm->thrd_cond),
                                             &(sm->thrd_lock),
                                             &timeout);
        if (0 == wait_rc) {
            LOGDBG("SM got work");
        } else if (ETIMEDOUT != wait_rc) {
            LOGERR("SM work condition wait failed (rc=%d)", wait_rc);
        }

        /* set flag to indicate we're no longer waiting */
        sm->waiting_for_work = 0;
        SM_UNLOCK();

        rc = complete_local_transfers();
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to complete local transfers");
        }

        if (sm->time_to_exit) {
            break;
        }
    }

    LOGDBG("service manager thread exiting");

    sm->sm_exit_rc = UNIFYFS_SUCCESS;
    return NULL;
}
