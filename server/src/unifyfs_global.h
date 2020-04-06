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
#include <config.h>

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
#include <stdbool.h>

// common headers
#include "arraylist.h"
#include "unifyfs_const.h"
#include "unifyfs_log.h"
#include "unifyfs_logio.h"
#include "unifyfs_meta.h"
#include "unifyfs_shm.h"
#include "unifyfs_tree.h"
#include "extent_tree.h"
#include "unifyfs_inode_tree.h"
#include "int2void.h"
#include "unifyfs-stack.h"

#include <margo.h>
#include <pthread.h>

//#if defined(UNIFYFSD_USE_MPI)
# include <mpi.h>
//#endif



/* Some global variables/structures used throughout the server code */

/* PMI server rank and server count */
extern int glb_pmi_rank;
extern int glb_pmi_size;
extern int server_pid;

/* hostname for this server */
extern char glb_host[UNIFYFS_MAX_HOSTNAME];

typedef struct {
    //char* hostname;
    char* margo_svr_addr_str;
    hg_addr_t margo_svr_addr;
    int pmi_rank;
} server_info_t;

extern server_info_t* glb_servers; /* array of server info structs */
extern size_t glb_num_servers; /* number of entries in glb_servers array */

/* maps a global file id to its extent map */
extern struct unifyfs_inode_tree* global_inode_tree;

/* maps a global file id to its metadata extent map
 * TODO: move to metadata? */
extern struct unifyfs_inode_tree meta_inode_tree;

/* stack to manage free communication tags */
void* glb_tag_stack;

/* maps a tag to a collective state structure (stored as void*) */
extern struct int2void glb_tag2state;

/* configuration flag whether to use local extent tracking
 * on server to service client read requests of local data */
extern bool unifyfs_local_extents;

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
    READREQ_COMPLETE,          /* all reads completed */
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

// forward declaration of reqmgr_thrd
struct reqmgr_thrd;

/**
 * Structure to maintain application client state, including
 * logio and shared memory contexts, margo rpc address, etc.
 */
typedef struct app_client {
    int app_id;              /* index of app in server app_configs array */
    int client_id;           /* this client's index in app's clients array */
    int dbg_rank;            /* client debug rank - NOT CURRENTLY USED */
    int connected;           /* is client currently connected? */

    hg_addr_t margo_addr;    /* client Margo address */

    struct reqmgr_thrd* reqmgr; /* this client's request manager thread */

    logio_context* logio;    /* logio context for write data */

    shm_context* shmem_data;  /* shmem context for read data */

    shm_context* shmem_super; /* shmem context for superblock region */
    size_t super_meta_offset; /* superblock offset to index metadata */
    size_t super_meta_size;   /* size of index metadata region in bytes */
} app_client;

/**
 * Structure to maintain application configuration state
 * and track connected clients.
 */
typedef struct app_config {
    /* application id - MD5(mount_prefix) */
    int app_id;

    /* mount prefix for application's UnifyFS files */
    char mount_prefix[UNIFYFS_MAX_FILENAME];

    /* array of clients associated with this app */
    size_t num_clients;
    app_client* clients[MAX_APP_CLIENTS];
} app_config;

app_config* get_application(int app_id);

app_config* new_application(int app_id);

unifyfs_rc cleanup_application(app_config* app);

app_client* get_app_client(int app_id,
                           int client_id);

app_client* new_app_client(app_config* app,
                           const char* margo_addr_str,
                           const int dbg_rank);

unifyfs_rc attach_app_client(app_client* client,
                             const char* logio_spill_dir,
                             const size_t logio_spill_size,
                             const size_t logio_shmem_size,
                             const size_t shmem_data_size,
                             const size_t shmem_super_size,
                             const size_t super_meta_offset,
                             const size_t super_meta_size);

unifyfs_rc disconnect_app_client(app_client* clnt);

unifyfs_rc cleanup_app_client(app_config* app, app_client* clnt);

#endif // UNIFYFS_GLOBAL_H
