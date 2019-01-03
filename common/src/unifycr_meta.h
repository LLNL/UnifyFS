/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#ifndef UNIFYCR_META_H
#define UNIFYCR_META_H

#include <sys/stat.h>

#include "unifycr_const.h"

/**
 * Server commands
 */
typedef enum {
    COMM_MOUNT,
    COMM_META_FSYNC,
    COMM_META_GET,
    COMM_META_SET,
    COMM_READ,
    COMM_UNMOUNT,
    COMM_DIGEST,
    COMM_SYNC_DEL,
} cmd_lst_t;

typedef struct {
    int fid;
    int gfid;
    char filename[UNIFYCR_MAX_FILENAME];
    struct stat file_attr;
} unifycr_file_attr_t;

typedef struct {
    off_t file_pos;
    off_t mem_pos;
    size_t length;
    int fid;
} unifycr_index_t;

/* defines header for read reply as written by request manager
 * back to application client via shared memory, the data
 * payload of length bytes immediately follows the header */
typedef struct {
    size_t offset; /* offset within file */
    size_t length; /* number of bytes */
    int src_fid;   /* global file id */
    int errcode;   /* indicates whether read encountered error */
} shm_meta_t;

#endif /* UNIFYCR_META_H */
