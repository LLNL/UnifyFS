/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "unifyfs_global.h"
#include "unifyfs_tree.h"
#include "margo_server.h"
#include "unifyfs_server_rpcs.h"
#include "unifyfs_group_rpc.h"

/*
 * Broadcast operation for file extends
 */
typedef struct {
    margo_request request;
    hg_handle_t   handle;
} unifyfs_coll_request_t;

static
int rpc_allocate_extbcast_handle(int rank, unifyfs_coll_request_t* request)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, addr,
        unifyfsd_rpc_context->rpcs.extbcast_request_id, &request->handle);
    assert(hret == HG_SUCCESS);

    return rc;
}

static int rpc_invoke_extbcast_request(extbcast_request_in_t* in,
                                       unifyfs_coll_request_t* request)
{
    int rc = (int)UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_iforward(request->handle, in, &request->request);
    assert(hret == HG_SUCCESS);

    return rc;
}

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
    printf("%d: BUCKEYES request_forward\n", glb_pmi_rank);
    fflush(stdout);

    int ret;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    /* allocate memory for request objects
     * TODO: possibly get this from memory pool */
    unifyfs_coll_request_t* requests = calloc(child_count,
                                              sizeof(unifyfs_coll_request_t));
    /* forward request down the tree */
    int i;
    for (i = 0; i < child_count; i++) {
        /* get rank of this child */
        int child = child_ranks[i];

        /* allocate handle */
        ret = rpc_allocate_extbcast_handle(child, &requests[i]);

        /* invoke filesize request rpc on child */
        ret = rpc_invoke_extbcast_request(in, &requests[i]);
    }

    /* wait for the requests to finish */
    extbcast_request_out_t out;
    for (i = 0; i < child_count; i++) {
        hg_return_t hret;

        /* TODO: get outputs */
        hret = margo_wait(requests[i].request);
        assert(HG_SUCCESS == hret);

        /* get the output of the rpc */
        hret = margo_get_output(requests[i].handle, &out);
        assert(HG_SUCCESS == hret);

        /* set return value
         * TODO: check if we have an error and handle it */
        ret = out.ret;
    }

    return ret;
}

#define UNIFYFS_BCAST_K_ARY 2

/* request a filesize operation to all servers for a given file
 * from a given server */
static void extbcast_request_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES request_rpc (extbcast)\n", glb_pmi_rank);
    fflush(stdout);

    hg_return_t hret;

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get instance id */
    margo_instance_id mid = margo_hg_handle_get_instance(handle);

    /* get input params */
    extbcast_request_in_t in;
    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* get root of tree and global file id to lookup filesize
     * record tag calling process wants us to include in our
     * later response */
    int gfid     = (int) in.gfid;
    int32_t num_extents = (int32_t) in.num_extends;

    /* allocate memory for extends */
    struct extent_tree_node* extents;
    extents = calloc(num_extents, sizeof(struct extent_tree_node));

    /* get client address */
    const struct hg_info* info = margo_get_info(handle);
    hg_addr_t client_address = info->addr;

    hg_size_t buf_size = num_extents*sizeof(struct extent_tree_node);

    /* expose local bulk buffer */
    hg_bulk_t extent_data;
    void* datap = extents;
