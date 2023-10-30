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
#include "unifyfs_p2p_rpc.h"
#include "unifyfs_rpc_util.h"


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
            LOGERR("failed to get handle for child request to server %d - %s",
                   peer_rank, HG_Error_to_string(hret));
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
    double timeout_ms = margo_server_server_timeout_msec;
    hg_return_t hret = margo_iforward_timed(chdl, input_ptr, timeout_ms, creq);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to forward request(%p) - %s", creq,
               HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    }

    return ret;
}

/* Helper function for the UNIFYFS_SERVER_BCAST_RPC_METAGET case in
 * get_child_response().  Handles merging the output from a child
 * with that from the parent. */
static int merge_metaget_all_bcast_outputs(
            metaget_all_bcast_out_t* p_out,
            metaget_all_bcast_out_t* c_out,
            hg_handle_t p_hdl,
            hg_handle_t c_hdl)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret = HG_SUCCESS;
    hg_return_t bulk_create_hret = HG_SUCCESS;

    int32_t parent_num_files = p_out->num_files;
    int32_t child_num_files = c_out->num_files;
    hg_size_t child_buf_size = child_num_files * sizeof(unifyfs_file_attr_t);

    /* Quick optimization:  If there's no files for the child or
     * parent, we can exit now.  (In fact, trying to perform a bulk transfer
     * of size 0 will fail, so if we don't bail now, we'd just have to have
     * more checks for this case down below.) */
    if ((0 == parent_num_files) && (0 == child_num_files)) {
        return UNIFYFS_SUCCESS;
    }

    // Some variables we'll need for the bulk transfer(s)
    unifyfs_file_attr_t* child_attr_list = NULL;
    hg_bulk_t local_bulk;
    const struct hg_info* info = NULL;
    hg_addr_t server_addr;
    margo_instance_id mid;

    // If the number of child files is 0, don't bother with the bulk
    // transfer - it will just fail with HG_INVALID_ARG
    if (child_num_files) {

        // Pull the bulk data (the list of file_attr structs) over
        child_attr_list = calloc(child_num_files, sizeof(unifyfs_file_attr_t));
        if (!child_attr_list) {
            return ENOMEM;
        }

        // Figure out some margo-specific info that we need for the transfer
        info = margo_get_info(c_hdl);
        server_addr = info->addr;
        mid = margo_hg_handle_get_instance(c_hdl);

        hg_size_t segment_sizes[1] = { child_buf_size };
        void* segment_ptrs[1] = { (void*)child_attr_list };
        bulk_create_hret =
            margo_bulk_create(mid, 1, segment_ptrs, segment_sizes,
                              HG_BULK_WRITE_ONLY, &local_bulk);
        if (HG_SUCCESS != bulk_create_hret) {
            LOGERR("margo_bulk_create() failed - %s",
                HG_Error_to_string(bulk_create_hret));
            free(child_attr_list);
            return UNIFYFS_ERROR_MARGO;
        }

        hret = margo_bulk_transfer(mid, HG_BULK_PULL, server_addr,
                                   c_out->file_meta, 0, local_bulk, 0,
                                   child_buf_size);
        if (HG_SUCCESS != hret) {
            LOGERR("margo_bulk_transfer() failed - %s",
                   HG_Error_to_string(hret));
            margo_bulk_free(local_bulk);
            free(child_attr_list);
            return UNIFYFS_ERROR_MARGO;
        }

        margo_bulk_free(local_bulk);
    }

    /* OK, file attrs from the child (assuming there were any files) are now
     * stored in child_attr_list.  And if there were 0 files, then
     * child_attr_list is NULL. */

    // Now get the bulk data from the parent (assuming there is any)
    unifyfs_file_attr_t* parent_attr_list = NULL;
    if (parent_num_files + child_num_files > 0) {
        parent_attr_list = calloc(parent_num_files + child_num_files,
                                  sizeof(unifyfs_file_attr_t));
        /* Note: Deliberately allocating enough space for the child file attrs,
         * since we're going to be copying them in anyway.
         * Also, we had to check to see if there actually was any need to
         * allocate memory because if you pass 0 into calloc(), you'll likely
         * get a NULL back, and that would confuse the error checking on the
         * next lines. */
        if (!parent_attr_list) {
            free(child_attr_list);
            return ENOMEM;
        }
    }

    if (parent_num_files) {
        hg_size_t parent_buf_size =
            parent_num_files * sizeof(unifyfs_file_attr_t);

        // Figure out some margo-specific info that we need for the transfer
        info = margo_get_info(p_hdl);
        server_addr = info->addr;
        // address of the bulk data on the server side
        mid = margo_hg_handle_get_instance(p_hdl);

        hg_size_t segment_sizes[1] = { parent_buf_size };
        void* segment_ptrs[1] = { (void*)parent_attr_list };
        bulk_create_hret =
            margo_bulk_create(mid, 1, segment_ptrs, segment_sizes,
                              HG_BULK_WRITE_ONLY, &local_bulk);

        if (HG_SUCCESS != bulk_create_hret) {
            LOGERR("margo_bulk_create() failed - %s",
                HG_Error_to_string(bulk_create_hret));
            free(parent_attr_list);
            free(child_attr_list);
            return UNIFYFS_ERROR_MARGO;
        }

        /* It would be nice if we didn't have to actually do a margo transfer
         * here.  The data we need exists in our current address space
         * somewhere.  Unfortunately, we don't know where because that's
         * hidden from us by Margo.  The best we can do is hope that Margo is
         * optimized for this case and this transfer ends up just being a
         * mem copy. */
        hret = margo_bulk_transfer(mid, HG_BULK_PULL, server_addr,
                                   p_out->file_meta, 0, local_bulk, 0,
                                   parent_buf_size);
        if (HG_SUCCESS != hret) {
            LOGERR("margo_bulk_transfer() failed - %s",
                   HG_Error_to_string(hret));
            margo_bulk_free(local_bulk);
            free(parent_attr_list);
            free(child_attr_list);
            return UNIFYFS_ERROR_MARGO;
        }
        margo_bulk_free(local_bulk);
    }

    /* OK, file attrs from the parent (assuming there were any files) are now
     * stored in parent_attr_list.  And parent_attr_list is actually big
     * enough to hold all the file attrs from the parent and child.
     *
     * The next step is to append the child filenames string to the parent's,
     * and update the string offsets stored in the child file attrs' filename
     * members. */

    uint64_t parent_filenames_len =
        p_out->filenames ? strlen(p_out->filenames) : 0;
    uint64_t child_filenames_len =
        c_out->filenames ? strlen(c_out->filenames) : 0;

    char* new_filenames = calloc(parent_filenames_len+child_filenames_len+1,
                                 sizeof(char));
    if (!new_filenames) {
        free(parent_attr_list);
        free(child_attr_list);
        return ENOMEM;
    }

    if (p_out->filenames) {
        strcpy(new_filenames, p_out->filenames);
    }
    if (c_out->filenames) {
        strcat(new_filenames, c_out->filenames);
    }
    free(p_out->filenames);
    p_out->filenames = new_filenames;

    // Now update all the offset values in the child_attr_list
    for (unsigned int i = 0; i < child_num_files; i++) {
        uint64_t new_offset = (uint64_t)child_attr_list[i].filename +
                              parent_filenames_len;
        child_attr_list[i].filename = (char*)new_offset;
    }

    /* Now we need to append the child file attrs to the parent file attrs,
     * create a new hg_bulk and replace the old parent bulk with the new one.
     */
    memcpy(&parent_attr_list[parent_num_files], child_attr_list,
           child_buf_size);
    free(child_attr_list);

    size_t parent_buf_size =
        (parent_num_files + child_num_files) * sizeof(unifyfs_file_attr_t);
    hg_size_t segment_sizes[1] = { parent_buf_size };
    void* segment_ptrs[1] = { (void*)parent_attr_list };

    // Save the parent's old bulk so that we can restore it if the
    // bulk create fails, or free it if the create succeeds
    hg_bulk_t parent_old_bulk = p_out->file_meta;

    hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                             segment_ptrs, segment_sizes,
                             HG_BULK_READ_ONLY, &p_out->file_meta);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed - %s", HG_Error_to_string(hret));
        p_out->file_meta = parent_old_bulk;
        free(parent_attr_list);
        return UNIFYFS_ERROR_MARGO;
    }

    margo_bulk_free(parent_old_bulk);

    /* Lastly, update the num_files value */
    p_out->num_files += child_num_files;

    return ret;
}

