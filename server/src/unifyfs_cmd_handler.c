/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2019, UT-Battelle, LLC.
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

// system headers
#include <fcntl.h>
#include <sys/mman.h>

// server components
#include "unifyfs_global.h"
#include "unifyfs_metadata.h"
#include "unifyfs_request_manager.h"

// margo rpcs
#include "margo_server.h"
#include "unifyfs_client_rpcs.h"
#include "unifyfs_rpc_util.h"
#include "unifyfs_misc.h"


/* BEGIN MARGO CLIENT-SERVER RPC HANDLER FUNCTIONS */

/* called by client to register with the server, client provides a
 * structure of values on input, some of which specify global
 * values across all clients in the app_id, and some of which are
 * specific to the client process,
 *
 * server creates a structure for the given app_id (if needed),
 * and then fills in a set of values for the particular client,
 *
 * server attaches to client shared memory regions, opens files
 * holding spill over data, and launchers request manager for
 * client */
static void unifyfs_mount_rpc(hg_handle_t handle)
{
    int ret = (int)UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_mount_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read app_id and client_id from input */
    int app_id = unifyfs_generate_gfid(in.mount_prefix);
    int client_id = -1;

    /* lookup app_config for given app_id */
    app_config* app_cfg = get_application(app_id);
    if (app_cfg == NULL) {
        /* insert new app_config into our app_configs array */
        LOGDBG("creating new application for app_id=%d", app_id);
        app_cfg = new_application(app_id);
        if (NULL == app_cfg) {
            ret = UNIFYFS_FAILURE;
        }
    } else {
        LOGDBG("using existing app_config for app_id=%d", app_id);
    }

    if (NULL != app_cfg) {
        LOGDBG("creating new app client for %s", in.client_addr_str);
        app_client* client = new_app_client(app_cfg,
                                            in.client_addr_str,
                                            in.dbg_rank);
        if (NULL == client) {
            LOGERR("failed to create new client for app_id=%d dbg_rank=%d",
                   app_id, (int)in.dbg_rank);
            ret = (int)UNIFYFS_FAILURE;
        } else {
            client_id = client->client_id;
            LOGDBG("created new application client %d:%d", app_id, client_id);
        }
    }

    /* build output structure to return to caller */
    unifyfs_mount_out_t out;
    out.app_id = (int32_t) app_id;
    out.client_id = (int32_t) client_id;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_mount_rpc)

/* server attaches to client shared memory regions, opens files
 * holding spillover data */
static void unifyfs_attach_rpc(hg_handle_t handle)
{
    int ret = (int)UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_attach_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read app_id and client_id from input */
    int app_id = in.app_id;
    int client_id = in.client_id;

    /* lookup client structure and attach it */
    app_client* client = get_app_client(app_id, client_id);
    if (NULL != client) {
        LOGDBG("attaching client %d:%d", app_id, client_id);
        ret = attach_app_client(client,
                                in.logio_spill_dir,
                                in.logio_spill_size,
                                in.logio_mem_size,
                                in.shmem_data_size,
                                in.shmem_super_size,
                                in.meta_offset,
                                in.meta_size);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("attach_app_client() failed");
        }
    } else {
        LOGERR("client not found (app_id=%d, client_id=%d)",
               app_id, client_id);
        ret = (int)UNIFYFS_FAILURE;
    }

    /* build output structure to return to caller */
    unifyfs_attach_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_attach_rpc)

static void unifyfs_unmount_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_unmount_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read app_id and client_id from input */
    int app_id    = in.app_id;
    int client_id = in.client_id;

    /* disconnect app client */
    int ret = UNIFYFS_SUCCESS;
    app_client* clnt = get_app_client(app_id, client_id);
    if (NULL != clnt) {
        ret = disconnect_app_client(clnt);
    } else {
        LOGERR("application client not found");
        ret = EINVAL;
    }

    /* build output structure to return to caller */
    unifyfs_unmount_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_unmount_rpc)

/* returns file meta data including file size and file name
 * given a global file id */
static void unifyfs_metaget_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_metaget_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* given the global file id, look up file attributes
     * from key/value store */
    unifyfs_file_attr_t attr_val;
    //int ret = unifyfs_get_file_attribute(in.gfid, &attr_val);
//    int ret = gfid2ext_tree_metaget(&glb_gfid2ext, in.gfid, &attr_val);
    int ret = unifyfs_inode_metaget(in.gfid, &attr_val);

    /* build our output values */
    unifyfs_metaget_out_t out;
    out.attr = attr_val;
    out.ret  = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_metaget_rpc)

/* given a global file id and a file name,
 * record key/value entry for this file */
