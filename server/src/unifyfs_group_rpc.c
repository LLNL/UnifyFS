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

#include "unifyfs_group_rpc.h"


#ifndef UNIFYFS_BCAST_K_ARY
# define UNIFYFS_BCAST_K_ARY 2
#endif


/* helper method to initialize collective request rpc handle for child peer */
static int get_child_request_handle(hg_id_t request_hgid,
                                    int peer_rank,
                                    hg_handle_t* chdl)
{
    int ret = UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = get_margo_server_address(peer_rank);
    if (HG_ADDR_NULL == addr) {
        LOGERR("missing margo address for rank=%d", peer_rank);
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* get handle to rpc function */
        hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
                                        request_hgid, chdl);
        if (hret != HG_SUCCESS) {
            LOGERR("failed to get handle for child request to server %d",
                peer_rank);
            ret = UNIFYFS_ERROR_MARGO;
        }
    }

    return ret;
}

/* helper method to forward collective rpc request to one child */
static int forward_child_request(void* input_ptr,
                                 hg_handle_t chdl,
                                 margo_request* creq)
{
    int ret = UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_iforward(chdl, input_ptr, creq);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to forward request(%p)", creq);
        ret = UNIFYFS_ERROR_MARGO;
    }

    return ret;
}

/* helper method to wait for collective rpc child request completion */
static int wait_for_child_request(margo_request* creq)
{
    int ret = UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_wait(*creq);
    if (hret != HG_SUCCESS) {
        LOGERR("wait on request(%p) failed", creq);
        ret = UNIFYFS_ERROR_MARGO;
    }

    return ret;
}

static coll_request* collective_create(server_rpc_e req_type,
                                       hg_handle_t handle,
                                       hg_id_t op_hgid,
                                       int tree_root_rank,
                                       void* input_struct,
                                       void* output_struct,
                                       size_t output_size,
                                       hg_bulk_t bulk_in,
                                       hg_bulk_t bulk_forward,
                                       void* bulk_buf)
{
    coll_request* coll_req = calloc(1, sizeof(*coll_req));
    if (NULL != coll_req) {
        LOGDBG("BCAST_RPC: collective(%p) create (type=%d)",
               coll_req, req_type);
        coll_req->req_type     = req_type;
        coll_req->resp_hdl     = handle;
        coll_req->input        = input_struct;
        coll_req->output       = output_struct;
        coll_req->output_sz    = output_size;
        coll_req->bulk_in      = bulk_in;
        coll_req->bulk_forward = bulk_forward;
        coll_req->bulk_buf     = bulk_buf;

        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, tree_root_rank,
                          UNIFYFS_BCAST_K_ARY, &(coll_req->tree));

        size_t n_children = (size_t) coll_req->tree.child_count;
        if (n_children) {
            coll_req->child_hdls = calloc(n_children, sizeof(hg_handle_t));
            coll_req->child_reqs = calloc(n_children, sizeof(margo_request));
            if ((NULL == coll_req->child_hdls) ||
                (NULL == coll_req->child_reqs)) {
                LOGERR("allocation of children state failed");
                free(coll_req);
                return NULL;
            }

            int* ranks = coll_req->tree.child_ranks;
            for (int i = 0; i < coll_req->tree.child_count; i++) {
                /* allocate child request handle */
                hg_handle_t* chdl = coll_req->child_hdls + i;
                int rc = get_child_request_handle(op_hgid, ranks[i], chdl);
                if (rc != UNIFYFS_SUCCESS) {
                    LOGERR("failed to get child request handle");
                    *chdl = HG_HANDLE_NULL;
                }
            }
        }
    }
    return coll_req;
}

/* reset collective input bulk handle to original value */
static void coll_restore_input_bulk(coll_request* coll_req)
{
    void* input = coll_req->input;
    if ((NULL == input) || (HG_BULK_NULL == coll_req->bulk_in)
        || (HG_BULK_NULL == coll_req->bulk_forward)) {
        return;
    }

    /* update input structure bulk handle using stored value */
    switch (coll_req->req_type) {
    case UNIFYFS_SERVER_BCAST_RPC_EXTENTS: {
        extent_bcast_in_t* ebi = (extent_bcast_in_t*) input;
        ebi->extents = coll_req->bulk_in;
        break;
    }
    case UNIFYFS_SERVER_BCAST_RPC_LAMINATE: {
        laminate_bcast_in_t* lbi = (laminate_bcast_in_t*) input;
        lbi->extents = coll_req->bulk_in;
        break;
    }
    default:
        LOGERR("invalid collective request type %d", coll_req->req_type);
        break;
    }
}

