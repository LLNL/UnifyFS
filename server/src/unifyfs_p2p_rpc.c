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

#include "unifyfs_global.h"
#include "unifyfs_p2p_rpc.h"
#include "unifyfs_group_rpc.h"
#include "unifyfs_rpc_util.h"

/*************************************************************************
 * Peer-to-peer RPC helper methods
 *************************************************************************/

/* determine server responsible for maintaining target file's metadata */
int hash_gfid_to_server(int gfid)
{
    return gfid % glb_pmi_size;
}

/* helper method to initialize peer request rpc handle */
int get_p2p_request_handle(hg_id_t request_hgid,
                           int peer_rank,
                           p2p_request* req)
{
    int rc = UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    req->peer = get_margo_server_address(peer_rank);
    if (HG_ADDR_NULL == req->peer) {
        LOGERR("missing margo address for rank=%d", peer_rank);
        return UNIFYFS_ERROR_MARGO;
    }

    /* get handle to rpc function */
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, req->peer,
                                    request_hgid, &(req->handle));
    if (hret != HG_SUCCESS) {
        LOGERR("failed to get handle for p2p request(%p) to server %d - %s",
               req, peer_rank, HG_Error_to_string(hret));
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/* helper method to forward peer rpc request */
int forward_p2p_request(void* input_ptr,
                        p2p_request* req)
{
    int rc = UNIFYFS_SUCCESS;

    /* call rpc function */
    double timeout_ms = margo_server_server_timeout_msec;
    hg_return_t hret = margo_iforward_timed(req->handle, input_ptr,
                                            timeout_ms, &(req->request));
    if (hret != HG_SUCCESS) {
        LOGERR("failed to forward p2p request(%p) - %s",
               req, HG_Error_to_string(hret));
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/* helper method to wait for peer rpc request completion */
int wait_for_p2p_request(p2p_request* req)
{
    int rc = UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_wait(req->request);
    if (hret != HG_SUCCESS) {
        LOGERR("wait on p2p request(%p) failed - %s",
               req, HG_Error_to_string(hret));
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}


/*************************************************************************
 * File chunk reads request/response
 *************************************************************************/

/* invokes the server-server chunk read request rpc */
int invoke_chunk_read_request_rpc(int dst_srvr_rank,
                                  server_read_req_t* rdreq,
                                  server_chunk_reads_t* remote_reads)
{
    int num_chunks = remote_reads->num_chunks;
    if (dst_srvr_rank == glb_pmi_rank) {
        // short-circuit for local requests
        return sm_issue_chunk_reads(glb_pmi_rank,
                                    rdreq->app_id,
                                    rdreq->client_id,
                                    rdreq->req_ndx,
                                    num_chunks,
                                    remote_reads->total_sz,
                                    (char*)(remote_reads->reqs));
    }

    int ret = UNIFYFS_SUCCESS;
    chunk_read_request_in_t in;
    chunk_read_request_out_t out;
    hg_return_t hret;
    hg_size_t bulk_sz = (hg_size_t)num_chunks * sizeof(chunk_read_req_t);

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.chunk_read_request_id;
    int rc = get_p2p_request_handle(req_hgid, dst_srvr_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill input struct */
    in.src_rank        = (int32_t) glb_pmi_rank;
    in.app_id          = (int32_t) rdreq->app_id;
    in.client_id       = (int32_t) rdreq->client_id;
    in.req_id          = (int32_t) rdreq->req_ndx;
    in.num_chks        = (int32_t) num_chunks;
    in.total_data_size = (hg_size_t) remote_reads->total_sz;
    in.bulk_size       = bulk_sz;

    /* register request buffer for bulk remote access */
    void* data_buf = remote_reads->reqs;
    hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                             &data_buf, &bulk_sz,
                             HG_BULK_READ_ONLY, &in.bulk_handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        LOGDBG("invoking the chunk-read-request rpc function");
        rc = forward_p2p_request((void*)&in, &preq);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("forward of chunk-read request rpc to server[%d] failed",
                   dst_srvr_rank);
            margo_bulk_free(in.bulk_handle);
            margo_destroy(preq.handle);
            return UNIFYFS_ERROR_MARGO;
        }

        /* wait for request completion */
        rc = wait_for_p2p_request(&preq);
        if (rc != UNIFYFS_SUCCESS) {
            ret = rc;
        } else {
            /* decode response */
            hret = margo_get_output(preq.handle, &out);
            if (hret == HG_SUCCESS) {
                ret = (int)out.ret;
                LOGDBG("Got chunk-read response from server[%d] - ret=%d",
                       dst_srvr_rank, ret);
                margo_free_output(preq.handle, &out);
            } else {
                LOGERR("margo_get_output() failed - %s",
                       HG_Error_to_string(hret));
                ret = UNIFYFS_ERROR_MARGO;
            }
        }

        margo_bulk_free(in.bulk_handle);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* handler for server-server chunk read request */
static void chunk_read_request_rpc(hg_handle_t handle)
{
    int32_t ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    chunk_read_request_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            /* extract params from input struct */
            size_t bulk_sz = (size_t)in->bulk_size;
            if (bulk_sz) {
                /* allocate and register local target buffer for bulk access */
                void* reqbuf = pull_margo_bulk_buffer(handle, in->bulk_handle,
                                                     in->bulk_size, NULL);
                if (NULL == reqbuf) {
                    LOGERR("failed to get bulk chunk reads");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    req->req_type = UNIFYFS_SERVER_RPC_CHUNK_READ;
                    req->handle = handle;
                    req->input = (void*) in;
                    req->bulk_buf = reqbuf;
                    req->bulk_sz = bulk_sz;
                    ret = sm_submit_service_request(req);
                }
                if (ret != UNIFYFS_SUCCESS) {
                    margo_free_input(handle, in);
                }
            } else {
                LOGWARN("empty chunk read request");
                ret = EINVAL;
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            if (NULL != req->bulk_buf) {
                free(req->bulk_buf);
            }
            free(req);
        }

        /* return to caller */
        chunk_read_request_out_t out;
        out.ret = ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(chunk_read_request_rpc)

/* Respond to chunk read request. Sends a set of read reply
 * headers and corresponding data back to the requesting server.
 * The headers and data are posted as a bulk transfer buffer */
int invoke_chunk_read_response_rpc(server_chunk_reads_t* scr)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* rank of destination server */
    int dst_rank = scr->rank;
    assert(dst_rank < (int)glb_num_servers);

    /* forward response to requesting server */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.chunk_read_response_id;
    int rc = get_p2p_request_handle(req_hgid, dst_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get address and size of our response buffer */
    void* data_buf = (void*) scr->resp;
    hg_size_t bulk_sz = scr->total_sz;

    /* register our response buffer for bulk remote read access */
    chunk_read_response_in_t in;
    hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid,
                                         1, &data_buf, &bulk_sz,
                                         HG_BULK_READ_ONLY, &in.bulk_handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed - %s", HG_Error_to_string(hret));
        margo_destroy(preq.handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* fill input struct */
    in.src_rank  = (int32_t) glb_pmi_rank;
    in.app_id    = (int32_t) scr->app_id;
    in.client_id = (int32_t) scr->client_id;
    in.req_id    = (int32_t) scr->rdreq_id;
    in.num_chks  = (int32_t) scr->num_chunks;
    in.bulk_size = bulk_sz;

    /* call the read response rpc */
    LOGDBG("invoking the chunk-read-response rpc function");
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        ret = rc;
    } else {
        rc = wait_for_p2p_request(&preq);
        if (rc != UNIFYFS_SUCCESS) {
            ret = rc;
        } else {
            /* rpc executed, now decode response */
            chunk_read_response_out_t out;
            hret = margo_get_output(preq.handle, &out);
            if (hret == HG_SUCCESS) {
                ret = (int)out.ret;
                LOGDBG("chunk-read-response rpc to server[%d] - ret=%d",
                       dst_rank, rc);
                margo_free_output(preq.handle, &out);
            } else {
                LOGERR("margo_get_output() failed - %s",
                       HG_Error_to_string(hret));
                ret = UNIFYFS_ERROR_MARGO;
            }
        }
    }

    /* free resources allocated for executing margo rpc */
    margo_bulk_free(in.bulk_handle);
    margo_destroy(preq.handle);

    /* free response data buffer */
    free(data_buf);
    scr->resp = NULL;

    return ret;
}

/* handler for server-server chunk read response */
static void chunk_read_response_rpc(hg_handle_t handle)
{
    int32_t ret = UNIFYFS_SUCCESS;
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

        LOGDBG("received read response from server[%d] (%d chunks)",
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
            char* resp_buf = (char*) pull_margo_bulk_buffer(handle,
                                                           in.bulk_handle,
                                                           in.bulk_size,
                                                           NULL);
            if (NULL == resp_buf) {
                /* allocation failed, that's bad */
                LOGERR("failed to get chunk read responses buffer");
                ret = (int32_t)UNIFYFS_ERROR_MARGO;
            } else {
                LOGDBG("got chunk read responses buffer (%zu bytes)", bulk_sz);

                /* process read replies we just received */
                int rc = rm_post_chunk_read_responses(app_id, client_id,
                                                      src_rank, req_id,
                                                      num_chks, bulk_sz,
                                                      resp_buf);
                if (rc != UNIFYFS_SUCCESS) {
                    LOGERR("failed to handle chunk read responses");
                    ret = rc;
                }
            }
        }
        margo_free_input(handle, &in);
    }

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


/*************************************************************************
 * File extents metadata update request
 *************************************************************************/

/* Add extents to target file */
int unifyfs_invoke_add_extents_rpc(int gfid,
                                   unsigned int num_extents,
                                   extent_metadata* extents)
{
    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        /* I'm the owner, already did local add */
        return UNIFYFS_SUCCESS;
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extent_add_id;
    int rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* create a margo bulk transfer handle for extents array */
    hg_bulk_t bulk_handle;
    void* buf = (void*) extents;
    size_t buf_sz = (size_t)num_extents * sizeof(extent_metadata);
    hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid,
                                         1, &buf, &buf_sz,
                                         HG_BULK_READ_ONLY, &bulk_handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed - %s", HG_Error_to_string(hret));
        margo_destroy(preq.handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* fill rpc input struct and forward request */
    add_extents_in_t in;
    in.src_rank    = (int32_t) glb_pmi_rank;
    in.gfid        = (int32_t) gfid;
    in.num_extents = (int32_t) num_extents;
    in.extents     = bulk_handle;
    LOGDBG("forwarding add_extents(gfid=%d) to server[%d]", gfid, owner_rank);
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_bulk_free(bulk_handle);
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    int ret = UNIFYFS_SUCCESS;
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        ret = rc;
    } else {
        /* get the output of the rpc */
        add_extents_out_t out;
        hret = margo_get_output(preq.handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            /* set return value */
            ret = out.ret;
            margo_free_output(preq.handle, &out);
        }
    }

    margo_bulk_free(bulk_handle);
    margo_destroy(preq.handle);

    return ret;
}

/* Add extents rpc handler */
static void add_extents_rpc(hg_handle_t handle)
{
    LOGDBG("add_extents rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    add_extents_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            size_t num_extents = (size_t) in->num_extents;
            size_t bulk_sz = num_extents * sizeof(extent_metadata);

            /* allocate memory for extents */
            void* extents_buf = pull_margo_bulk_buffer(handle, in->extents,
                                                      bulk_sz, NULL);
            if (NULL == extents_buf) {
                LOGERR("failed to get bulk extents");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                req->req_type = UNIFYFS_SERVER_RPC_EXTENTS_ADD;
                req->handle   = handle;
                req->input    = (void*) in;
                req->bulk_buf = extents_buf;
                req->bulk_sz  = bulk_sz;
                ret = sm_submit_service_request(req);
            }
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            if (NULL != req->bulk_buf) {
                free(req->bulk_buf);
            }
            free(req);
        }

        /* return to caller */
        add_extents_out_t out;
        out.ret = (int32_t) ret;
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(add_extents_rpc)


/*************************************************************************
 * File extents metadata lookup request
 *************************************************************************/

/* Lookup extent locations for target file */
int unifyfs_invoke_find_extents_rpc(int gfid,
                                    unsigned int num_extents,
                                    unifyfs_extent_t* extents,
                                    unsigned int* num_chunks,
                                    chunk_read_req_t** chunks)
{
    if ((NULL == num_chunks) || (NULL == chunks)) {
        return EINVAL;
    }
    *num_chunks = 0;
    *chunks = NULL;

    int owner_rank = hash_gfid_to_server(gfid);
    int is_owner = (owner_rank == glb_pmi_rank);

    /* do local inode metadata lookup */
    unifyfs_file_attr_t attrs;
    int ret = sm_get_fileattr(gfid, &attrs);
    if (ret == UNIFYFS_SUCCESS) {
        int file_laminated = (attrs.is_shared && attrs.is_laminated);
        if (is_owner || use_server_local_extents || file_laminated) {
            /* try local lookup */
            int full_coverage = 0;
            ret = sm_find_extents(gfid, (size_t)num_extents, extents,
                                  num_chunks, chunks, &full_coverage);
            if (ret) {
                LOGERR("failed to find extents for gfid=%d (ret=%d)",
                       gfid, ret);
            } else if (0 == *num_chunks) { /* found no data */
                LOGDBG("local lookup found no matching chunks");
            } else { /* found some chunks */
                if (full_coverage) {
                    LOGDBG("local lookup found chunks with full coverage");
                } else {
                    LOGDBG("local lookup found chunks with partial coverage");
                }
            }
            if (is_owner || file_laminated || full_coverage) {
                return ret;
            }
            /* else, fall through to owner lookup */
            if (*num_chunks > 0) {
                /* release local results */
                *num_chunks = 0;
                free(*chunks);
                *chunks = NULL;
            }
        }
    }

    /* forward request to file owner */
    p2p_request preq;
    margo_instance_id mid = unifyfsd_rpc_context->svr_mid;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extent_lookup_id;
    int rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* create a margo bulk transfer handle for extents array */
    hg_bulk_t bulk_req_handle;
    void* buf = (void*) extents;
    size_t buf_sz = (size_t)num_extents * sizeof(unifyfs_extent_t);
    hg_return_t hret = margo_bulk_create(mid, 1, &buf, &buf_sz,
                                         HG_BULK_READ_ONLY, &bulk_req_handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed - %s", HG_Error_to_string(hret));
        margo_destroy(preq.handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* fill rpc input struct and forward request */
    find_extents_in_t in;
    in.src_rank    = (int32_t) glb_pmi_rank;
    in.gfid        = (int32_t) gfid;
    in.num_extents = (int32_t) num_extents;
    in.extents     = bulk_req_handle;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }
    margo_bulk_free(bulk_req_handle);

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    find_extents_out_t out;
    hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        if (ret == UNIFYFS_SUCCESS) {
            /* get number of chunks */
            unsigned int n_chks = (unsigned int) out.num_locations;
            if (n_chks > 0) {
                /* get bulk buffer with chunk locations */
                buf_sz = (size_t)n_chks * sizeof(chunk_read_req_t);
                buf = pull_margo_bulk_buffer(preq.handle, out.locations,
                                             buf_sz, NULL);
                if (NULL == buf) {
                    LOGERR("failed to get bulk chunk locations");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* lookup requested extents */
                    LOGDBG("received %u chunk locations for gfid=%d",
                           n_chks, gfid);
                    *chunks = (chunk_read_req_t*) buf;
                    *num_chunks = (unsigned int) n_chks;
                }
            }
        }
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* find extents rpc handler */
static void find_extents_rpc(hg_handle_t handle)
{
    LOGDBG("find_extents rpc handler");

    int32_t ret;

    /* get input params */
    find_extents_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            size_t num_extents = (size_t) in->num_extents;
            size_t bulk_sz = num_extents * sizeof(unifyfs_extent_t);

            /* allocate memory for extents */
            void* extents_buf = pull_margo_bulk_buffer(handle, in->extents,
                                                      bulk_sz, NULL);
            if (NULL == extents_buf) {
                LOGERR("failed to get bulk extents");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                req->req_type = UNIFYFS_SERVER_RPC_EXTENTS_FIND;
                req->handle   = handle;
                req->input    = (void*) in;
                req->bulk_buf = extents_buf;
                req->bulk_sz  = bulk_sz;
                ret = sm_submit_service_request(req);
            }
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            if (NULL != req->bulk_buf) {
                free(req->bulk_buf);
            }
            free(req);
        }

        /* return to caller */
        find_extents_out_t out;
        out.ret           = (int32_t) ret;
        out.num_locations = 0;
        out.locations     = HG_BULK_NULL;

        /* send output back to caller */
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(find_extents_rpc)


/*************************************************************************
 * File attributes request
 *************************************************************************/

/* Get file attributes for target file */
int unifyfs_invoke_metaget_rpc(int gfid,
                               unifyfs_file_attr_t* attrs)
{
    if (NULL == attrs) {
        return EINVAL;
    }

    int owner_rank = hash_gfid_to_server(gfid);
    int need_local_metadata = 0;

    /* do local inode metadata lookup */
    int rc = sm_get_fileattr(gfid, attrs);
    if (owner_rank == glb_pmi_rank) {
         /* local server is the owner */
        return rc;
    } else if (rc == UNIFYFS_SUCCESS) {
        if (attrs->is_laminated) {
            /* if laminated, we already have final metadata locally */
            return UNIFYFS_SUCCESS;
        }

        /* use cached attributes if within threshold */
        struct timespec tp = {0};
        clock_gettime(CLOCK_REALTIME, &tp);
        time_t expire = attrs->ctime.tv_sec + UNIFYFS_METADATA_CACHE_SECONDS;
        if (tp.tv_sec <= expire) {
            LOGINFO("using cached attributes for gfid=%d", gfid);
            return UNIFYFS_SUCCESS;
        } else {
            LOGINFO("cached attributes for gfid=%d have expired", gfid);
        }
    } else if (rc == ENOENT) {
        /* metaget above failed with ENOENT, need to create inode */
        need_local_metadata = 1;
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.metaget_id;
    rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    metaget_in_t in;
    in.gfid = (int32_t) gfid;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    metaget_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        if (ret == UNIFYFS_SUCCESS) {
            *attrs = out.attr;
            if (out.attr.filename != NULL) {
                attrs->filename = strdup(out.attr.filename);
            }
            if (need_local_metadata) {
                sm_set_fileattr(gfid, UNIFYFS_FILE_ATTR_OP_CREATE, attrs);
            }
        }
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* Metaget rpc handler */
static void metaget_rpc(hg_handle_t handle)
{
    LOGDBG("metaget rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    metaget_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            req->req_type = UNIFYFS_SERVER_RPC_METAGET;
            req->handle   = handle;
            req->input    = (void*) in;
            req->bulk_buf = NULL;
            req->bulk_sz  = 0;
            ret = sm_submit_service_request(req);
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            free(req);
        }

        /* return to caller */
        metaget_out_t out;
        out.ret = (int32_t) ret;
        unifyfs_file_attr_set_invalid(&(out.attr));
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(metaget_rpc)


/*************************************************************************
 * File size request
 *************************************************************************/

/*  Get current global size for the target file */
int unifyfs_invoke_filesize_rpc(int gfid,
                                size_t* filesize)
{
    if (NULL == filesize) {
        return EINVAL;
    }

    int owner_rank = hash_gfid_to_server(gfid);

    /* do local inode metadata lookup to check for laminated */
    unifyfs_file_attr_t attrs;
    int rc = sm_get_fileattr(gfid, &attrs);
    if ((rc == UNIFYFS_SUCCESS) && (attrs.is_laminated)) {
        /* if laminated, we already have final metadata stored locally */
        *filesize = (size_t) attrs.size;
        return UNIFYFS_SUCCESS;
    }
    if (owner_rank == glb_pmi_rank) {
        *filesize = (size_t) attrs.size;
        return rc;
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.filesize_id;
    rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    filesize_in_t in;
    in.gfid = (int32_t)gfid;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    filesize_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        if (ret == UNIFYFS_SUCCESS) {
            *filesize = (size_t) out.filesize;
        }
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* Filesize rpc handler */
static void filesize_rpc(hg_handle_t handle)
{
    LOGDBG("filesize rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    filesize_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            req->req_type = UNIFYFS_SERVER_RPC_FILESIZE;
            req->handle   = handle;
            req->input    = (void*) in;
            req->bulk_buf = NULL;
            req->bulk_sz  = 0;
            ret = sm_submit_service_request(req);
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            free(req);
        }

        /* return to caller */
        filesize_out_t out;
        out.ret = (int32_t) ret;
        out.filesize = 0;
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(filesize_rpc)


/*************************************************************************
 * File attributes update request
 *************************************************************************/

/* Set metadata for target file */
int unifyfs_invoke_metaset_rpc(int gfid,
                               int attr_op,
                               unifyfs_file_attr_t* attrs)
{
    if (NULL == attrs) {
        return EINVAL;
    }

    int ret = sm_set_fileattr(gfid, attr_op, attrs);
    if (ret != UNIFYFS_SUCCESS) {
        return ret;
    }

    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        /* I'm the owner, return local result */
        return ret;
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.metaset_id;
    int rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    metaset_in_t in;
    in.gfid   = (int32_t) gfid;
    in.fileop = (int32_t) attr_op;
    in.attr   = *attrs;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    metaset_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* Metaset rpc handler */
static void metaset_rpc(hg_handle_t handle)
{
    LOGDBG("metaset rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    metaset_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            req->req_type = UNIFYFS_SERVER_RPC_METASET;
            req->handle   = handle;
            req->input    = (void*) in;
            req->bulk_buf = NULL;
            req->bulk_sz  = 0;
            ret = sm_submit_service_request(req);
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            free(req);
        }

        /* return to caller */
        metaset_out_t out;
        out.ret = (int32_t) ret;
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(metaset_rpc)


/*************************************************************************
 * File lamination request
 *************************************************************************/

/*  Laminate the target file */
int unifyfs_invoke_laminate_rpc(int gfid)
{
    int ret;
    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        /* I'm the owner, do local inode metadata update */
        return sm_laminate(gfid);
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.laminate_id;
    int rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    laminate_in_t in;
    in.gfid = (int32_t) gfid;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    laminate_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* Laminate rpc handler */
static void laminate_rpc(hg_handle_t handle)
{
    LOGDBG("laminate rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    laminate_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            req->req_type = UNIFYFS_SERVER_RPC_LAMINATE;
            req->handle   = handle;
            req->input    = (void*) in;
            req->bulk_buf = NULL;
            req->bulk_sz  = 0;
            ret = sm_submit_service_request(req);
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            free(req);
        }

        /* return to caller */
        laminate_out_t out;
        out.ret = (int32_t) ret;
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(laminate_rpc)


/*************************************************************************
 * File transfer request
 *************************************************************************/

/* Transfer the target file */
int unifyfs_invoke_transfer_rpc(int client_app,
                                int client_id,
                                int transfer_id,
                                int gfid,
                                int transfer_mode,
                                const char* dest_file)
{
    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        return sm_transfer(glb_pmi_rank, client_app, client_id, transfer_id,
                           gfid, transfer_mode, dest_file, NULL);
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.transfer_id;
    int rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    transfer_in_t in;
    in.src_rank    = (int32_t) glb_pmi_rank;
    in.client_app  = (int32_t) client_app;
    in.client_id   = (int32_t) client_id;
    in.transfer_id = (int32_t) transfer_id;
    in.gfid        = (int32_t) gfid;
    in.mode        = (int32_t) transfer_mode;
    in.dst_file    = (hg_const_string_t) dest_file;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    transfer_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* Transfer rpc handler */
static void transfer_rpc(hg_handle_t handle)
{
    LOGDBG("transfer rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    transfer_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            req->req_type = UNIFYFS_SERVER_RPC_TRANSFER;
            req->handle   = handle;
            req->input    = (void*) in;
            req->bulk_buf = NULL;
            req->bulk_sz  = 0;
            ret = sm_submit_service_request(req);
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            free(req);
        }

        /* return to caller */
        transfer_out_t out;
        out.ret = (int32_t) ret;
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(transfer_rpc)


/*************************************************************************
 * File truncation request
 *************************************************************************/

/* Truncate the target file */
int unifyfs_invoke_truncate_rpc(int gfid,
                                size_t filesize)
{
    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        return sm_truncate(gfid, filesize);
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.truncate_id;
    int rc = get_p2p_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    truncate_in_t in;
    in.gfid     = (int32_t) gfid;
    in.filesize = (hg_size_t) filesize;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    truncate_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/* Truncate rpc handler */
static void truncate_rpc(hg_handle_t handle)
{
    LOGDBG("truncate rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    truncate_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            req->req_type = UNIFYFS_SERVER_RPC_TRUNCATE;
            req->handle   = handle;
            req->input    = (void*) in;
            req->bulk_buf = NULL;
            req->bulk_sz  = 0;
            ret = sm_submit_service_request(req);
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            free(req);
        }

        /* return to caller */
        truncate_out_t out;
        out.ret = (int32_t) ret;
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(truncate_rpc)

/*************************************************************************
 * Server pid report
 *************************************************************************/

int unifyfs_invoke_server_pid_rpc(void)
{
    /* forward pid to server rank 0 */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.server_pid_id;
    int rc = get_p2p_request_handle(req_hgid, 0, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    server_pid_in_t in;
    in.rank = glb_pmi_rank;
    in.pid = server_pid;
    rc = forward_p2p_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_p2p_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        margo_destroy(preq.handle);
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    server_pid_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

static void server_pid_rpc(hg_handle_t handle)
{
    LOGDBG("server pid report rpc handler");

    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    server_pid_in_t* in = malloc(sizeof(*in));
    server_rpc_req_t* req = malloc(sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            req->req_type = UNIFYFS_SERVER_RPC_PID_REPORT;
            req->handle = handle;
            req->input = (void*) in;
            req->bulk_buf = NULL;
            req->bulk_sz = 0;
            ret = sm_submit_service_request(req);
            if (ret != UNIFYFS_SUCCESS) {
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        if (NULL != req) {
            free(req);
        }

        /* return to caller */
        server_pid_out_t out;
        out.ret = (int32_t) ret;
        hg_return_t hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(server_pid_rpc)