static void unifyfs_metaset_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_metaset_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* store file name for given global file id */
    unifyfs_file_attr_t fattr;
    int create         = (int) in.create;
    fattr.gfid         = in.gfid;
    strlcpy(fattr.filename, in.filename, sizeof(fattr.filename));
    fattr.mode         = in.mode;
    fattr.uid          = in.uid;
    fattr.gid          = in.gid;
    fattr.size         = in.size;
    fattr.atime        = in.atime;
    fattr.mtime        = in.mtime;
    fattr.ctime        = in.ctime;
    fattr.is_laminated = in.is_laminated;

    /* if we're creating the file,
     * we initialize both the size and laminate flags */
    int ret = unifyfs_set_file_attribute(create, create, &fattr);

    /* use the new collective for file creation */
    ret = rm_cmd_metaset(in.app_id, in.local_rank_idx, fattr.gfid,create,
                         &fattr);

    /* build our output values */
    unifyfs_metaset_out_t out;
    out.ret = ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_metaset_rpc)

/* given a client identified by (app_id, client_id) as input, read the write
 * extents for one or more of the client's files from the shared memory index
 * and update the global metadata for the file(s) */
static void unifyfs_sync_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_sync_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read the write indices for all client files from shmem and
     * propagate their extents to our global metadata */
    int ret = rm_cmd_sync(in.app_id, in.client_id);

    /* build our output values */
    unifyfs_sync_out_t out;
    out.ret = ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_sync_rpc)

/* given an app_id, client_id, global file id,
 * return current file size */
static void unifyfs_filesize_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_filesize_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read data for a single read request from client,
     * returns data to client through shared memory */
    size_t filesize = 0;
    int ret = rm_cmd_filesize(in.app_id, in.client_id,
                              in.gfid, &filesize);

    /* build our output values */
    unifyfs_filesize_out_t out;
    out.ret      = (int32_t)   ret;
    out.filesize = (hg_size_t) filesize;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_filesize_rpc)

/* given an app_id, client_id, global file id,
 * and file size, truncate file to that size */
static void unifyfs_truncate_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_truncate_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* truncate file to specified size */
    int ret = rm_cmd_truncate(in.app_id, in.client_id,
                              in.gfid, in.filesize);

    /* build our output values */
    unifyfs_truncate_out_t out;
    out.ret = (int32_t) ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_truncate_rpc)

/* given an app_id, client_id, and global file id,
 * remove file from system */
static void unifyfs_unlink_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_unlink_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* truncate file to specified size */
    int ret = rm_cmd_unlink(in.app_id, in.client_id, in.gfid);

    /* build our output values */
    unifyfs_unlink_out_t out;
    out.ret = (int32_t) ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_unlink_rpc)

/* given an app_id, client_id, and global file id,
 * laminate file */
static void unifyfs_laminate_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_laminate_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* truncate file to specified size */
    int ret = rm_cmd_laminate(in.app_id, in.client_id, in.gfid);

    /* build our output values */
    unifyfs_laminate_out_t out;
    out.ret = (int32_t) ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_laminate_rpc)

/* given an app_id, client_id, global file id, an offset, and a length,
 * initiate read operation to lookup and return data,
 * client synchronizes with server again later when data is available
 * to be copied into user buffers */
static void unifyfs_read_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_read_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read data for a single read request from client,
     * returns data to client through shared memory */
    int ret = rm_cmd_read(in.app_id, in.client_id,
                          in.gfid, in.offset, in.length);

    /* build our output values */
    unifyfs_read_out_t out;
    out.ret = ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_read_rpc)

/* given an app_id, client_id, global file id, and a count
 * of read requests, follow by list of offset/length tuples
 * initiate read requests for data,
 * client synchronizes with server again later when data is available
 * to be copied into user buffers */
static void unifyfs_mread_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_mread_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* allocate buffer to hold array of read requests */
    hg_size_t size = in.bulk_size;
    void* buffer = (void*)malloc(size);
    assert(buffer);

    /* get pointer to mercury structures to set up bulk transfer */
    const struct hg_info* hgi = margo_get_info(handle);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* register local target buffer for bulk access */
    hg_bulk_t bulk_handle;
    hret = margo_bulk_create(mid, 1, &buffer, &size,
                             HG_BULK_WRITE_ONLY, &bulk_handle);
    assert(hret == HG_SUCCESS);

    /* get list of read requests */
    hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                               in.bulk_handle, 0, bulk_handle, 0, size);
    assert(hret == HG_SUCCESS);

    /* initiate read operations to fetch data for read requests */
    int ret = rm_cmd_mread(in.app_id, in.client_id,
                           in.read_count, buffer);

    /* build our output values */
    unifyfs_mread_out_t out;
    out.ret = ret;

    /* return to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_bulk_free(bulk_handle);
    free(buffer);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_mread_rpc)