>>>>>>> Implementation of collective operations in the server for removing mdhim
    margo_bulk_create(mid, 1, &datap, &buf_size,
                      HG_BULK_READWRITE, &extent_data);

    /* request for bulk transfer */
    margo_request bulk_request;

    /* initiate data transfer
     * TODO: see if we can make this asynchronous */
    margo_bulk_itransfer(mid, HG_BULK_PULL, client_address,
                         in.exttree, 0,
                         extent_data, 0,
                         buf_size,
                         &bulk_request);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    /* update input structure to point to local bulk handle */
    in.exttree = extent_data;

    /* allocate memory for request objects
     * TODO: possibly get this from memory pool */
    unifyfs_coll_request_t* requests =
        malloc(sizeof(unifyfs_coll_request_t) * bcast_tree.child_count);

    /* allogate mercury handles for forwarding the request */
    int i;
    for (i = 0; i < bcast_tree.child_count; i++) {
        /* get rank of this child */
        int child = bcast_tree.child_ranks[i];
        /* allocate handle */
        ret = rpc_allocate_extbcast_handle(child, &requests[i]);
    }

    /* wait for bulk request to finish */
    hret = margo_wait(bulk_request);

    LOGDBG("received %d extents (%lu bytes) from %d",
           num_extents, buf_size, in.root);

    /* forward request down the tree */
    for (i = 0; i < bcast_tree.child_count; i++) {
        /* invoke filesize request rpc on child */
        ret = rpc_invoke_extbcast_request(&in, &requests[i]);
    }

    ret = unifyfs_inode_add_remote_extents(gfid, num_extents, extents);
    if (ret) {
        LOGERR("filling remote extent failed (ret=%d)\n", ret);
        // what do we do now?
    }

    /* wait for the requests to finish */
    for (i = 0; i < bcast_tree.child_count; i++) {
        extbcast_request_out_t out;
        /* TODO: get outputs */
        hret = margo_wait(requests[i].request);

        /* get the output of the rpc */
        hret = margo_get_output(requests[i].handle, &out);

        /* set return value
         * TODO: check if we have an error and handle it */
        ret = out.ret;
    }

    unifyfs_tree_free(&bcast_tree);

    /* build our output values */
    extbcast_request_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    free(requests);
}
DEFINE_MARGO_RPC_HANDLER(extbcast_request_rpc)

/**
 * @brief
 *
 * @return int UnifyFS return code
 */
int unifyfs_broadcast_extent_tree(int gfid)
{
    LOGDBG("%d: BUCKEYES unifyfs_broadcast_extend_tree\n", glb_pmi_rank);

    /* assuming success */
    int ret = UNIFYFS_SUCCESS;

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, glb_pmi_rank,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    hg_size_t num_extents = 0;
    struct extent_tree_node* extents = NULL;

    ret = unifyfs_inode_get_local_extents(gfid, &num_extents, &extents);
    if (ret) {
        LOGERR("reading all extents failed (gfid=%d, ret=%d)\n", gfid, ret);
        // abort function?
    }

    hg_size_t buf_size = num_extents * sizeof(*extents);

    LOGDBG("broadcasting %lu extents (%lu bytes): ", num_extents, buf_size);

    /* create bulk data structure containing the extends
     * NOTE: bulk data is always read only at the root of the broadcast tree */
    hg_bulk_t extent_data;
    void* datap = (void*) extents;
    margo_bulk_create(unifyfsd_rpc_context->svr_mid, 1,
                      &datap, &buf_size,
                      HG_BULK_READ_ONLY, &extent_data);

    /* fill in input struct */
    extbcast_request_in_t in;
    in.root = (int32_t)glb_pmi_rank;
    in.gfid = gfid;
    in.num_extends = num_extents;
    in.exttree = extent_data;

    /*  */
    extbcast_request_forward(&bcast_tree, &in);

    /* free bulk data handle */
    margo_bulk_free(extent_data);
    free(extents);

    unifyfs_tree_free(&bcast_tree);

    return ret;
}

/*************************************************************************
 * filesize
 *************************************************************************/

