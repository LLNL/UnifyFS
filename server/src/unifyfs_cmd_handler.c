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

/**
 * attach to the client-side shared memory
 * @param app_config: application information
 * @param app_id: the server-side
 * @param sock_id: position in poll_set in unifyfs_sock.h
 * @return success/error code
 */
static int attach_to_shm(app_config_t* app_config,
                         int app_id,
                         int client_side_id)
{
    char shm_name[GEN_STR_LEN] = {0};

    /* attach shared superblock, a superblock is created by each
     * client to store the raw file data.
     * The overflowed data are spilled to SSD. */

    /* define name of superblock region for this client */
    sprintf(shm_name, "%d-super-%d", app_id, client_side_id);

    /* attach to superblock */
    void* addr = unifyfs_shm_alloc(shm_name, app_config->superblock_sz);
    if (addr == NULL) {
        LOGERR("Failed to attach to superblock %s", shm_name);
        return (int)UNIFYFS_ERROR_SHMEM;
    }
    app_config->shm_superblocks[client_side_id] = addr;

    /* copy name of superblock region */
    strcpy(app_config->super_buf_name[client_side_id], shm_name);

    /* attach shared request buffer, a request buffer is created by each
     * client to convey the client-side read request to the delegator */

    /* define name of request buffer region for this client */
    sprintf(shm_name, "%d-req-%d", app_id, client_side_id);

    /* attach to request buffer region */
    addr = unifyfs_shm_alloc(shm_name, app_config->req_buf_sz);
    if (addr == NULL) {
        LOGERR("Failed to attach to request buffer %s", shm_name);
        return (int)UNIFYFS_ERROR_SHMEM;
    }
    app_config->shm_req_bufs[client_side_id] = addr;

    /* copy name of request buffer region */
    strcpy(app_config->req_buf_name[client_side_id], shm_name);

    /* initialize shared receive buffer, a request buffer is created
     * by each client for the delegator to temporarily buffer the
     * received data for this client */

    /* define name of receive buffer region for this client */
    sprintf(shm_name, "%d-recv-%d", app_id, client_side_id);

    /* attach to request buffer region */
    addr = unifyfs_shm_alloc(shm_name, app_config->recv_buf_sz);
    if (addr == NULL) {
        LOGERR("Failed to attach to receive buffer %s", shm_name);
        return (int)UNIFYFS_ERROR_SHMEM;
    }
    app_config->shm_recv_bufs[client_side_id] = addr;
    shm_header_t* shm_hdr = (shm_header_t*)addr;
    pthread_mutex_init(&(shm_hdr->sync), NULL);
    shm_hdr->meta_cnt = 0;
    shm_hdr->bytes = 0;
    shm_hdr->state = SHMEM_REGION_EMPTY;

    /* copy name of request buffer region */
    strcpy(app_config->recv_buf_name[client_side_id], shm_name);

    return UNIFYFS_SUCCESS;
}

/**
 * open spilled log file, spilled log file
 * is created once the client-side shared superblock
 * overflows.
 * @param app_config: application information
 * @param app_id: the server-side application id
 * @param sock_id: position in poll_set in unifyfs_sock.h
 * @return success/error code
 */
