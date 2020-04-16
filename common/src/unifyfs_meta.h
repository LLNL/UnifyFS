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
#include <unistd.h>
#include <string.h>

#include "unifyfs_const.h"

#ifdef __cplusplus
extern "C" {
#endif

/* structure used to detect clients/servers colocated on a host */
typedef struct {
    char hostname[UNIFYFS_MAX_HOSTNAME];
    int rank;
} name_rank_pair_t;

/* write-log metadata index structure */
typedef struct {
    off_t file_pos; /* start offset of data in file */
    off_t log_pos;  /* start offset of data in write log */
    size_t length;  /* length of data */
    int gfid;       /* global file id */
} unifyfs_index_t;

/* UnifyFS file attributes */
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

static inline int unifyfs_file_attr_set_invalid(unifyfs_file_attr_t* attr)
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

/* given an input mode, mask it with umask and return.
 * set perms=0 to request all read/write bits */
static inline
mode_t unifyfs_getmode(mode_t perms)
{
    /* perms == 0 is shorthand for all read and write bits */
    if (perms == 0) {
        perms = 0666;
    }

    /* get current user mask */
    mode_t mask = umask(0);
    umask(mask);

    /* mask off bits from desired permissions */
    mode_t ret = (perms & 0777) & ~mask;
    return ret;
}

/* qsort comparison function for name_rank_pair_t */
static inline
int compare_name_rank_pair(const void* a, const void* b)
{
    const name_rank_pair_t* pair_a = (const name_rank_pair_t*) a;
    const name_rank_pair_t* pair_b = (const name_rank_pair_t*) b;

    /* compare the hostnames */
    int cmp = strcmp(pair_a->hostname, pair_b->hostname);
    if (0 == cmp) {
        /* if hostnames are the same, compare the rank */
        cmp = pair_a->rank - pair_b->rank;
    }
    return cmp;
}

/* qsort comparison function for int */
static inline
int compare_int(const void* a, const void* b)
{
    int aval = *(const int*)a;
    int bval = *(const int*)b;
    return aval - bval;
}


/*
 * Hash a file path to a uint64_t using MD5
 * @param path absolute file path
 * @return hash value
 */
uint64_t compute_path_md5(const char* path);

/*
 * Hash a file path to an integer gfid
 * @param path absolute file path
 * @return gfid
 */
static inline
int unifyfs_generate_gfid(const char* path)
{
    /* until we support 64-bit gfids, use top 32 bits */
    uint64_t hash64 = compute_path_md5(path);
    uint32_t hash32 = (uint32_t)(hash64 >> 32);

    /* TODO: Remove next statement once we get rid of MDHIM.
     *
     * MDHIM requires positive values for integer keys, due to the way
     * slice servers are calculated. We use an integer key for the
     * gfid -> file attributes index. To guarantee a positive value, we
     * shift right one bit to make sure the top bit is zero. */
    hash32 = hash32 >> 1;

    return (int)hash32;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_META_H */
