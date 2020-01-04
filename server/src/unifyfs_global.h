/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
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

#ifndef UNIFYFS_GLOBAL_H
#define UNIFYFS_GLOBAL_H

// system headers
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// common headers
#include "arraylist.h"
#include "unifyfs_const.h"
#include "unifyfs_log.h"
#include "unifyfs_meta.h"
#include "unifyfs_shm.h"
#include "unifyfs_sock.h"

#include <margo.h>
#include <pthread.h>

#if defined(UNIFYFSD_USE_MPI)
# include <mpi.h>
#endif

extern arraylist_t* app_config_list;
extern arraylist_t* rm_thrd_list;

extern char glb_host[UNIFYFS_MAX_HOSTNAME];
extern int glb_pmi_rank, glb_pmi_size;

extern size_t max_recs_per_slice;

/* defines commands for messages sent to service manager threads */
typedef enum {
    SVC_CMD_INVALID = 0,
    SVC_CMD_RDREQ_CHK,     /* read requests (chunk_read_req_t) */
} service_cmd_e;

// NEW READ REQUEST STRUCTURES
typedef enum {
    READREQ_NULL = 0,          /* request not initialized */
    READREQ_READY,             /* request ready to be issued */
    READREQ_STARTED,           /* chunk requests issued */
    READREQ_PARTIAL_COMPLETE,  /* some reads completed */
    READREQ_COMPLETE           /* all reads completed */
} readreq_status_e;

typedef struct {
    size_t nbytes;      /* size of data chunk */
    size_t offset;      /* file offset */
    size_t log_offset;  /* remote log offset */
    int log_app_id;     /* remote log application id */
    int log_client_id;  /* remote log client id */
} chunk_read_req_t;

typedef struct {
    size_t offset;    /* file offset */
    size_t nbytes;    /* requested read size */
    ssize_t read_rc;  /* bytes read (or negative error code) */
} chunk_read_resp_t;

typedef struct {
    int rank;                /* remote delegator rank */
    int rdreq_id;            /* read-request id */
    int app_id;              /* app id of requesting client process */
    int client_id;           /* client id of requesting client process */
    int num_chunks;          /* number of chunk requests/responses */
    readreq_status_e status; /* summary status for chunk reads */
    size_t total_sz;         /* total size of data requested */
    chunk_read_req_t* reqs;  /* @RM: subarray of server_read_req_t.chunks
                              * @SM: received requests buffer */
    chunk_read_resp_t* resp; /* @RM: received responses buffer
                              * @SM: allocated responses buffer */
} remote_chunk_reads_t;

typedef struct {
    size_t length;  /* length of data to read */
    size_t offset;  /* file offset */
    int gfid;       /* global file id */
    int errcode;    /* request completion status */
} client_read_req_t;

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
    size_t superblock_sz; /* size of memory region used to store data */
    size_t meta_offset;   /* superblock offset to index metadata */
    size_t meta_size;     /* size of index metadata region in bytes */
    size_t data_offset;   /* superblock offset to data log */
    size_t data_size;     /* size of data log in bytes */
    size_t req_buf_sz;    /* buffer size for client to issue read requests */
    size_t recv_buf_sz;   /* buffer size for read replies to client */

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
    char* shm_superblocks[MAX_NUM_CLIENTS]; /* superblock data */
    char* shm_req_bufs[MAX_NUM_CLIENTS];    /* read request shm */
    char* shm_recv_bufs[MAX_NUM_CLIENTS];   /* read reply shm */

    /* client address for rpc invocation */
    hg_addr_t client_addr[MAX_NUM_CLIENTS];

    /* file names */
    char super_buf_name[MAX_NUM_CLIENTS][UNIFYFS_MAX_FILENAME];
    char req_buf_name[MAX_NUM_CLIENTS][UNIFYFS_MAX_FILENAME];
    char recv_buf_name[MAX_NUM_CLIENTS][UNIFYFS_MAX_FILENAME];
    char spill_log_name[MAX_NUM_CLIENTS][UNIFYFS_MAX_FILENAME];
    char spill_index_log_name[MAX_NUM_CLIENTS][UNIFYFS_MAX_FILENAME];

    /* directory holding spill over files */
    char external_spill_dir[UNIFYFS_MAX_FILENAME];
} app_config_t;

typedef int fattr_key_t;

int invert_sock_ids[MAX_NUM_CLIENTS];

typedef struct {
    //char* hostname;
    char* margo_svr_addr_str;
    hg_addr_t margo_svr_addr;
    int pmi_rank;
} server_info_t;

extern char glb_host[UNIFYFS_MAX_HOSTNAME];
extern size_t glb_num_servers;
extern server_info_t* glb_servers;


#endif // UNIFYFS_GLOBAL_H
