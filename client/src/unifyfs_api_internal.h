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

#ifndef UNIFYFS_API_INTERNAL_H
#define UNIFYFS_API_INTERNAL_H

// client headers
#include "unifyfs_api.h"
#include "unifyfs-internal.h"

// common headers
#include "unifyfs_client.h"

/* ---  types and structures --- */

enum unifyfs_file_storage {
    FILE_STORAGE_NULL = 0,
    FILE_STORAGE_LOGIO
};

/* Client file metadata */
typedef struct {
    int fid;                      /* local file index in filemetas array */
    int storage;                  /* FILE_STORAGE type */

    int pending_unlink;           /* received unlink callback */

    int needs_writes_sync;               /* have unsynced writes */
    int needs_reads_sync;       /* have unsynced read extents from server */
    struct seg_tree extents_sync; /* Segment tree containing our coalesced
                                   * writes between sync operations */

    struct seg_tree extents;      /* Segment tree of all local data extents */

    unifyfs_file_attr_t attrs;    /* UnifyFS and POSIX file attributes */
} unifyfs_filemeta_t;

/* struct used to map a full path to its local file id,
 * an array of these is kept and a simple linear search
 * is used to find a match */
typedef struct {
    /* flag incidating whether slot is in use */
    int in_use;

    /* full path and name of file */
    char filename[UNIFYFS_MAX_FILENAME];
} unifyfs_filename_t;

/* UnifyFS file system client structure */
typedef struct unifyfs_client {
    unifyfs_client_state state;


    /* mountpoint configuration */
    unifyfs_cfg_t cfg;               /* user-provided configuration */

    bool use_fsync_persist;          /* persist data to storage on fsync() */
    bool use_local_extents;          /* enable tracking of local extents */
    bool use_node_local_extents;     /* enable tracking of extents within
                                      * node only for laminate files */
    bool use_write_sync;             /* sync for every write operation */
    bool use_unifyfs_magic;          /* return UNIFYFS (true) or TMPFS (false)
                                      * magic value from statfs() */

    int max_files;                   /* max number of files to store */

    size_t write_index_size;         /* size of metadata log */
    size_t max_write_index_entries;  /* max metadata log entries */

    /* tracks current working directory within namespace */
    char* cwd;

    /* mutex for synchronizing updates to below state */
    pthread_mutex_t sync;

    /* an arraylist to maintain the active mread requests for the client */
    arraylist_t* active_mreads;
    unsigned int mread_id_generator; /* to generate unique mread ids */

    /* an arraylist to maintain the active transfer requests for the client */
    arraylist_t* active_transfers;
    unsigned int transfer_id_generator; /* to generate unique transfer ids */

    /* per-file metadata */
    void* free_fid_stack;
    unifyfs_filename_t* unifyfs_filelist;
    unifyfs_filemeta_t* unifyfs_filemetas;

    /* Other clients log-io context */
    logio_context* logio_ctx_ptrs[UNIFYFS_SERVER_MAX_APP_CLIENTS];

} unifyfs_client;

/* Client initialization and finalization methods */
int unifyfs_client_init(unifyfs_client* client);
int unifyfs_client_fini(unifyfs_client* client);

/* find client with given app_id and client_id */
unifyfs_client* unifyfs_find_client(int app_id,
                                    int client_id,
                                    int* list_position);

/* lock/unlock access to shared data structures in client superblock */
int unifyfs_stack_lock(unifyfs_client* client);
int unifyfs_stack_unlock(unifyfs_client* client);

/* set global file metadata */
int unifyfs_set_global_file_meta(unifyfs_client* client,
                                 int gfid,
                                 unifyfs_file_attr_op_e op,
                                 unifyfs_file_attr_t* gfattr);

/* get global file metadata */
int unifyfs_get_global_file_meta(unifyfs_client* client,
                                 int gfid,
                                 unifyfs_file_attr_t* gfattr);

/* sync all writes for client files with the server */
int unifyfs_sync_files(unifyfs_client* client);

/* get current file size. if we have a local file corresponding to the
 * given gfid, we use the local metadata. otherwise, we use a global
 * metadata lookup */
off_t unifyfs_gfid_filesize(unifyfs_client* client,
                            int gfid);

#endif // UNIFYFS_API_INTERNAL_H
