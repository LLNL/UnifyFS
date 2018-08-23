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
#include "../../client/src/unifycr_clientcalls_rpc.h"

/**
* handle client-side requests, including init, open, fsync,
* read, close, stat and unmount
*
* @param cmd_buf: received command from client
* @param sock_id: position in poll_set
* @return success/error code
*/
#if 0
int delegator_handle_command(char *ptr_cmd, int sock_id)
{

    /*init setup*/

    int rc = 0, ret_sz = 0;
    int cmd = *((int *)ptr_cmd);
    char *ptr_ack;

    switch (cmd) {
    case COMM_SYNC_DEL:
        (void)0;
        ptr_ack = sock_get_ack_buf(sock_id);
        ret_sz = pack_ack_msg(ptr_ack, cmd, ACK_SUCCESS,
                              &local_rank_cnt, sizeof(int));
        rc = sock_ack_cli(sock_id, ret_sz);
        return rc;

    case COMM_MOUNT:
        rc = sync_with_client(ptr_cmd, sock_id);

        ptr_ack = sock_get_ack_buf(sock_id);
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc,
                              &max_recs_per_slice, sizeof(long));
        rc = sock_ack_cli(sock_id, ret_sz);
        return rc;

    case COMM_META_GET:
        (void)0;
        /*get file attribute*/
        unifycr_file_attr_t attr_val = { 0, };

        fattr_key_t _fattr_key = *((int *)(ptr_cmd + 1 * sizeof(int)));

        rc = meta_process_attr_get(&_fattr_key, &attr_val);

        ptr_ack = sock_get_ack_buf(sock_id);
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc,
                                &attr_val, sizeof(unifycr_file_attr_t));
        rc = sock_ack_cli(sock_id, ret_sz);

        break;

    case COMM_META_SET:
        (void)0;
        /*set file attribute*/
        rc = meta_process_attr_set(ptr_cmd, sock_id);

        ptr_ack = sock_get_ack_buf(sock_id);
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc, NULL, 0);
        rc = sock_ack_cli(sock_id, ret_sz);
        /*ToDo: deliver the error code/success to client*/

        break;

    case COMM_META_FSYNC:
        /* synchronize both index and file attribute
         * metadata to the key-value store
         */

        rc = meta_process_fsync(sock_id);

        /*ack the result*/
        ptr_ack = sock_get_ack_buf(sock_id);
        ret_sz = pack_ack_msg(ptr_ack, cmd, rc, NULL, 0);
        rc = sock_ack_cli(sock_id, ret_sz);

        break;

#if 0
    case COMM_META:
        (void)0;
        int type = *((int *)ptr_cmd + 1);
        if (type == 1) {
            /*get file attribute*/
            unifycr_file_attr_t attr_val;
            //rc = meta_process_attr_get(ptr_cmd,
             //                          sock_id, &attr_val);

            ptr_ack = sock_get_ack_buf(sock_id);
            ret_sz = pack_ack_msg(ptr_ack, cmd, rc,
                                  &attr_val, sizeof(unifycr_file_attr_t));
            rc = sock_ack_cli(sock_id, ret_sz);

        }

        if (type == 2) {
            /*set file attribute*/
            //rc = meta_process_attr_set(ptr_cmd, sock_id);

            ptr_ack = sock_get_ack_buf(sock_id);
            ret_sz = pack_ack_msg(ptr_ack, cmd, rc, NULL, 0);
            rc = sock_ack_cli(sock_id, ret_sz);
            /*ToDo: deliver the error code/success to client*/
        }

        if (type == 3) {
            /*synchronize both index and file attribute
             *metadata to the key-value store*/

            rc = meta_process_fsync(sock_id);

            /*ack the result*/
            ptr_ack = sock_get_ack_buf(sock_id);
            ret_sz = pack_ack_msg(ptr_ack, cmd, rc, NULL, 0);
            rc = sock_ack_cli(sock_id, ret_sz);
        }
        break;