static int open_log_file(app_config_t* app_config,
                         int app_id, int client_side_id)
{
    /* build name to spill over log file,
     * have one of these per app_id and client_id,
     * client writes data to spill over file when it fills
     * memory storage */
    char path[UNIFYFS_MAX_FILENAME] = {0};
    snprintf(path, sizeof(path), "%s/spill_%d_%d.log",
             app_config->external_spill_dir, app_id, client_side_id);

    /* copy filename of spill over file into app_config */
    strcpy(app_config->spill_log_name[client_side_id], path);

    /* open spill over file for reading */
    app_config->spill_log_fds[client_side_id] = open(path, O_RDONLY, 0666);
    if (app_config->spill_log_fds[client_side_id] < 0) {
        LOGERR("failed to open spill file %s", path);
        return (int)UNIFYFS_ERROR_FILE;
    }

    /* build name of spill over index file,
     * this contains index meta data for data the client wrote to the
     * spill over file */
    snprintf(path, sizeof(path), "%s/spill_index_%d_%d.log",
             app_config->external_spill_dir, app_id, client_side_id);

    /* copy name of spill over index metadata file to app_config */
    strcpy(app_config->spill_index_log_name[client_side_id], path);

    /* open spill over index file for reading */
    app_config->spill_index_log_fds[client_side_id] =
        open(path, O_RDONLY, 0666);
    if (app_config->spill_index_log_fds[client_side_id] < 0) {
        LOGERR("failed to open spill index file %s", path);
        return (int)UNIFYFS_ERROR_FILE;
    }

    return UNIFYFS_SUCCESS;
}



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
    int rc;
    int ret = (int)UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_mount_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read app_id and client_id from input */
    int app_id    = in.app_id;
    int client_id = in.local_rank_idx;

    /* lookup app_config for given app_id */
    app_config_t* tmp_config =
        (app_config_t*) arraylist_get(app_config_list, app_id);

    /* fill in and insert a new entry for this app_id
     * if we don't already have one */
    if (tmp_config == NULL) {
        /* don't have an app_config for this app_id,
         * so allocate and fill one in */
        tmp_config = (app_config_t*)malloc(sizeof(app_config_t));

        /* record size of shared memory regions */
        tmp_config->req_buf_sz    = in.req_buf_sz;
        tmp_config->recv_buf_sz   = in.recv_buf_sz;
        tmp_config->superblock_sz = in.superblock_sz;

        /* record offset and size of index entries */
        tmp_config->meta_offset = in.meta_offset;
        tmp_config->meta_size   = in.meta_size;

        /* record offset and size of file data */
        tmp_config->data_offset = in.data_offset;
        tmp_config->data_size   = in.data_size;

        /* record directory holding spill over files */
        strcpy(tmp_config->external_spill_dir, in.external_spill_dir);

        /* record number of clients on this node */
        tmp_config->num_procs_per_node = in.num_procs_per_node;

        /* initialize per-client fields */
        int i;
        for (i = 0; i < MAX_NUM_CLIENTS; i++) {
            tmp_config->client_ranks[i]        = -1;
            tmp_config->shm_req_bufs[i]        = NULL;
            tmp_config->shm_recv_bufs[i]       = NULL;
            tmp_config->shm_superblocks[i]     = NULL;
            tmp_config->spill_log_fds[i]       = -1;
            tmp_config->spill_index_log_fds[i] = -1;
            tmp_config->client_addr[i]         = HG_ADDR_NULL;
        }

        /* insert new app_config into our list, indexed by app_id */
        rc = arraylist_insert(app_config_list, app_id, tmp_config);
        if (rc != 0) {
            ret = rc;
        }
    } else {
        LOGDBG("using existing app_config for app_id=%d", app_id);
    }

    /* convert client_addr_str sent in input struct to margo hg_addr_t,
     * which is the address type needed to call rpc functions, etc */
    hret = margo_addr_lookup(unifyfsd_rpc_context->shm_mid,
                             in.client_addr_str,
                             &(tmp_config->client_addr[client_id]));

    /* record client id of process on this node */
    tmp_config->client_ranks[client_id] = client_id;

    /* record global rank of client process for debugging */
    tmp_config->dbg_ranks[client_id] = in.dbg_rank;

    /* attach to shared memory regions of this client */
    rc = attach_to_shm(tmp_config, app_id, client_id);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("attach_to_shm() failed for app_id=%d client_id=%d rc=%d",
               app_id, client_id, rc);
        ret = rc;
    }

    /* open spill over files for this client */
    rc = open_log_file(tmp_config, app_id, client_id);
    if (rc < 0)  {
        LOGERR("open_log_file() failed for app_id=%d client_id=%d rc=%d",
               app_id, client_id, rc);
        ret = rc;
    }

    /* create request manager thread */
    reqmgr_thrd_t* rm_thrd = unifyfs_rm_thrd_create(app_id, client_id);
    if (rm_thrd != NULL) {
        /* TODO: seems like it would be cleaner to avoid thread_list
         * and instead just record address to struct */
        /* remember id for thread control for this client */
        tmp_config->thrd_idxs[client_id] = rm_thrd->thrd_ndx;
    } else {
        /* failed to create request manager thread */
        LOGERR("unifyfs_rm_thrd_create() failed for app_id=%d client_id=%d",
               app_id, client_id);
        ret = UNIFYFS_FAILURE;
    }

    /* build output structure to return to caller */
    unifyfs_mount_out_t out;
    out.ret = ret;
    out.max_recs_per_slice = max_recs_per_slice;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_mount_rpc)

