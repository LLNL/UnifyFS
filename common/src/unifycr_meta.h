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
    struct stat file_attr;
    char filename[UNIFYCR_MAX_FILENAME];
} unifycr_file_attr_t;

typedef struct {
    size_t file_pos;
    size_t mem_pos;
    size_t length;
    int fid;
} unifycr_index_t;

/* file metadata format in the shared memory */
typedef struct {
    int src_fid;
    size_t offset;
    size_t length;
} shm_meta_t;

/* header for contents of recv/req shmem buffers */
typedef struct {
    size_t sz;
    size_t cnt;
} shm_hdr_t;

#endif /* UNIFYCR_META_H */