#endif

    case COMM_READ:
        (void)0;
        int num = *(((int *)ptr_cmd) + 1);
        /* result is handled by the individual thread in
         * the request manager*/
        rc = rm_read_remote_data(sock_id, num);
        break;

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
#endif

/**
* pack the message to be returned to the client.
* format: command type, error code, payload
* @param ptr_cmd: command buffer
* @param rc: error code
* @param val: payload
* @return success/error code
*/
int pack_ack_msg(char *ptr_cmd, int cmd, int rc, void *val,
                 int val_len)
{
    int ret_sz = 0;

    memset(ptr_cmd, 0, CMD_BUF_SIZE);
    memcpy(ptr_cmd, &cmd, sizeof(int));
    ret_sz += sizeof(int);
    memcpy(ptr_cmd + sizeof(int), &rc, sizeof(int));
    ret_sz += sizeof(int);

    if (val != NULL) {
        memcpy(ptr_cmd + 2 * sizeof(int), val, val_len);
        ret_sz += val_len;
    }
    return ret_sz;
}

static void unifycr_mount_rpc(hg_handle_t handle)
{
    unifycr_mount_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);
	printf("mount called for app_id %d\n", in.app_id);

    unifycr_mount_out_t out;
    //const struct hg_info* hgi = margo_get_info(handle);
    //assert(hgi);
    //margo_instance_id mid = margo_hg_info_get_instance(hgi);
    out.ret = 0;
    app_config_t *tmp_config;
    int rc = 0;
    if (arraylist_get(app_config_list, in.app_id) == NULL) {
        tmp_config = (app_config_t *)malloc(sizeof(app_config_t));
        tmp_config->num_procs_per_node = in.num_procs_per_node;
        tmp_config->req_buf_sz = in.req_buf_sz;
        tmp_config->recv_buf_sz = in.recv_buf_sz;
        tmp_config->superblock_sz = in.superblock_sz;

        tmp_config->meta_offset = in.meta_offset;
        tmp_config->meta_size = in.meta_size;

        tmp_config->fmeta_offset = in.fmeta_offset;
        tmp_config->fmeta_size = in.fmeta_size;

        tmp_config->data_offset = in.data_offset;
        tmp_config->data_size = in.data_size;
		printf("external spill dir: [%s]\n", in.external_spill_dir);
        strcpy(tmp_config->external_spill_dir, in.external_spill_dir);

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

        rc = arraylist_insert(app_config_list, in.app_id, tmp_config);
		printf("arraylist_insert returned %d for app_id %d\n", rc, in.app_id);
        if (rc != 0) {
			printf("insert failed: %d\n", rc);
            out.ret = rc;
        }
    } else {
        tmp_config = (app_config_t *)arraylist_get(app_config_list,
                     in.app_id);
    }
    thrd_ctrl_t* thrd_ctrl = NULL;
    cli_signature_t* cli_signature = NULL;
    if ( out.ret == 0 ) {
        /* The following code attach a delegator thread
        * to this new connection */
        thrd_ctrl = (thrd_ctrl_t *)malloc(sizeof(thrd_ctrl_t));
        memset(thrd_ctrl, 0, sizeof(thrd_ctrl_t));

        cli_signature = (cli_signature_t *)malloc(sizeof(cli_signature_t));
        cli_signature->app_id = in.app_id;
        cli_signature->sock_id = in.local_rank_idx;
        rc = pthread_mutex_init(&(thrd_ctrl->thrd_lock), NULL);
        if (rc != 0) {
			printf("pthread_mutex_init failed: %d\n", rc);
            out.ret = ULFS_ERROR_THRDINIT;
        }
    }

    if ( out.ret == 0 ) {
        rc = pthread_cond_init(&(thrd_ctrl->thrd_cond), NULL);
        if (rc != 0) {
			printf("pthread_cond_init failed: %d\n", rc);
            out.ret = ULFS_ERROR_THRDINIT;
        }
    }

    if ( out.ret == 0 ) {
        thrd_ctrl->del_req_set = (msg_meta_t *)malloc(sizeof(msg_meta_t));
        if (!thrd_ctrl->del_req_set) {
			printf("thread_ctrl req_set ealloc failed: %d\n", rc);
            out.ret =  ULFS_ERROR_NOMEM;
        }
    }
	
	if (out.ret == 0 ) {
    	memset(thrd_ctrl->del_req_set, 0, sizeof(msg_meta_t));

    	thrd_ctrl->del_req_stat =
        	(del_req_stat_t *)malloc(sizeof(del_req_stat_t));

		if (!thrd_ctrl->del_req_stat) {
			printf("thread_ctrl del_req_stat malloc failed: %d\n", rc);
       		out.ret = ULFS_ERROR_NOMEM;
		}
    }
	
	if (out.ret == 0 ) {
		memset(thrd_ctrl->del_req_stat, 0, sizeof(del_req_stat_t));

    	thrd_ctrl->del_req_stat->req_stat =
       	 (per_del_stat_t *)malloc(sizeof(per_del_stat_t) * glb_size);
    	if (!thrd_ctrl->del_req_stat->req_stat) {
			printf("thread_ctrl del_req_stat->req_stat malloc failed: %d\n", rc);
			out.ret = ULFS_ERROR_NOMEM;
		}
	}


	if ( out.ret == 0 ) {
		memset(thrd_ctrl->del_req_stat->req_stat,
           0, sizeof(per_del_stat_t) * glb_size);
		rc = arraylist_add(thrd_list, thrd_ctrl);
    	if (rc != 0) {
			printf("arraylist_addthrd_list, thrd_ctrl failed: %d\n", rc);
			out.ret = rc;
		}
    }


	if ( out.ret == 0 ) {
		int client_id = in.local_rank_idx;
		printf("setting thread_idxs[%d] to %d\n", client_id, arraylist_size(thrd_list) - 1); 
    	tmp_config->thrd_idxs[client_id] = arraylist_size(thrd_list) - 1;
    	tmp_config->client_ranks[client_id] = in.local_rank_idx;
    	tmp_config->dbg_ranks[client_id] = 0; /*add debug rank*/

	    rc = attach_to_shm(tmp_config, in.app_id, in.local_rank_idx);
   		if (rc != ULFS_SUCCESS) {
			printf("attach to shm failed: %d\n", rc);
        	out.ret = rc;
		}
    }

	if ( out.ret == 0 ) {
    	rc = open_log_file(tmp_config, in.app_id, in.local_rank_idx);
    	if (rc < 0)  {
			printf("open log file failed: %d\n", rc);
       		out.ret = rc;
		}
    }

	if ( out.ret == 0 ) {
		thrd_ctrl->has_waiting_delegator = 0;
    	thrd_ctrl->has_waiting_dispatcher = 0;
    	rc = pthread_create(&(thrd_ctrl->thrd), NULL, rm_delegate_request_thread,
                        	cli_signature);
    	if (rc != 0) {
			printf("pthread create failed: %d\n", rc);
       		out.ret = ULFS_ERROR_THRDINIT;
		}
    }

	if (out.ret == 0) {
		printf("max recs per slice = %ld\n",  max_recs_per_slice);
		out.max_recs_per_slice = max_recs_per_slice;
	}

    margo_free_input(handle, &in);

    hg_return_t hret = margo_respond(handle, &out);

    assert(hret == HG_SUCCESS);

    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_mount_rpc)

