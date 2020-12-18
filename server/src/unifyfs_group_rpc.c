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
#include "unifyfs_tree.h"
#include "margo_server.h"
#include "unifyfs_server_rpcs.h"
#include "unifyfs_group_rpc.h"

#ifndef UNIFYFS_BCAST_K_ARY
# define UNIFYFS_BCAST_K_ARY 2
#endif

/* server collective (coll) margo request structure */
typedef struct {
    margo_request request;
    hg_handle_t   handle;
} coll_request;

/* helper method to initialize collective request rpc handle for child peer */
static int get_request_handle(hg_id_t request_hgid,
                              int peer_rank,
                              coll_request* creq)
{
    int rc = UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[peer_rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
                                    request_hgid, &(creq->handle));
    if (hret != HG_SUCCESS) {
        LOGERR("failed to get handle for request(%p) to server %d",
               creq, peer_rank);
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/* helper method to forward collective rpc request to one child */
static int forward_request(void* input_ptr,
                           coll_request* creq)
{
    int rc = UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_iforward(creq->handle, input_ptr,
                                      &(creq->request));
    if (hret != HG_SUCCESS) {
        LOGERR("failed to forward request(%p)", creq);
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/* helper method to wait for collective rpc child request completion */
static int wait_for_request(coll_request* creq)
{
    int rc = UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_wait(creq->request);
    if (hret != HG_SUCCESS) {
        LOGERR("wait on request(%p) failed", creq);
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/*************************************************************************
 * Broadcast file extents metadata
 *************************************************************************/


/* file extents metadata broadcast rpc handler */
static void extent_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: extent bcast handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get instance id */
    margo_instance_id mid = margo_hg_handle_get_instance(handle);

    /* get input params */
    extent_bcast_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* get root of tree and global file id to lookup filesize
         * record tag calling process wants us to include in our
         * later response */
        int gfid = (int) in.gfid;
        int32_t num_extents = (int32_t) in.num_extents;

        /* allocate memory for extents */
        struct extent_tree_node* extents;
        extents = calloc(num_extents, sizeof(struct extent_tree_node));

        /* get client address */
        const struct hg_info* info = margo_get_info(handle);
        hg_addr_t client_address = info->addr;

        /* expose local bulk buffer */
        hg_size_t buf_size = num_extents * sizeof(struct extent_tree_node);
        hg_bulk_t extent_data;
        void* datap = extents;
        hret = margo_bulk_create(mid, 1, &datap, &buf_size,
                                 HG_BULK_READWRITE, &extent_data);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_bulk_create() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            int i, rc;
            hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extent_bcast_id;

            /* create communication tree structure */
            unifyfs_tree_t bcast_tree;
            unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                              UNIFYFS_BCAST_K_ARY, &bcast_tree);

            /* initiate data transfer */
            margo_request bulk_request;
            hret = margo_bulk_itransfer(mid, HG_BULK_PULL, client_address,
                                        in.extents, 0,
                                        extent_data, 0,
                                        buf_size,
                                        &bulk_request);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_bulk_itransfer() failed");
                ret = UNIFYFS_ERROR_MARGO;
            }

            /* update input structure to point to local bulk handle */
            in.extents = extent_data;

            /* allocate memory for request objects
             * TODO: possibly get this from memory pool */
            coll_request* requests =
                calloc(bcast_tree.child_count, sizeof(*requests));
            if (NULL == requests) {
                ret = ENOMEM;
            } else {
                /* allocate mercury handles for forwarding the request */
                for (i = 0; i < bcast_tree.child_count; i++) {
                    /* allocate handle for request to this child */
                    int child = bcast_tree.child_ranks[i];
                    get_request_handle(req_hgid, child, requests+i);
                }
            }

            /* wait for data transfer to finish */
            hret = margo_wait(bulk_request);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_wait() for bulk transfer failed");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                LOGDBG("received %d extents (%zu bytes) from %d",
                       num_extents, (size_t)buf_size, (int)in.root);

                if (NULL != requests) {
                    /* forward request down the tree */
                    for (i = 0; i < bcast_tree.child_count; i++) {
                        /* invoke filesize request rpc on child */
                        rc = forward_request((void*)&in, requests+i);
                    }
                }

                ret = unifyfs_inode_add_extents(gfid, num_extents, extents);
                if (ret) {
                    LOGERR("add of remote extents failed (ret=%d)", ret);
                    // what do we do now?
                }
                LOGDBG("added %d extents (%zu bytes) from %d",
                       num_extents, (size_t)buf_size, (int)in.root);

                if (NULL != requests) {
                    /* wait for the requests to finish */
                    coll_request* req;
                    for (i = 0; i < bcast_tree.child_count; i++) {
                        req = requests + i;
                        rc = wait_for_request(req);
                        if (rc == UNIFYFS_SUCCESS) {
                            /* get the output of the rpc */
                            extent_bcast_out_t out;
                            hret = margo_get_output(req->handle, &out);
                            if (hret != HG_SUCCESS) {
                                LOGERR("margo_get_output() failed");
                                ret = UNIFYFS_ERROR_MARGO;
                            } else {
                                /* set return value */
                                int child_ret = (int) out.ret;
                                LOGDBG("MARGOTREE: extbcast child[%d] "
                                       "response: %d", i, child_ret);
                                if (child_ret != UNIFYFS_SUCCESS) {
                                    ret = child_ret;
                                }
                                margo_free_output(req->handle, &out);
                            }
                            margo_destroy(req->handle);
                        } else {
                            ret = rc;
                        }
                    }
                    free(requests);
                }
            }
            /* free bulk data handle */
            margo_bulk_free(extent_data);

            /* release communication tree resources */
            unifyfs_tree_free(&bcast_tree);
        }
        margo_free_input(handle, &in);
    }

    /* build our output values */
    extent_bcast_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    LOGDBG("MARGOTREE: extent bcast rpc handler - responded");

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(extent_bcast_rpc)

