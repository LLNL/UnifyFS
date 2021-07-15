/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef UNIFYFS_TRANSFER_H
#define UNIFYFS_TRANSFER_H

#include "unifyfs_global.h"

/* server transfer modes */
typedef enum {
    TRANSFER_MODE_OWNER = 0, /* owner transfers all data */
    TRANSFER_MODE_LOCAL = 1  /* each server transfers local data */
} transfer_mode_e;

/* transfer helper thread arguments structure */
typedef struct transfer_thread_args {
    const char* dst_file;  /* destination file */
    int gfid;              /* source file */

    /* requesting client and transfer info */
    int client_server; /* rank of server where request originated */
    int client_app;    /* app of originating client */
    int client_id;     /* id of originating client */
    int transfer_id;   /* transfer request id at originating client */

    /* local extents to transfer to destination file */
    struct extent_tree_node* local_extents;
    size_t n_extents;

    size_t local_data_sz;         /* total size of local data */

    server_rpc_req_t* bcast_req;  /* bcast rpc req state */

    int status;                   /* status for entire set of transfers */
    pthread_t thrd;               /* pthread id for transfer helper thread */
} transfer_thread_args;

void release_transfer_thread_args(transfer_thread_args* tta);

/* find local extents for the given gfid and initialize transfer helper
 * thread state */
int create_local_transfers(int gfid,
                           const char* dest_file,
                           transfer_thread_args* tta);

/**
 * transfer helper thread main
 * @param arg  pointer to transfer_thread_args struct
 *
 * @return pointer to transfer_thread_args struct
 */
void* transfer_helper_thread(void* arg);

#endif /* UNIFYFS_TRANSFER_H */