static void collective_cleanup(coll_request* coll_req)
{
    if (NULL == coll_req) {
        return;
    }

    LOGDBG("BCAST_RPC: collective(%p) cleanup", coll_req);

    /* release communication tree resources */
    unifyfs_tree_free(&(coll_req->tree));

    /* release margo resources */
    if (HG_HANDLE_NULL != coll_req->resp_hdl) {
        if (NULL != coll_req->input) {
            coll_restore_input_bulk(coll_req);
            margo_free_input(coll_req->resp_hdl, coll_req->input);
        }
        margo_destroy(coll_req->resp_hdl);
    }
    if (HG_BULK_NULL != coll_req->bulk_forward) {
        margo_bulk_free(coll_req->bulk_forward);
    }

    /* free allocated memory */
    if (NULL != coll_req->input) {
        free(coll_req->input);
    }
    if (NULL != coll_req->output) {
        free(coll_req->output);
    }
    if (NULL != coll_req->child_hdls) {
        free(coll_req->child_hdls);
    }
    if (NULL != coll_req->child_reqs) {
        free(coll_req->child_reqs);
    }
    if (NULL != coll_req->bulk_buf) {
        free(coll_req->bulk_buf);
    }
    free(coll_req);
}

/* Forward the collective request to any children */
static int collective_forward(coll_request* coll_req)
{
    /* get info for tree */
    int child_count = coll_req->tree.child_count;
    if (0 == child_count) {
        return UNIFYFS_SUCCESS;
    }

    LOGDBG("BCAST_RPC: collective(%p) forward", coll_req);

    /* forward request down the tree */
    int ret = UNIFYFS_SUCCESS;
    for (int i = 0; i < child_count; i++) {
        /* invoke bcast request rpc on child */
        margo_request* creq = coll_req->child_reqs + i;
        hg_handle_t* chdl = coll_req->child_hdls + i;
        int rc = forward_child_request(coll_req->input, *chdl, creq);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("forward to child[%d] failed", i);
            ret = rc;
        }
    }

    return ret;
}



/* set collective output return value to local result value */
void collective_set_local_retval(coll_request* coll_req, int val)
{
    /* update collective return value using local op return value */
    void* output = coll_req->output;
    if (NULL == output) {
        return;
    }

    switch (coll_req->req_type) {
    case UNIFYFS_SERVER_BCAST_RPC_EXTENTS: {
        extent_bcast_out_t* ebo = (extent_bcast_out_t*) output;
        ebo->ret = val;
        break;
    }
    case UNIFYFS_SERVER_BCAST_RPC_FILEATTR: {
        fileattr_bcast_out_t* fbo = (fileattr_bcast_out_t*) output;
        fbo->ret = val;
        break;
    }
    case UNIFYFS_SERVER_BCAST_RPC_LAMINATE: {
        laminate_bcast_out_t* lbo = (laminate_bcast_out_t*) output;
        lbo->ret = val;
        break;
    }
    case UNIFYFS_SERVER_BCAST_RPC_TRUNCATE: {
        truncate_bcast_out_t* tbo = (truncate_bcast_out_t*) output;
        tbo->ret = val;
        break;
    }
    case UNIFYFS_SERVER_BCAST_RPC_UNLINK: {
        unlink_bcast_out_t* ubo = (unlink_bcast_out_t*) output;
        ubo->ret = val;
        break;
    }
    default:
        LOGERR("invalid collective request type %d", coll_req->req_type);
        break;
    }
}

