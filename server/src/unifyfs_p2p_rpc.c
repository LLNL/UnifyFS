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
#include "margo_server.h"
#include "unifyfs_server_rpcs.h"
#include "unifyfs_p2p_rpc.h"
#include "unifyfs_group_rpc.h"

/*************************************************************************
 * Peer-to-peer RPC helper methods
 *************************************************************************/

/* determine server responsible for maintaining target file's metadata */
int hash_gfid_to_server(int gfid)
{
    return gfid % glb_pmi_size;
}

/* server peer-to-peer (p2p) margo request structure */
typedef struct {
    margo_request request;
    hg_addr_t     peer;
    hg_handle_t   handle;
} p2p_request;

/* helper method to initialize peer request rpc handle */
static
int get_request_handle(hg_id_t request_hgid,
                       int peer_rank,
                       p2p_request* req)
{
    int rc = UNIFYFS_SUCCESS;

    /* get address for specified server rank */
    req->peer = glb_servers[peer_rank].margo_svr_addr;

    /* get handle to rpc function */
    hg_return_t hret = margo_create(unifyfsd_rpc_context->svr_mid, req->peer,
                                    request_hgid, &(req->handle));
    if (hret != HG_SUCCESS) {
        LOGERR("failed to get handle for p2p request(%p) to server %d",
               req, peer_rank);
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/* helper method to forward peer rpc request */
static
int forward_request(void* input_ptr,
                    p2p_request* req)
{
    int rc = UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_iforward(req->handle, input_ptr,
                                      &(req->request));
    if (hret != HG_SUCCESS) {
        LOGERR("failed to forward p2p request(%p)", req);
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/* helper method to wait for peer rpc request completion */
static
int wait_for_request(p2p_request* req)
{
    int rc = UNIFYFS_SUCCESS;

    /* call rpc function */
    hg_return_t hret = margo_wait(req->request);
    if (hret != HG_SUCCESS) {
        LOGERR("wait on p2p request(%p) failed", req);
        rc = UNIFYFS_ERROR_MARGO;
    }

    return rc;
}

/*************************************************************************
 * File extents metadata update request
 *************************************************************************/

/* Add extents rpc handler */
static void add_extents_rpc(hg_handle_t handle)
{
    LOGDBG("add_extents rpc handler");

    /* assume we'll succeed */
    int32_t ret = UNIFYFS_SUCCESS;

    const struct hg_info* hgi = margo_get_info(handle);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* get input params */
    add_extents_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        int sender = in.src_rank;
        int gfid = in.gfid;
        size_t num_extents = (size_t) in.num_extents;
        size_t bulk_sz = num_extents * sizeof(struct extent_tree_node);

        /* allocate memory for extents */
        void* extents_buf = malloc(bulk_sz);
        if (NULL == extents_buf) {
            LOGERR("allocation for bulk extents failed");
            ret = ENOMEM;
        } else {
            /* register local target buffer for bulk access */
            hg_bulk_t bulk_handle;
            hret = margo_bulk_create(mid, 1, &extents_buf, &bulk_sz,
                                     HG_BULK_WRITE_ONLY, &bulk_handle);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_bulk_create() failed");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                /* get list of read requests */
                hret = margo_bulk_transfer(mid, HG_BULK_PULL,
                                           hgi->addr, in.extents, 0,
                                           bulk_handle, 0,
                                           bulk_sz);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_bulk_transfer() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* store new extents */
                    LOGDBG("received %zu extents for gfid=%d from %d",
                           num_extents, gfid, sender);
                    struct extent_tree_node* extents = extents_buf;
                    ret = unifyfs_inode_add_extents(gfid, num_extents, extents);
                    if (ret) {
                        LOGERR("failed to add extents from %d (ret=%d)",
                               sender, ret);
                    }
                }
                margo_bulk_free(bulk_handle);
            }
            free(extents_buf);
        }
        margo_free_input(handle, &in);
    }

    /* build our output values */
    add_extents_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(add_extents_rpc)

/* Add extents to target file */
int unifyfs_invoke_add_extents_rpc(int gfid,
                                   unsigned int num_extents,
                                   struct extent_tree_node* extents)
{
    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        /* I'm the owner, already did local add */
        return UNIFYFS_SUCCESS;
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extent_add_id;
    int rc = get_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* create a margo bulk transfer handle for extents array */
    hg_bulk_t bulk_handle;
    void* buf = (void*) extents;
    size_t buf_sz = (size_t)num_extents * sizeof(struct extent_tree_node);
    hg_return_t hret = margo_bulk_create(unifyfsd_rpc_context->svr_mid,
                                         1, &buf, &buf_sz,
                                         HG_BULK_READ_ONLY, &bulk_handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        return UNIFYFS_ERROR_MARGO;
    }

    /* fill rpc input struct and forward request */
    add_extents_in_t in;
    in.src_rank = (int32_t) glb_pmi_rank;
    in.gfid = (int32_t) gfid;
    in.num_extents = (int32_t) num_extents;
    in.extents = bulk_handle;
    rc = forward_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }
    margo_bulk_free(bulk_handle);

    /* wait for request completion */
    rc = wait_for_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    add_extents_out_t out;
    hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/*************************************************************************
 * File extents metadata lookup request
 *************************************************************************/

/* find extents rpc handler */
static void find_extents_rpc(hg_handle_t handle)
{
    LOGDBG("find_extents rpc handler");

    int32_t ret;
    unsigned int num_chunks = 0;
    chunk_read_req_t* chunk_locs = NULL;

    const struct hg_info* hgi = margo_get_info(handle);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* get input params */
    find_extents_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        int sender = in.src_rank;
        int gfid = in.gfid;
        size_t num_extents = (size_t) in.num_extents;
        size_t bulk_sz = num_extents * sizeof(unifyfs_inode_extent_t);

        /* allocate memory for extents */
        void* extents_buf = malloc(bulk_sz);
        if (NULL == extents_buf) {
            LOGERR("allocation for bulk extents failed");
            ret = ENOMEM;
        } else {
            /* register local target buffer for bulk access */
            hg_bulk_t bulk_req_handle;
            hret = margo_bulk_create(mid, 1, &extents_buf, &bulk_sz,
                                     HG_BULK_WRITE_ONLY, &bulk_req_handle);
            if (hret != HG_SUCCESS) {
                LOGERR("margo_bulk_create() failed");
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                /* get list of read requests */
                hret = margo_bulk_transfer(mid, HG_BULK_PULL,
                                           hgi->addr, in.extents, 0,
                                           bulk_req_handle, 0,
                                           bulk_sz);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_bulk_transfer() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* lookup requested extents */
                    LOGDBG("received %zu extent lookups for gfid=%d from %d",
                           num_extents, gfid, sender);
                    unifyfs_inode_extent_t* extents = extents_buf;
                    ret = unifyfs_inode_resolve_extent_chunks(num_extents,
                                                              extents,
                                                              &num_chunks,
                                                              &chunk_locs);
                    if (ret) {
                        LOGERR("failed to find extents for %d (ret=%d)",
                               sender, ret);
                    }
                }
                margo_bulk_free(bulk_req_handle);
            }
            free(extents_buf);
        }
        margo_free_input(handle, &in);
    }

    /* fill rpc response struct with output values */
    hg_bulk_t bulk_resp_handle;
    find_extents_out_t out;
    out.ret = ret;
    out.num_locations = 0;
    if (ret == UNIFYFS_SUCCESS) {
        void* buf = (void*) chunk_locs;
        size_t buf_sz = (size_t)num_chunks * sizeof(chunk_read_req_t);
        hret = margo_bulk_create(mid, 1, &buf, &buf_sz,
                                 HG_BULK_READ_ONLY, &bulk_resp_handle);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_bulk_create() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            out.num_locations = num_chunks;
            out.locations = bulk_resp_handle;
        }
    }

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }
    if (out.num_locations) {
        margo_bulk_free(bulk_resp_handle);
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(find_extents_rpc)

/* Lookup extent locations for target file */
int unifyfs_invoke_find_extents_rpc(int gfid,
                                    unsigned int num_extents,
                                    unifyfs_inode_extent_t* extents,
                                    unsigned int* num_chunks,
                                    chunk_read_req_t** chunks)
{
    if ((NULL == num_chunks) || (NULL == chunks)) {
        return EINVAL;
    }
    *num_chunks = 0;
    *chunks = NULL;

    int owner_rank = hash_gfid_to_server(gfid);

    /* do local inode metadata lookup to check for laminated */
    unifyfs_file_attr_t attrs;
    int ret = unifyfs_inode_metaget(gfid, &attrs);
    if (ret == UNIFYFS_SUCCESS) {
        if (attrs.is_laminated || (owner_rank == glb_pmi_rank)) {
            /* do local lookup */
            ret = unifyfs_inode_resolve_extent_chunks((size_t)num_extents,
                                                      extents,
                                                      num_chunks, chunks);
            if (ret) {
                LOGERR("failed to find extents for gfid=%d (ret=%d)",
                       gfid, ret);
            }
            return ret;
        }
    }

    /* forward request to file owner */
    p2p_request preq;
    margo_instance_id mid = unifyfsd_rpc_context->svr_mid;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.extent_lookup_id;
    int rc = get_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* create a margo bulk transfer handle for extents array */
    hg_bulk_t bulk_req_handle;
    void* buf = (void*) extents;
    size_t buf_sz = (size_t)num_extents * sizeof(unifyfs_inode_extent_t);
    hg_return_t hret = margo_bulk_create(mid, 1, &buf, &buf_sz,
                                         HG_BULK_READ_ONLY, &bulk_req_handle);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        return UNIFYFS_ERROR_MARGO;
    }

    /* fill rpc input struct and forward request */
    find_extents_in_t in;
    in.src_rank = (int32_t) glb_pmi_rank;
    in.gfid = (int32_t) gfid;
    in.num_extents = (int32_t) num_extents;
    in.extents = bulk_req_handle;
    rc = forward_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }
    margo_bulk_free(bulk_req_handle);

    /* wait for request completion */
    rc = wait_for_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get the output of the rpc */
    find_extents_out_t out;
    hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        if (ret == UNIFYFS_SUCCESS) {
            /* allocate local buffer for chunk locations */
            unsigned int n_chks = (unsigned int) out.num_locations;
            buf_sz = (size_t)n_chks * sizeof(chunk_read_req_t);
            buf = malloc(buf_sz);
            if (NULL == buf) {
                LOGERR("allocation for bulk locations failed");
                ret = ENOMEM;
            } else {
                /* create a margo bulk transfer handle for locations array */
                hg_bulk_t bulk_resp_handle;
                hret = margo_bulk_create(mid, 1, &buf, &buf_sz,
                                         HG_BULK_WRITE_ONLY,
                                         &bulk_resp_handle);
                if (hret != HG_SUCCESS) {
                    LOGERR("margo_bulk_create() failed");
                    ret = UNIFYFS_ERROR_MARGO;
                } else {
                    /* pull locations array */
                    hret = margo_bulk_transfer(mid, HG_BULK_PULL,
                                               preq.peer, out.locations, 0,
                                               bulk_resp_handle, 0,
                                               buf_sz);
                    if (hret != HG_SUCCESS) {
                        LOGERR("margo_bulk_transfer() failed");
                        ret = UNIFYFS_ERROR_MARGO;
                    } else {
                        /* lookup requested extents */
                        LOGDBG("received %u chunk locations for gfid=%d",
                               n_chks, gfid);
                        *chunks = (chunk_read_req_t*) buf;
                        *num_chunks = (unsigned int) n_chks;
                    }
                    margo_bulk_free(bulk_resp_handle);
                }
            }
        }
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/*************************************************************************
 * File attributes request
 *************************************************************************/

