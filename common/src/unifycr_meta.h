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

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unifycr_const.h"

#ifdef __cplusplus
extern "C" {
#endif

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

    /* essential stat fields */
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    struct timespec atime;
    struct timespec mtime;
    struct timespec ctime;
} unifycr_file_attr_t;

enum {
    UNIFYCR_STAT_DEFAULT_DEV = 0,
    UNIFYCR_STAT_DEFAULT_BLKSIZE = 4096,
    UNIFYCR_STAT_DEFAULT_FILE_MODE = S_IFREG | 0644,
    UNIFYCR_STAT_DEFAULT_DIR_MODE = S_IFDIR | 0755,
};

static inline
void unifycr_file_attr_to_stat(unifycr_file_attr_t* fattr, struct stat* sb)
{
    if (fattr && sb) {
        sb->st_dev = UNIFYCR_STAT_DEFAULT_DEV;
        sb->st_ino = fattr->gfid;
        sb->st_mode = fattr->mode;
        sb->st_uid = fattr->uid;
        sb->st_gid = fattr->gid;
        sb->st_rdev = UNIFYCR_STAT_DEFAULT_DEV;
        sb->st_size = fattr->size;
        sb->st_blksize = UNIFYCR_STAT_DEFAULT_BLKSIZE;
        sb->st_blocks = fattr->size / UNIFYCR_STAT_DEFAULT_BLKSIZE;
        if (fattr->size % UNIFYCR_STAT_DEFAULT_BLKSIZE > 0) {
            sb->st_blocks += 1;
        }
        sb->st_atime = fattr->atime.tv_sec;
        sb->st_mtime = fattr->mtime.tv_sec;
        sb->st_ctime = fattr->ctime.tv_sec;
    }
}

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

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYCR_META_H */