/* Forward the extent broadcast to all children and wait for responses */
static
int extent_bcast_forward(const unifyfs_tree_t* broadcast_tree,
                         extent_bcast_in_t* in)
{
    LOGDBG("MARGOTREE: extent bcast forward");

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    if (0 == child_count) {
        return UNIFYFS_SUCCESS;
    }

    int* child_ranks = broadcast_tree->child_ranks;

    /* allocate memory for request objects
     * TODO: possibly get this from memory pool */
    coll_request* requests = calloc(child_count,
                                    sizeof(*requests));

    /* forward request down the tree */
    int i, rc, ret;
    coll_request* req;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extent_bcast_id;
    for (i = 0; i < child_count; i++) {
        req = requests + i;

        /* allocate handle */
        rc = get_request_handle(req_hgid, child_ranks[i], req);
        if (rc == UNIFYFS_SUCCESS) {
            /* invoke extbcast request rpc on child */
            rc = forward_request((void*)in, req);
        } else {
            ret = rc;
        }
    }

    /* wait for the requests to finish */
    for (i = 0; i < child_count; i++) {
        req = requests + i;
        rc = wait_for_request(req);
        if (rc == UNIFYFS_SUCCESS) {
            LOGDBG("MARGOTREE: extent bcast - child[%d] responded", i);
            /* get the output of the rpc */
            extent_bcast_out_t out;
            hg_return_t hret = margo_get_output(req->handle, &out);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_get_output() failed");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                /* set return value */
                int child_ret = out.ret;
                if (child_ret != UNIFYFS_SUCCESS) {
                    ret = child_ret;
                }
                margo_free_output(req->handle, &out);
            }
            margo_destroy(req->handle);
        } else {
            ret = rc;
        }
    }

    return ret;
}

/* Execute broadcast tree for extent metadata */
int unifyfs_invoke_broadcast_extents_rpc(int gfid, unsigned int len,
                                         struct extent_tree_node* extents)
{
    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    hg_size_t num_extents = len;
    hg_size_t buf_size = num_extents * sizeof(*extents);

    LOGDBG("broadcasting %u extents for gfid=%d)",
           len, gfid);

    /* create bulk data structure containing the extents
     * NOTE: bulk data is always read only at the root of the broadcast tree */
    hg_bulk_t extents_bulk;
    void* datap = (void*) extents;
    hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                                         &datap, &buf_size,
                                         HG_BULK_READ_ONLY, &extents_bulk);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* fill in input struct */
        extent_bcast_in_t in;
        in.root = (int32_t)glb_pmi_rank;
        in.gfid = gfid;
        in.num_extents = num_extents;
        in.extents = extents_bulk;

        extent_bcast_forward(&bcast_tree, &in);

        /* free bulk data handle */
        margo_bulk_free(extents_bulk);
    }

    /* free tree resources and passed extents */
    unifyfs_tree_free(&bcast_tree);
    free(extents);

    return ret;
}