static int coll_get_child_response(coll_request* coll_req,
                                   hg_handle_t chdl)
{
    int ret = UNIFYFS_SUCCESS;
    void* out = calloc(1, coll_req->output_sz);
    if (NULL == out) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_output(chdl, out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_output() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            /* update collective return value using child response */
            int child_ret = UNIFYFS_SUCCESS;
            void* output = coll_req->output;

            switch (coll_req->req_type) {
            case UNIFYFS_SERVER_BCAST_RPC_EXTENTS: {
                extent_bcast_out_t* cebo = (extent_bcast_out_t*) out;
                extent_bcast_out_t* ebo  = (extent_bcast_out_t*) output;
                child_ret = cebo->ret;
                if (child_ret != UNIFYFS_SUCCESS) {
                    ebo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_FILEATTR: {
                fileattr_bcast_out_t* cfbo = (fileattr_bcast_out_t*) out;
                fileattr_bcast_out_t* fbo  = (fileattr_bcast_out_t*) output;
                child_ret = cfbo->ret;
                if (child_ret != UNIFYFS_SUCCESS) {
                    fbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_LAMINATE: {
                laminate_bcast_out_t* clbo = (laminate_bcast_out_t*) out;
                laminate_bcast_out_t* lbo  = (laminate_bcast_out_t*) output;
                child_ret = clbo->ret;
                if (child_ret != UNIFYFS_SUCCESS) {
                    lbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_TRUNCATE: {
                truncate_bcast_out_t* ctbo = (truncate_bcast_out_t*) out;
                truncate_bcast_out_t* tbo  = (truncate_bcast_out_t*) output;
                child_ret = ctbo->ret;
                if (child_ret != UNIFYFS_SUCCESS) {
                    tbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_UNLINK: {
                unlink_bcast_out_t* cubo = (unlink_bcast_out_t*) out;
                unlink_bcast_out_t* ubo  = (unlink_bcast_out_t*) output;
                child_ret = cubo->ret;
                if (child_ret != UNIFYFS_SUCCESS) {
                    ubo->ret = child_ret;
                }
                break;
            }
            default:
                child_ret = UNIFYFS_FAILURE;
                LOGERR("invalid collective request type %d",
                       coll_req->req_type);
                break;
            }

            ret = child_ret;

            margo_free_output(chdl, out);
        }
    }

    return ret;
}

/* Forward the collective request to any children */
static int collective_finish(coll_request* coll_req)
{
    int ret = UNIFYFS_SUCCESS;

    /* get info for tree */
    int child_count = coll_req->tree.child_count;

    LOGDBG("BCAST_RPC: collective(%p) finish", coll_req);

    if (child_count) {
        /* wait for child requests to finish */
        int i, rc;
        if (NULL != coll_req->child_reqs) {
            margo_request* creq;
            hg_handle_t* chdl;
            /* MJB TODO - use margo_wait_any() instead of our own loop */
            for (i = 0; i < child_count; i++) {
                chdl = coll_req->child_hdls + i;
                creq = coll_req->child_reqs + i;
                rc = wait_for_child_request(creq);
                if (rc == UNIFYFS_SUCCESS) {
                    /* get the output of the rpc */
                    int child_ret = coll_get_child_response(coll_req, *chdl);
                    LOGDBG("BCAST_RPC: collective(%p) child[%d] resp=%d",
                        coll_req, i, child_ret);
                    if (child_ret != UNIFYFS_SUCCESS) {
                        ret = child_ret;
                    }
                } else {
                    ret = rc;
                }
                margo_destroy(*chdl);
            }
        } else {
            LOGERR("child count is %d, but NULL child reqs array",
                   child_count);
            ret = UNIFYFS_FAILURE;
        }
    }

    if (NULL != coll_req->output) {
        /* send output back to caller */
        hg_return_t hret = margo_respond(coll_req->resp_hdl, coll_req->output);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        LOGDBG("BCAST_RPC: collective(%p, op=%d) responded",
               coll_req, (int)(coll_req->req_type));
    }

    collective_cleanup(coll_req);

    return ret;
}


/*************************************************************************
 * Broadcast progress via ULT
 *************************************************************************/

int invoke_bcast_progress_rpc(coll_request* coll_req)
{
    int ret = UNIFYFS_SUCCESS;

    /* get address for local server rank */
    hg_addr_t addr = get_margo_server_address(glb_pmi_rank);
    if (HG_ADDR_NULL == addr) {
        LOGERR("missing local margo address");
        return UNIFYFS_ERROR_MARGO;
    }

    /* get handle to local rpc function */
    hg_handle_t handle;
    hg_id_t hgid = unifyfsd_rpc_context->rpcs.bcast_progress_id;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
                                    hgid, &handle);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to get handle for bcast progress");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* call rpc function */
        bcast_progress_in_t in;
        in.coll_req = (hg_ptr_t) coll_req;
        hret = margo_forward(handle, &in);
        if (hret != HG_SUCCESS) {
            LOGERR("failed to forward bcast progress for coll(%p)", coll_req);
            ret = UNIFYFS_ERROR_MARGO;
        }
    }

    return ret;
}

/* generic broadcast rpc progression handler */
static void bcast_progress_rpc(hg_handle_t handle)
{
    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    bcast_progress_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* call collective_finish() to progress bcast operation */
        coll_request* coll = (coll_request*) in.coll_req;
        LOGDBG("BCAST_RPC: bcast progress collective(%p)", coll);
        ret = collective_finish(coll);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("collective_finish() failed for coll_req(%p) (rc=%d)",
                   coll, ret);
        }
    }

    /* finish rpc */
    bcast_progress_out_t out;
    out.ret = ret;
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bcast_progress_rpc)


/*************************************************************************
 * Broadcast file extents metadata
 *************************************************************************/

/* file extents metadata broadcast rpc handler */
static void extent_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: extents handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    extent_bcast_in_t* in = calloc(1, sizeof(*in));
    extent_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            size_t num_extents = (size_t) in->num_extents;
            size_t bulk_sz = num_extents * sizeof(struct extent_tree_node);
            hg_bulk_t local_bulk = HG_BULK_NULL;
            void* extents_buf = pull_margo_bulk_buffer(handle, in->extents,
                                                       bulk_sz, &local_bulk);
            if (NULL == extents_buf) {
                LOGERR("failed to get bulk extents");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.extent_bcast_id;
                server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_EXTENTS;
                coll = collective_create(rpc, handle, op_hgid, (int)(in->root),
                                        (void*)in, (void*)out, sizeof(*out),
                                        in->extents, local_bulk, extents_buf);
                if (NULL == coll) {
                    ret = ENOMEM;
                } else {
                    /* update input structure that we are forwarding to point
                     * to our local bulk buffer. will be restore on cleanup. */
                    in->extents = local_bulk;
                    ret = collective_forward(coll);
                    if (ret == UNIFYFS_SUCCESS) {
                        req->req_type = rpc;
                        req->coll = coll;
                        req->handle = handle;
                        req->input = (void*) in;
                        req->bulk_buf = extents_buf;
                        req->bulk_sz = bulk_sz;
                        ret = sm_submit_service_request(req);
                        if (ret != UNIFYFS_SUCCESS) {
                            LOGERR("failed to submit coll request to svcmgr");
                        }
                    }
                }
            }
        }
    }

    if (ret != UNIFYFS_SUCCESS) {
        /* report failure back to caller */
        extent_bcast_out_t ebo;
        ebo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &ebo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }
}
DEFINE_MARGO_RPC_HANDLER(extent_bcast_rpc)