/* Metaget rpc handler */
static void metaget_rpc(hg_handle_t handle)
{
    LOGDBG("metaget rpc handler");

    int32_t ret;

    /* initialize invalid attributes */
    unifyfs_file_attr_t attrs;
    unifyfs_file_attr_set_invalid(&attrs);

    /* get input params */
    metaget_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        ret = unifyfs_inode_metaget(in.gfid, &attrs);
        margo_free_input(handle, &in);
    }

    /* fill output values */
    metaget_out_t out;
    out.ret = ret;
    out.attr = attrs;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(metaget_rpc)

/* Get file attributes for target file */
int unifyfs_invoke_metaget_rpc(int gfid,
                               unifyfs_file_attr_t* attrs)
{
    if (NULL == attrs) {
        return EINVAL;
    }

    int owner_rank = hash_gfid_to_server(gfid);

    /* do local inode metadata lookup to check for laminated */
    int rc = unifyfs_inode_metaget(gfid, attrs);
    if ((rc == UNIFYFS_SUCCESS) && (attrs->is_laminated)) {
        /* if laminated, we already have final metadata locally */
        return UNIFYFS_SUCCESS;
    }
    if (owner_rank == glb_pmi_rank) {
        return rc;
    }

    int need_local_metadata = 0;
    if (rc == ENOENT) {
        /* inode_metaget above failed with ENOENT, need to create inode */
        need_local_metadata = 1;
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.metaget_id;
    rc = get_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    metaget_in_t in;
    in.gfid = (int32_t)gfid;
    rc = forward_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    metaget_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed");
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
                unifyfs_inode_metaset(gfid, UNIFYFS_FILE_ATTR_OP_CREATE,
                                      attrs);
            }
        }
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/*************************************************************************
 * File size request
 *************************************************************************/

