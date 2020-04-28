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

// common headers
#include "unifyfs_api.h"
#include "unifyfs_logio.h"
#include "unifyfs_shm.h"

/* UnifyFS file system client structure */
typedef struct unifyfs_client {
    int app;                   /* application id (gfid for mountpoint) */
    int client;                /* client id within application */
    const char* mountpoint;    /* mountpoint prefix */
    logio_context* logio_ctx;  /* log-based I/O context for write data */
    shm_context* shmem_data;   /* shmem region context for read data */
    unifyfs_options opts;      /* UnifyFS behavior options for mountpoint */
} unifyfs_client;

#endif // UNIFYFS_API_INTERNAL_H
