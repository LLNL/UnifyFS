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

typedef struct {
    margo_request request;
    hg_handle_t   handle;
} unifyfs_coll_request_t;

/* helper method to initialize collective request rpc handle */
static int get_request_handle(hg_id_t request_hgid,
                              int peer_rank,
                              unifyfs_coll_request_t* creq)
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

/* helper method to forward collective rpc request */
static int forward_request(void* input_ptr,
                           unifyfs_coll_request_t* creq)
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

/* helper method to wait for collective rpc request completion */
static int wait_for_request(unifyfs_coll_request_t* creq)
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

/*
 * Broadcast file extents
 */

/**
 * @brief Blocking function to forward extent broadcast request
 *
 * @param broadcast_tree The tree for the broadcast
 * @param in Input data for the broadcast
 * @return int
 */
static int extbcast_request_forward(const unifyfs_tree_t* broadcast_tree,
                                    extbcast_request_in_t* in)
{
    LOGDBG("MARGOTREE: extent bcast forward");

    int rc;
    int ret = UNIFYFS_SUCCESS;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    /* allocate memory for request objects
     * TODO: possibly get this from memory pool */
    unifyfs_coll_request_t* requests = calloc(child_count,
                                              sizeof(*requests));
    /* forward request down the tree */
    int i;
    unifyfs_coll_request_t* req;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extbcast_request_id;
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
            /* get the output of the rpc */
            extbcast_request_out_t out;
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

/* update local extents for file given input data, and forward the
 * request to any children */
static void extbcast_request_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: extent bcast rpc handler");

    /* assume we'll succeed */

    int32_t ret = UNIFYFS_SUCCESS;

    /* get instance id */
    margo_instance_id mid = margo_hg_handle_get_instance(handle);

    /* get input params */
    extbcast_request_in_t in;
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
            hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extbcast_request_id;

            /* create communication tree structure */
            unifyfs_tree_t bcast_tree;
            unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                              UNIFYFS_BCAST_K_ARY, &bcast_tree);

            /* initiate data transfer */
            margo_request bulk_request;
            hret = margo_bulk_itransfer(mid, HG_BULK_PULL, client_address,
                                        in.exttree, 0,
                                        extent_data, 0,
                                        buf_size,
                                        &bulk_request);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_bulk_itransfer() failed");
                ret = UNIFYFS_ERROR_MARGO;
            }

            /* update input structure to point to local bulk handle */
            in.exttree = extent_data;

            /* allocate memory for request objects
             * TODO: possibly get this from memory pool */
            unifyfs_coll_request_t* requests =
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

                if (NULL != requests) {
                    /* wait for the requests to finish */
                    unifyfs_coll_request_t* req;
                    for (i = 0; i < bcast_tree.child_count; i++) {
                        req = requests + i;
                        rc = wait_for_request(req);
                        if (rc == UNIFYFS_SUCCESS) {
                            /* get the output of the rpc */
                            extbcast_request_out_t out;
                            hret = margo_get_output(req->handle, &out);
                            if (hret != HG_SUCCESS) {
                                LOGERR("margo_get_output() failed");
                                ret = UNIFYFS_ERROR_MARGO;
                            } else {
                                /* set return value */
                                int child_ret = (int) out.ret;
                                LOGDBG("MARGOTREE: extbcast child[%d]"
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

            /* release communication tree resources */
            unifyfs_tree_free(&bcast_tree);
        }
        margo_free_input(handle, &in);
    }

    /* build our output values */
    extbcast_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(extbcast_request_rpc)

/**
 * @brief
 *
 * @return int UnifyFS return code
 */
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

    LOGDBG("broadcasting %u extents (%zu bytes) for gfid=%d)",
           len, (size_t)buf_size, gfid);

    /* create bulk data structure containing the extents
     * NOTE: bulk data is always read only at the root of the broadcast tree */
    hg_bulk_t extent_data;
    void* datap = (void*) extents;
    hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                                         &datap, &buf_size,
                                         HG_BULK_READ_ONLY, &extent_data);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* fill in input struct */
        extbcast_request_in_t in;
        in.root = (int32_t)glb_pmi_rank;
        in.gfid = gfid;
        in.num_extents = num_extents;
        in.exttree = extent_data;

        extbcast_request_forward(&bcast_tree, &in);

        /* free bulk data handle */
        margo_bulk_free(extent_data);
    }

    /* free tree resources and passed extents */
    unifyfs_tree_free(&bcast_tree);
    free(extents);

    return ret;
}

/*************************************************************************
 * filesize
 *************************************************************************/

