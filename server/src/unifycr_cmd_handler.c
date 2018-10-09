/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2018, UT-Battelle, LLC.
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

/**
* handle client-side requests, including init, open, fsync,
* read, close, stat and unmount
*
* @param cmd_buf: received command from client
* @param sock_id: position in poll_set
* @return success/error code
*/
int delegator_handle_command(char *ptr_cmd, int sock_id)
{

    /*init setup*/

    int rc = 0, ret_sz = 0;
    int cmd = *((int *)ptr_cmd);
    char *ptr_ack = sock_get_ack_buf(sock_id);
    char *ptr_args = ptr_cmd + sizeof(cmd);

    switch (cmd) {
    case COMM_SYNC_DEL:
        /* ack the result */
        ret_sz = pack_ack_msg(ptr_ack, cmd, ACK_SUCCESS,
                              &local_rank_cnt, sizeof(int));
        rc = sock_ack_cli(sock_id, ret_sz);
        break;

    case COMM_MOUNT:
        rc = sync_with_client(ptr_cmd, sock_id);

        /* ack the result */
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc,
                              &max_recs_per_slice, sizeof(max_recs_per_slice));
        rc = sock_ack_cli(sock_id, ret_sz);
        break;

    case COMM_META_GET:
    {
        /*get file attribute*/
        unifycr_file_attr_t attr_val = { 0, };

        int gfid = *((int *)(ptr_cmd + sizeof(int)));

        // get the metadata from the metadata store
        rc = unifycr_get_file_attribute(gfid, &attr_val);

        ptr_ack = sock_get_ack_buf(sock_id);
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc,
                                &attr_val, sizeof(unifycr_file_attr_t));
        rc = sock_ack_cli(sock_id, ret_sz);

        break;
    }

    case COMM_META_SET:
    {
        // get the file attribute out of the command buffer
        unifycr_file_attr_t *fattr_ptr =
            (unifycr_file_attr_t *)(ptr_cmd + sizeof(int));

        rc = unifycr_set_file_attribute(fattr_ptr);

        /* ack the result */
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc, NULL, 0);
        rc = sock_ack_cli(sock_id, ret_sz);
        break;
    }

    case COMM_META_FSYNC:
        /* synchronize both index and file attribute
         * metadata to the key-value store */
        rc = meta_process_fsync(sock_id);

        /* ack the result */
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc, NULL, 0);
        rc = sock_ack_cli(sock_id, ret_sz);
        break;

    case COMM_READ:
        {
            size_t count = *(size_t *)ptr_args;
            /* result is handled by the individual thread in
             * the request manager */
            rc = rm_read_remote_data(sock_id, count);
            break;
        }

    case COMM_UNMOUNT:
        unifycr_broadcast_exit(sock_id);
        rc = ULFS_SUCCESS;
        break;

    case COMM_DIGEST:
        break;

    default:
        LOG(LOG_DBG, "rank:%d,Unsupported command type %d\n",
            glb_rank, cmd);
        rc = -1;
        break;
    }
    return rc;
}

/**
* pack the message to be returned to the client.
* format: command type, error code, payload
* @param ptr_cmd: command buffer
* @param rc: error code
* @param val: payload
* @return success/error code
*/
int pack_ack_msg(char *ptr_cmd, int cmd, int rc,
                 void *val, int val_len)
{
    int ret_sz = 0;

    memset(ptr_cmd, 0, CMD_BUF_SIZE);

    memcpy(ptr_cmd, &cmd, sizeof(cmd));
    ret_sz += sizeof(cmd);

    memcpy(ptr_cmd + ret_sz, &rc, sizeof(rc));
    ret_sz += sizeof(rc);

    if (val != NULL) {
        memcpy(ptr_cmd + ret_sz, val, val_len);
        ret_sz += val_len;
    }
    return ret_sz;
}