/* Execute broadcast tree for extent metadata */
int unifyfs_invoke_broadcast_extents_rpc(int gfid)
{
    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    LOGDBG("BCAST_RPC: starting extents for gfid=%d", gfid);

    size_t n_extents;
    struct extent_tree_node* extents;
    ret = unifyfs_inode_get_extents(gfid, &n_extents, &extents);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to get extents for gfid=%d", gfid);
        return ret;
    }

    if (0 == n_extents) {
        /* nothing to broadcast */
        return UNIFYFS_SUCCESS;
    }

    /* create bulk data structure containing the extents
     * NOTE: bulk data is always read only at the root of the broadcast tree */
    hg_size_t buf_size = n_extents * sizeof(*extents);
    hg_bulk_t extents_bulk;
    void* buf = (void*) extents;
    hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                                         &buf, &buf_size,
                                         HG_BULK_READ_ONLY, &extents_bulk);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        coll_request* coll = NULL;
        extent_bcast_in_t* in = calloc(1, sizeof(*in));
        if (NULL == in) {
            ret = ENOMEM;
        } else {
            /* set input params */
            in->root        = (int32_t) glb_pmi_rank;
            in->gfid        = (int32_t) gfid;
            in->extents     = extents_bulk;
            in->num_extents = (int32_t) n_extents;

            hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.extent_bcast_id;
            server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_EXTENTS;
            coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                     glb_pmi_rank, (void*)in,
                                     NULL, sizeof(extent_bcast_out_t),
                                     HG_BULK_NULL, extents_bulk, buf);
            if (NULL == coll) {
                ret = ENOMEM;
            } else {
                ret = collective_forward(coll);
                if (ret == UNIFYFS_SUCCESS) {
                    ret = invoke_bcast_progress_rpc(coll);
                }
            }
        }
    }

    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != extents) {
            free(extents);
        }
    }

    return ret;
}

