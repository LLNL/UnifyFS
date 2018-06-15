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

#ifndef UNIFYCR_GLOBAL_H
#define UNIFYCR_GLOBAL_H

#include <pthread.h>
#include <stdlib.h>
#include "unifycr_configurator.h"
#include "arraylist.h"

typedef enum {
    COMM_MOUNT, /*the list of addrs: appid, size of buffer, offset of data section, metadata section*/
    COMM_META,
    COMM_READ,
    COMM_UNMOUNT,
    COMM_DIGEST,
    COMM_SYNC_DEL,
} cmd_lst_t;

typedef enum {
    XFER_COMM_DATA,
    XFER_COMM_EXIT,
} service_cmd_lst_t;

typedef enum {
    ACK_SUCCESS,
    ACK_FAIL,
} ack_status_t;

typedef struct {
    int dest_app_id;
    int dest_client_id; /*note: the id is the app-side rank on the remote node*/
    long dest_offset;
    int dest_delegator_rank;
    long length;
    int src_delegator_rank;
    int src_cli_id;
    int src_app_id;
    int src_fid;
    long src_offset;
    int src_thrd;
    int src_dbg_rank;
    int arrival_time;
} send_msg_t;

typedef struct {
    long src_fid;
    long src_offset;
    long length;
} recv_msg_t;


typedef struct {
    int num;
    send_msg_t msg_meta[MAX_META_PER_SEND];
} msg_meta_t;

typedef struct {
    long superblock_sz;
    long meta_offset;
    long meta_size;
    long fmeta_offset;
    long fmeta_size;
    long data_offset;
    long data_size;
    int req_buf_sz;
    int recv_buf_sz;
    int num_procs_per_node;
    int client_ranks[MAX_NUM_CLIENTS];
    int thrd_idxs[MAX_NUM_CLIENTS];
    int shm_superblock_fds[MAX_NUM_CLIENTS];
    int shm_req_fds[MAX_NUM_CLIENTS];
    int shm_recv_fds[MAX_NUM_CLIENTS];
    char *shm_superblocks[MAX_NUM_CLIENTS];
    char *shm_req_bufs[MAX_NUM_CLIENTS];
    char *shm_recv_bufs[MAX_NUM_CLIENTS];
    int spill_log_fds[MAX_NUM_CLIENTS];
    int spill_index_log_fds[MAX_NUM_CLIENTS];
    int dbg_ranks[MAX_NUM_CLIENTS];
    char external_spill_dir[UNIFYCR_MAX_FILENAME];
    char recv_buf_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char req_buf_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char super_buf_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char spill_log_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char spill_index_log_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
} app_config_t;

typedef struct {
    int req_cnt; /*number of requests to this delegator*/
    int del_id; /*rank of delegator*/
} per_del_stat_t;

typedef struct {
    per_del_stat_t *req_stat;
    int del_cnt; /*number of unique delegators*/
} del_req_stat_t;

typedef struct {
    pthread_t thrd;
    pthread_cond_t  thrd_cond;
    pthread_mutex_t thrd_lock;

    int has_waiting_delegator;
    int has_waiting_dispatcher;
    /* a list of read requests to
     * be sent to each
     * delegator*/
    msg_meta_t *del_req_set;

    /* statistics of read requests
     * to be sent to each delegator*/
    del_req_stat_t *del_req_stat;
    char del_req_msg_buf[REQ_BUF_LEN];
    char del_recv_msg_buf[RECV_BUF_CNT][RECV_BUF_LEN];
    int exit_flag;
} thrd_ctrl_t;

typedef struct {
    int app_id;
    int sock_id;
} cli_signature_t;

typedef int fattr_key_t;

typedef struct {
    char fname[UNIFYCR_MAX_FILENAME];
    struct stat file_attr;
} fattr_val_t;

extern arraylist_t *app_config_list;
extern arraylist_t *thrd_list;

int invert_sock_ids[MAX_NUM_CLIENTS];

extern pthread_t data_thrd;
extern int glb_rank, glb_size;
extern int local_rank_idx;
extern int *local_rank_lst;
extern int local_rank_cnt;
extern long max_recs_per_slice;

#endif