/*************************************************************************
 * Broadcast file attributes and extents metadata due to laminate
 *************************************************************************/

/* file extents metadata broadcast rpc handler */
static void laminate_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: laminate bcast handler");

    int32_t ret;

    /* get instance id */
    margo_instance_id mid = margo_hg_handle_get_instance(handle);

    /* get input params */
    laminate_bcast_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* get root of tree and global file id to lookup filesize
         * record tag calling process wants us to include in our
         * later response */
        int gfid = (int) in.gfid;
        size_t num_extents = (size_t) in.num_extents;
        unifyfs_file_attr_t* fattr = &(in.attr);

        /* allocate memory for extents */
        struct extent_tree_node* extents;
        extents = calloc(num_extents, sizeof(struct extent_tree_node));

        /* get client address */
        const struct hg_info* info = margo_get_info(handle);
        hg_addr_t client_address = info->addr;

        /* expose local bulk buffer */
        hg_size_t buf_size = num_extents * sizeof(struct extent_tree_node);
        hg_bulk_t extent_data;
        void* datap = extents;
        hret = margo_bulk_create(mid, 1, &datap, &buf_size,
                                 HG_BULK_READWRITE, &extent_data);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_bulk_create() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            int i, rc;
            hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.laminate_bcast_id;

            /* create communication tree structure */
            unifyfs_tree_t bcast_tree;
            unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                              UNIFYFS_BCAST_K_ARY, &bcast_tree);

            /* initiate data transfer */
            margo_request bulk_request;
            hret = margo_bulk_itransfer(mid, HG_BULK_PULL,
                                        client_address, in.extents, 0,
                                        extent_data, 0,
                                        buf_size, &bulk_request);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_bulk_itransfer() failed");
                ret = UNIFYFS_ERROR_MARGO;
            }

            /* allocate memory for request objects
             * TODO: possibly get this from memory pool */
            coll_request* requests =
                calloc(bcast_tree.child_count, sizeof(*requests));
            if (NULL == requests) {
                ret = ENOMEM;
            } else {
                /* allocate mercury handles for forwarding the request */
                for (i = 0; i < bcast_tree.child_count; i++) {
                    /* allocate handle for request to this child */
                    int child = bcast_tree.child_ranks[i];
                    get_request_handle(req_hgid, child, requests+i);
                }
            }

            /* wait for data transfer to finish */
            hret = margo_wait(bulk_request);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_wait() for bulk transfer failed");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                LOGDBG("laminating gfid=%d, received %zu extents from %d",
                       gfid, num_extents, (int)in.root);

                if (NULL != requests) {
                    /* update input structure to point to local bulk handle */
                    in.extents = extent_data;

                    /* forward request down the tree */
                    for (i = 0; i < bcast_tree.child_count; i++) {
                        /* invoke filesize request rpc on child */
                        rc = forward_request((void*)&in, requests+i);
                    }
                }

                /* add the final set of extents */
                ret = unifyfs_inode_add_extents(gfid, num_extents, extents);
                if (ret != UNIFYFS_SUCCESS) {
                    LOGERR("laminate extents update failed (ret=%d)", ret);
                }

                /* update attributes only after final extents added */
                ret = unifyfs_inode_metaset(gfid,
                                            UNIFYFS_FILE_ATTR_OP_LAMINATE,
                                            fattr);
                if (ret != UNIFYFS_SUCCESS) {
                    LOGERR("laminate attrs update failed (ret=%d)", ret);
                }

                if (NULL != requests) {
                    /* wait for the requests to finish */
                    coll_request* req;
                    for (i = 0; i < bcast_tree.child_count; i++) {
                        req = requests + i;
                        rc = wait_for_request(req);
                        if (rc == UNIFYFS_SUCCESS) {
                            /* get the output of the rpc */
                            laminate_bcast_out_t out;
                            hret = margo_get_output(req->handle, &out);
                            if (hret != HG_SUCCESS) {
                                LOGERR("margo_get_output() failed");
                                ret = UNIFYFS_ERROR_MARGO;
                            } else {
                                /* set return value */
                                int child_ret = (int) out.ret;
                                LOGDBG("MARGOTREE: laminate child[%d] "
                                        "response: %d", i, child_ret);
                                if (child_ret != UNIFYFS_SUCCESS) {
                                    ret = child_ret;
                                }
                                margo_free_output(req->handle, &out);
                            }
                            margo_destroy(req->handle);
                        } else {
                            ret = rc;
                        }
                    }
                    free(requests);
                }
            }
            /* free bulk data handle */
            margo_bulk_free(extent_data);

            /* release communication tree resources */
            unifyfs_tree_free(&bcast_tree);
        }
        margo_free_input(handle, &in);
    }

    /* build our output values */
    laminate_bcast_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    LOGDBG("MARGOTREE: laminate bcast handler - responded");

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(laminate_bcast_rpc)

