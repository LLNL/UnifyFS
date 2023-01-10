/*
 * Copyright (c) 2022, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2022, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unifyfs.h"
#include "unifyfs_rc.h"

static int preload_initialized; // = 0

void unifyfs_preload_init(void) __attribute__ ((constructor));
void unifyfs_preload_fini(void) __attribute__ ((destructor));

static void unifyfs_preload_mount(void)
{
    if (preload_initialized) {
        return;
    }

    char* mountpoint = getenv("UNIFYFS_PRELOAD_MOUNTPOINT");
    if (NULL == mountpoint) {
        mountpoint = strdup("/unifyfs");
    }

    int rank = 0;
    int world_sz = 1;
    int rc = unifyfs_mount(mountpoint, rank, (size_t)world_sz);
    if (UNIFYFS_SUCCESS != rc) {
        fprintf(stderr,
                "UNIFYFS ERROR: unifyfs_mount(%s) failed with '%s'\n",
                mountpoint, unifyfs_rc_enum_description((unifyfs_rc)rc));
    } else {
        fprintf(stderr, "DEBUG: mounted\n");
        preload_initialized = 1;
    }
}

static void unifyfs_preload_unmount(void)
{
    if (preload_initialized) {
        int rc = unifyfs_unmount();
        if (UNIFYFS_SUCCESS != rc) {
            fprintf(stderr,
                    "UNIFYFS ERROR: unifyfs_unmount() failed with '%s'\n",
                    unifyfs_rc_enum_description((unifyfs_rc)rc));
        }
        fprintf(stderr, "DEBUG: unmounted\n");
        preload_initialized = 0;
    }
}

void unifyfs_preload_init(void)
{
    unifyfs_preload_mount();
    fflush(NULL);
}

void unifyfs_preload_fini(void)
{
    unifyfs_preload_unmount();
    fflush(NULL);
}