static int filesize_forward(const unifyfs_tree_t* broadcast_tree,
                            filesize_in_t* in, hg_size_t* filesize)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;
    int ret;
    int i;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    hg_size_t _filesize = 0;

    ret = unifyfs_inode_get_filesize(in->gfid, &_filesize);
    if (ret) {
        goto out;
    }

    printf("%d:BUCKEYES sending filesize to %d children\n",
            glb_pmi_rank, child_count);

    if (child_count > 0) {
        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests = calloc(child_count,
                                      sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        for (i = 0; i < child_count; i++) {
            /* get rank of this child */
            int child = child_ranks[i];
            unifyfs_coll_request_t* req = &requests[i];

            printf("children[%d]: %s\n",
                   child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            hret = margo_create(unifyfsd_rpc_context->svr_mid,
                                glb_servers[child].margo_svr_addr,
                                unifyfsd_rpc_context->rpcs.filesize_id,
                                &req->handle);
            assert(hret == HG_SUCCESS);

            /* invoke filesize request rpc on child */
            hret = margo_iforward(req->handle, in, &req->request);
            assert(hret == HG_SUCCESS);
        }

        /* wait for the requests to finish */
        filesize_out_t out;
        for (i = 0; i < child_count; i++) {
            /* TODO: get outputs */
            hret = margo_wait(requests[i].request);

            /* get the output of the rpc */
            hret = margo_get_output(requests[i].handle, &out);

            printf("get response: ret=%d, filesize=%lu\n",
                    out.ret, out.filesize);

            /* set return value
             * TODO: check if we have an error and handle it */
            ret = out.ret;
            _filesize = out.filesize > _filesize ? out.filesize : _filesize;
        }

        free(requests);
    }

    *filesize = _filesize;
out:
    return ret;
}

static void filesize_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    filesize_in_t in;
    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    hg_size_t filesize = 0;
    ret = filesize_forward(&bcast_tree, &in, &filesize);

    /* build our output values */
    filesize_out_t out;
    out.ret = ret;
    out.filesize = filesize;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    unifyfs_tree_free(&bcast_tree);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(filesize_rpc)

int unifyfs_invoke_filesize_rpc(int gfid, size_t* filesize)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

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
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;
    int ret;
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

    printf("%d:BUCKEYES sending truncate to %d children\n",
            glb_pmi_rank, child_count);

    if (child_count > 0) {
        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests = calloc(child_count,
                                      sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        for (i = 0; i < child_count; i++) {
            /* get rank of this child */
            int child = child_ranks[i];
            unifyfs_coll_request_t* req = &requests[i];

            printf("children[%d]: %s\n",
                   child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            hret = margo_create(unifyfsd_rpc_context->svr_mid,
                                glb_servers[child].margo_svr_addr,
                                unifyfsd_rpc_context->rpcs.truncate_id,
                                &req->handle);
            assert(hret == HG_SUCCESS);

            /* invoke filesize request rpc on child */
            hret = margo_iforward(req->handle, in, &req->request);
            assert(hret == HG_SUCCESS);
        }

        /* wait for the requests to finish */
        truncate_out_t out;
        for (i = 0; i < child_count; i++) {
            /* TODO: get outputs */
            hret = margo_wait(requests[i].request);

            /* get the output of the rpc */
            hret = margo_get_output(requests[i].handle, &out);

            printf("get response: ret=%d\n", out.ret);

            /* set return value
             * TODO: check if we have an error and handle it */
            ret = out.ret;
        }

        free(requests);
    }

out:
    return ret;
}

static void truncate_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    truncate_in_t in;
    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    ret = truncate_forward(&bcast_tree, &in);

    /* build our output values */
    truncate_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    unifyfs_tree_free(&bcast_tree);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(truncate_rpc)

int unifyfs_invoke_truncate_rpc(int gfid, size_t filesize)
{
    LOGDBG("%d: BUCKEYES truncate\n", glb_pmi_rank);

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
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;
    int ret;
    int i;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    ret = unifyfs_inode_metaset(in->gfid, in->create, &in->attr);
    if (ret) {
        goto out;
    }

    printf("%d:BUCKEYES sending metaset to %d children\n",
            glb_pmi_rank, child_count);

    if (child_count > 0) {
        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests = calloc(child_count,
                                      sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        for (i = 0; i < child_count; i++) {
            /* get rank of this child */
            int child = child_ranks[i];
            unifyfs_coll_request_t* req = &requests[i];

            printf("children[%d]: %s\n",
                   child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            hret = margo_create(unifyfsd_rpc_context->svr_mid,
                                glb_servers[child].margo_svr_addr,
                                unifyfsd_rpc_context->rpcs.metaset_id,
                                &req->handle);
            assert(hret == HG_SUCCESS);

            /* invoke filesize request rpc on child */
            hret = margo_iforward(req->handle, in, &req->request);
            assert(hret == HG_SUCCESS);
        }

        /* wait for the requests to finish */
        metaset_out_t out;
        for (i = 0; i < child_count; i++) {
            /* TODO: get outputs */
            hret = margo_wait(requests[i].request);

            /* get the output of the rpc */
            hret = margo_get_output(requests[i].handle, &out);

            printf("get response: ret=%d", out.ret);

            /* set return value
             * TODO: check if we have an error and handle it */
            ret = out.ret;
        }

        free(requests);
    }
out:
    return ret;
}

static void metaset_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    metaset_in_t in;
    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    ret = metaset_forward(&bcast_tree, &in);

    /* build our output values */
    metaset_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    unifyfs_tree_free(&bcast_tree);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(metaset_rpc)

int unifyfs_invoke_metaset_rpc(int gfid, int create,
                                unifyfs_file_attr_t* fattr)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

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
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;
    int ret;
    int i;

    /* get info for tree */
    int child_count  = broadcast_tree->child_count;
    int* child_ranks = broadcast_tree->child_ranks;

    ret = unifyfs_inode_unlink(in->gfid);
    if (ret) {
        goto out;
    }

    printf("%d:BUCKEYES sending unlink to %d children\n",
            glb_pmi_rank, child_count);

    if (child_count > 0) {
        /* allocate memory for request objects
         * TODO: possibly get this from memory pool */
        unifyfs_coll_request_t* requests = calloc(child_count,
                                      sizeof(unifyfs_coll_request_t));
        if (!requests) {
            ret = ENOMEM;
            goto out;
        }

        /* forward request down the tree */
        for (i = 0; i < child_count; i++) {
            /* get rank of this child */
            int child = child_ranks[i];
            unifyfs_coll_request_t* req = &requests[i];

            printf("children[%d]: %s\n",
                   child, glb_servers[child].margo_svr_addr_str);

            /* allocate handle */
            hret = margo_create(unifyfsd_rpc_context->svr_mid,
                                glb_servers[child].margo_svr_addr,
                                unifyfsd_rpc_context->rpcs.unlink_id,
                                &req->handle);
            assert(hret == HG_SUCCESS);

            hret = margo_iforward(req->handle, in, &req->request);
            assert(hret == HG_SUCCESS);
        }

        /* wait for the requests to finish */
        unlink_out_t out;
        for (i = 0; i < child_count; i++) {
            /* TODO: get outputs */
            hret = margo_wait(requests[i].request);

            /* get the output of the rpc */
            hret = margo_get_output(requests[i].handle, &out);

            printf("unlink got response from child (rank=%d): ret=%d\n",
                    child_ranks[i], out.ret);

            /* set return value
             * TODO: check if we have an error and handle it */
            ret = out.ret;
        }

        free(requests);
    }

out:
    return ret;
}

static void unlink_rpc(hg_handle_t handle)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

    hg_return_t hret;

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    /* get input params */
    unlink_in_t in;

    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* create communication tree */
    unifyfs_tree_t bcast_tree;
    unifyfs_tree_init(glb_pmi_rank, glb_pmi_size, in.root,
                      UNIFYFS_BCAST_K_ARY, &bcast_tree);

    ret = unlink_forward(&bcast_tree, &in);

    /* build our output values */
    unlink_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    unifyfs_tree_free(&bcast_tree);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unlink_rpc)

int unifyfs_invoke_unlink_rpc(int gfid)
{
    printf("%d: BUCKEYES %s\n", glb_pmi_rank, __func__);
    fflush(stdout);

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