static void unifycr_metaget_rpc(hg_handle_t handle)
{
    unifycr_metaget_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

	printf("getting attribute for %d\n", in.gfid);
    unifycr_metaget_out_t out;

    unifycr_file_attr_t attr_val;
    ret = meta_process_attr_get(in.gfid, &attr_val);
	out.st_size = attr_val.file_attr.st_size;
    out.filename = attr_val.filename;
	printf("filename: %s\n", out.filename);
    out.ret = ret;

    margo_free_input(handle, &in);

    hg_return_t hret = margo_respond(handle, &out);

    assert(hret == HG_SUCCESS);

    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_metaget_rpc)

static void unifycr_metaset_rpc(hg_handle_t handle)
{
    unifycr_metaset_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    unifycr_metaset_out_t out;

	printf("Setting attribute for %d\n", in.gfid);
    out.ret = meta_process_attr_set(in.gfid, in.filename);

    margo_free_input(handle, &in);

    hg_return_t hret = margo_respond(handle, &out);

    assert(hret == HG_SUCCESS);

    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_metaset_rpc)

static void unifycr_fsync_rpc(hg_handle_t handle)
{
    unifycr_fsync_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    unifycr_fsync_out_t out;
    out.ret = meta_process_fsync(in.app_id, in.local_rank_idx, in.gfid);

    hg_return_t hret = margo_respond(handle, &out);

    assert(hret == HG_SUCCESS);

    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifycr_fsync_rpc)