/**
* receive and store the client-side information,
* then attach to the client-side shared buffers.
*
* @param cmd_buf: received command from client
* @param sock_id: position in poll_set
* @return success/error code
*/
int sync_with_client(char *cmd_buf, int sock_id)
{
    unifycr_client_context_t client_ctx;

    // unpack the client context from the command buffer
    unifycr_unpack_client_context(cmd_buf + sizeof(int), &client_ctx);

    int app_id = client_ctx.app_id;
    int local_rank_idx = client_ctx.local_rank_index;
    int dbg_rank = client_ctx.dbg_rank;

    app_config_t *tmp_config;
    /* if this client is from a new application, then
     * initialize this application's information
     * */

    int rc;
    if (arraylist_get(app_config_list, app_id) == NULL) {
        /* LOG(LOG_DBG, "superblock_sz:%ld, num_procs_per_node:%ld,
         *     req_buf_sz:%ld, data_size:%ld\n",
         *     superblock_sz, num_procs_per_node, req_buf_sz, data_size); */
        tmp_config = (app_config_t *) calloc(1, sizeof(app_config_t));
        memcpy(tmp_config->external_spill_dir,
               client_ctx.external_spill_dir,
               sizeof(tmp_config->external_spill_dir));

        /*don't forget to free*/
        tmp_config->num_procs_per_node = client_ctx.num_procs_per_node;
        tmp_config->req_buf_sz = client_ctx.req_buf_sz;
        tmp_config->recv_buf_sz = client_ctx.recv_buf_sz;
        tmp_config->superblock_sz = client_ctx.superblock_sz;

        tmp_config->meta_offset = client_ctx.meta_offset;
        tmp_config->meta_size = client_ctx.meta_size;

        tmp_config->fmeta_offset = client_ctx.fmeta_offset;
        tmp_config->fmeta_size = client_ctx.fmeta_size;

        tmp_config->data_offset = client_ctx.data_offset;
        tmp_config->data_size = client_ctx.data_size;

        int i;
        for (i = 0; i < MAX_NUM_CLIENTS; i++) {
            tmp_config->client_ranks[i] = -1;
            tmp_config->shm_recv_bufs[i] = NULL;
            tmp_config->shm_req_bufs[i] = NULL;
            tmp_config->shm_superblocks[i] = NULL;
            tmp_config->shm_superblock_fds[i] = -1;
            tmp_config->shm_recv_fds[i] = -1;
            tmp_config->shm_req_fds[i] = -1;
            tmp_config->spill_log_fds[i] = -1;
            tmp_config->spill_index_log_fds[i] = -1;
        }

        rc = arraylist_insert(app_config_list, app_id, tmp_config);
        if (rc != 0) {
            return rc;
        }
    } else {
        tmp_config = (app_config_t *) arraylist_get(app_config_list,
                                                    app_id);
    }
    /* The following code attach a delegator thread
     * to this new connection */
    thrd_ctrl_t *thrd_ctrl = (thrd_ctrl_t *)
        calloc(1, sizeof(thrd_ctrl_t));

    thrd_ctrl->exit_flag = 0;
    cli_signature_t *cli_signature = (cli_signature_t *)
        calloc(1, sizeof(cli_signature_t));
    cli_signature->app_id = app_id;
    cli_signature->sock_id = sock_id;

    rc = pthread_mutex_init(&(thrd_ctrl->thrd_lock), NULL);
    if (rc != 0) {
        return (int)UNIFYCR_ERROR_THRDINIT;
    }

    rc = pthread_cond_init(&(thrd_ctrl->thrd_cond), NULL);
    if (rc != 0) {
        return (int)UNIFYCR_ERROR_THRDINIT;
    }

    thrd_ctrl->del_req_set = (msg_meta_t *)
        calloc(1, sizeof(msg_meta_t));
    if (!thrd_ctrl->del_req_set) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    thrd_ctrl->del_req_stat = (del_req_stat_t *)
        calloc(1, sizeof(del_req_stat_t));
    if (!thrd_ctrl->del_req_stat) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    thrd_ctrl->del_req_stat->req_stat = (per_del_stat_t *)
        calloc(glb_size, sizeof(per_del_stat_t));
    if (!thrd_ctrl->del_req_stat->req_stat) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    rc = arraylist_add(thrd_list, thrd_ctrl);
    if (rc != 0) {
        return rc;
    }

    tmp_config->thrd_idxs[sock_id] = arraylist_size(thrd_list) - 1;
    tmp_config->client_ranks[sock_id] = local_rank_idx;
    tmp_config->dbg_ranks[sock_id] = dbg_rank; /*add debug rank*/

    invert_sock_ids[sock_id] = app_id;

    rc = attach_to_shm(tmp_config, app_id, sock_id);
    if (rc != ULFS_SUCCESS) {
        return rc;
    }

    rc = open_log_file(tmp_config, app_id, sock_id);
    if (rc < 0) {
        return rc;
    }

    thrd_ctrl->has_waiting_delegator = 0;
    thrd_ctrl->has_waiting_dispatcher = 0;
    rc = pthread_create(&(thrd_ctrl->thrd), NULL,
                        rm_delegate_request_thread,
                        cli_signature);
    if (rc != 0) {
        return (int)UNIFYCR_ERROR_THRDINIT;
    }

    return rc;
}

