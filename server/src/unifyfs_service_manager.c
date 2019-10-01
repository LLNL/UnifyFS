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
#include <mpi.h>

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

/* Decode and issue chunk-reads received from request manager
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

    size_t total_data_sz = *((size_t*)ptr);
    ptr += sizeof(size_t);

    /* get pointer to read request */
    chunk_read_req_t* reqs = (chunk_read_req_t*)ptr;

    remote_chunk_reads_t* rcr = (remote_chunk_reads_t*)
        calloc(1, sizeof(remote_chunk_reads_t));
    if (NULL == rcr) {
        LOGERR("failed to allocate remote_chunk_reads");
        return UNIFYFS_ERROR_NOMEM;
    }
    rcr->rank = src_rank;
    rcr->app_id = src_app_id;
    rcr->client_id = src_client_id;
    rcr->rdreq_id = src_req_id;
    rcr->num_chunks = num_chks;
    rcr->reqs = NULL;

    size_t resp_sz = sizeof(chunk_read_resp_t) * num_chks;
    size_t buf_sz = resp_sz + total_data_sz;
    rcr->total_sz = buf_sz;

    // NOTE: calloc() is required here, don't use malloc
    char* crbuf = (char*) calloc(1, buf_sz);
    if (NULL == crbuf) {
        LOGERR("failed to allocate chunk_read_reqs");
        free(rcr);
        return UNIFYFS_ERROR_NOMEM;
    }
    chunk_read_resp_t* resp = (chunk_read_resp_t*)crbuf;
    rcr->resp = resp;

    char* databuf = crbuf + resp_sz;

    LOGDBG("issuing %d requests, total data size = %zu",
           num_chks, total_data_sz);

    /* points to offset in read reply buffer */
    size_t buf_cursor = 0;

    int i;
    int last_app = -1;
    app_config_t* app_config = NULL;
    for (i = 0; i < num_chks; i++) {
        chunk_read_req_t* rreq = reqs + i;
        chunk_read_resp_t* rresp = resp + i;

        /* get size of data we are to read */
        size_t size = rreq->nbytes;
        size_t offset = rreq->log_offset;

        /* record request metadata in response */
        rresp->nbytes = size;
        rresp->offset = rreq->offset;
        LOGDBG("reading chunk(offset=%zu, size=%zu)", rreq->offset, size);

        /* get app id and client id for this read task,
         * defines log files holding data */
        int app_id = rreq->log_app_id;
        int cli_id = rreq->log_client_id;
        if (app_id != last_app) {
            /* look up app config for given app id */
            app_config = (app_config_t*)
                arraylist_get(app_config_list, app_id);
            assert(app_config);
            last_app = app_id;
        }
        int spillfd = app_config->spill_log_fds[cli_id];
        char* log_ptr = app_config->shm_superblocks[cli_id] +
                        app_config->data_offset + offset;

        char* buf_ptr = databuf + buf_cursor;

        /* prepare read opertions based on data location */
        size_t sz_from_mem = 0;
        size_t sz_from_spill = 0;
        if ((offset + size) <= app_config->data_size) {
            /* requested data is totally in shared memory */
            sz_from_mem = size;
        } else if (offset < app_config->data_size) {
            /* part of the requested data is in shared memory */
            sz_from_mem = app_config->data_size - offset;
            sz_from_spill = size - sz_from_mem;
        } else {
            /* all requested data is in spillover file */
            sz_from_spill = size;
        }
        if (sz_from_mem > 0) {
            /* read data from shared memory */
            memcpy(buf_ptr, log_ptr, sz_from_mem);
            rresp->read_rc = sz_from_mem;
        }
        if (sz_from_spill > 0) {
            /* read data from spillover file */
            ssize_t nread = pread(spillfd, (buf_ptr + sz_from_mem),
                                  sz_from_spill, 0);
            if (-1 == nread) {
                rresp->read_rc = (ssize_t)(-errno);
            } else {
                rresp->read_rc += nread;
            }
        }
        buf_cursor += size;

        /* update accounting for burst size */
        sm->burst_data_sz += size;
    }

    if (src_rank != glb_mpi_rank) {
        /* add chunk_reads to svcmgr response list */
        LOGDBG("adding to svcmgr chunk_reads");
        assert(NULL != sm);
        SM_LOCK();
        arraylist_add(sm->chunk_reads, rcr);
        SM_UNLOCK();
        LOGDBG("done adding to svcmgr chunk_reads");
        return UNIFYFS_SUCCESS;
    } else { /* response is for myself */
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
    sm = (svcmgr_state_t*)calloc(1, sizeof(svcmgr_state_t));
    if (NULL == sm) {
        LOGERR("failed to allocate service manager state!");
        return (int)UNIFYFS_ERROR_NOMEM;
    }

    /* tracks how much data we process in each burst */
    sm->burst_data_sz = 0;

    /* allocate a list to track chunk read requests */
    sm->chunk_reads = arraylist_create();
    if (sm->chunk_reads == NULL) {
        LOGERR("failed to allocate service manager chunk_reads!");
        svcmgr_fini();
        return (int)UNIFYFS_ERROR_NOMEM;
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

        free(sm);
        sm = NULL;
    }
    return (int)UNIFYFS_SUCCESS;
}