static void unifycr_read_rpc(hg_handle_t handle)
{
    unifycr_read_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    unifycr_read_out_t out;

	const struct hg_info* hgi = margo_get_info(handle);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    out.ret = rm_read_remote_data(in.app_id, in.local_rank_idx, in.gfid, in.offset, in.length);

	printf("completed rm_read_remote_data for gfid: %d, offset: %d, length: %d\n", in.gfid, in.offset, in.length);
    margo_free_input(handle, &in);

    hg_return_t hret = margo_respond(handle, &out);
	printf("responded in rpc handler\n");

    assert(hret == HG_SUCCESS);

    margo_destroy(handle);
	printf("end of read rpc handler\n");
}
DEFINE_MARGO_RPC_HANDLER(unifycr_read_rpc)

static void unifycr_mread_rpc(hg_handle_t handle)
{
    unifycr_mread_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    unifycr_mread_out_t out;

	printf("calling rm_read_remote_data for gfid: %d, read_count: %d, bulk_size: %d\n", in.gfid, in.read_count, in.bulk_size);
	hg_size_t size = in.bulk_size;
	void* buffer = (void*)malloc(size);
	assert(buffer);
	const struct hg_info* hgi = margo_get_info(handle);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* register local target buffer for bulk access */
	hg_bulk_t bulk_handle;

    hg_return_t hret = margo_bulk_create(mid, 1, &buffer,
        &size, HG_BULK_WRITE_ONLY, &bulk_handle);
 
	hret = margo_bulk_transfer(mid, HG_BULK_PULL,
        hgi->addr, in.bulk_handle, 0,
        bulk_handle, 0, size);
    assert(hret == HG_SUCCESS);

    out.ret = rm_mread_remote_data(in.app_id, in.local_rank_idx, in.gfid, in.read_count,
                                  buffer);

	printf("completed rm_read_remote_data for gfid: %d, read_count: %d\n", in.gfid, in.read_count);
    margo_free_input(handle, &in);

    hret = margo_respond(handle, &out);
	printf("responded in rpc handler\n");

    assert(hret == HG_SUCCESS);

	margo_bulk_free(bulk_handle);
    margo_destroy(handle);
	free(buffer);
	printf("end of rpc handler\n");
}
DEFINE_MARGO_RPC_HANDLER(unifycr_mread_rpc)