static int get_child_response(coll_request* coll_req,
                              hg_handle_t chdl)
{
    int ret = UNIFYFS_SUCCESS;
    void* out = calloc(1, coll_req->output_sz);
    if (NULL == out) {
        ret = ENOMEM;
    } else {
        hg_return_t hret = margo_get_output(chdl, out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_output() failed - %s", HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            /* update collective return value using child response */
            int child_ret = UNIFYFS_SUCCESS;
            void* output = coll_req->output;

            switch (coll_req->req_type) {
            case UNIFYFS_SERVER_BCAST_RPC_BOOTSTRAP: {
                bootstrap_complete_bcast_out_t* cbbo =
                    (bootstrap_complete_bcast_out_t*) out;
                bootstrap_complete_bcast_out_t* bbo =
                    (bootstrap_complete_bcast_out_t*) output;
                child_ret = cbbo->ret;
                if ((NULL != bbo) && (child_ret != UNIFYFS_SUCCESS)) {
                    bbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_EXTENTS: {
                extent_bcast_out_t* cebo = (extent_bcast_out_t*) out;
                extent_bcast_out_t* ebo  = (extent_bcast_out_t*) output;
                child_ret = cebo->ret;
                if ((NULL != ebo) && (child_ret != UNIFYFS_SUCCESS)) {
                    ebo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_FILEATTR: {
                fileattr_bcast_out_t* cfbo = (fileattr_bcast_out_t*) out;
                fileattr_bcast_out_t* fbo  = (fileattr_bcast_out_t*) output;
                child_ret = cfbo->ret;
                if ((NULL != fbo) && (child_ret != UNIFYFS_SUCCESS)) {
                    fbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_LAMINATE: {
                laminate_bcast_out_t* clbo = (laminate_bcast_out_t*) out;
                laminate_bcast_out_t* lbo  = (laminate_bcast_out_t*) output;
                child_ret = clbo->ret;
                if ((NULL != lbo) && (child_ret != UNIFYFS_SUCCESS)) {
                    lbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_TRANSFER: {
                transfer_bcast_out_t* ctbo = (transfer_bcast_out_t*) out;
                transfer_bcast_out_t* tbo  = (transfer_bcast_out_t*) output;
                child_ret = ctbo->ret;
                if ((NULL != tbo) && (child_ret != UNIFYFS_SUCCESS)) {
                    tbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_TRUNCATE: {
                truncate_bcast_out_t* ctbo = (truncate_bcast_out_t*) out;
                truncate_bcast_out_t* tbo  = (truncate_bcast_out_t*) output;
                child_ret = ctbo->ret;
                if ((NULL != tbo) && (child_ret != UNIFYFS_SUCCESS)) {
                    tbo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_UNLINK: {
                unlink_bcast_out_t* cubo = (unlink_bcast_out_t*) out;
                unlink_bcast_out_t* ubo  = (unlink_bcast_out_t*) output;
                child_ret = cubo->ret;
                if ((NULL != ubo) && (child_ret != UNIFYFS_SUCCESS)) {
                    ubo->ret = child_ret;
                }
                break;
            }
            case UNIFYFS_SERVER_BCAST_RPC_METAGET: {
                // NOTE: This case is different.  It's currently the only
                // case that actually returns more than just a single error
                // code up the tree.
                metaget_all_bcast_out_t* cmabo =
                     (metaget_all_bcast_out_t*) out;
                metaget_all_bcast_out_t* mabo  =
                    (metaget_all_bcast_out_t*) output;
                child_ret = cmabo->ret;
                if ((NULL != mabo) && (child_ret != UNIFYFS_SUCCESS)) {
                    mabo->ret = child_ret;
                }
                if ((NULL != cmabo) && (NULL != mabo)) {
                    merge_metaget_all_bcast_outputs(
                        mabo, cmabo, coll_req->progress_hdl, chdl);
                } else {
                    /* One or both of the output structures is missing.
                     * (This shouldn't ever happen.) */
                    LOGERR(
                        "Missing required output structs when handling "
                        "UNIFYFS_SERVER_BCAST_RPC_METAGET child responses!");
                    LOGERR("Parent output struct: 0x%lX", (uint64_t)mabo);
                    LOGERR("Child output struct: 0x%lX", (uint64_t)cmabo);
                    mabo->ret = UNIFYFS_ERROR_BADCONFIG;
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
        free(out);
    }

    return ret;
}

static int wait_for_all_child_requests(coll_request* coll_req,
                                       int n_children)
{
    if (NULL == coll_req) {
        return EINVAL;
    }

    if (n_children == 0) {
        return UNIFYFS_SUCCESS;
    } else if (NULL == coll_req->child_reqs) {
        LOGERR("collective(%p) has %d children, but NULL child_reqs array",
               coll_req, n_children);
        return EINVAL;
    }

    int ret = UNIFYFS_SUCCESS;
    int n_complete = 0;

    /* use margo_wait_any() until all requests completed/errored */
    do {
        size_t complete_ndx;
        hg_return_t hret = margo_wait_any((size_t)n_children,
                                          coll_req->child_reqs,
                                          &complete_ndx);
        if (HG_SUCCESS == hret) {
            n_complete++;
            hg_handle_t* chdl   = coll_req->child_hdls + complete_ndx;
            margo_request* creq = coll_req->child_reqs + complete_ndx;

            /* get the output of the rpc */
            int child_ret = get_child_response(coll_req, *chdl);
            LOGDBG("BCAST_RPC: collective(%p) child[%zu] resp=%d",
                   coll_req, complete_ndx, child_ret);
            if (child_ret != UNIFYFS_SUCCESS) {
                ret = child_ret;
            }

            /* set request to MARGO_REQUEST_NULL so that the next call to
             * margo_wait_any() will ignore it */
            *creq = MARGO_REQUEST_NULL;

            /* release the handle for the completed request */
            margo_destroy(*chdl);
            *chdl = HG_HANDLE_NULL;
        } else {
            LOGERR("margo_wait_any() failed with error code=%s",
                   HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;

            for (int i = 0; i < n_children; i++) {
                hg_handle_t* chdl = coll_req->child_hdls + i;
                if (HG_HANDLE_NULL != *chdl) {
                    margo_destroy(*chdl);
                    *chdl = HG_HANDLE_NULL;
                }
            }

            break; /* out of do/while loop */
        }
    } while (n_complete < n_children);

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
        LOGDBG("BCAST_RPC: collective(%p) create (type=%d, root=%d)",
               coll_req, req_type, tree_root_rank);
        coll_req->resp_hdl      = handle;
        coll_req->req_type      = req_type;
        coll_req->output        = output_struct;
        coll_req->input         = input_struct;
        coll_req->bulk_in       = bulk_in;
        coll_req->output_sz     = output_size;
        coll_req->bulk_buf      = bulk_buf;
        coll_req->bulk_forward  = bulk_forward;
        coll_req->progress_req  = MARGO_REQUEST_NULL;
        coll_req->progress_hdl  = HG_HANDLE_NULL;
        coll_req->app_id        = -1;
        coll_req->client_id     = -1;
        coll_req->client_req_id = -1;
        coll_req->auto_cleanup  =  1;
        /* Default behavior is for bcast_progress_rpc() to automatically
         * call collective_cleanup() on the instance.  In cases where such
         * behavior is incorrect - such as when results need to be returned
         * from the collective's children - this variable can be changed
         * before calling bcast_progress_rpc(). */


        int rc = ABT_mutex_create(&coll_req->resp_valid_sync);
        if (ABT_SUCCESS != rc) {
            LOGERR("ABT_mutex_create failed");
            free(coll_req);
            return NULL;
        }
        rc = ABT_cond_create(&coll_req->resp_valid_cond);
        if (ABT_SUCCESS != rc) {
            LOGERR("ABT_cond_create failed");
            ABT_mutex_free(&coll_req->resp_valid_sync);
            free(coll_req);
            return NULL;
        }

        rc = unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, tree_root_rank,
                                   UNIFYFS_BCAST_K_ARY, &(coll_req->tree));
        if (rc) {
            LOGERR("unifyfs_tree_init() failed");
            ABT_mutex_free(&coll_req->resp_valid_sync);
            ABT_cond_free(&coll_req->resp_valid_cond);
            free(coll_req);
            return NULL;
        }
        size_t n_children = (size_t) coll_req->tree.child_count;
        if (n_children) {
            coll_req->child_hdls = calloc(n_children, sizeof(hg_handle_t));
            coll_req->child_reqs = calloc(n_children, sizeof(margo_request));
            if ((NULL == coll_req->child_hdls) ||
                (NULL == coll_req->child_reqs)) {
                LOGERR("allocation of children state failed");
                free(coll_req->child_hdls);
                free(coll_req->child_reqs);
                /* Note: calling free() on NULL is explicitly allowed */
                ABT_mutex_free(&coll_req->resp_valid_sync);
                ABT_cond_free(&coll_req->resp_valid_cond);
                free(coll_req);
                return NULL;
            }

            int* ranks = coll_req->tree.child_ranks;
            for (int i = 0; i < coll_req->tree.child_count; i++) {
                /* allocate child request handle */
                LOGDBG("collective(%p) - child[%d] is rank=%d",
                        coll_req, i, ranks[i]);
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

void collective_cleanup(coll_request* coll_req)
{
    if (NULL == coll_req) {
        return;
    }

    LOGDBG("BCAST_RPC: collective(%p) cleanup", coll_req);

    /* release margo resources */
    if (HG_HANDLE_NULL != coll_req->progress_hdl) {
        if (MARGO_REQUEST_NULL != coll_req->progress_req) {
            margo_wait(coll_req->progress_req);
        }
        margo_destroy(coll_req->progress_hdl);
    }

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

    /* Release the Argobots mutex and condition variable */
    ABT_cond_free(&coll_req->resp_valid_cond);
    ABT_mutex_free(&coll_req->resp_valid_sync);

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

    /* release communication tree resources */
    unifyfs_tree_free(&(coll_req->tree));
    memset(coll_req, 0, sizeof(*coll_req));
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
        LOGDBG("BCAST_RPC: collective(%p) forwarding to child[%d]",
               coll_req, i);
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
    case UNIFYFS_SERVER_BCAST_RPC_BOOTSTRAP: {
        bootstrap_complete_bcast_out_t* bbo =
            (bootstrap_complete_bcast_out_t*) output;
        bbo->ret = val;
        break;
    }
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
    case UNIFYFS_SERVER_BCAST_RPC_TRANSFER: {
        transfer_bcast_out_t* tbo = (transfer_bcast_out_t*) output;
        tbo->ret = val;
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
    case UNIFYFS_SERVER_BCAST_RPC_METAGET: {
        metaget_all_bcast_out_t* mabo = (metaget_all_bcast_out_t*) output;
        mabo->ret = val;
    }
    default:
        LOGERR("invalid collective request type %d", coll_req->req_type);
        break;
    }
}

/* finish collective process by waiting for any child responses and
 * sending parent response (if applicable) */
int collective_finish(coll_request* coll_req)
{
    int ret = UNIFYFS_SUCCESS;

    LOGDBG("BCAST_RPC: collective(%p) finish", coll_req);

    /* wait for responses from children */
    int child_count = coll_req->tree.child_count;
    int rc = wait_for_all_child_requests(coll_req, child_count);
    if (rc != UNIFYFS_SUCCESS) {
        ret = rc;
    }

    /* If there's output data AND there's a caller to send it back to,
     * then send the output back to the caller.  If we're at the root
     * of the tree, though, there might be output data, but no place
     * to send it. */
    if ((NULL != coll_req->output) && (HG_HANDLE_NULL != coll_req->resp_hdl)) {
        hg_return_t hret = margo_respond(coll_req->resp_hdl, coll_req->output);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
        }

        LOGDBG("BCAST_RPC: collective(%p, op=%d) responded",
               coll_req, (int)(coll_req->req_type));
    }

    /* Signal the condition variable in case there are other threads
     * waiting for the child responses */
    ABT_mutex_lock(coll_req->resp_valid_sync);
    ABT_cond_signal(coll_req->resp_valid_cond);
    /* There should only be a single thread waiting on the CV, so we don't
     * need to use ABT_cond_broadcast() */
    ABT_mutex_unlock(coll_req->resp_valid_sync);
    /* Locking the mutex before signaling is required in order to ensure
     * that the waiting thread has had a chance to actually call
     * ABT_cond_timedwait() before this thread signals the CV. */

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
    hg_id_t hgid = unifyfsd_rpc_context->rpcs.bcast_progress_id;
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
                                    hgid, &(coll_req->progress_hdl));
    if (hret != HG_SUCCESS) {
        LOGERR("failed to get handle for bcast progress  - %s",
               HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* call local rpc function, which allows progress to be handled
         * by a ULT */
        bcast_progress_in_t in;
        in.coll_req = (hg_ptr_t) coll_req;
        hret = margo_iforward(coll_req->progress_hdl, &in,
                              &(coll_req->progress_req));
        if (hret != HG_SUCCESS) {
            LOGERR("failed to forward bcast progress for coll(%p) - %s",
                   HG_Error_to_string(hret), coll_req);
            ret = UNIFYFS_ERROR_MARGO;
        }
    }

    return ret;
}

/* generic broadcast rpc progression handler */
static void bcast_progress_rpc(hg_handle_t handle)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;
    coll_request* coll = NULL;
    bool cleanup_collective = false;

    bcast_progress_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        coll = (coll_request*) in.coll_req;
        margo_free_input(handle, &in);

        cleanup_collective = ((NULL != coll) && (coll->auto_cleanup));
        /* We have to check the auto_cleanup variable now because in the case
         * where auto_cleanup is false, another thread will be freeing the
         * collective.  And once the memory is freed, we can't read the
         * auto_cleanup variable.
         *
         * There's a condition variable that's signaled by collective_finish(),
         * and the memory won't be freed until some time after that happens, so
         * it's safe to check the variable up here. */

        /* call collective_finish() to progress bcast operation */
        LOGDBG("BCAST_RPC: bcast progress collective(%p)", coll);
        ret = collective_finish(coll);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("collective_finish() failed for collective(%p) (rc=%d)",
                   coll, ret);
        }
    }

    /* finish rpc */
    bcast_progress_out_t out;
    out.ret = (int32_t) ret;
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
    }

    if (cleanup_collective) {
        collective_cleanup(coll);
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bcast_progress_rpc)


/***************************************************
 * Broadcast server bootstrap completion
 ***************************************************/

/* bootstrap complete broadcast rpc handler */
static void bootstrap_complete_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: bootstrap handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    bootstrap_complete_bcast_in_t* in = calloc(1, sizeof(*in));
    bootstrap_complete_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            hg_id_t op_hgid =
                unifyfsd_rpc_context->rpcs.bootstrap_complete_bcast_id;
            server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_BOOTSTRAP;
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
        bootstrap_complete_bcast_out_t bbo;
        bbo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &bbo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }
}
DEFINE_MARGO_RPC_HANDLER(bootstrap_complete_bcast_rpc)

/* Execute broadcast tree for 'bootstrap complete' notification */
int unifyfs_invoke_broadcast_bootstrap_complete(void)
{
    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    LOGDBG("BCAST_RPC: starting bootstrap complete");
    coll_request* coll = NULL;
    bootstrap_complete_bcast_in_t* in = calloc(1, sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        /* set input params */
        in->root = (int32_t) glb_pmi_rank;
        hg_id_t op_hgid =
            unifyfsd_rpc_context->rpcs.bootstrap_complete_bcast_id;
        server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_BOOTSTRAP;
        coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                 glb_pmi_rank, (void*)in,
                                 NULL, sizeof(bootstrap_complete_bcast_out_t),
                                 HG_BULK_NULL, HG_BULK_NULL, NULL);
        if (NULL == coll) {
            ret = ENOMEM;
        } else {
            ret = collective_forward(coll);
            if (ret == UNIFYFS_SUCCESS) {
                /* avoid cleanup by the progress rpc */
                coll->auto_cleanup = 0;
                ABT_mutex_lock(coll->resp_valid_sync);
                ret = invoke_bcast_progress_rpc(coll);
                if (ret == UNIFYFS_SUCCESS) {
                    /* wait for all the child responses to come back */
                    struct timespec timeout;
                    clock_gettime(CLOCK_REALTIME, &timeout);
                    timeout.tv_sec += 5; /* 5 sec */
                    int rc = ABT_cond_timedwait(coll->resp_valid_cond,
                                                coll->resp_valid_sync,
                                                &timeout);
                    if (ABT_ERR_COND_TIMEDOUT == rc) {
                        LOGERR("timeout");
                        ret = UNIFYFS_ERROR_TIMEOUT;
                    } else if (rc) {
                        LOGERR("failed to wait on condition (err=%d)", rc);
                        ret = UNIFYFS_ERROR_MARGO;
                    } else if (NULL != coll->output) {
                        bootstrap_complete_bcast_out_t* out =
                            (bootstrap_complete_bcast_out_t*) coll->output;
                        ret = out->ret;
                    }
                }
                ABT_mutex_unlock(coll->resp_valid_sync);
            } else {
                LOGERR("collective(%p) forward failed - cleaning up", coll);
            }
            collective_cleanup(coll);
        }
    }

    return ret;
}

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
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            size_t num_extents = (size_t) in->num_extents;
            size_t bulk_sz = num_extents * sizeof(struct extent_metadata);
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
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
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
int unifyfs_invoke_broadcast_extents(int gfid)
{
    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    LOGDBG("BCAST_RPC: starting extents for gfid=%d", gfid);

    size_t n_extents;
    struct extent_metadata* extents;
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
        LOGERR("margo_bulk_create() failed - %s", HG_Error_to_string(hret));
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

/* file lamination broadcast rpc handler */
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
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            size_t n_extents = (size_t) in->num_extents;
            size_t bulk_sz = n_extents * sizeof(struct extent_metadata);
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
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
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
    struct extent_metadata* extents;
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
            LOGERR("margo_bulk_create() failed - %s",
                   HG_Error_to_string(hret));
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
 * Broadcast file transfer request
 *************************************************************************/

/* file transfer broadcast rpc handler */
static void transfer_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: transfer handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    transfer_bcast_in_t* in = calloc(1, sizeof(*in));
    transfer_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.transfer_bcast_id;
            server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_TRANSFER;
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
        transfer_bcast_out_t tbo;
        tbo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &tbo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }
}
DEFINE_MARGO_RPC_HANDLER(transfer_bcast_rpc)

/* Execute broadcast tree for attributes and extent metadata due to transfer */
int unifyfs_invoke_broadcast_transfer(int client_app,
                                      int client_id,
                                      int transfer_id,
                                      int gfid,
                                      int transfer_mode,
                                      const char* dest_file)
{
    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    LOGDBG("BCAST_RPC: starting transfer(mode=%d) for gfid=%d to file %s",
           transfer_mode, gfid, dest_file);

    coll_request* coll = NULL;
    transfer_bcast_in_t* in = calloc(1, sizeof(*in));
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    if ((NULL == in) || (NULL == req)) {
        ret = ENOMEM;
    } else {
        /* set input params */
        in->root        = (int32_t) glb_pmi_rank;
        in->gfid        = (int32_t) gfid;
        in->mode        = (int32_t) transfer_mode;
        in->dst_file    = (hg_const_string_t) strdup(dest_file);

        hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.transfer_bcast_id;
        server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_TRANSFER;
        coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                 glb_pmi_rank, (void*)in,
                                 NULL, sizeof(transfer_bcast_out_t),
                                 HG_BULK_NULL, HG_BULK_NULL, NULL);
        if (NULL == coll) {
            ret = ENOMEM;
        } else {
            int rc = collective_forward(coll);
            if (rc == UNIFYFS_SUCCESS) {
                coll->app_id = client_app;
                coll->client_id = client_id;
                coll->client_req_id = transfer_id;
                req->req_type = rpc;
                req->coll = coll;
                req->handle = HG_HANDLE_NULL;
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
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
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
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
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
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
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
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
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
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
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
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
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

/*************************************************************************
 * Broadcast metaget all request
 *************************************************************************/

/* metaget all broacast rpc handler */
static void metaget_all_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("BCAST_RPC: metaget_all handler");

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    coll_request* coll = NULL;
    server_rpc_req_t* req = calloc(1, sizeof(*req));
    metaget_all_bcast_in_t* in = calloc(1, sizeof(*in));
    metaget_all_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == req) || (NULL == in) || (NULL == out)) {
        ret = ENOMEM;
    } else {
        /* get input params */
        LOGDBG("BCAST_RPC: getting input params");
        hg_return_t hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed - %s", HG_Error_to_string(hret));
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            LOGDBG("BCAST_RPC: creating collective");
            hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.metaget_all_bcast_id;
            server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_METAGET;
            coll = collective_create(rpc, handle, op_hgid, (int)(in->root),
                                     (void*)in, (void*)out, sizeof(*out),
                                     HG_BULK_NULL, HG_BULK_NULL, NULL);
            if (NULL == coll) {
                ret = ENOMEM;
            } else {
                LOGDBG("BCAST_RPC: forwarding collective");
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
        metaget_all_bcast_out_t mgabo;
        mgabo.ret = (int32_t)ret;
        hg_return_t hret = margo_respond(handle, &mgabo);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed - %s", HG_Error_to_string(hret));
        }

        if (NULL != coll) {
            collective_cleanup(coll);
        } else {
            margo_destroy(handle);
        }
    }

    LOGDBG("BCAST_RPC: exiting metaget_all handler");

}

DEFINE_MARGO_RPC_HANDLER(metaget_all_bcast_rpc)

/* Execute broadcast tree for a metaget_all operation*/
/* Upon success, file_attrs will hold data that has been allocated with
 * malloc() and must be freed by the caller with free().  In the event of an
 * error, the caller must *NOT* free the pointer.
 *
 * Note that 0 files is still considered a successful result.  In that case,
 * num_file_attrs will point to 0 and *file_attrs will point to NULL. */
int unifyfs_invoke_broadcast_metaget_all(unifyfs_file_attr_t** file_attrs,
                                         int* num_file_attrs)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t bulk_create_hret = HG_NOMEM;
    hg_return_t bulk_transfer_hret = HG_OTHER_ERROR;
    /* Have to assume the bulk create and bulk transfer operations failed or
     * else we might try to clean up non-existent data down in the clean-up
     * section. */

    hg_bulk_t local_bulk;

    LOGDBG("BCAST_RPC: starting metaget_all broadcast");

    coll_request* coll = NULL;
    unifyfs_file_attr_t* attr_list = NULL;
    unifyfs_file_attr_t* local_file_attrs = NULL;
    /* attr_list holds the metadata we received from other server processes
     * via the broadcast RPC.  local_file_attrs holds metadata for files
     * that the current server process owns. */
    metaget_all_bcast_in_t* in = calloc(1, sizeof(*in));
    metaget_all_bcast_out_t* out = calloc(1, sizeof(*out));
    if ((NULL == in) || (NULL == out)) {
        ret = ENOMEM;
        goto Exit_Invoke_BMA;
    }

    /* get input params */
    in->root = (int32_t) glb_pmi_rank;

    hg_id_t op_hgid = unifyfsd_rpc_context->rpcs.metaget_all_bcast_id;
    server_rpc_e rpc = UNIFYFS_SERVER_BCAST_RPC_METAGET;
    coll = collective_create(rpc, HG_HANDLE_NULL, op_hgid,
                                glb_pmi_rank, (void*)in,
                                (void*)out, sizeof(metaget_all_bcast_out_t),
                                HG_BULK_NULL, HG_BULK_NULL, NULL);
    /* Note: We are passing in HG_HANDLE_NULL for the response handle
     * because we are the root of the tree and there's nobody for us to
     * respond to. */

    if (NULL == coll) {
        ret = ENOMEM;
        goto Exit_Invoke_BMA;
    }

    ret = collective_forward(coll);
    if (UNIFYFS_SUCCESS != ret) {
        goto Exit_Invoke_BMA;
    }

    /* We don't want the progress rpc to clean up for us because
     * we need to get the output data */
    coll->auto_cleanup = 0;

    ABT_mutex_lock(coll->resp_valid_sync);
    /* Have to lock the mutex before the bcast_progress_rpc call
     * so that we're sure to be waiting on the condition
     * variable before the progress thread gets to the point
     * where it signals the CV. */

    ret = invoke_bcast_progress_rpc(coll);
    if (UNIFYFS_SUCCESS != ret) {
        LOGERR("invoke_bcast_progress_rpc() failed with error %d", ret);
        goto Exit_Invoke_BMA;
    }

    /* While the broadcast RPC is running, let's fetch the metadata for
     * all the files the server we're currently running as owns. */
    unsigned int num_local_files = 0;
    ret = unifyfs_get_owned_files(&num_local_files, &local_file_attrs);
    if (UNIFYFS_SUCCESS != ret) {
        LOGERR("unifyfs_get_owned_files() failed with error %d", ret);
        goto Exit_Invoke_BMA;
    }

    // Wait for all the child responses to come back
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 10; /* 10 sec */
    int rc = ABT_cond_timedwait(coll->resp_valid_cond,
                                coll->resp_valid_sync,
                                &timeout);
    if (ABT_ERR_COND_TIMEDOUT == rc) {
        LOGERR("timeout");
        ret = UNIFYFS_ERROR_TIMEOUT;
    } else if (rc) {
        LOGERR("failed to wait on condition (err=%d)", rc);
        ret = UNIFYFS_ERROR_MARGO;
    }
    ABT_mutex_unlock(coll->resp_valid_sync);
    // Now we can get the data from the output struct

    if (sizeof(metaget_all_bcast_out_t) != coll->output_sz) {
        LOGERR("Unexpected size (%zu) for collective output - expected %zu",
               coll->output_sz, sizeof(metaget_all_bcast_out_t));
    }

    // Pull the bulk data (the list of file_attr structs) over
    metaget_all_bcast_out_t* results = (metaget_all_bcast_out_t*)coll->output;

    /* Now check the number of files - if it's 0, then we don't need to
     * bother with the bulk tranfser.  (In fact, if we were to try, we'd
     * get an error.) */

    if (results->num_files) {
        hg_size_t buf_size = results->num_files * sizeof(unifyfs_file_attr_t);
        attr_list = calloc(results->num_files, sizeof(unifyfs_file_attr_t));
        if (NULL == attr_list) {
            ret = ENOMEM;
            goto Exit_Invoke_BMA;
        }

        // Figure out some margo-specific info that we need for the transfer
        const struct hg_info* info = margo_get_info(coll->progress_hdl);
        hg_addr_t server_addr = info->addr;
        // address of the bulk data on the server side
        margo_instance_id mid =
            margo_hg_handle_get_instance(coll->progress_hdl);

        bulk_create_hret =
            margo_bulk_create(mid, 1, (void**)&attr_list, &buf_size,
                            HG_BULK_WRITE_ONLY, &local_bulk);
        if (HG_SUCCESS != bulk_create_hret) {
            LOGERR("margo_bulk_create() failed - %s",
                HG_Error_to_string(bulk_create_hret));
            ret = UNIFYFS_ERROR_MARGO;
            goto Exit_Invoke_BMA;
        }

        bulk_transfer_hret =
            margo_bulk_transfer(mid, HG_BULK_PULL, server_addr,
                                results->file_meta, 0, local_bulk, 0, buf_size);
        if (HG_SUCCESS != bulk_transfer_hret) {
            LOGERR("margo_bulk_transfer() failed - %s",
                HG_Error_to_string(bulk_transfer_hret));
            ret = UNIFYFS_ERROR_MARGO;
            goto Exit_Invoke_BMA;
        }

        /* At this point, attr_list should have the file_attr_t  structs from
         * all the other servers.  However, we still need to assign the
         * filename values for each struct. */

        for (unsigned int i = 0; i < results->num_files; i++) {
            /* Remember that we abused the filename pointer to actually hold
             * an offset into the filenames string that we sent separately.
             * (See the comments in process_metaget_bcast_rpc()) */
            uint64_t start_offset = (uint64_t)attr_list[i].filename;
            uint64_t name_len;
            if (i < (results->num_files-1)) {
                name_len = ((uint64_t)attr_list[i+1].filename) - start_offset;
            } else {
                /* length is calculated differently for the last file in
                 * file_attrs */
                name_len = strlen(&results->filenames[start_offset]);
            }
            attr_list[i].filename =
                strndup(&results->filenames[start_offset], name_len);
            if (NULL == attr_list[i].filename) {
                ret = ENOMEM;
                LOGERR("strdup() failed processing filename");
                /* If we're actually getting ENOMEM from strdup(), the error
                 * log is probably also going to fail... */
                goto Exit_Invoke_BMA;
                /* Technically, attr_list probably contains valid data and we
                 * could try to return a partial list.  If we did, though, then
                 * we'd have ensure we checked for NULL before dereferencing
                 * the filename pointer *EVERYWHERE* else.  Additionally, if
                 * we've really run out of memory, then other things are
                 * probably going to start failing pretty quickly.  In short,
                 * trying to salvage this situation isn't worth the hassle. */
            }
        }
    }

    LOGINFO("Total number of files owned by the current server: %d",
            num_local_files);
    LOGINFO("Total number of files returned from children: %d",
            results->num_files);
    LOGINFO("Return code from children: %d", results->ret);

    /* Need to merge the metadata for the local files with the metadata
     * returned by the RPC */
    if (num_local_files) {
        size_t new_size = (results->num_files + num_local_files) *
                          sizeof(unifyfs_file_attr_t);
        unifyfs_file_attr_t* merged_attr_list = realloc(attr_list, new_size);
        if (merged_attr_list) {
            attr_list = merged_attr_list;
        } else {
            LOGERR("Failed to realloc() file attr list!");
            ret = ENOMEM;
            goto Exit_Invoke_BMA;
            //TODO:  Make sure we clean this up in the event of an error!
        }
        memcpy(&attr_list[results->num_files], local_file_attrs,
               num_local_files * sizeof(unifyfs_file_attr_t));
    }

    // Make the results visible to the caller
    *file_attrs = attr_list;
    *num_file_attrs = results->num_files + num_local_files;

Exit_Invoke_BMA:
    /* If we hit an error somewhere, then there's a bunch of clean-up
     * that we need to do... */
    if (UNIFYFS_SUCCESS != ret) {
        if (HG_SUCCESS == bulk_transfer_hret) {
            /* We made it past the bulk transfer before failing, which means
             * we probably called strdup() on a bunch of filename strings.
             * Have to free all of those... */
            for (unsigned int i = 0; i < results->num_files; i++) {
                free(attr_list[i].filename);
            }
        }
        /* If there's been an error, we won't be returning the attr_list
         * pointer to the caller, so make sure it's freed. */
        free(attr_list);

        if (coll) {
            /* If we have a collective struct, then there's some more
             * clean-up to do:
             *
             * Depending on where any errors occurred above, the mutex
             * might have been left in a locked state.  Argobots doesn't
             * provide a way to test this, so we'll do a trylock() followed
             * by an unlock to ensure it's unlocked. */
            ABT_mutex_trylock(coll->resp_valid_sync);
            ABT_mutex_unlock(coll->resp_valid_sync);
        } else {
            /* If we never got as far as creating the collective, then just
             * free the input and output structs.  (These were all initialized
             * to NULL, so it's safe to call free() on them even if we never
             * actually got around to allocating them.) */
            free(in);
            free(out);
        }
    }

    if (local_file_attrs) {
        /* If we successfully allocated memory for local_file_attrs,
         * then we need to free it. */
        free(local_file_attrs);
    }

    if (HG_SUCCESS == bulk_create_hret) {
        /* If we successfully created the bulk, then we need to free it
         * (regardless of the overall success of the function). */
        margo_bulk_free(local_bulk);
    }

    if (coll) {
        /* If we successfully created the collective, then we need to
         * clean it up.  */
        collective_cleanup(coll);
    }

    return ret;
}