/*************************************************************************
 * Broadcast file attributes and extents metadata due to laminate
 *************************************************************************/

/* file extents metadata broadcast rpc handler */
static void laminate_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: laminate handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    laminate_bcast_in_t* in = calloc(1, sizeof(*in));
    laminate_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            size_t n_extents = (size_t) in->num_extents;
            size_t bulk_sz = n_extents * sizeof(struct extent_tree_node);
            hg_bulk_t local_bulk = HG_BULK_NULL;
            void* extents_buf = pull_margo_bulk_buffer(handle, in->extents,
                                                      bulk_sz, &local_bulk);
            if (NULL == extents_buf) {
                LOGERR("failed to get bulk extents");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.laminate_bcast_id;
                server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_LAMINATE;
                coll = collective_create(rpc, handle, op_hgid, (int)(in->root),
                                        (void*)in, (void*)out, sizeof(*out),
                                        in->extents, local_bulk, extents_buf);
                if (NULL == coll) {
                    ret = ENOMEM;
                } else {
                    /* update input structure that we are forwarding to point
                     * to our local bulk buffer. will be restore on cleanup. */
                    in->extents = local_bulk;
                    ret = collective_forward(coll);
                    if (ret == UNIFYFS_SUCCESS) {
                        req->req_type = rpc;
                        req->coll = coll;
                        req->handle = handle;
                        req->input = (void*) in;
                        req->bulk_buf = extents_buf;
                        req->bulk_sz = bulk_sz;
                        ret = sm_submit_service_request(req);
                        if (ret != UNIFYFS_SUCCESS) {
                            LOGERR("failed to submit coll request to svcmgr");
                        }
                    }
                }
            }
        }
    }

    if (ret != UNIFYFS_SUCCESS) {
        /* report failure back to caller */
        laminate_bcast_out_t lbo;
        lbo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &lbo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }
}
DEFINE_MARGO_RPC_HANDLER(laminate_bcast_rpc)