/* Forward the laminate broadcast to all children and wait for responses */
static
int laminate_bcast_forward(const unifyfs_tree_t* broadcast_tree,
                           laminate_bcast_in_t* in)
{
    /* get info for tree */
    int* child_ranks = broadcast_tree->child_ranks;
    int child_count  = broadcast_tree->child_count;
    if (0 == child_count) {
        return UNIFYFS_SUCCESS;
    }

    int gfid = (int) in->gfid;
    LOGDBG("MARGOTREE: laminate bcast forward for gfid=%d", gfid);

    /* allocate memory for request objects
     * TODO: possibly get this from memory pool */
    coll_request* requests = calloc(child_count,
                                    sizeof(*requests));

    /* forward request down the tree */
    int i, rc, ret;
    coll_request* req;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.laminate_bcast_id;
    for (i = 0; i < child_count; i++) {
        req = requests + i;

        /* allocate handle */
        rc = get_request_handle(req_hgid, child_ranks[i], req);
        if (rc == UNIFYFS_SUCCESS) {
            /* invoke extbcast request rpc on child */
            rc = forward_request((void*)in, req);
        } else {
            ret = rc;
        }
    }

    /* wait for the requests to finish */
    for (i = 0; i < child_count; i++) {
        req = requests + i;
        rc = wait_for_request(req);
        if (rc == UNIFYFS_SUCCESS) {
            LOGDBG("MARGOTREE: laminate bcast - child[%d] responded", i);
            /* get the output of the rpc */
            laminate_bcast_out_t out;
            hg_return_t hret = margo_get_output(req->handle, &out);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_get_output() failed");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                /* set return value */
                int child_ret = out.ret;
                if (child_ret != UNIFYFS_SUCCESS) {
                    ret = child_ret;
                }
                margo_free_output(req->handle, &out);
            }
            margo_destroy(req->handle);
        } else {
            ret = rc;
        }
    }

    return ret;
}

/* Execute broadcast tree for attributes and extent metadata due to laminate */
int unifyfs_invoke_broadcast_laminate(int gfid)
{
    int ret;

    LOGDBG("broadcasting laminate for gfid=%d", gfid);

    /* get attributes and extents metadata */
    unifyfs_file_attr_t attrs;
    ret = unifyfs_inode_metaget(gfid, &attrs);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to get file attributes for gfid=%d", gfid);
        return ret;
    }

    size_t n_extents;
    struct extent_tree_node* extents;
    ret = unifyfs_inode_get_extents(gfid, &n_extents, &extents);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to get extents for gfid=%d", gfid);
        return ret;
    }

    /* create bulk data structure containing the extents
     * NOTE: bulk data is always read only at the root of the broadcast tree */
    hg_size_t num_extents = n_extents;
    hg_size_t buf_size = num_extents * sizeof(*extents);
    hg_bulk_t extents_bulk;
    void* datap = (void*) extents;
    hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                                         &datap, &buf_size,
                                         HG_BULK_READ_ONLY, &extents_bulk);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create broadcast communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                          UNIFYFS_BCAST_K_ARY, &bcast_tree);

        /* fill input struct and forward */
        laminate_bcast_in_t in;
        in.root = (int32_t) glb_pmi_rank;
        in.gfid = (int32_t) gfid;
        in.attr = attrs;
        in.num_extents = (int32_t) num_extents;
        in.extents = extents_bulk;
        laminate_bcast_forward(&bcast_tree, &in);

        /* free tree resources */
        unifyfs_tree_free(&bcast_tree);

        /* free bulk data handle */
        margo_bulk_free(extents_bulk);
    }

    /* free extents array */
    free(extents);

    return ret;
}


/*************************************************************************
 * Broadcast file truncation
 *************************************************************************/

