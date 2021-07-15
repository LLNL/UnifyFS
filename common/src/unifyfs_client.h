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

#ifndef UNIFYFS_CLIENT_H
#define UNIFYFS_CLIENT_H

#include "unifyfs_logio.h"
#include "unifyfs_meta.h"
#include "unifyfs_shm.h"


 /* client state used by both client library & server */
typedef struct client_state {
    /* mountpoint information */
    char*  mount_prefix;      /* mountpoint prefix string */
    size_t mount_prefixlen;   /* strlen() of mount_prefix */
    int app_id;               /* application id (gfid for mountpoint) */
    int client_id;            /* client id within application */
    bool is_mounted;          /* is mountpoint active? */

    /* application rank (for debugging) */
    int app_rank;

    /* tracks current working directory within namespace */
    char* cwd;

    /* has all the client's state (below) been initialized? */
    bool initialized;

    /* log-based I/O context */
    logio_context* logio_ctx;

    /* superblock - shared memory region for client metadata */
    shm_context* shm_super_ctx;
    unifyfs_write_index write_index;

} unifyfs_client_state;

#endif /* UNIFYFS_CLIENT_H */
