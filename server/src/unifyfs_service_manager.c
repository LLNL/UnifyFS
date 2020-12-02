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
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"
#include "unifyfs_server_rpcs.h"
#include "margo_server.h"

/* Service Manager (SM) state */
typedef struct {
    /* the SM thread */
    pthread_t thrd;

    /* argobots mutex for synchronizing access to request state between
     * margo rpc handler ULTs and SM thread */
    ABT_mutex sync;

    /* thread status */
    int initialized;
    volatile int time_to_exit;

    /* thread return status code */
    int sm_exit_rc;

    /* list of chunk read requests from remote servers */
    arraylist_t* chunk_reads;

} svcmgr_state_t;
svcmgr_state_t* sm; // = NULL

/* lock macro for debugging SM locking */
#define SM_LOCK() \
do { \
    LOGDBG("locking service manager state"); \
    ABT_mutex_lock(sm->sync); \
} while (0)

/* unlock macro for debugging SM locking */
#define SM_UNLOCK() \
do { \
    LOGDBG("unlocking service manager state"); \
    ABT_mutex_unlock(sm->sync); \
} while (0)

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
                         char* msg_buf)
{
    /* get pointer to start of receive buffer */
    char* ptr = msg_buf;

    /* advance past command */
    ptr += sizeof(int);

    /* extract number of chunk read requests */
    assert(num_chks == *((int*)ptr));
    ptr += sizeof(int);

    /* total data size we'll be reading */
    size_t total_data_sz = *((size_t*)ptr);
    ptr += sizeof(size_t);

    /* get pointer to read request array */
    chunk_read_req_t* reqs = (chunk_read_req_t*)ptr;

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
        LOGERR("failed to allocate chunk_read_reqs");
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

    LOGDBG("issuing %d requests, total data size = %zu",
           num_chks, total_data_sz);

    /* points to offset in read reply buffer to place
     * data for next read */
    size_t buf_cursor = 0;

    int i;
    app_client* app_clnt = NULL;
    for (i = 0; i < num_chks; i++) {
        /* pointer to next read request */
        chunk_read_req_t* rreq = reqs + i;

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
        LOGDBG("reading chunk(offset=%zu, size=%zu)",
               rreq->offset, nbytes);

        /* get pointer to next position in buffer to store read data */
        char* buf_ptr = databuf + buf_cursor;

        /* read data from client log */
        int app_id = rreq->log_app_id;
        int cli_id = rreq->log_client_id;
        app_clnt = get_app_client(app_id, cli_id);
        if (NULL != app_clnt) {
            logio_context* logio_ctx = app_clnt->logio;
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
                rresp->read_rc = (ssize_t)(-EINVAL);
            }
        } else {
            rresp->read_rc = (ssize_t)(-EINVAL);
        }

        /* update to point to next slot in read reply buffer */
        buf_cursor += nbytes;
    }

    if (src_rank != glb_pmi_rank) {
        /* we need to send these read responses to another rank,
         * add chunk_reads to svcmgr response list and another
         * thread will take care of that */
        LOGDBG("adding to svcmgr chunk_reads");
        assert(NULL != sm);

        SM_LOCK();
        arraylist_add(sm->chunk_reads, scr);
        SM_UNLOCK();

        /* scr will be freed later by the sending thread */

        LOGDBG("done adding to svcmgr chunk_reads");
        return UNIFYFS_SUCCESS;
    } else {
        /* response is for myself, post it directly */
        LOGDBG("responding to myself");
        int rc = rm_post_chunk_read_responses(src_app_id, src_client_id,
                                              src_rank, src_req_id,
                                              num_chks, buf_sz, crbuf);
        if (rc != (int)UNIFYFS_SUCCESS) {
            LOGERR("failed to handle chunk read responses");
        }

        /* clean up allocated buffers */
        free(scr);

        return rc;
    }
}

