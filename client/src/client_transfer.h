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

#include "unifyfs-internal.h"

/* client transfer (stage-in/out) support */

#define UNIFYFS_TX_BUFSIZE (8*(1<<20))

enum {
    UNIFYFS_TX_STAGE_OUT = 0,
    UNIFYFS_TX_STAGE_IN = 1,
    UNIFYFS_TX_SERIAL = 0,
    UNIFYFS_TX_PARALLEL = 1,
};

int do_transfer_file_serial(const char* src,
                            const char* dst,
                            struct stat* sb_src,
                            int direction);

int do_transfer_file_parallel(const char* src,
                              const char* dst,
                              struct stat* sb_src,
                              int direction);