/* Forward the truncate broadcast to all children and wait for responses */
static
int truncate_bcast_forward(const unifyfs_tree_t* broadcast_tree,
                           truncate_bcast_in_t* in)
{
    int i, rc, ret;
    int gfid = (int) in->gfid;
    size_t fsize = (size_t) in->filesize;
    LOGDBG("MARGOTREE: truncate bcast forward - gfid=%d size=%zu",
           gfid, fsize);

    /* apply truncation to local file state */
    ret = unifyfs_inode_truncate(gfid, (unsigned long)fsize);
    if (ret != UNIFYFS_SUCCESS) {
        /* owner is root of broadcast tree */
        int is_owner = ((int)(in->root) == glb_pmi_rank);
        if ((ret == ENOENT) && !is_owner) {
            /* it's ok if inode doesn't exist at non-owners */
            ret = UNIFYFS_SUCCESS;
        } else {
            LOGERR("unifyfs_inode_truncate(gfid=%d, size=%zu) failed - ret=%d",
                   gfid, fsize, ret);
            goto out;
        }
    }

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;
    if (child_count > 0) {
        LOGDBG("MARGOTREE: sending truncate to %d children",
               child_count);

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        coll_request* requests = calloc(child_count,
                                      sizeof(coll_request));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        coll_request* req;
        hg_id_t hgid = unifyfsd_rpc_context->rpcs.truncate_bcast_id;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: truncate child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(hgid, child, req);
            if (rc == UNIFYFS_SUCCESS) {
                /* invoke truncate request rpc on child */
                rc = forward_request((void*)in, req);
            } else {
                ret = rc;
            }
        }

        /* wait for the requests to finish */
        for (i = 0; i < child_count; i++) {
            req = requests + i;
            rc = wait_for_request(req);
            if (rc == UNIFYFS_SUCCESS) {
                /* get the output of the rpc */
                truncate_bcast_out_t out;
                hg_return_t hret = margo_get_output(req->handle, &out);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_get_output() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* set return value */
                    int child_ret = out.ret;
                    LOGDBG("MARGOTREE: truncate child[%d] response: ret=%d",
                           i, child_ret);
                    if (child_ret != UNIFYFS_SUCCESS) {
                        ret = child_ret;
                    }
                    margo_free_output(req->handle, &out);
                }
                margo_destroy(req->handle);
            } else {
                ret = rc;
            }
        }

        free(requests);
    }

out:
    return ret;
}

/* truncate broadcast rpc handler */
static void truncate_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: truncate bcast handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    truncate_bcast_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = truncate_bcast_forward(&bcast_tree, &in);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    truncate_bcast_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(truncate_bcast_rpc)

/* Execute broadcast tree for file truncate */
int unifyfs_invoke_broadcast_truncate(int gfid, size_t filesize)
{
    LOGDBG("broadcasting truncate for gfid=%d filesize=%zu",
           gfid, filesize);

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    truncate_bcast_in_t in;
    in.root = (int32_t) glb_pmi_rank;
    in.gfid = gfid;
    in.filesize = filesize;

    ret = truncate_bcast_forward(&bcast_tree, &in);
    if (ret) {
        LOGERR("truncate_bcast_forward failed: (ret=%d)", ret);
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

/*************************************************************************
 * Broadcast updates to file attributes
 *************************************************************************/

/* Forward the fileattr broadcast to all children and wait for responses */
static
int fileattr_bcast_forward(const unifyfs_tree_t* broadcast_tree,
                           fileattr_bcast_in_t* in)
{
    int i, rc, ret;
    int gfid = (int) in->gfid;

    LOGDBG("MARGOTREE: fileattr bcast forward (gfid=%d)", gfid);

    /* set local metadata for target file */
    ret = unifyfs_inode_metaset(gfid, in->attrop, &in->attr);
    if (ret) {
        goto out;
    }

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;
    if (child_count > 0) {
        LOGDBG("MARGOTREE: %d: sending metaset to %d children",
               glb_pmi_rank, child_count);

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        coll_request* requests = calloc(child_count,
                                      sizeof(coll_request));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        coll_request* req;
        hg_id_t hgid = unifyfsd_rpc_context->rpcs.fileattr_bcast_id;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: metaset child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(hgid, child, req);
            if (rc == UNIFYFS_SUCCESS) {
                /* invoke metaset request rpc on child */
                rc = forward_request((void*)in, req);
            } else {
                ret = rc;
            }
        }

        /* wait for the requests to finish */
        for (i = 0; i < child_count; i++) {
            req = requests + i;
            rc = wait_for_request(req);
            if (rc == UNIFYFS_SUCCESS) {
                /* get the output of the rpc */
                fileattr_bcast_out_t out;
                hg_return_t hret = margo_get_output(req->handle, &out);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_get_output() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* set return value */
                    int child_ret = out.ret;
                    LOGDBG("MARGOTREE: metaset child[%d] response: ret=%d",
                           i, child_ret);
                    if (child_ret != UNIFYFS_SUCCESS) {
                        ret = child_ret;
                    }
                    margo_free_output(req->handle, &out);
                }
                margo_destroy(req->handle);
            } else {
                ret = rc;
            }
        }

        free(requests);
    }
out:
    return ret;
}

