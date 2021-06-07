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
#include "unifyfs_inode.h"
#include "unifyfs_fops.h"
#include "unifyfs_metadata_mdhim.h"

typedef struct {
    client_rpc_e req_type;
    hg_handle_t handle;
    void* input;
    void* bulk_buf;
    size_t bulk_sz;
} client_rpc_req_t;

typedef struct {
    readreq_status_e status;   /* aggregate request status */
    int in_use;                /* currently using this req? */
    int req_ndx;               /* index in reqmgr read_reqs array */
    int app_id;                /* app id of requesting client process */
    int client_id;             /* client id of requesting client process */
    int client_mread;          /* client mread id */
    int client_read_ndx;       /* client mread request index */
    int num_server_reads;      /* size of remote_reads array */
    chunk_read_req_t* chunks;  /* array of chunk-reads */
    server_chunk_reads_t* remote_reads; /* per-server remote reads array */
    unifyfs_inode_extent_t extent; /* the requested extent */
} server_read_req_t;

/* Request manager state structure - created by main thread for each request
 * manager thread. Contains shared data structures for client-server and
 * server-server requests and associated synchronization constructs */
typedef struct reqmgr_thrd {
    /* request manager (RM) thread */
    pthread_t thrd;
    pid_t tid;

    /* condition variable to synchronize request manager thread
     * and margo rpc handler ULT delivering work */
    pthread_cond_t thrd_cond;

    /* lock for shared data structures (variables below) */
    pthread_mutex_t thrd_lock;

    /* flag indicating request manager thread is waiting on thrd_cond CV */
    int waiting_for_work;

    /* flag indicating a margo rpc handler ULT is waiting on thrd_cond CV */
    int has_waiting_dispatcher;

    /* argobots mutex for synchronizing access to request state between
     * margo rpc handler ULTs and request manager thread */
    ABT_mutex reqs_sync;

    /* array of server read requests */
    int num_read_reqs;
    int next_rdreq_ndx;
    server_read_req_t read_reqs[RM_MAX_SERVER_READS];

    /* list of client rpc requests */
    arraylist_t* client_reqs;

    /* flag set to indicate request manager thread should exit */
    int exit_flag;

    /* flag set after thread has exited and join completed */
    int exited;

    /* app_id this thread is serving */
    int app_id;

    /* client_id this thread is serving */
    int client_id;
} reqmgr_thrd_t;

/* reserve/release read requests */
server_read_req_t* rm_reserve_read_req(reqmgr_thrd_t* thrd_ctrl);
int rm_release_read_req(reqmgr_thrd_t* thrd_ctrl,
                        server_read_req_t* rdreq);

/* issue remote chunk read requests for extent chunks
 * listed within keyvals */
int rm_create_chunk_requests(reqmgr_thrd_t* thrd_ctrl,
                             server_read_req_t* rdreq,
                             int num_vals,
                             unifyfs_keyval_t* keyvals);

/* create Request Manager thread for application client */
reqmgr_thrd_t* unifyfs_rm_thrd_create(int app_id,
                                      int client_id);

/* Request Manager pthread main */
void* request_manager_thread(void* arg);

/* function called by main thread to instruct
 * resource manager thread to exit,
 * returns UNIFYFS_SUCCESS on success */
int rm_request_exit(reqmgr_thrd_t* thrd_ctrl);

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
                                   server_chunk_reads_t* del_reads);

/**
 * @brief hand over a read request to the request manager thread.
 *
 * @param req all members except for status and req_ndx need to be filled by
 * the caller. @req->chunks and @req->remote_reads should be allocated from
 * heap, and should not be freed by the caller.
 *
 * @return 0 on success, errno otherwise
 */
int rm_submit_read_request(server_read_req_t* req);

/**
 * @brief submit a client rpc request to the request manager thread.
 *
 * @param ctx   application client context
 * @param req   pointer to client rpc request struct
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int rm_submit_client_rpc_request(unifyfs_fops_ctx_t* ctx,
                                 client_rpc_req_t* req);

/* MARGO SERVER-SERVER RPC INVOCATION FUNCTIONS */

int invoke_chunk_read_request_rpc(int dst_srvr_rank,
                                  server_read_req_t* rdreq,
                                  int num_chunks,
                                  void* data_buf, size_t buf_sz);

/* create arraylist of failed clients for cleanup */
void queue_cleanup(reqmgr_thrd_t* thrd_ctrl);

#endif