/**
* attach to the client-side shared memory
* @param app_config: application information
* @param app_id: the server-side
* @param sock_id: position in poll_set in unifycr_sock.h
* @return success/error code
*/
int attach_to_shm(app_config_t *app_config, int app_id, int sock_id)
{
    int ret = 0;
    char shm_name[GEN_STR_LEN] = {0};

    int client_side_id = app_config->client_ranks[sock_id];

    /* attach shared superblock,
     * a superblock is created by each
     * client to store the raw file data.
     * The overflowed data are spilled to
     * SSD.
     * */
    sprintf(shm_name, "%d-super-%d", app_id, client_side_id);
    int tmp_fd = shm_open(shm_name,
                          MMAP_OPEN_FLAG, MMAP_OPEN_MODE);
    if (-1 == (ret = tmp_fd)) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    ret = ftruncate(tmp_fd, app_config->superblock_sz);
    if (-1 == ret) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    app_config->shm_superblock_fds[client_side_id] = tmp_fd;

    strcpy(app_config->super_buf_name[client_side_id], shm_name);
    app_config->shm_superblocks[client_side_id] =
        mmap(NULL, app_config->superblock_sz, PROT_READ | PROT_WRITE,
             MAP_SHARED, tmp_fd, SEEK_SET);
    if (NULL == app_config->shm_superblocks[client_side_id]) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }

    /* attach shared request buffer,
     * a request buffer is created by each
     * client to convey the client-side read
     * request to the delegator
     * */
    sprintf(shm_name, "%d-req-%d", app_id, client_side_id);
    tmp_fd = shm_open(shm_name, MMAP_OPEN_FLAG, MMAP_OPEN_MODE);
    if (-1 == (ret = tmp_fd)) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }

    ret = ftruncate(tmp_fd, app_config->req_buf_sz);
    if (-1 == ret) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    app_config->shm_req_fds[client_side_id] = tmp_fd;

    strcpy(app_config->req_buf_name[client_side_id], shm_name);
    app_config->shm_req_bufs[client_side_id] = mmap(NULL,
            app_config->req_buf_sz, PROT_READ | PROT_WRITE,
            MAP_SHARED, tmp_fd, SEEK_SET);
    if (NULL == app_config->shm_req_bufs[client_side_id]) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }


    /* initialize shared receive buffer,
     * a request buffer is created by each
     * client for the delegator to
     * temporarily buffer the received
     * data for this client
     *
     * */
    sprintf(shm_name, "%d-recv-%d", app_id, client_side_id);

    tmp_fd = shm_open(shm_name, MMAP_OPEN_FLAG, MMAP_OPEN_MODE);
    if (-1 == (ret = tmp_fd)) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    ret = ftruncate(tmp_fd, app_config->recv_buf_sz);
    if (-1 == ret) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }
    app_config->shm_recv_fds[client_side_id] = tmp_fd;

    strcpy(app_config->recv_buf_name[client_side_id], shm_name);
    app_config->shm_recv_bufs[client_side_id] =
        mmap(NULL, app_config->recv_buf_sz, PROT_READ | PROT_WRITE,
             MAP_SHARED, tmp_fd, SEEK_SET);
    if (NULL == app_config->shm_recv_bufs[client_side_id]) {
        return (int)UNIFYCR_ERROR_SHMEM;
    }

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
int open_log_file(app_config_t *app_config,
                  int app_id, int sock_id)
{
    int client_side_id = app_config->client_ranks[sock_id];
    char path[UNIFYCR_MAX_FILENAME] = {0};

    snprintf(path, sizeof(path), "%s/spill_%d_%d.log",
            app_config->external_spill_dir, app_id, client_side_id);
    /*  LOG(LOG_DBG, "opening log file %s, client_side_id:%d\n",
     *      path, client_side_id);
     */
    app_config->spill_log_fds[client_side_id] = open(path, O_RDONLY, 0666);
    if (app_config->spill_log_fds[client_side_id] < 0) {
        fprintf(stderr,
                "ERROR - rank:%d, failed to open spill data file %s\n",
                glb_rank, path);
        fflush(stderr);
        return (int)UNIFYCR_ERROR_FILE;
    }
    strcpy(app_config->spill_log_name[client_side_id], path);

    snprintf(path, sizeof(path), "%s/spill_index_%d_%d.log",
            app_config->external_spill_dir, app_id, client_side_id);
    /*  LOG(LOG_DBG, "opening index log file %s, client_side_id:%d\n",
     *      path, client_side_id);
     */
    app_config->spill_index_log_fds[client_side_id] =
        open(path, O_RDONLY, 0666);
    if (app_config->spill_index_log_fds[client_side_id] < 0) {
        fprintf(stderr,
                "ERROR - rank:%d, failed to open spill index file %s\n",
                glb_rank, path);
        fflush(stderr);
        return (int)UNIFYCR_ERROR_FILE;
    }
    strcpy(app_config->spill_index_log_name[client_side_id], path);

    return ULFS_SUCCESS;
}

/**
* broadcast the exit command to all other
* delegators in the job
* @return success/error code
*/
int unifycr_broadcast_exit(int sock_id)
{
    int exit_cmd = XFER_COMM_EXIT, rc = ULFS_SUCCESS;
    int i;
    for (i = 0; i < glb_size; i++) {
        MPI_Send(&exit_cmd, sizeof(int), MPI_CHAR,
                 i,
                 CLI_DATA_TAG, MPI_COMM_WORLD);
    }

    int cmd = COMM_UNMOUNT;

    char *ptr_ack = sock_get_ack_buf(sock_id);
    int ret_sz = pack_ack_msg(ptr_ack, cmd, rc, NULL, 0);
    rc = sock_ack_cli(sock_id, ret_sz);
    return rc;
}