/* file attributes broadcast rpc handler */
static void fileattr_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: fileattr bcast handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    fileattr_bcast_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = fileattr_bcast_forward(&bcast_tree, &in);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    fileattr_bcast_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(fileattr_bcast_rpc)

/* Execute broadcast tree for file attributes update */
int unifyfs_invoke_broadcast_fileattr(int gfid,
                                      int attr_op,
                                      unifyfs_file_attr_t* fattr)
{
    LOGDBG("broadcasting file attributes for gfid=%d", gfid);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    fileattr_bcast_in_t in;
    in.root = (int32_t) glb_pmi_rank;
    in.gfid = gfid;
    in.attrop = attr_op;
    in.attr = *fattr;

    int ret = fileattr_bcast_forward(&bcast_tree, &in);
    if (ret) {
        LOGERR("fileattr_bcast_forward failed: (ret=%d)", ret);
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

/*************************************************************************
 * Broadcast file unlink
 *************************************************************************/

/* Forward the unlink broadcast to all children and wait for responses */
static
int unlink_bcast_forward(const unifyfs_tree_t* broadcast_tree,
                         unlink_bcast_in_t* in)
{
    int i, rc, ret;
    int gfid = (int) in->gfid;

    LOGDBG("MARGOTREE: unlink bcast forward (gfid=%d)", gfid);

    /* remove local file metadata */
    ret = unifyfs_inode_unlink(in->gfid);
    if (ret) {
        goto out;
    }

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;
    if (child_count > 0) {
        LOGDBG("MARGOTREE: %d: sending unlink to %d children",
               glb_pmi_rank, child_count);

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        coll_request* requests = calloc(child_count,
                                      sizeof(coll_request));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        coll_request* req;
        hg_id_t hgid = unifyfsd_rpc_context->rpcs.unlink_bcast_id;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: unlink child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(hgid, child, req);
            if (rc == UNIFYFS_SUCCESS) {
                /* invoke unlink request rpc on child */
                rc = forward_request((void*)in, req);
            } else {
                ret = rc;
            }
        }

        /* wait for the requests to finish */
        for (i = 0; i < child_count; i++) {
            req = requests + i;
            rc = wait_for_request(req);
            if (rc == UNIFYFS_SUCCESS) {
                /* get the output of the rpc */
                unlink_bcast_out_t out;
                hg_return_t hret = margo_get_output(req->handle, &out);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_get_output() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* set return value */
                    int child_ret = out.ret;
                    LOGDBG("MARGOTREE: unlink child[%d] response: ret=%d",
                           i, child_ret);
                    if (child_ret != UNIFYFS_SUCCESS) {
                        ret = child_ret;
                    }
                    margo_free_output(req->handle, &out);
                }
                margo_destroy(req->handle);
            } else {
                ret = rc;
            }
        }

        free(requests);
    }

out:
    return ret;
}

/* unlink broacast rpc handler */
static void unlink_bcast_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: unlink bcast handler");

    int32_t ret;

    /* get input params */
    unlink_bcast_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = unlink_bcast_forward(&bcast_tree, &in);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    unlink_bcast_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unlink_bcast_rpc)

/* Execute broadcast tree for file unlink */
int unifyfs_invoke_broadcast_unlink(int gfid)
{
    LOGDBG("broadcasting unlink for gfid=%d", gfid);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    unlink_bcast_in_t in;
    in.root = (int32_t) glb_pmi_rank;
    in.gfid = (int32_t) gfid;

    int ret = unlink_bcast_forward(&bcast_tree, &in);
    if (ret) {
        LOGERR("unlink_bcast_forward failed: (ret=%d)", ret);
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}