static void unifyfs_unmount_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_unmount_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* read app_id and client_id from input */
    int app_id    = in.app_id;
    int client_id = in.local_rank_idx;

    /* build output structure to return to caller */
    unifyfs_unmount_out_t out;
    out.ret = UNIFYFS_SUCCESS;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);

    /* lookup app_config for given app_id */
    app_config_t* app_config =
        (app_config_t*) arraylist_get(app_config_list, app_id);

    /* get thread id for this client */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* look up thread control structure */
    reqmgr_thrd_t* thrd_ctrl = rm_get_thread(thrd_id);

    /* shutdown the delegator thread */
    rm_cmd_exit(thrd_ctrl);

    /* detach from the request shared memory */
    if (NULL != app_config->shm_req_bufs[client_id]) {
        unifyfs_shm_free(app_config->req_buf_name[client_id],
                         app_config->req_buf_sz,
                         (void**)&(app_config->shm_req_bufs[client_id]));
    }

    /* detach from the read shared memory buffer */
    if (NULL != app_config->shm_recv_bufs[client_id]) {
        unifyfs_shm_free(app_config->recv_buf_name[client_id],
                         app_config->recv_buf_sz,
                         (void**)&(app_config->shm_recv_bufs[client_id]));
    }

    /* free margo hg_addr_t client addresses in app_config struct */
    margo_addr_free(unifyfsd_rpc_context->shm_mid,
                    app_config->client_addr[client_id]);
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
    int ret = unifyfs_get_file_attribute(in.gfid, &attr_val);

    /* build our output values */
    unifyfs_metaget_out_t out;
    out.gfid         = attr_val.gfid;
    out.mode         = attr_val.mode;
    out.uid          = attr_val.uid;
    out.gid          = attr_val.gid;
    out.size         = attr_val.size;
    out.atime        = attr_val.atime;
    out.mtime        = attr_val.mtime;
    out.ctime        = attr_val.ctime;
    out.filename     = attr_val.filename;
    out.is_laminated = attr_val.is_laminated;
    out.ret          = ret;

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
    memset(&fattr, 0, sizeof(fattr));
    int create         = (int) in.create;
    fattr.gfid         = in.gfid;
    strncpy(fattr.filename, in.filename, sizeof(fattr.filename));
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

/* given app_id, client_id, and a global file id as input,
 * read extent location metadata from client shared memory
 * and insert corresponding key/value pairs into global index */
static void unifyfs_fsync_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_fsync_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* given global file id, read index metadata from client and
     * insert into global index key/value store */
    int ret = rm_cmd_fsync(in.app_id, in.local_rank_idx, in.gfid);

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
DEFINE_MARGO_RPC_HANDLER(unifyfs_fsync_rpc)


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
    int ret = rm_cmd_filesize(in.app_id, in.local_rank_idx,
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
    int ret = rm_cmd_truncate(in.app_id, in.local_rank_idx,
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
    unifyfs_truncate_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* truncate file to specified size */
    int ret = rm_cmd_unlink(in.app_id, in.local_rank_idx, in.gfid);

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
DEFINE_MARGO_RPC_HANDLER(unifyfs_unlink_rpc)

/* given an app_id, client_id, and global file id,
 * laminate file */
static void unifyfs_laminate_rpc(hg_handle_t handle)
{
    /* get input params */
    unifyfs_truncate_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* truncate file to specified size */
    int ret = rm_cmd_laminate(in.app_id, in.local_rank_idx, in.gfid);

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
    int ret = rm_cmd_read(in.app_id, in.local_rank_idx,
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
    int ret = rm_cmd_mread(in.app_id, in.local_rank_idx,
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