static int filesize_forward(const unifyfs_tree_t* broadcast_tree,
                            filesize_in_t* in, hg_size_t* filesize)
{
    LOGDBG("MARGOTREE: filesize forward");

    hg_return_t hret;
    int i, rc;
    int ret = UNIFYFS_SUCCESS;

    hg_size_t _filesize = 0;
    unifyfs_inode_get_filesize(in->gfid, &_filesize);

    /* get info for tree */
    int child_count = broadcast_tree->child_count;
    if (child_count > 0) {
        int* child_ranks = broadcast_tree->child_ranks;

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests =
            calloc(child_count, sizeof(unifyfs_coll_request_t));
        if (!requests) {
            return ENOMEM;
        }

        LOGDBG("MARGOTREE: %d: sending filesize to %d children",
               glb_pmi_rank, child_count);

        /* forward request down the tree */
        unifyfs_coll_request_t* req;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(unifyfsd_rpc_context->rpcs.filesize_id,
                                    child, req);
            if (rc == UNIFYFS_SUCCESS) {
                /* invoke filesize request rpc on child */
                rc = forward_request((void*)in, req);
            }
        }

        /* wait for the requests to finish */
        for (i = 0; i < child_count; i++) {
            req = requests + i;
            rc = wait_for_request(req);
            if (rc == UNIFYFS_SUCCESS) {
                /* get the output of the rpc */
                filesize_out_t out;
                hret = margo_get_output(req->handle, &out);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_get_output() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* set return values */
                    int child_ret = out.ret;
                    LOGDBG("MARGOTREE: filesize child[%d] response: %d",
                            i, child_ret);
                    if (child_ret == UNIFYFS_SUCCESS) {
                        LOGDBG("MARGOTREE: filesize child[%d] size: %lu",
                               i, (unsigned long)out.filesize);
                        if (out.filesize > _filesize) {
                            /* QUESTION: why is max always preferred? */
                            _filesize = out.filesize;
                        }
                    } else {
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

    *filesize = _filesize;
    return ret;
}

static void filesize_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: filesize rpc handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;
    hg_size_t filesize = 0;

    /* get input params */
    filesize_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = filesize_forward(&bcast_tree, &in, &filesize);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    filesize_out_t out;
    out.ret = ret;
    out.filesize = filesize;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(filesize_rpc)

int unifyfs_invoke_filesize_rpc(int gfid, size_t* filesize)
{
    LOGDBG("MARGOTREE: invoke filesize");

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    filesize_in_t in;
    in.root = (int32_t) glb_pmi_rank;
    in.gfid = gfid;

    hg_size_t _filesize = 0;

    ret = filesize_forward(&bcast_tree, &in, &_filesize);
    if (ret) {
        LOGERR("filesize_forward failed: (ret=%d)", ret);
    } else {
        *filesize = _filesize;
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

/*************************************************************************
 * truncate
 *************************************************************************/

static
int truncate_forward(const unifyfs_tree_t* broadcast_tree, truncate_in_t* in)
{
    LOGDBG("MARGOTREE: truncate forward - gfid=%d size=%lu",
           (int)in->gfid, (unsigned long)in->filesize);

    hg_return_t hret;
    int rc, ret;
    int i;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    ret = unifyfs_inode_truncate(in->gfid, in->filesize);
    if (ret) {
        LOGERR("unifyfs_inode_truncate failed (gfid=%d, ret=%d)",
                in->gfid, ret);
        goto out;
    }


    if (child_count > 0) {
        LOGDBG("MARGOTREE: sending truncate to %d children",
               child_count);

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests = calloc(child_count,
                                      sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        unifyfs_coll_request_t* req;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: truncate child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(unifyfsd_rpc_context->rpcs.truncate_id,
                                    child, req);
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
                truncate_out_t out;
                hret = margo_get_output(req->handle, &out);
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

static void truncate_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: truncate rpc handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    truncate_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = truncate_forward(&bcast_tree, &in);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    truncate_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(truncate_rpc)

int unifyfs_invoke_truncate_rpc(int gfid, size_t filesize)
{
    LOGDBG("MARGOTREE: invoke truncate - gfid=%d filesize=%zu",
           gfid, filesize);

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    truncate_in_t in;
    in.root = (int32_t) glb_pmi_rank;
    in.gfid = gfid;
    in.filesize = filesize;

    ret = truncate_forward(&bcast_tree, &in);
    if (ret) {
        LOGERR("truncate_forward failed: (ret=%d)", ret);
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

/*************************************************************************
 * metaset
 *************************************************************************/

static
int metaset_forward(const unifyfs_tree_t* broadcast_tree, metaset_in_t* in)
{
    LOGDBG("MARGOTREE: metaset forward");

    hg_return_t hret;
    int rc, ret;
    int i;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    ret = unifyfs_inode_metaset(in->gfid, in->create, &in->attr);
    if (ret) {
        goto out;
    }

    if (child_count > 0) {
        LOGDBG("MARGOTREE: %d: sending metaset to %d children",
               glb_pmi_rank, child_count);

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests = calloc(child_count,
                                      sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        unifyfs_coll_request_t* req;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: metaset child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(unifyfsd_rpc_context->rpcs.metaset_id,
                                    child, req);
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
                metaset_out_t out;
                hret = margo_get_output(req->handle, &out);
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

static void metaset_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: metaset rpc handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    metaset_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = metaset_forward(&bcast_tree, &in);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    metaset_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(metaset_rpc)

int unifyfs_invoke_metaset_rpc(int gfid, int create,
                                unifyfs_file_attr_t* fattr)
{
    LOGDBG("MARGOTREE: invoke metaset");

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    metaset_in_t in;
    in.root = (int32_t) glb_pmi_rank;
    in.gfid = gfid;
    in.create = create;
    in.attr = *fattr;

    ret = metaset_forward(&bcast_tree, &in);
    if (ret) {
        LOGERR("metaset_forward failed: (ret=%d)", ret);
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

/*************************************************************************
 * unlink
 *************************************************************************/

static
int unlink_forward(const unifyfs_tree_t* broadcast_tree, unlink_in_t* in)
{
    LOGDBG("MARGOTREE: unlink forward");

    hg_return_t hret;
    int rc, ret;
    int i;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    ret = unifyfs_inode_unlink(in->gfid);
    if (ret) {
        goto out;
    }

    if (child_count > 0) {
        LOGDBG("MARGOTREE: %d: sending unlink to %d children",
               glb_pmi_rank, child_count);

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests = calloc(child_count,
                                      sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        unifyfs_coll_request_t* req;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: unlink child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(unifyfsd_rpc_context->rpcs.unlink_id,
                                    child, req);
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
                unlink_out_t out;
                hret = margo_get_output(req->handle, &out);
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

static void unlink_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: unlink rpc handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    unlink_in_t in;

    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = unlink_forward(&bcast_tree, &in);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    unlink_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unlink_rpc)

int unifyfs_invoke_unlink_rpc(int gfid)
{
    LOGDBG("MARGOTREE: invoke unlink");

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    unlink_in_t in;
    in.root = (int32_t) glb_pmi_rank;
    in.gfid = gfid;

    ret = unlink_forward(&bcast_tree, &in);
    if (ret) {
        LOGERR("unlink_forward failed: (ret=%d)", ret);
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

/*************************************************************************
 * laminate
 *************************************************************************/

static
int laminate_forward(const unifyfs_tree_t* broadcast_tree, laminate_in_t* in)
{
    LOGDBG("MARGOTREE: laminate forward");

    hg_return_t hret;
    int rc, ret;
    int i;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    ret = unifyfs_inode_laminate(in->gfid);
    if (ret) {
        goto out;
    }

    if (child_count > 0) {
        LOGDBG("MARGOTREE: %d: sending laminate to %d children",
               glb_pmi_rank, child_count);

        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests =
            calloc(child_count, sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        unifyfs_coll_request_t* req;
        for (i = 0; i < child_count; i++) {
            req = requests + i;

            /* get rank of this child */
            int child = child_ranks[i];
            LOGDBG("MARGOTREE: laminate child[%d] is rank %d - %s",
                   i, child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            rc = get_request_handle(unifyfsd_rpc_context->rpcs.laminate_id,
                                    child, req);
            if (rc == UNIFYFS_SUCCESS) {
                /* invoke laminate request rpc on child */
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
                laminate_out_t out;
                hret = margo_get_output(req->handle, &out);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_get_output() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* set return value */
                    int child_ret = out.ret;
                    LOGDBG("MARGOTREE: laminate child[%d] response: ret=%d",
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

static void laminate_rpc(hg_handle_t handle)
{
    LOGDBG("MARGOTREE: laminate rpc handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    laminate_in_t in;

    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* create communication tree */
        unifyfs_tree_t bcast_tree;
        unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                        UNIFYFS_BCAST_K_ARY, &bcast_tree);

        ret = laminate_forward(&bcast_tree, &in);

        unifyfs_tree_free(&bcast_tree);
        margo_free_input(handle, &in);
    }

    /* build our output values */
    laminate_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(laminate_rpc)

int unifyfs_invoke_laminate_rpc(int gfid)
{
    LOGDBG("MARGOTREE: invoke laminate");

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* fill in input struct */
    laminate_in_t in;

    in.root = (int32_t) glb_pmi_rank;
    in.gfid = gfid;

    ret = laminate_forward(&bcast_tree, &in);
    if (ret) {
        LOGERR("laminate_forward failed: (ret=%d)", ret);
    }

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

