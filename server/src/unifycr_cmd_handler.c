/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
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

#include <unistd.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <mercury.h>
#include <margo.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "log.h"
#include "unifycr_global.h"
#include "unifycr_meta.h"
#include "unifycr_cmd_handler.h"
#include "unifycr_request_manager.h"
#include "unifycr_setup.h"
#include "unifycr_const.h"
#include "unifycr_sock.h"
#include "unifycr_metadata.h"
#include "unifycr_client_context.h"
#include "unifycr_shm.h"
#include "../../client/src/unifycr_clientcalls_rpc.h"

/**
 * attach to the client-side shared memory
 * @param app_config: application information
 * @param app_id: the server-side
 * @param sock_id: position in poll_set in unifycr_sock.h
 * @return success/error code
 */
static int attach_to_shm(
    app_config_t *app_config,
    int app_id,
    int client_side_id)
{
    char shm_name[GEN_STR_LEN] = {0};

    /* attach shared superblock, a superblock is created by each
     * client to store the raw file data.
     * The overflowed data are spilled to SSD. */

    /* define name of superblock region for this client */
    sprintf(shm_name, "%d-super-%d", app_id, client_side_id);

    /* copy name of superblock region */
    strcpy(app_config->super_buf_name[client_side_id], shm_name);

    /* attach to superblock */
    void *addr = unifycr_shm_alloc(shm_name, app_config->superblock_sz);
    if (addr == NULL) {
        LOGERR("Failed to attach to superblock %s", shm_name);
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    app_config->shm_superblocks[client_side_id] = addr;

    /* attach shared request buffer, a request buffer is created by each
     * client to convey the client-side read request to the delegator */

    /* define name of request buffer region for this client */
    sprintf(shm_name, "%d-req-%d", app_id, client_side_id);

    /* copy name of request buffer region */
    strcpy(app_config->req_buf_name[client_side_id], shm_name);

    /* attach to request buffer region */
    addr = unifycr_shm_alloc(shm_name, app_config->req_buf_sz);
    if (addr == NULL) {
        LOGERR("Failed to attach to request buffer %s", shm_name);
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    app_config->shm_req_bufs[client_side_id] = addr;

    /* initialize shared receive buffer, a request buffer is created
     * by each client for the delegator to temporarily buffer the
     * received data for this client */

    /* define name of receive buffer region for this client */
    sprintf(shm_name, "%d-recv-%d", app_id, client_side_id);

    /* copy name of request buffer region */
    strcpy(app_config->recv_buf_name[client_side_id], shm_name);

    /* attach to request buffer region */
    addr = unifycr_shm_alloc(shm_name, app_config->recv_buf_sz);
    if (addr == NULL) {
        LOGERR("Failed to attach to receive buffer %s", shm_name);
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    app_config->shm_recv_bufs[client_side_id] = addr;

    return ULFS_SUCCESS;
}

/**
 * open spilled log file, spilled log file
 * is created once the client-side shared superblock
 * overflows.
 * @param app_config: application information
 * @param app_id: the server-side application id
 * @param sock_id: position in poll_set in unifycr_sock.h
 * @return success/error code
 */
static int open_log_file(app_config_t *app_config,
                  int app_id, int client_side_id)
{
    /* build name to spill over log file,
     * have one of these per app_id and client_id,
     * client writes data to spill over file when it fills
     * memory storage */
    char path[UNIFYCR_MAX_FILENAME] = {0};
    snprintf(path, sizeof(path), "%s/spill_%d_%d.log",
        app_config->external_spill_dir, app_id, client_side_id);

    /* copy filename of spill over file into app_config */
    strcpy(app_config->spill_log_name[client_side_id], path);

    /* open spill over file for reading */
    app_config->spill_log_fds[client_side_id] = open(path, O_RDONLY, 0666);
    if (app_config->spill_log_fds[client_side_id] < 0) {
        printf("rank:%d, openning file %s failure\n", glb_rank, path);
        fflush(stdout);
        return (int)UNIFYCR_ERROR_FILE;
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
        printf("rank:%d, openning index file %s failure\n", glb_rank, path);
        fflush(stdout);
        return (int)UNIFYCR_ERROR_FILE;
    }

    return ULFS_SUCCESS;
}

/* create a request manager thread for the given app_id
 * and client_id, returns pointer to thread control structure
 * on success and NULL on failure */
static thrd_ctrl_t *unifycr_rm_thrd_create(int app_id, int client_id)
{
    /* allocate a new thread control structure */
    size_t bytes = sizeof(thrd_ctrl_t);
    thrd_ctrl_t *thrd_ctrl = (thrd_ctrl_t *)malloc(bytes);
    if (thrd_ctrl == NULL) {
        LOGERR("Failed to allocate structure for request "
            "manager thread for app_id=%d client_id=%d",
            app_id, client_id);
        return NULL;
    }
    memset(thrd_ctrl, 0, bytes);

    /* allocate an array for listing read requests from client */
    bytes = sizeof(msg_meta_t);
    thrd_ctrl->del_req_set = (msg_meta_t *)malloc(bytes);
    if (thrd_ctrl->del_req_set == NULL) {
        LOGERR("Failed to allocate read request structure for request "
            "manager thread for app_id=%d client_id=%d",
            app_id, client_id);
        free(thrd_ctrl);
        return NULL;
    }
    memset(thrd_ctrl->del_req_set, 0, bytes);

    /* allocate structure for tracking outstanding read requests
     * this delegator has with service managers on other nodes */
    bytes = sizeof(del_req_stat_t);
    thrd_ctrl->del_req_stat = (del_req_stat_t *)malloc(bytes);
    if (thrd_ctrl->del_req_stat == NULL) {
        LOGERR("Failed to allocate delegator structure for request "
            "manager thread for app_id=%d client_id=%d",
            app_id, client_id);
        free(thrd_ctrl->del_req_set);
        free(thrd_ctrl);
        return NULL;
    }
    memset(thrd_ctrl->del_req_set, 0, bytes);

    /* allocate a structure to track requests we have on each
     * remote service manager */
    bytes = sizeof(per_del_stat_t) * glb_size;
    thrd_ctrl->del_req_stat->req_stat = (per_del_stat_t *)malloc(bytes);
    if (thrd_ctrl->del_req_stat->req_stat == NULL) {
        LOGERR("Failed to allocate per-delegator structure for request "
            "manager thread for app_id=%d client_id=%d",
            app_id, client_id);
        free(thrd_ctrl->del_req_stat);
        free(thrd_ctrl->del_req_set);
        free(thrd_ctrl);
        return NULL;
    }
    memset(thrd_ctrl->del_req_stat->req_stat, 0, bytes);

    /* initialize lock for shared data structures between
     * main thread and request delegator thread */
    int rc = pthread_mutex_init(&(thrd_ctrl->thrd_lock), NULL);
    if (rc != 0) {
        LOGERR("pthread_mutex_init failed for request "
            "manager thread app_id=%d client_id=%d rc=%d (%s)",
            app_id, client_id, rc, strerror(rc));
        free(thrd_ctrl->del_req_stat->req_stat);
        free(thrd_ctrl->del_req_stat);
        free(thrd_ctrl->del_req_set);
        free(thrd_ctrl);
        return NULL;
    }

    /* initailize condition variable to flow control
     * work between main thread and request delegator thread */
    rc = pthread_cond_init(&(thrd_ctrl->thrd_cond), NULL);
    if (rc != 0) {
        LOGERR("pthread_cond_init failed for request "
            "manager thread app_id=%d client_id=%d rc=%d (%s)",
            app_id, client_id, rc, strerror(rc));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        free(thrd_ctrl->del_req_stat->req_stat);
        free(thrd_ctrl->del_req_stat);
        free(thrd_ctrl->del_req_set);
        free(thrd_ctrl);
        return NULL;
    }

    /* record app and client id this thread will be serving */
    thrd_ctrl->app_id    = app_id;
    thrd_ctrl->client_id = client_id;

    /* initialize flow control flags */
    thrd_ctrl->exit_flag              = 0;
    thrd_ctrl->has_waiting_delegator  = 0;
    thrd_ctrl->has_waiting_dispatcher = 0;

    /* TODO: can't we just tack this address onto tmp_config structure? */
    /* insert our thread control structure into our list of
     * active request manager threads, important to do this before
     * launching thread since it uses list to lookup its structure */
    rc = arraylist_add(thrd_list, thrd_ctrl);
    if (rc != 0) {
        pthread_cond_destroy(&(thrd_ctrl->thrd_cond));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        free(thrd_ctrl->del_req_stat->req_stat);
        free(thrd_ctrl->del_req_stat);
        free(thrd_ctrl->del_req_set);
        free(thrd_ctrl);
        return NULL;
    }

    /* launch request manager thread */
    rc = pthread_create(&(thrd_ctrl->thrd), NULL,
        rm_delegate_request_thread, (void *)thrd_ctrl);
    if (rc != 0) {
        LOGERR("pthread_create failed for request "
            "manager thread app_id=%d client_id=%d rc=%d (%s)",
            app_id, client_id, rc, strerror(rc));
        pthread_cond_destroy(&(thrd_ctrl->thrd_cond));
        pthread_mutex_destroy(&(thrd_ctrl->thrd_lock));
        free(thrd_ctrl->del_req_stat->req_stat);
        free(thrd_ctrl->del_req_stat);
        free(thrd_ctrl->del_req_set);
        free(thrd_ctrl);
        return NULL;
    }

    return thrd_ctrl;
}

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
static void unifycr_mount_rpc(hg_handle_t handle)
{
    int rc;

    /* get input params */
    unifycr_mount_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    /* read app_id and client_id from input */
    int app_id    = in.app_id;
    int client_id = in.local_rank_idx;

    /* lookup app_config for given app_id */
    app_config_t *tmp_config =
        (app_config_t *) arraylist_get(app_config_list, app_id);

    /* fill in and insert a new entry for this app_id
     * if we don't already have one */
    if (tmp_config == NULL) {
        /* don't have an app_config for this app_id,
         * so allocate and fill one in */
        tmp_config = (app_config_t *)malloc(sizeof(app_config_t));

        /* record size of shared memory regions */
        tmp_config->req_buf_sz    = in.req_buf_sz;
        tmp_config->recv_buf_sz   = in.recv_buf_sz;
        tmp_config->superblock_sz = in.superblock_sz;

        /* record offset and size of index entries */
        tmp_config->meta_offset = in.meta_offset;
        tmp_config->meta_size   = in.meta_size;

        /* record offset and size of file meta data entries */
        tmp_config->fmeta_offset = in.fmeta_offset;
        tmp_config->fmeta_size   = in.fmeta_size;

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
        }

        /* insert new app_config into our list, indexed by app_id */
        rc = arraylist_insert(app_config_list, app_id, tmp_config);
        if (rc != 0) {
            ret = rc;
        }
    }

    /* record client id of process on this node */
    tmp_config->client_ranks[client_id] = client_id;

    /* record global rank of client process for debugging */
    tmp_config->dbg_ranks[client_id] = in.dbg_rank;

    /* attach to shared memory regions of this client */
    rc = attach_to_shm(tmp_config, app_id, client_id);
    if (rc != ULFS_SUCCESS) {
        ret = rc;
    }

    /* open spill over files for this client */
    rc = open_log_file(tmp_config, app_id, client_id);
    if (rc < 0)  {
        ret = rc;
    }

    /* create request manager thread */
    thrd_ctrl_t *thrd_ctrl = unifycr_rm_thrd_create(app_id, client_id);
    if (thrd_ctrl != NULL) {
        /* TODO: seems like it would be cleaner to avoid thread_list
         * and instead just record address to struct */
        /* remember id for thread control for this client */
        tmp_config->thrd_idxs[client_id] = arraylist_size(thrd_list) - 1;
    } else {
        /* failed to create request manager thread */
        ret = UNIFYCR_FAILURE;
    }

    /* build output structure to return to caller */
    unifycr_mount_out_t out;
    out.ret = ret;
    out.max_recs_per_slice = max_recs_per_slice;

    /* send output back to caller */
    hg_return_t hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_mount_rpc)

static void unifycr_unmount_rpc(hg_handle_t handle)
{
    /* get input params */
    unifycr_unmount_in_t in;
    int margo_ret = margo_get_input(handle, &in);
    assert(margo_ret == HG_SUCCESS);

    /* read app_id and client_id from input */
    int app_id    = in.app_id;
    int client_id = in.local_rank_idx;

    /* lookup app_config for given app_id */
    app_config_t* app_config =
        (app_config_t *) arraylist_get(app_config_list, app_id);

    /* TODO: do cleanup here */

    /* get thread id for this client */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* look up thread control structure */
    thrd_ctrl_t* thrd_ctrl = (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    /* shutdown the delegator thread */
    rm_cmd_exit(thrd_ctrl);

    /* detach from the request shared memory */
    unifycr_shm_free(app_config->req_buf_name[client_id],
                     app_config->req_buf_sz,
                     &(app_config->shm_req_bufs[client_id]));
    app_config->shm_req_bufs[client_id] = NULL;

    /* detach from the read shared memory buffer */
    unifycr_shm_free(app_config->recv_buf_name[client_id],
                     app_config->recv_buf_sz,
                     &(app_config->shm_recv_bufs[client_id]));
    app_config->shm_recv_bufs[client_id] = NULL;

    /* destroy the sockets except for the ones for acks */
    sock_sanitize_cli(client_id);

    /* build output structure to return to caller */
    unifycr_mount_out_t out;
    out.ret = UNIFYCR_SUCCESS;

    /* send output back to caller */
    hg_return_t hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_unmount_rpc)

/* returns file meta data including file size and file name
 * given a global file id */
static void unifycr_metaget_rpc(hg_handle_t handle)
{
    /* get input params */
    unifycr_metaget_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    /* given the global file id, look up file attributes
     * from key/value store */
    unifycr_file_attr_t attr_val;
    ret = meta_process_attr_get(in.gfid, &attr_val);

    /* build our output values */
    unifycr_metaget_out_t out;
    out.st_size  = attr_val.file_attr.st_size;
    out.filename = attr_val.filename;
    out.ret      = ret;

    /* send output back to caller */
    hg_return_t hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_metaget_rpc)

/* given a global file id and a file name,
 * record key/value entry for this file */
static void unifycr_metaset_rpc(hg_handle_t handle)
{
    /* get input params */
    unifycr_metaset_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    /* store file name for given global file id */
    ret = meta_process_attr_set(in.gfid, in.filename);

    /* build our output values */
    unifycr_metaset_out_t out;
    out.ret = ret;

    /* return to caller */
    hg_return_t hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_metaset_rpc)

/* given app_id, client_id, and a global file id as input,
 * read extent location metadata from client shared memory
 * and insert corresponding key/value pairs into global index */
static void unifycr_fsync_rpc(hg_handle_t handle)
{
    /* get input params */
    unifycr_fsync_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    /* given global file id, read index metadata from client and
     * insert into global index key/value store */
    ret = meta_process_fsync(in.app_id, in.local_rank_idx, in.gfid);

    /* build our output values */
    unifycr_metaset_out_t out;
    out.ret = ret;

    /* return to caller */
    hg_return_t hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_fsync_rpc)

/* given an app_id, client_id, global file id, an offset, and a length,
 * initiate read operation to lookup and return data,
 * client synchronizes with server again later when data is available
 * to be copied into user buffers */
static void unifycr_read_rpc(hg_handle_t handle)
{
    /* get input params */
    unifycr_read_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    /* read data for a single read request from client,
     * returns data to client through shared memory */
    ret = rm_cmd_read(in.app_id, in.local_rank_idx,
        in.gfid, in.offset, in.length);

    /* build our output values */
    unifycr_read_out_t out;
    out.ret = ret;

    /* return to caller */
    hg_return_t hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    /* free margo resources */
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_read_rpc)

/* given an app_id, client_id, global file id, and a count
 * of read requests, follow by list of offset/length tuples
 * initiate read requests for data,
 * client synchronizes with server again later when data is available
 * to be copied into user buffers */
static void unifycr_mread_rpc(hg_handle_t handle)
{
    /* get input params */
    unifycr_mread_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    /* allocate buffer to hold array of read requests */
    hg_size_t size = in.bulk_size;
    void *buffer = (void *)malloc(size);
    assert(buffer);

    /* get pointer to mercury structures to set up bulk transfer */
    const struct hg_info *hgi = margo_get_info(handle);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* register local target buffer for bulk access */
    hg_bulk_t bulk_handle;
    hg_return_t hret = margo_bulk_create(mid, 1, &buffer,
        &size, HG_BULK_WRITE_ONLY, &bulk_handle);

    /* get list of read requests */
    hret = margo_bulk_transfer(mid, HG_BULK_PULL,
        hgi->addr, in.bulk_handle, 0, bulk_handle, 0, size);
    assert(hret == HG_SUCCESS);

    /* initiate read operations to fetch data for read requests */
    ret = rm_cmd_mread(in.app_id, in.local_rank_idx,
        in.gfid, in.read_count, buffer);

    /* build our output values */
    unifycr_mread_out_t out;
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
DEFINE_MARGO_RPC_HANDLER(unifycr_mread_rpc)
