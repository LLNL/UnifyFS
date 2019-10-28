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