/* initialize and launch service manager thread */
int svcmgr_init(void)
{
    /* allocate a service manager struct,
     * store in global variable */
    sm = (svcmgr_state_t*)calloc(1, sizeof(svcmgr_state_t));
    if (NULL == sm) {
        LOGERR("failed to allocate service manager state!");
        return ENOMEM;
    }

    /* allocate a list to track chunk read requests */
    sm->chunk_reads = arraylist_create();
    if (sm->chunk_reads == NULL) {
        LOGERR("failed to allocate service manager chunk_reads!");
        svcmgr_fini();
        return ENOMEM;
    }

    ABT_mutex_create(&(sm->sync));

    sm->initialized = 1;

    int rc = pthread_create(&(sm->thrd), NULL,
                            service_manager_thread, (void*)sm);
    if (rc != 0) {
        LOGERR("failed to create service manager thread");
        svcmgr_fini();
        return (int)UNIFYFS_ERROR_THRDINIT;
    }

    return (int)UNIFYFS_SUCCESS;
}

/* join service manager thread (if created) and clean up state */
int svcmgr_fini(void)
{
    if (NULL != sm) {
        if (sm->thrd) {
            sm->time_to_exit = 1;
            pthread_join(sm->thrd, NULL);
        }

        if (sm->initialized) {
            SM_LOCK();
        }

        arraylist_free(sm->chunk_reads);

        if (sm->initialized) {
            SM_UNLOCK();
        }

        /* free the service manager struct allocated during init */
        free(sm);
        sm = NULL;
    }
    return (int)UNIFYFS_SUCCESS;
}

/* iterate over list of chunk reads and send responses */
static int send_chunk_read_responses(void)
{
    /* assume we'll succeed */
    int rc = (int)UNIFYFS_SUCCESS;

    /* this will hold a list of chunk read requests if we find any */
    arraylist_t* chunk_reads = NULL;

    /* lock to access global service manager object */
    ABT_mutex_lock(sm->sync);

    /* if we have any chunk reads, take pointer to the list
     * of chunk read requests and replace it with a newly allocated
     * list on the service manager structure */
    int num_chunk_reads = arraylist_size(sm->chunk_reads);
    if (num_chunk_reads) {
        /* got some chunk read requets, take the list and replace
         * it with an empty list */
        LOGDBG("processing %d chunk read responses", num_chunk_reads);
        chunk_reads = sm->chunk_reads;
        sm->chunk_reads = arraylist_create();
    }

    /* release lock on service manager object */
    ABT_mutex_unlock(sm->sync);

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

    LOGDBG("I am the service manager thread!");
    assert(sm == (svcmgr_state_t*)arg);

    /* handle chunk reads until signaled to exit */
    while (1) {
        rc = send_chunk_read_responses();
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to send chunk read responses");
        }

        if (sm->time_to_exit) {
            break;
        }

        /* wait an interval */
        usleep(MIN_SLEEP_INTERVAL);
    }

    LOGDBG("service manager thread exiting");

    sm->sm_exit_rc = (int)UNIFYFS_SUCCESS;
    return NULL;
}

/* BEGIN MARGO SERVER-SERVER RPC INVOCATION FUNCTIONS */

/* invokes the chunk_read_response rpc, this sends a set of read
 * reply headers and corresponding data back to a server that
 * had requested we read data on its behalf, the headers and
 * data are posted as a bulk transfer buffer */
