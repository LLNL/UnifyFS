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

#include "unifyfs.h"
#include "unifyfs_api_internal.h"
#include "posix_client.h"


/* =======================================
 * Global variable declarations
 * ======================================= */

char*  unifyfs_mount_prefix;
size_t unifyfs_mount_prefixlen;


/* =======================================
 * Operations to mount/unmount file system
 * ======================================= */

/**
 * Mount UnifyFS file system at given prefix
 *
 * @param prefix: directory prefix
 * @param rank: client rank within application
 * @param size: the number of application clients
 *
 * @return success/error code
 */
int unifyfs_mount(const char prefix[],
                  int rank,
                  size_t size)
{
    // generate app_id from mountpoint prefix
    int app_id = unifyfs_generate_gfid(prefix);
    if (-1 != unifyfs_mount_id) {
        if (app_id != unifyfs_mount_id) {
            LOGERR("multiple mount support not yet implemented");
            return UNIFYFS_FAILURE;
        } else {
            LOGDBG("already mounted");
            return UNIFYFS_SUCCESS;
        }
    }

    // record our rank for debugging messages
    client_rank = rank;
    global_rank_cnt = (int)size;

    // record mountpoint prefix string
    unifyfs_mount_prefix    = strdup(prefix);
    unifyfs_mount_prefixlen = strlen(prefix);

    // initialize as UnifyFS POSIX client
    return posix_client_init();
}

/**
 * Unmount the mounted UnifyFS file system
 *
 * @return success/error code
 */
int unifyfs_unmount(void)
{
    if (-1 == unifyfs_mount_id) {
        return UNIFYFS_SUCCESS;
    }

    int rc = posix_client_fini();
    if (UNIFYFS_SUCCESS != rc) {
        return rc;
    }

    /* free mount prefix string */
    if (unifyfs_mount_prefix != NULL) {
        free(unifyfs_mount_prefix);
        unifyfs_mount_prefix = NULL;
        unifyfs_mount_prefixlen = 0;
    }

    return UNIFYFS_SUCCESS;
}
