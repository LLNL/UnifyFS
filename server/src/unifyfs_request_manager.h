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

#ifndef UNIFYFS_REQUEST_MANAGER_H
#define UNIFYFS_REQUEST_MANAGER_H

#include "unifyfs_global.h"

typedef struct {
    readreq_status_e status;   /* aggregate request status */
    int req_ndx;               /* index in reqmgr read_reqs array */
    int app_id;                /* app id of requesting client process */
    int client_id;             /* client id of requesting client process */
    int num_remote_reads;      /* size of remote_reads array */
    client_read_req_t extent;  /* client read extent, includes gfid */
    chunk_read_req_t* chunks;  /* array of chunk-reads */
    remote_chunk_reads_t* remote_reads; /* per-delegator remote reads array */
} server_read_req_t;

/* this structure is created by the main thread for each request
 * manager thread, contains shared data structures where main thread
 * issues read requests and request manager processes them, contains
 * condition variable and lock for coordination between threads */
typedef struct reqmgr_thrd {
    /* request manager thread */
    pthread_t thrd;

    /* condition variable to synchronize request manager thread
     * and main thread delivering work */
    pthread_cond_t thrd_cond;

    /* lock for shared data structures (variables below) */
    pthread_mutex_t thrd_lock;

    /* flag indicating that request manager thread is waiting
     * for work inside of critical region */
    int has_waiting_delegator;

    /* flag indicating main thread is in critical section waiting
     * for request manager thread */
    int has_waiting_dispatcher;

    int num_read_reqs;
    int next_rdreq_ndx;
    server_read_req_t read_reqs[RM_MAX_ACTIVE_REQUESTS];

    /* buffer to build read request messages */
    char del_req_msg_buf[REQ_BUF_LEN];

    /* flag set to indicate request manager thread should exit */
    int exit_flag;

    /* flag set after thread has exited and join completed */
    int exited;

    /* app_id this thread is serving */
    int app_id;

    /* client_id this thread is serving */
    int client_id;
} reqmgr_thrd_t;


/* create Request Manager thread for application client */
reqmgr_thrd_t* unifyfs_rm_thrd_create(int app_id,
                                      int client_id);

/* Request Manager pthread main */
void* rm_delegate_request_thread(void* arg);

/* functions called by rpc handlers to assign work
 * to request manager threads */
int rm_cmd_mread(int app_id, int client_id,
                 size_t req_num, void* reqbuf);

int rm_cmd_read(int app_id, int client_id, int gfid,
                size_t offset, size_t length);

int rm_cmd_filesize(int app_id, int client_id, int gfid, size_t* outsize);

/* create a file or update attribute */
int rm_cmd_metaset(int app_id, int client_id, int gfid, int create,
                   unifyfs_file_attr_t* attr);

/* truncate file to specified size */
int rm_cmd_truncate(int app_id, int client_id, int gfid, size_t size);

/* delete file */
int rm_cmd_unlink(int app_id, int client_id, int gfid);

/* laminate file */
int rm_cmd_laminate(int app_id, int client_id, int gfid);

/* function called by main thread to instruct
 * resource manager thread to exit,
 * returns UNIFYFS_SUCCESS on success */
int rm_cmd_exit(reqmgr_thrd_t* thrd_ctrl);

/* retrieve all write index entries for app-client and
 * store them in global metadata */
int rm_cmd_sync(int app_id, int client_side_id);

/* update state for remote chunk reads with received response data */
int rm_post_chunk_read_responses(int app_id,
                                 int client_id,
                                 int src_rank,
                                 int req_id,
                                 int num_chks,
                                 size_t bulk_sz,
                                 char* resp_buf);

/* process the requested chunk data returned from service managers */
int rm_handle_chunk_read_responses(reqmgr_thrd_t* thrd_ctrl,
                                   server_read_req_t* rdreq,
                                   remote_chunk_reads_t* del_reads);

/* MARGO SERVER-SERVER RPC INVOCATION FUNCTIONS */

#if 0 // DISABLE UNUSED RPCS
int invoke_server_hello_rpc(int dst_srvr_rank);

int invoke_server_request_rpc(int dst_srvr_rank,
                              int req_id,
                              int tag,
                              void* data_buf, size_t buf_sz);
#endif // DISABLE UNUSED RPCS

int invoke_chunk_read_request_rpc(int dst_srvr_rank,
                                  server_read_req_t* rdreq,
                                  int num_chunks,
                                  void* data_buf, size_t buf_sz);

#endif