int invoke_chunk_read_response_rpc(server_chunk_reads_t* scr)
{
    /* assume we'll succeed */
    int rc = UNIFYFS_SUCCESS;

    /* rank of destination server */
    int dst_rank = scr->rank;
    assert(dst_rank < (int)glb_num_servers);

    /* get address of destinaton server */
    hg_addr_t dst_addr = glb_servers[dst_rank].margo_svr_addr;

    /* pointer to struct containing rpc context info,
     * shorter name for convience */
    ServerRpcContext_t* ctx = unifyfsd_rpc_context;

    /* get handle to read response rpc on destination server */
    hg_handle_t handle;
    hg_id_t resp_id = ctx->rpcs.chunk_read_response_id;
    hg_return_t hret = margo_create(ctx->svr_mid, dst_addr,
                                    resp_id, &handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_create() failed");
        return UNIFYFS_ERROR_MARGO;
    }

    /* get address and size of our response buffer */
    void* data_buf    = (void*)scr->resp;
    hg_size_t bulk_sz = scr->total_sz;

    /* register our response buffer for bulk remote read access */
    chunk_read_response_in_t in;
    hret = margo_bulk_create(ctx->svr_mid, 1, &data_buf, &bulk_sz,
                             HG_BULK_READ_ONLY, &in.bulk_handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        return UNIFYFS_ERROR_MARGO;
    }

    /* fill in input struct */
    in.src_rank  = (int32_t)glb_pmi_rank;
    in.app_id    = (int32_t)scr->app_id;
    in.client_id = (int32_t)scr->client_id;
    in.req_id    = (int32_t)scr->rdreq_id;
    in.num_chks  = (int32_t)scr->num_chunks;
    in.bulk_size = bulk_sz;

    /* call the read response rpc */
    LOGDBG("invoking the chunk-read-response rpc function");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        rc = UNIFYFS_ERROR_MARGO;
    } else {
        /* rpc executed, now decode response */
        chunk_read_response_out_t out;
        hret = margo_get_output(handle, &out);
        if (hret == HG_SUCCESS) {
            rc = (int)out.ret;
            LOGDBG("chunk-read-response rpc to %d - ret=%d",
                   dst_rank, rc);
            margo_free_output(handle, &out);
        } else {
            LOGERR("margo_get_output() failed");
            rc = UNIFYFS_ERROR_MARGO;
        }
    }

    /* free resources allocated for executing margo rpc */
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);

    /* free response data buffer */
    free(data_buf);
    scr->resp = NULL;

    return rc;
}

/* BEGIN MARGO SERVER-SERVER RPC HANDLERS */

/* handler for server-server chunk read request */
static void chunk_read_request_rpc(hg_handle_t handle)
{
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    chunk_read_request_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* extract params from input struct */
        int src_rank   = (int)in.src_rank;
        int app_id     = (int)in.app_id;
        int client_id  = (int)in.client_id;
        int req_id     = (int)in.req_id;
        int num_chks   = (int)in.num_chks;
        size_t bulk_sz = (size_t)in.bulk_size;

        LOGDBG("handling chunk read request from server %d: "
               "req=%d num_chunks=%d bulk_sz=%zu",
               src_rank, req_id, num_chks, bulk_sz);

        /* get margo info */
        const struct hg_info* hgi = margo_get_info(handle);
        assert(NULL != hgi);

        margo_instance_id mid = margo_hg_info_get_instance(hgi);
        assert(mid != MARGO_INSTANCE_NULL);

        hg_bulk_t bulk_handle;
        int reqcmd = (int)SVC_CMD_INVALID;
        void* reqbuf = NULL;
        if (bulk_sz) {
            /* allocate and register local target buffer for bulk access */
            reqbuf = malloc(bulk_sz);
            if (NULL != reqbuf) {
                hret = margo_bulk_create(mid, 1, &reqbuf, &in.bulk_size,
                                        HG_BULK_WRITE_ONLY, &bulk_handle);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_bulk_create() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* pull request data */
                    hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                                               in.bulk_handle, 0,
                                               bulk_handle, 0, in.bulk_size);
                    if (hret != HG_SUCCESS) {
                        LOGERR("margo_bulk_transfer() failed");
                        ret = UNIFYFS_ERROR_MARGO;
                    } else {
                        /* first int in request buffer is the command */
                        reqcmd = *(int*)reqbuf;

                        /* verify this is a request for data */
                        if (reqcmd == (int)SVC_CMD_RDREQ_CHK) {
                            /* chunk read request command */
                            LOGDBG("request command: SVC_CMD_RDREQ_CHK");
                            ret = sm_issue_chunk_reads(src_rank,
                                                       app_id, client_id,
                                                       req_id, num_chks,
                                                       (char*)reqbuf);
                        } else {
                            LOGERR("invalid command %d from server %d",
                                   reqcmd, src_rank);
                            ret = EINVAL;
                        }
                    }
                    margo_bulk_free(bulk_handle);
                }
                free(reqbuf);
            } else {
                ret = ENOMEM;
            }
        }
        margo_free_input(handle, &in);
    }

    /* return output to caller */
    chunk_read_request_out_t out;
    out.ret = ret;
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(chunk_read_request_rpc)
