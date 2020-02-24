/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef UNIFYFS_META_H
#define UNIFYFS_META_H

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

#include "unifyfs_const.h"

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
    int gfid;
    char filename[UNIFYFS_MAX_FILENAME];

    /* essential stat fields */
    uint32_t mode;   /* st_mode bits */
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    struct timespec atime;
    struct timespec mtime;
    struct timespec ctime;

    /* Set when the file is laminated */
    uint32_t is_laminated;
} unifyfs_file_attr_t;

enum {
    UNIFYFS_STAT_DEFAULT_DEV = 0,
    UNIFYFS_STAT_DEFAULT_BLKSIZE = 4096,
    UNIFYFS_STAT_DEFAULT_FILE_MODE = S_IFREG | 0644,
    UNIFYFS_STAT_DEFAULT_DIR_MODE = S_IFDIR | 0755,
};

static inline int unifyfs_file_attr_set_invalid(unifyfs_file_attr_t *attr)
{
    if (!attr) {
        return EINVAL;
    }

    attr->gfid = -1;
    attr->mode = -1;
    attr->uid = -1;
    attr->gid = -1;
    attr->size = (uint64_t) -1;
    attr->atime.tv_sec = (uint64_t) -1;
    attr->mtime.tv_sec = (uint64_t) -1;
    attr->ctime.tv_sec = (uint64_t) -1;
    attr->is_laminated = -1;

    attr->filename[0] = '\0';

    return 0;
}

/*
 * updates @dst with new values from @src. 
 * ignores fields from @src with negative values.
 */
static inline int
unifyfs_file_attr_update(unifyfs_file_attr_t* dst, unifyfs_file_attr_t* src)
{
    if (!dst || !src) {
        return EINVAL;
    }

    if (dst->gfid != src->gfid) {
        return EINVAL;
    }

    /* update fields only with >=0 values */
    if (src->mode >= 0) {
        dst->mode = src->mode;
    }

    if (src->uid >= 0) {
        dst->uid = src->uid;
    }

    if (src->gid >= 0) {
        dst->gid = src->gid;
    }

    if (src->size != (uint64_t) -1) {
        dst->size = src->size;
    }

    if (src->atime.tv_sec != (uint64_t) -1) {
        dst->atime = src->atime;
    }

    if (src->mtime.tv_sec != (uint64_t) -1) {
        dst->mtime = src->mtime;
    }

    if (src->ctime.tv_sec != (uint64_t) -1) {
        dst->ctime = src->ctime;
    }

    if (src->is_laminated >= 0) {
        dst->is_laminated = src->is_laminated;
    }

    /* FIXME: is this necessary? */
    if (src->filename && !strcmp(dst->filename, src->filename)) {
        sprintf(dst->filename, "%s", src->filename);
    }

    return 0;
}

static inline
void unifyfs_file_attr_to_stat(unifyfs_file_attr_t* fattr, struct stat* sb)
{
    if (fattr && sb) {
        sb->st_dev = UNIFYFS_STAT_DEFAULT_DEV;
        sb->st_ino = fattr->gfid;
        sb->st_mode = fattr->mode;
        sb->st_uid = fattr->uid;
        sb->st_gid = fattr->gid;
        sb->st_rdev = UNIFYFS_STAT_DEFAULT_DEV;
        sb->st_size = fattr->size;
        sb->st_blksize = UNIFYFS_STAT_DEFAULT_BLKSIZE;
        sb->st_blocks = fattr->size / UNIFYFS_STAT_DEFAULT_BLKSIZE;
        if (fattr->size % UNIFYFS_STAT_DEFAULT_BLKSIZE > 0) {
            sb->st_blocks += 1;
        }

        /*
         * Re-purpose st_nlink to tell us if the file is laminated or not.
         * That way, if we do eventually make /unifyfs mountable, we can easily
         * see with 'ls -l' or stat if the file is laminated or not.
         */
        sb->st_nlink = fattr->is_laminated ? 1 : 0;

        sb->st_atime = fattr->atime.tv_sec;
        sb->st_mtime = fattr->mtime.tv_sec;
        sb->st_ctime = fattr->ctime.tv_sec;
    }
}

typedef struct {
    off_t file_pos; /* starting logical offset of data in file */
    off_t log_pos;  /* starting physical offset of data in log */
    size_t length;  /* length of data */
    int gfid;       /* global file id */
} unifyfs_index_t;

/* Header for read request reply in client shared memory region.
 * The associated data payload immediately follows the header in
 * the shmem region.
 *   offset  - offset within file
 *   length  - data size
 *   gfid    - global file id
 *   errcode - read error code (zero on success) */
typedef struct {
    size_t offset;
    size_t length;
    int gfid;
    int errcode;
} shm_meta_t;

/* State values for client shared memory region */
typedef enum {
    SHMEM_REGION_EMPTY = 0,        // set by client to indicate drain complete
    SHMEM_REGION_DATA_READY = 1,   // set by server to initiate client drain
    SHMEM_REGION_DATA_COMPLETE = 2 // set by server when done writing data
} shm_region_state_e;

/* Header for client shared memory region.
 *   sync     - for synchronizing updates/access by server threads
 *   meta_cnt - number of shm_meta_t (i.e., read replies) currently in shmem
 *   bytes    - total bytes of shmem region in use (shm_meta_t + payloads)
 *   state    - region state variable used for client-server coordination */
 typedef struct {
    pthread_mutex_t sync;
    volatile size_t meta_cnt;
    volatile size_t bytes;
    volatile shm_region_state_e state;
} shm_header_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_META_H */
