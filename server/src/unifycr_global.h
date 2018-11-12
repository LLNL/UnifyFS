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

/* defines commands for messages sent to service manager threads */
typedef enum {
    XFER_COMM_DATA, /* message contains read requests */
    XFER_COMM_EXIT, /* indicates service manager thread should exit */
} service_cmd_lst_t;

/* this defines a read request as sent from the request manager to the
 * service manager, it contains info about the physical location of
 * the data:
 *
 *   dest_delegator_rank - rank of delegator hosting data log file
 *   dest_app_id, dest_client_id - defines file on host delegator
 *   dest_offset - phyiscal offset of data in log file
 *   length - number of bytes to be read
 *
 * it also contains a return address to use in the read reply that
 * the service manager sends back to the request manager:
 * 
 *   src_delegator_rank - rank of requesting delegator process
 *   src_thrd - thread id of request manager (used to compute MPI tag)
 *   src_app_id, src_cli_id
 *   src_fid - global file id
 *   src_offset - starting offset in logical file
 *   length - number of bytes
 *   src_dbg_rank - rank of application process making the request
 *
 * the arrival_time field is included but not set by the request
 * manager, it is used to tag the time the request reaches the
 * service manager for prioritizing read replies */
typedef struct {
    int dest_app_id;         /* app id of log file */
    int dest_client_id;      /* client id of log file */
    long dest_offset;        /* data offset within log file */
    int dest_delegator_rank; /* delegator rank of service manager */
    long length;             /* length of data to be read */
    int src_delegator_rank;  /* delegator rank of request manager */
    int src_cli_id;          /* client id of requesting client process */
    int src_app_id;          /* app id of requesting client process */
    int src_fid;             /* global file id */
    long src_offset;         /* logical file offset */
    int src_thrd;            /* thread id of request manager */
    int src_dbg_rank;        /* MPI rank of client process */
    int arrival_time;        /* records time reaches service mgr */
} send_msg_t;

/* defines header for read reply messages sent from service manager
 * back to request manager, data payload of length bytes immediately
 * follows the header */
typedef struct {
    long src_fid;    /* global file id */
    long src_offset; /* logical offset in file */
    long length;     /* number of bytes */
} recv_msg_t;

/* defines a fixed-length list of read requests */
typedef struct {
    int num; /* number of active read requests */
    send_msg_t msg_meta[MAX_META_PER_SEND]; /* list of requests */
} msg_meta_t;

/* one entry per delegator for which we have active read requests,
 * records rank of delegator and request count */
typedef struct {
    int req_cnt; /* number of requests to this delegator */
    int del_id;  /* rank of delegator */
} per_del_stat_t;

/* records list of delegator information (rank, req count) for
 * set of delegators we have active read requests for */
typedef struct {
    per_del_stat_t *req_stat; /* delegator rank and request count */
    int del_cnt; /* number of delegators we have read requests for */
} del_req_stat_t;

/* this structure is created by the main thread for each request
 * manager thread, contains shared data structures where main thread
 * issues read requests and request manager processes them, contains
 * condition variable and lock for coordination between threads */
typedef struct {
    /* request manager thread */
    pthread_t thrd;

    /* condition variable to synchronize request manager thread
     * and main thread delivering work */
    pthread_cond_t  thrd_cond;

    /* lock for shared data structures (variables below) */
    pthread_mutex_t thrd_lock;

    /* flag indicating that request manager thread is waiting
     * for work inside of critical region */
    int has_waiting_delegator;

    /* flag indicating main thread is in critical section waiting
     * for request manager thread */
    int has_waiting_dispatcher;

    /* a list of read requests to be sent to each delegator,
     * main thread adds items to this list, request manager
     * processes them */
    msg_meta_t *del_req_set;

    /* statistics of read requests to be sent to each delegator */
    del_req_stat_t *del_req_stat;

    /* buffer to build read request messages */
    char del_req_msg_buf[REQ_BUF_LEN];

    /* memory for posting receives for incoming read reply messages
     * from the service threads */
    char del_recv_msg_buf[RECV_BUF_CNT][RECV_BUF_LEN];

    /* flag set by main thread indicating whether request
     * manager thread should exit */
    int exit_flag;
} thrd_ctrl_t;

/* structure passed to request manager thread when it starts */
typedef struct {
    int app_id;  /* app id of client we'll be serving */
    int sock_id; /* socket id for client we'll be serving */
} cli_signature_t;

/* one of these structures is created for each app id,
 * it contains info for each client like names, file descriptors,
 * and memory locations of file data
 *
 * file data stored in the superblock is in memory,
 * this is mapped as a shared memory region by the delegator
 * process, this data can be accessed by service manager threads
 * using memcpy()
 *
 * when the super block is full, file data is written
 * to the spillover file, data here can be accessed by
 * service manager threads via read() calls */
typedef struct {
    /* global values which are identical across all clients,
     * for this given app id */
    long superblock_sz; /* size of memory region used to store data */
    long meta_offset;   /* offset within superblock to index metadata */
    long meta_size;     /* size of index metadata region in bytes */
    long fmeta_offset;  /* offset within superblock to file attribute (stat) metadata */
    long fmeta_size;    /* size of file attribute (stat) metadata region in bytes */
    long data_offset;   /* offset within superblock to data log */
    long data_size;     /* size of data log in bytes */
    int req_buf_sz;     /* size of buffer to be used for client to issue read requests */
    int recv_buf_sz;    /* size of buffer to copy read replies to client */

    /* number of clients on the node */
    int num_procs_per_node;

    /* map from socket id to other values */
    int client_ranks[MAX_NUM_CLIENTS]; /* map to client id */
    int thrd_idxs[MAX_NUM_CLIENTS];    /* map to thread id */
    int dbg_ranks[MAX_NUM_CLIENTS];    /* map to client rank */

    /* file descriptors */
    int spill_log_fds[MAX_NUM_CLIENTS];       /* spillover data */
    int spill_index_log_fds[MAX_NUM_CLIENTS]; /* spillover index */

    /* shared memory pointers */
    char *shm_superblocks[MAX_NUM_CLIENTS]; /* superblock data */
    char *shm_req_bufs[MAX_NUM_CLIENTS];    /* read request shm */
    char *shm_recv_bufs[MAX_NUM_CLIENTS];   /* read reply shm */

    /* file names */
    char super_buf_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char req_buf_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char recv_buf_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char spill_log_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];
    char spill_index_log_name[MAX_NUM_CLIENTS][UNIFYCR_MAX_FILENAME];

    /* directory holding spill over files */
    char external_spill_dir[UNIFYCR_MAX_FILENAME];
} app_config_t;

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
extern int *local_rank_lst;
extern int local_rank_cnt;
extern long max_recs_per_slice;

#if defined(UNIFYCR_MULTIPLE_DELEGATORS)
extern int local_rank_idx;
#endif

#endif