/**
* receive and store the client-side information,
* then attach to the client-side shared buffers.
*
* @param cmd_buf: received command from client
* @param sock_id: position in poll_set
* @return success/error code
*/
#if 0
int sync_with_client(char *cmd_buf, int sock_id)
{
    unifycr_client_context_t client_ctx;
    // increase offset by size of cmd
    off_t offset;

    offset = 0;
    offset += sizeof(int);

    // unpack the client context from the command buffer
    unifycr_unpack_client_context(cmd_buf, &offset, &client_ctx);

    int app_id = client_ctx.app_id;
    int local_rank_idx = client_ctx.local_rank_index;
    int dbg_rank = client_ctx.dbg_rank;

    app_config_t *tmp_config;
    /*if this client is from a new application, then
     * initialize this application's information
     * */

    int rc;
    if (arraylist_get(app_config_list, app_id) == NULL) {
        /*          LOG(LOG_DBG, "superblock_sz:%ld, num_procs_per_node:%ld,
                             req_buf_sz:%ld, data_size:%ld\n",
                            superblock_sz, num_procs_per_node, req_buf_sz, data_size); */
        tmp_config = (app_config_t *)malloc(sizeof(app_config_t));
        memcpy(tmp_config->external_spill_dir,
               client_ctx.external_spill_dir, UNIFYCR_MAX_FILENAME);

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
        tmp_config = (app_config_t *)arraylist_get(app_config_list,
                     app_id);
    }
    /* The following code attach a delegator thread
     * to this new connection */
    thrd_ctrl_t *thrd_ctrl =
        (thrd_ctrl_t *)malloc(sizeof(thrd_ctrl_t));
    memset(thrd_ctrl, 0, sizeof(thrd_ctrl_t));

    thrd_ctrl->exit_flag = 0;
    cli_signature_t *cli_signature =
        (cli_signature_t *)malloc(sizeof(cli_signature_t));
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

    thrd_ctrl->del_req_set =
        (msg_meta_t *)malloc(sizeof(msg_meta_t));
    if (!thrd_ctrl->del_req_set) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }
    memset(thrd_ctrl->del_req_set,
           0, sizeof(msg_meta_t));

    thrd_ctrl->del_req_stat =
        (del_req_stat_t *)malloc(sizeof(del_req_stat_t));
    if (!thrd_ctrl->del_req_stat) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }
    memset(thrd_ctrl->del_req_stat, 0, sizeof(del_req_stat_t));

    thrd_ctrl->del_req_stat->req_stat =
        (per_del_stat_t *)malloc(sizeof(per_del_stat_t) * glb_size);
    if (!thrd_ctrl->del_req_stat->req_stat) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }
    memset(thrd_ctrl->del_req_stat->req_stat,
           0, sizeof(per_del_stat_t) * glb_size);
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
    rc = pthread_create(&(thrd_ctrl->thrd), NULL, rm_delegate_request_thread,
                        cli_signature);
    if (rc != 0) {
        return (int)UNIFYCR_ERROR_THRDINIT;
    }

    return rc;
}
#endif
/**
* attach to the client-side shared memory
* @param app_config: application information
* @param app_id: the server-side
* @param sock_id: position in poll_set in unifycr_sock.h
* @return success/error code
*/
int attach_to_shm(app_config_t *app_config, int app_id, int client_side_id)
{
    int ret = 0;
    char shm_name[GEN_STR_LEN] = {0};

    //client_side_id = app_config->client_ranks[sock_get_id()];

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
	printf("shm_req_buf: %p\n", app_config->shm_req_bufs[client_side_id]);
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
                  int app_id, int client_side_id)
{
    char path[UNIFYCR_MAX_FILENAME] = {0};
    snprintf(path, sizeof(path), "%s/spill_%d_%d.log",
            app_config->external_spill_dir, app_id, client_side_id);

    app_config->spill_log_fds[client_side_id] = open(path, O_RDONLY, 0666);
    printf("openning log file %s, client_side_id:%d\n",
                path, client_side_id);
    strcpy(app_config->spill_log_name[client_side_id], path);
    if (app_config->spill_log_fds[client_side_id] < 0) {
        printf("rank:%d, openning file %s failure\n", glb_rank, path);
        fflush(stdout);
        return (int)UNIFYCR_ERROR_FILE;
    }

    snprintf(path, sizeof(path), "%s/spill_index_%d_%d.log",
            app_config->external_spill_dir, app_id, client_side_id);
	app_config->spill_index_log_fds[client_side_id] =
			open(path, O_RDONLY, 0666);
    strcpy(app_config->spill_index_log_name[client_side_id], path);
    if (app_config->spill_index_log_fds[client_side_id] < 0) {
        printf("rank:%d, openning index file %s failure\n", glb_rank, path);
        fflush(stdout);
        return (int)UNIFYCR_ERROR_FILE;
    }

    return ULFS_SUCCESS;
}

/**
* broad cast the exit command to all other
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