/* Filesize rpc handler */
static void filesize_rpc(hg_handle_t handle)
{
    LOGDBG("filesize rpc handler");

    int32_t ret;
    hg_size_t filesize = 0;

    /* get input params */
    filesize_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        ret = unifyfs_inode_get_filesize(in.gfid, &filesize);
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
    int rc = unifyfs_inode_metaget(gfid, &attrs);
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
    rc = get_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    filesize_in_t in;
    in.gfid = (int32_t)gfid;
    rc = forward_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    filesize_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed");
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

/*************************************************************************
 * File attributes update request
 *************************************************************************/

/* Metaset rpc handler */
static void metaset_rpc(hg_handle_t handle)
{
    LOGDBG("metaset rpc handler");

    int32_t ret;

    /* get input params */
    metaset_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        unifyfs_file_attr_op_e attr_op = in.fileop;
        ret = unifyfs_inode_metaset(in.gfid, attr_op, &(in.attr));
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

/* Set metadata for target file */
int unifyfs_invoke_metaset_rpc(int gfid,
                               int attr_op,
                               unifyfs_file_attr_t* attrs)
{
    if (NULL == attrs) {
        return EINVAL;
    }

    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        /* I'm the owner, do local inode metadata update */
        return unifyfs_inode_metaset(gfid, attr_op, attrs);
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.metaset_id;
    int rc = get_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    metaset_in_t in;
    in.gfid = (int32_t) gfid;
    in.fileop = (int32_t) attr_op;
    in.attr = *attrs;
    rc = forward_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    metaset_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);

        /* if update at owner succeeded, do it locally */
        if (ret == UNIFYFS_SUCCESS) {
            ret = unifyfs_inode_metaset(gfid, attr_op, attrs);
        }
    }
    margo_destroy(preq.handle);

    return ret;
}