/* Execute broadcast tree for attributes and extent metadata due to laminate */
int unifyfs_invoke_broadcast_laminate(int gfid)
{
    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* get attributes and extents metadata */
    unifyfs_file_attr_t attrs;
    ret = unifyfs_inode_metaget(gfid, &attrs);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to get file attributes for gfid=%d", gfid);
        return ret;
    }

    if (!attrs.is_shared) {
        /* no need to broadcast for private files */
        LOGDBG("gfid=%d is private, not broadcasting", gfid);
        return UNIFYFS_SUCCESS;
    }

    LOGDBG("BCAST_RPC: starting laminate for gfid=%d", gfid);

    size_t n_extents;
    struct extent_tree_node* extents;
    ret = unifyfs_inode_get_extents(gfid, &n_extents, &extents);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to get extents for gfid=%d", gfid);
        return ret;
    }

    /* create bulk data structure containing the extents
     * NOTE: bulk data is always read only at the root of the broadcast tree */
    hg_bulk_t extents_bulk = HG_BULK_NULL;
    if (n_extents) {
        void* buf = (void*) extents;
        hg_size_t buf_size = n_extents * sizeof(*extents);
        hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                                             &buf, &buf_size,
                                             HG_BULK_READ_ONLY, &extents_bulk);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_bulk_create() failed");
            free(buf);
            return UNIFYFS_ERROR_MARGO;
        }
    }

    coll_request* coll = NULL;
    laminate_bcast_in_t* in = calloc(1, sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        /* set input params */
        in->root        = (int32_t) glb_pmi_rank;
        in->gfid        = (int32_t) gfid;
        in->attr        = attrs;
        in->extents     = extents_bulk;
        in->num_extents = (int32_t) n_extents;

        hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.laminate_bcast_id;
        server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_LAMINATE;
        coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                 glb_pmi_rank, (void*)in,
                                 NULL, sizeof(laminate_bcast_out_t),
                                 HG_BULK_NULL, extents_bulk, extents);
        if (NULL == coll) {
            ret = ENOMEM;
        } else {
            ret = collective_forward(coll);
            if (ret == UNIFYFS_SUCCESS) {
                ret = invoke_bcast_progress_rpc(coll);
            }
        }
    }

    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != extents) {
            free(extents);
        }
    }

    return ret;
}


/*************************************************************************
 * Broadcast file truncation
 *************************************************************************/

/* truncate broadcast rpc handler */
static void truncate_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: truncate handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    truncate_bcast_in_t* in = calloc(1, sizeof(*in));
    truncate_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.truncate_bcast_id;
            server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_TRUNCATE;
            coll = collective_create(rpc, handle, op_hgid, (int)(in->root),
                                     (void*)in, (void*)out, sizeof(*out),
                                     HG_BULK_NULL, HG_BULK_NULL, NULL);
            if (NULL == coll) {
                ret = ENOMEM;
            } else {
                ret = collective_forward(coll);
                if (ret == UNIFYFS_SUCCESS) {
                    req->req_type = rpc;
                    req->coll = coll;
                    req->handle = handle;
                    req->input = (void*) in;
                    req->bulk_buf = NULL;
                    req->bulk_sz = 0;
                    ret = sm_submit_service_request(req);
                    if (ret != UNIFYFS_SUCCESS) {
                        LOGERR("failed to submit coll request to svcmgr");
                    }
                }
            }
        }
    }

    if (ret != UNIFYFS_SUCCESS) {
        /* report failure back to caller */
        truncate_bcast_out_t tbo;
        tbo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &tbo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }
}
DEFINE_MARGO_RPC_HANDLER(truncate_bcast_rpc)

/* Execute broadcast tree for file truncate */
int unifyfs_invoke_broadcast_truncate(int gfid,
                                      size_t filesize)
{
    LOGDBG("BCAST_RPC: starting truncate(filesize=%zu) for gfid=%d",
           filesize, gfid);

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    truncate_bcast_in_t* in = calloc(1, sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        /* get input params */
        in->root = (int32_t) glb_pmi_rank;
        in->gfid = gfid;
        in->filesize = filesize;

        hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.truncate_bcast_id;
        server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_TRUNCATE;
        coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                 glb_pmi_rank, (void*)in,
                                 NULL, sizeof(truncate_bcast_out_t),
                                 HG_BULK_NULL, HG_BULK_NULL, NULL);
        if (NULL == coll) {
            ret = ENOMEM;
        } else {
            ret = collective_forward(coll);
            if (ret == UNIFYFS_SUCCESS) {
                ret = invoke_bcast_progress_rpc(coll);
            }
        }
    }
    return ret;
}

/*************************************************************************
 * Broadcast updates to file attributes
 *************************************************************************/

