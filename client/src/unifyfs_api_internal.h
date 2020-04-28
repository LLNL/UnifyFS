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

#include "unifyfs_api.h"
#include "unifyfs-internal.h"
#include "unifyfs-fixed.h"

// client-server rpc headers
#include "unifyfs_client_rpcs.h"
#include "unifyfs_rpc_util.h"
#include "margo_client.h"

/* UnifyFS file system client structure */
typedef struct unifyfs_client {
    int app_id;         /* application id (gfid for mountpoint) */
    int client_id;      /* client id within application */

    bool is_mounted;    /* has client mounted? */

    unifyfs_cfg_t cfg;  /* client configuration */
} unifyfs_client;

#endif // UNIFYFS_API_INTERNAL_H
