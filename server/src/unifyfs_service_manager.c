/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
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

#include <aio.h>
#include <time.h>

#include "unifyfs_global.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"
#include "unifyfs_server_rpcs.h"
#include "margo_server.h"

/* Service Manager (SM) state */
typedef struct {
    /* the SM thread */
    pthread_t thrd;

    /* state synchronization mutex */
    pthread_mutex_t sync;

    /* thread status */
    int initialized;
    volatile int time_to_exit;

    /* thread return status code */
    int sm_exit_rc;

    /* list of chunk read requests from remote delegators */
    arraylist_t* chunk_reads;

    /* tracks running total of bytes in current read burst */
    size_t burst_data_sz;
} svcmgr_state_t;
svcmgr_state_t* sm; // = NULL

/* lock macro for debugging SM locking */
#define SM_LOCK() \
do { \
    LOGDBG("locking service manager state"); \
    pthread_mutex_lock(&(sm->sync)); \
} while (0)

/* unlock macro for debugging SM locking */
#define SM_UNLOCK() \
do { \
    LOGDBG("unlocking service manager state"); \
    pthread_mutex_unlock(&(sm->sync)); \
} while (0)

/* Decode and issue chunk-reads received from request manager.
 * We get a list of read requests for data on our node.  Read
 * data for each request and construct a set of read replies
 * that will be sent back to the request manager.
 *
 * @param src_rank      : source delegator rank
 * @param src_app_id    : app id at source delegator
 * @param src_client_id : client id at source delegator
 * @param src_req_id    : request id at source delegator
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
    int num = *((int*)ptr);
    ptr += sizeof(int);
    assert(num == num_chks);

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
    remote_chunk_reads_t* rcr = (remote_chunk_reads_t*)
        calloc(1, sizeof(remote_chunk_reads_t));
    if (NULL == rcr) {
        LOGERR("failed to allocate remote_chunk_reads");
        return ENOMEM;
    }

    /* fill in chunk read request */
    rcr->rank       = src_rank;
    rcr->app_id     = src_app_id;
    rcr->client_id  = src_client_id;
    rcr->rdreq_id   = src_req_id;
    rcr->num_chunks = num_chks;
    rcr->reqs       = NULL;
    rcr->total_sz   = buf_sz;
    rcr->resp       = resp;

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

        /* update accounting for burst size */
        sm->burst_data_sz += nbytes;
    }

    if (src_rank != glb_pmi_rank) {
        /* we need to send these read responses to another rank,
         * add chunk_reads to svcmgr response list and another
         * thread will take care of that */
        LOGDBG("adding to svcmgr chunk_reads");
        assert(NULL != sm);

        SM_LOCK();
        arraylist_add(sm->chunk_reads, rcr);
        SM_UNLOCK();

        /* rcr will be freed later by the sending thread */

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
        free(rcr);

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

    /* tracks how much data we process in each burst */
    sm->burst_data_sz = 0;

    /* allocate a list to track chunk read requests */
    sm->chunk_reads = arraylist_create();
    if (sm->chunk_reads == NULL) {
        LOGERR("failed to allocate service manager chunk_reads!");
        svcmgr_fini();
        return ENOMEM;
    }

    int rc = pthread_mutex_init(&(sm->sync), NULL);
    if (0 != rc) {
        LOGERR("failed to initialize service manager mutex!");
        svcmgr_fini();
        return (int)UNIFYFS_ERROR_THRDINIT;
    }

    sm->initialized = 1;

    rc = pthread_create(&(sm->thrd), NULL,
                        sm_service_reads, (void*)sm);
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
            pthread_mutex_destroy(&(sm->sync));
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
    pthread_mutex_lock(&(sm->sync));

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
    pthread_mutex_unlock(&(sm->sync));

    /* iterate over each chunk read request */
    for (int i = 0; i < num_chunk_reads; i++) {
        /* get next chunk read request */
        remote_chunk_reads_t* rcr = (remote_chunk_reads_t*)
            arraylist_get(chunk_reads, i);

        rc = invoke_chunk_read_response_rpc(rcr);
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
void* sm_service_reads(void* arg)
{
    int rc;

    LOGDBG("I am service manager thread!");
    assert(sm == (svcmgr_state_t*)arg);

    /* handle chunk reads until signaled to exit */
    while (1) {
        rc = send_chunk_read_responses();
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to send chunk read responses");
        }

        pthread_mutex_lock(&(sm->sync));

        if (sm->time_to_exit) {
            pthread_mutex_unlock(&(sm->sync));
            break;
        }

#ifdef BURSTY_WAIT // REVISIT WHETHER BURSTY WAIT STILL DESIRABLE
        /* determine how long to wait next time based on
         * how much data we just processed in this burst */
        size_t bursty_interval;
        if (sm->burst_data_sz >= LARGE_BURSTY_DATA) {
            /* for large bursts above a threshold,
             * wait for a fixed amount of time */
            bursty_interval = MAX_BURSTY_INTERVAL;
        } else {
            /* for smaller bursts, set delay proportionally
             * to burst size we just processed */
            bursty_interval =
                (SLEEP_SLICE_PER_UNIT * sm->burst_data_sz) / MIB;
        }
        if (bursty_interval > MIN_SLEEP_INTERVAL) {
            usleep(SLEEP_INTERVAL); /* wait an interval */
        }
#else
        /* wait an interval */
        usleep(MIN_SLEEP_INTERVAL);
#endif // REVISIT WHETHER BURSTY WAIT STILL DESIRABLE

        /* reset our burst size counter */
        sm->burst_data_sz = 0;

        pthread_mutex_unlock(&(sm->sync));
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
int invoke_chunk_read_response_rpc(remote_chunk_reads_t* rcr)
{
    /* assume we'll succeed */
    int rc = (int)UNIFYFS_SUCCESS;

    /* rank of destination server */
    int dst_rank = rcr->rank;
    assert(dst_rank < (int)glb_num_servers);

    /* get address of destinaton server */
    hg_addr_t dst_addr = glb_servers[dst_rank].margo_svr_addr;

    /* pointer to struct containing rpc context info,
     * shorter name for convience */
    ServerRpcContext_t* ctx = unifyfsd_rpc_context;

    /* get handle to read response rpc on destination server */
    hg_handle_t handle;
    hg_return_t hret = margo_create(ctx->svr_mid, dst_addr,
        ctx->rpcs.chunk_read_response_id, &handle);
    assert(hret == HG_SUCCESS);

    /* get address and size of our response buffer */
    void* data_buf    = (void*)rcr->resp;
    hg_size_t bulk_sz = rcr->total_sz;

    /* fill in input struct */
    chunk_read_response_in_t in;
    in.src_rank  = (int32_t)glb_pmi_rank;
    in.app_id    = (int32_t)rcr->app_id;
    in.client_id = (int32_t)rcr->client_id;
    in.req_id    = (int32_t)rcr->rdreq_id;
    in.num_chks  = (int32_t)rcr->num_chunks;
    in.bulk_size = bulk_sz;

    /* register our response buffer for bulk remote read access */
    hret = margo_bulk_create(ctx->svr_mid, 1,
        &data_buf, &bulk_sz, HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* call the read response rpc */
    LOGDBG("invoking the chunk-read-response rpc function");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        /* failed to invoke the rpc */
        rc = (int)UNIFYFS_FAILURE;
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
            rc = (int)UNIFYFS_FAILURE;
        }
    }

    /* free resources allocated for executing margo rpc */
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);

    /* free response data buffer */
    free(data_buf);
    rcr->resp = NULL;

    return rc;
}

/* BEGIN MARGO SERVER-SERVER RPC HANDLERS */

/* handler for server-server hello
 *
 * print the message, and return my rank */
static void server_hello_rpc(hg_handle_t handle)
{
    /* get input params */
    server_hello_in_t in;
    int rc = margo_get_input(handle, &in);
    assert(rc == HG_SUCCESS);

    /* extract params from input struct */
    int src_rank = (int)in.src_rank;
    char* msg = strdup(in.message_str);
    if (NULL != msg) {
        LOGDBG("got message '%s' from server %d", msg, src_rank);
        free(msg);
    }

    /* fill output structure to return to caller */
    server_hello_out_t out;
    out.ret = (int32_t)glb_pmi_rank;

    /* send output back to caller */
    hg_return_t hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(server_hello_rpc)

/* handler for server-server request
 *
 * decode payload based on tag, and call appropriate svcmgr routine */
static void server_request_rpc(hg_handle_t handle)
{
    int32_t ret;
    hg_return_t hret;

    /* get input params */
    server_request_in_t in;
    int rc = margo_get_input(handle, &in);
    assert(rc == HG_SUCCESS);

    /* extract params from input struct */
    int src_rank   = (int)in.src_rank;
    int req_id     = (int)in.req_id;
    int tag        = (int)in.req_tag;
    size_t bulk_sz = (size_t)in.bulk_size;

    LOGDBG("handling request from server %d: tag=%d req=%d sz=%zu",
           src_rank, tag, req_id, bulk_sz);

    /* get margo info */
    const struct hg_info* hgi = margo_get_info(handle);
    assert(NULL != hgi);

    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    hg_bulk_t bulk_handle;
    void* reqbuf = NULL;
    if (bulk_sz) {
        /* allocate and register local target buffer for bulk access */
        reqbuf = malloc(bulk_sz);
        if (NULL == reqbuf) {
            ret = (int32_t)ENOMEM;
            goto request_out;
        }
        hret = margo_bulk_create(mid, 1, &reqbuf, &in.bulk_size,
                                 HG_BULK_WRITE_ONLY, &bulk_handle);
        assert(hret == HG_SUCCESS);

        /* pull request data */
        hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                                   in.bulk_handle, 0,
                                   bulk_handle, 0, in.bulk_size);
        assert(hret == HG_SUCCESS);
    }

    switch (tag) {
      default: {
        LOGERR("invalid request tag %d", tag);
        ret = (int32_t)EINVAL;
        break;
      }
    }

    server_request_out_t out;
request_out:

    /* fill output structure */
    out.ret = ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    if (NULL != reqbuf) {
        margo_bulk_free(bulk_handle);
        free(reqbuf);
    }
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(server_request_rpc)

/* handler for server-server request
 *
 * decode payload based on tag, and call appropriate svcmgr routine */
static void chunk_read_request_rpc(hg_handle_t handle)
{
    hg_return_t hret;

    /* get input params */
    chunk_read_request_in_t in;
    int rc = margo_get_input(handle, &in);
    assert(rc == HG_SUCCESS);

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
            assert(hret == HG_SUCCESS);

            /* pull request data */
            hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                                       in.bulk_handle, 0,
                                       bulk_handle, 0, in.bulk_size);
            assert(hret == HG_SUCCESS);

            /* first int in request buffer is the command */
            reqcmd = *(int*)reqbuf;
        }
    }

    /* verify this is a request for data */
    int32_t ret;
    if (reqcmd == (int)SVC_CMD_RDREQ_CHK) {
        /* chunk read request command */
        LOGDBG("request command: SVC_CMD_RDREQ_CHK");
        sm_issue_chunk_reads(src_rank, app_id, client_id, req_id,
                             num_chks, (char*)reqbuf);
        ret = (int32_t)UNIFYFS_SUCCESS;
    } else {
        LOGERR("invalid chunk read request command %d from server %d",
               reqcmd, src_rank);
        ret = (int32_t)EINVAL;
    }

    /* fill output structure */
    chunk_read_request_out_t out;
    out.ret = ret;

    /* return output to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    if (NULL != reqbuf) {
        margo_bulk_free(bulk_handle);
        free(reqbuf);
    }
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(chunk_read_request_rpc)