/* file attributes broadcast rpc handler */
static void fileattr_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: fileattr handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    fileattr_bcast_in_t* in = calloc(1, sizeof(*in));
    fileattr_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.fileattr_bcast_id;
            server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_FILEATTR;
            coll = collective_create(rpc, handle, op_hgid, (int)(in->root),
                                     (void*)in, (void*)out, sizeof(*out),
                                     HG_BULK_NULL, HG_BULK_NULL, NULL);
            if (NULL == coll) {
                ret = ENOMEM;
            } else {
                ret = collective_forward(coll);
                if (ret == UNIFYFS_SUCCESS) {
                    req->req_type = rpc;
                    req->coll = coll;
                    req->handle = handle;
                    req->input = (void*) in;
                    req->bulk_buf = NULL;
                    req->bulk_sz = 0;
                    ret = sm_submit_service_request(req);
                    if (ret != UNIFYFS_SUCCESS) {
                        LOGERR("failed to submit coll request to svcmgr");
                    }
                }
            }
        }
    }

    if (ret != UNIFYFS_SUCCESS) {
        /* report failure back to caller */
        fileattr_bcast_out_t fbo;
        fbo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &fbo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }
}
DEFINE_MARGO_RPC_HANDLER(fileattr_bcast_rpc)

/* Execute broadcast tree for file attributes update */
int unifyfs_invoke_broadcast_fileattr(int gfid,
                                      int attr_op,
                                      unifyfs_file_attr_t* fattr)
{
    LOGDBG("BCAST_RPC: starting metaset(op=%d) for gfid=%d", attr_op, gfid);

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    fileattr_bcast_in_t* in = calloc(1, sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        /* get input params */
        in->root   = (int32_t) glb_pmi_rank;
        in->gfid   = (int32_t) gfid;
        in->attrop = (int32_t) attr_op;
        in->attr   = *fattr;

        hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.fileattr_bcast_id;
        server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_FILEATTR;
        coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                 glb_pmi_rank, (void*)in,
                                 NULL, sizeof(fileattr_bcast_out_t),
                                 HG_BULK_NULL, HG_BULK_NULL, NULL);
        if (NULL == coll) {
            ret = ENOMEM;
        } else {
            ret = collective_forward(coll);
            if (ret == UNIFYFS_SUCCESS) {
                ret = invoke_bcast_progress_rpc(coll);
            }
        }
    }
    return ret;
}

/*************************************************************************
 * Broadcast file unlink
 *************************************************************************/

/* unlink broacast rpc handler */
static void unlink_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: unlink handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    unlink_bcast_in_t* in = calloc(1, sizeof(*in));
    unlink_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.unlink_bcast_id;
            server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_UNLINK;
            coll = collective_create(rpc, handle, op_hgid, (int)(in->root),
                                     (void*)in, (void*)out, sizeof(*out),
                                     HG_BULK_NULL, HG_BULK_NULL, NULL);
            if (NULL == coll) {
                ret = ENOMEM;
            } else {
                ret = collective_forward(coll);
                if (ret == UNIFYFS_SUCCESS) {
                    req->req_type = rpc;
                    req->coll = coll;
                    req->handle = handle;
                    req->input = (void*) in;
                    req->bulk_buf = NULL;
                    req->bulk_sz = 0;
                    ret = sm_submit_service_request(req);
                    if (ret != UNIFYFS_SUCCESS) {
                        LOGERR("failed to submit coll request to svcmgr");
                    }
                }
            }
        }
    }

    if (ret != UNIFYFS_SUCCESS) {
        /* report failure back to caller */
        unlink_bcast_out_t ubo;
        ubo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &ubo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }
}
DEFINE_MARGO_RPC_HANDLER(unlink_bcast_rpc)

/* Execute broadcast tree for file unlink */
int unifyfs_invoke_broadcast_unlink(int gfid)
{
    LOGDBG("BCAST_RPC: starting unlink for gfid=%d", gfid);

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    unlink_bcast_in_t* in = calloc(1, sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        /* get input params */
        in->root = (int32_t) glb_pmi_rank;
        in->gfid = gfid;

        hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.unlink_bcast_id;
        server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_UNLINK;
        coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                 glb_pmi_rank, (void*)in,
                                 NULL, sizeof(unlink_bcast_out_t),
                                 HG_BULK_NULL, HG_BULK_NULL, NULL);
        if (NULL == coll) {
            ret = ENOMEM;
        } else {
            ret = collective_forward(coll);
            if (ret == UNIFYFS_SUCCESS) {
                ret = invoke_bcast_progress_rpc(coll);
            }
        }
    }
    return ret;
}