/* iterate over list of chunk reads and send responses */
static int send_chunk_read_responses(void)
{
    int rc = (int)UNIFYFS_SUCCESS;
    pthread_mutex_lock(&(sm->sync));
    int num_chunk_reads = arraylist_size(sm->chunk_reads);
    if (num_chunk_reads) {
        LOGDBG("processing %d chunk read responses", num_chunk_reads);
        for (int i = 0; i < num_chunk_reads; i++) {
            /* get data structure */
            remote_chunk_reads_t* rcr = (remote_chunk_reads_t*)
                arraylist_get(sm->chunk_reads, i);
            rc = invoke_chunk_read_response_rpc(rcr);
        }
        arraylist_reset(sm->chunk_reads);
    }
    pthread_mutex_unlock(&(sm->sync));
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

/* invokes the chunk_read_response rpc */
int invoke_chunk_read_response_rpc(remote_chunk_reads_t* rcr)
{
    int rc = (int)UNIFYFS_SUCCESS;
    int dst_srvr_rank = rcr->rank;
    hg_handle_t handle;
    chunk_read_response_in_t in;
    chunk_read_response_out_t out;
    hg_return_t hret;
    hg_addr_t dst_srvr_addr;
    hg_size_t bulk_sz = rcr->total_sz;
    void* data_buf = (void*)rcr->resp;

    assert(dst_srvr_rank < (int)glb_num_servers);
    dst_srvr_addr = glb_servers[dst_srvr_rank].margo_svr_addr;

    hret = margo_create(unifyfsd_rpc_context->svr_mid, dst_srvr_addr,
                        unifyfsd_rpc_context->rpcs.chunk_read_response_id,
                        &handle);
    assert(hret == HG_SUCCESS);

    /* fill in input struct */
    in.src_rank = (int32_t)glb_mpi_rank;
    in.app_id = (int32_t)rcr->app_id;
    in.client_id = (int32_t)rcr->client_id;
    in.req_id = (int32_t)rcr->rdreq_id;
    in.num_chks = (int32_t)rcr->num_chunks;
    in.bulk_size = bulk_sz;

    /* register request buffer for bulk remote access */
    hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                             &data_buf, &bulk_sz,
                             HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    LOGDBG("invoking the chunk-read-response rpc function");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        rc = (int)UNIFYFS_FAILURE;
    } else {
        /* decode response */
        hret = margo_get_output(handle, &out);
        if (hret == HG_SUCCESS) {
            rc = (int)out.ret;
            LOGDBG("chunk-read-response rpc to %d - ret=%d",
                   dst_srvr_rank, rc);
            margo_free_output(handle, &out);
        } else {
            rc = (int)UNIFYFS_FAILURE;
        }
    }

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
    int rc, src_rank;
    hg_return_t hret;
    char* msg;
    server_hello_in_t in;
    server_hello_out_t out;

    /* get input params */
    rc = margo_get_input(handle, &in);
    assert(rc == HG_SUCCESS);
    src_rank = (int)in.src_rank;
    msg = strdup(in.message_str);
    if (NULL != msg) {
        LOGDBG("got message '%s' from server %d", msg, src_rank);
        free(msg);
    }

    /* fill output structure to return to caller */
    out.ret = (int32_t)glb_mpi_rank;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
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
    int rc, req_id, src_rank, tag;
    int32_t ret;
    hg_return_t hret;
    hg_bulk_t bulk_handle;
    size_t bulk_sz;
    server_request_in_t in;
    server_request_out_t out;

    /* get input params */
    rc = margo_get_input(handle, &in);
    assert(rc == HG_SUCCESS);
    src_rank = (int)in.src_rank;
    req_id = (int)in.req_id;
    tag = (int)in.req_tag;
    bulk_sz = (size_t)in.bulk_size;

    LOGDBG("handling request from server %d: tag=%d req=%d sz=%zu",
           src_rank, tag, req_id, bulk_sz);

    /* get margo info */
    const struct hg_info* hgi = margo_get_info(handle);
    assert(NULL != hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    int reqcmd = 0;
    void* reqbuf = NULL;
    if (bulk_sz) {
        /* allocate and register local target buffer for bulk access */
        reqbuf = malloc(bulk_sz);
        if (NULL == reqbuf) {
            ret = (int32_t)UNIFYFS_ERROR_NOMEM;
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
        reqcmd = *(int*)reqbuf;
    }

    switch (tag) {
      default: {
        LOGERR("invalid request tag %d", tag);
        ret = (int32_t)UNIFYFS_ERROR_INVAL;
        break;
      }
    }

request_out:

    /* fill output structure and return to caller */
    out.ret = ret;
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
    int rc, req_id, num_chks;
    int src_rank, app_id, client_id;
    int32_t ret;
    hg_return_t hret;
    hg_bulk_t bulk_handle;
    size_t bulk_sz;
    chunk_read_request_in_t in;
    chunk_read_request_out_t out;

    /* get input params */
    rc = margo_get_input(handle, &in);
    assert(rc == HG_SUCCESS);
    src_rank = (int)in.src_rank;
    app_id = (int)in.app_id;
    client_id = (int)in.client_id;
    req_id = (int)in.req_id;
    num_chks = (int)in.num_chks;
    bulk_sz = (size_t)in.bulk_size;

    LOGDBG("handling chunk read request from server %d: "
           "req=%d num_chunks=%d bulk_sz=%zu",
           src_rank, req_id, num_chks, bulk_sz);

    /* get margo info */
    const struct hg_info* hgi = margo_get_info(handle);
    assert(NULL != hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

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
            reqcmd = *(int*)reqbuf;
        }
    }
    /* verify this is a request for data */
    if (reqcmd == (int)SVC_CMD_RDREQ_CHK) {
        LOGDBG("request command: SVC_CMD_RDREQ_CHK");
        /* chunk read request */
        sm_issue_chunk_reads(src_rank, app_id, client_id, req_id,
                             num_chks, (char*)reqbuf);
        ret = (int32_t)UNIFYFS_SUCCESS;
    } else {
        LOGERR("invalid chunk read request command %d from server %d",
               reqcmd, src_rank);
        ret = (int32_t)UNIFYFS_ERROR_INVAL;
    }

    /* fill output structure and return to caller */
    out.ret = ret;
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
