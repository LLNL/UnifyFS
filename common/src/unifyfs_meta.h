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

#ifndef UNIFYFS_META_H
#define UNIFYFS_META_H

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unifyfs_const.h"
#include "unifyfs_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UNIFYFS_METADATA_CACHE_SECONDS
# define UNIFYFS_METADATA_CACHE_SECONDS 5
#endif

/* a valid gfid generated via MD5 hash will never be zero */
#define INVALID_GFID (0)

/* extent slice size used for metadata */
extern size_t meta_slice_sz;

/* calculate number of slices in an extent given by start offset and length */
size_t meta_num_slices(size_t offset, size_t length);

/* structure used to detect clients/servers colocated on a host */
typedef struct {
    char hostname[UNIFYFS_MAX_HOSTNAME];
    int rank;
} name_rank_pair_t;

/* generic file extent */
typedef struct {
    size_t offset;
    size_t length;
    int gfid;
} unifyfs_extent_t;

/* write-log metadata index structures */
typedef struct {
    off_t file_pos; /* start offset of data in file */
    off_t log_pos;  /* start offset of data in write log */
    size_t length;  /* length of data */
    int gfid;       /* global file id */
} unifyfs_index_t;

/* client specific write-log metadata index structures */
typedef struct {
    off_t file_pos; /* start offset of data in file */
    off_t log_pos;  /* start offset of data in write log */
    size_t length;  /* length of data */
    int gfid;       /* global file id */
    int log_app_id; /* app id associated with log */
    int log_client_id; /* client id associated with log */
} unifyfs_client_index_t;

/* array list for dynamic extents for read requests*/
typedef struct extents_list {
    unifyfs_client_index_t value;
    struct extents_list* next;
} extents_list_t;

typedef struct extents_list extents_list;

typedef struct {
    size_t  index_size;    /* size of index metadata region in bytes */
    size_t  index_offset;  /* superblock offset of index metadata region */

    size_t* ptr_num_entries;         /* pointer to number of index entries */
    unifyfs_index_t* index_entries;  /* pointer to first unifyfs_index_t */
} unifyfs_write_index;

/* UnifyFS file attributes */
typedef struct {
    char* filename;
    int gfid;

    /* Set when the file is laminated */
    int is_laminated;

    /* Set when file is shared between clients */
    int is_shared;

    /* essential stat fields */
    uint32_t mode;   /* st_mode bits */
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    struct timespec atime;
    struct timespec mtime;
    struct timespec ctime;

    /* metadata caching timestamp */
    time_t last_update;
} unifyfs_file_attr_t;

enum {
    UNIFYFS_STAT_DEFAULT_DEV = 0,
    UNIFYFS_STAT_DEFAULT_BLKSIZE = 4096,
    UNIFYFS_STAT_DEFAULT_FILE_MODE = S_IFREG | 0644,
    UNIFYFS_STAT_DEFAULT_DIR_MODE = S_IFDIR | 0755,
};

static inline
void unifyfs_file_attr_set_invalid(unifyfs_file_attr_t* attr)
{
    memset(attr, 0, sizeof(*attr));
    attr->filename     = NULL;
    attr->gfid         = INVALID_GFID;
    attr->is_laminated = -1;
    attr->is_shared    = -1;
    attr->mode         = (uint32_t) -1;
    attr->uid          = (uint32_t) -1;
    attr->gid          = (uint32_t) -1;
    attr->size         = (uint64_t) -1;
}

static inline
void debug_print_file_attr(unifyfs_file_attr_t* attr)
{
    if (!attr) {
        return;
    }
    LOGDBG("fileattr(%p) - gfid=%d filename=%s",
           attr, attr->gfid, attr->filename);
    LOGDBG("             - sz=%zu mode=%o uid=%d gid=%d",
           (size_t)attr->size, attr->mode, attr->uid, attr->gid);
    LOGDBG("             - shared=%d laminated=%d",
           attr->is_shared, attr->is_laminated);
    LOGDBG("             - atime=%ld.%09ld ctime=%ld.%09ld mtime=%ld.%09ld",
           attr->atime.tv_sec, attr->atime.tv_nsec,
           attr->ctime.tv_sec, attr->ctime.tv_nsec,
           attr->mtime.tv_sec, attr->mtime.tv_nsec);
}

typedef enum {
    UNIFYFS_FILE_ATTR_OP_INVALID = 0,
    UNIFYFS_FILE_ATTR_OP_CHGRP,
    UNIFYFS_FILE_ATTR_OP_CHMOD,
    UNIFYFS_FILE_ATTR_OP_CHOWN,
    UNIFYFS_FILE_ATTR_OP_CREATE,
    UNIFYFS_FILE_ATTR_OP_DATA,
    UNIFYFS_FILE_ATTR_OP_LAMINATE,
    UNIFYFS_FILE_ATTR_OP_TRUNCATE,
    UNIFYFS_FILE_ATTR_OP_UTIME,
} unifyfs_file_attr_op_e;

/*
 * updates @dst with new values from @src.
 * ignores fields from @src with negative values.
 */