/*************************************************************************
 * File lamination request
 *************************************************************************/

/* Laminate rpc handler */
static void laminate_rpc(hg_handle_t handle)
{
    LOGDBG("laminate rpc handler");

    int32_t ret;

    /* get input params */
    laminate_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        int gfid = (int) in.gfid;
        margo_free_input(handle, &in);

        ret = unifyfs_inode_laminate(gfid);
        if (ret == UNIFYFS_SUCCESS) {
            /* tell the rest of the servers */
            ret = unifyfs_invoke_broadcast_laminate(gfid);
        }
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

/*  Laminate the target file */
int unifyfs_invoke_laminate_rpc(int gfid)
{
    int ret;
    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        /* I'm the owner, do local inode metadata update */
        ret = unifyfs_inode_laminate(gfid);
        if (ret == UNIFYFS_SUCCESS) {
            /* tell the rest of the servers */
            ret = unifyfs_invoke_broadcast_laminate(gfid);
        }
        return ret;
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.laminate_id;
    int rc = get_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    laminate_in_t in;
    in.gfid = (int32_t)gfid;
    rc = forward_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get the output of the rpc */
    laminate_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}

/*************************************************************************
 * File truncation request
 *************************************************************************/

/* Truncate rpc handler */
static void truncate_rpc(hg_handle_t handle)
{
    LOGDBG("truncate rpc handler");

    int32_t ret;

    /* get input params */
    truncate_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        int gfid = (int) in.gfid;
        size_t fsize = (size_t) in.filesize;
        ret = unifyfs_invoke_broadcast_truncate(gfid, fsize);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("truncate(gfid=%d, size=%zu) broadcast failed",
                   gfid, fsize);
        }
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

/* Truncate the target file */
int unifyfs_invoke_truncate_rpc(int gfid,
                                size_t filesize)
{
    int owner_rank = hash_gfid_to_server(gfid);
    if (owner_rank == glb_pmi_rank) {
        /* I'm the owner, start broadcast update. The local inode will be
         * updated as part of this update. */
        return unifyfs_invoke_broadcast_truncate(gfid, filesize);
    }

    /* forward request to file owner */
    p2p_request preq;
    hg_id_t req_hgid = unifyfsd_rpc_context->rpcs.truncate_id;
    int rc = get_request_handle(req_hgid, owner_rank, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* fill rpc input struct and forward request */
    truncate_in_t in;
    in.gfid = (int32_t) gfid;
    in.filesize = (hg_size_t) filesize;
    rc = forward_request((void*)&in, &preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* wait for request completion */
    rc = wait_for_request(&preq);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* get the output of the rpc */
    int ret;
    truncate_out_t out;
    hg_return_t hret = margo_get_output(preq.handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* set return value */
        ret = out.ret;
        margo_free_output(preq.handle, &out);
    }
    margo_destroy(preq.handle);

    return ret;
}