static inline
int unifyfs_file_attr_update(int attr_op,
                             unifyfs_file_attr_t* dst,
                             unifyfs_file_attr_t* src)
{
    if (!dst || !src
        || (attr_op == UNIFYFS_FILE_ATTR_OP_INVALID)
        || (src->gfid == INVALID_GFID)) {
        return EINVAL;
    }

    if (attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) {
        dst->gfid = src->gfid;
    }

    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);
    dst->last_update = tp.tv_sec;

    LOGDBG("updating attributes for gfid=%d", dst->gfid);

    /* Update fields only with valid values and associated operation.
     * invalid values are set by unifyfs_file_attr_set_invalid() above */

    if ((src->mode != (uint32_t)-1) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CHMOD) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_LAMINATE))) {
        LOGDBG("setting mode to %o", src->mode);
        dst->mode = src->mode;
    }

    if ((src->uid != (uint32_t)-1) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CHOWN) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_CREATE))) {
        dst->uid = src->uid;
    }

    if ((src->gid != (uint32_t)-1) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CHGRP) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_CREATE))) {
        dst->gid = src->gid;
    }

    if ((src->size != (uint64_t)-1) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_DATA) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_LAMINATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_TRUNCATE))) {
        LOGDBG("setting attr.size to %" PRIu64, src->size);
        dst->size = src->size;
    }

    if ((src->atime.tv_sec != 0) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_UTIME))) {
        LOGDBG("setting attr.atime to %d.%09ld",
               (int)src->atime.tv_sec, src->atime.tv_nsec);
        dst->atime = src->atime;
    }

    if ((src->mtime.tv_sec != 0) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_UTIME) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_DATA) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_LAMINATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_TRUNCATE))) {
        LOGDBG("setting attr.mtime to %d.%09ld",
               (int)src->mtime.tv_sec, src->mtime.tv_nsec);
        dst->mtime = src->mtime;
    }

    if ((src->ctime.tv_sec != 0) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CHGRP) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_CHMOD) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_CHOWN) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_DATA) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_LAMINATE))) {
        LOGDBG("setting attr.ctime to %d.%09ld",
               (int)src->ctime.tv_sec, src->ctime.tv_nsec);
        dst->ctime = src->ctime;
    }

    if ((src->is_laminated != -1) &&
        ((attr_op == UNIFYFS_FILE_ATTR_OP_CREATE) ||
         (attr_op == UNIFYFS_FILE_ATTR_OP_LAMINATE))) {
        LOGDBG("setting attr.is_laminated to %d", src->is_laminated);
        dst->is_laminated = src->is_laminated;
    }

    if ((src->is_shared != -1) &&
        (attr_op == UNIFYFS_FILE_ATTR_OP_CREATE)) {
        LOGDBG("setting attr.is_shared to %d", src->is_shared);
        dst->is_shared = src->is_shared;
    }

    if (src->filename && !dst->filename) {
        LOGDBG("setting attr.filename to %s", src->filename);
        dst->filename = strdup(src->filename);
    }

    return 0;
}

/*
 * Convert UnifyFS file attr to struct stat/stat64
 *
 * fattr is type of unifyfs_file_attr_t*
 * sb is type of struct stat* or struct stat64*
 */
#define unifyfs_file_attr_to_stat(fattr, sb)                                  \
do {                                                                          \
    unifyfs_file_attr_t* _fattr = (fattr);                                    \
    if (_fattr && (NULL != (void*)(sb))) {                                    \
        debug_print_file_attr(_fattr);                                        \
        (sb)->st_dev  = UNIFYFS_STAT_DEFAULT_DEV;                             \
        (sb)->st_ino  = _fattr->gfid;                                         \
        (sb)->st_mode = _fattr->mode;                                         \
        (sb)->st_uid  = _fattr->uid;                                          \
        (sb)->st_gid  = _fattr->gid;                                          \
        (sb)->st_rdev = UNIFYFS_STAT_DEFAULT_DEV;                             \
        (sb)->st_size = _fattr->size;                                         \
                                                                              \
        /* TODO: use cfg.logio_chunk_size here for st_blksize     */          \
        /*       and report actual chunks allocated for st_blocks */          \
        (sb)->st_blksize = UNIFYFS_STAT_DEFAULT_BLKSIZE;                      \
        (sb)->st_blocks = _fattr->size / UNIFYFS_STAT_DEFAULT_BLKSIZE;        \
        if (_fattr->size % UNIFYFS_STAT_DEFAULT_BLKSIZE > 0) {                \
            (sb)->st_blocks += 1;                                             \
        }                                                                     \
        /* Re-purpose st_nlink to tell us if the file is laminated or not. */ \
        /* That way, if we do eventually make /unifyfs mountable, we can   */ \
        /* easily see with 'ls -l' or stat if the file is laminated or not.*/ \
        (sb)->st_nlink = _fattr->is_laminated ? 1 : 0;                        \
                                                                              \
        (sb)->st_atim = _fattr->atime;                                        \
        (sb)->st_mtim = _fattr->mtime;                                        \
        (sb)->st_ctim = _fattr->ctime;                                        \
    }                                                                         \
} while (0)


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

/*
 * Hash a file path to a uint64_t using MD5
 * @param path absolute file path
 * @return hash value
 */
uint64_t compute_path_md5(const char* path);

/*
 * Hash a file path to a positive integer gfid
 * @param path absolute file path
 * @return gfid
 */
static inline
int unifyfs_generate_gfid(const char* path)
{
    /* until we support 64-bit gfids, use top 32 bits */
    uint64_t hash64 = compute_path_md5(path);
    uint32_t hash32 = (uint32_t)(hash64 >> 32);

    /* To guarantee a positive value, we shift right one bit
     * to make sure the top bit is zero. */
    hash32 = hash32 >> 1;

    return (int)hash32;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_META_H */
