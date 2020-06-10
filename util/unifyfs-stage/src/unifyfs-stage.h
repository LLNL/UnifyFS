#ifndef __UNIFYFS_STAGE_H
#define __UNIFYFS_STAGE_H

#include <stdio.h>
#include <stdarg.h>
#include <unifyfs.h>

#define UNIFYFS_STAGE_MD5_BLOCKSIZE    (1048576)

/*
 * serial: each file is tranferred by a process.
 * parallel: a file is transferred by all processes.
 */
enum {
    UNIFYFS_STAGE_SERIAL = 0,
    UNIFYFS_STAGE_PARALLEL = 1,
};

struct _unifyfs_stage {
    int rank;               /* my rank */
    int total_ranks;        /* mpi world size */

    int checksum;           /* perform checksum? 0:no, 1:yes */
    int mode;               /* transfer mode? 0:serial, 1:parallel */
    int should_we_mount_unifyfs;  /* mount? 0:no (for testing), 1: yes */
    char* mountpoint;       /* unifyfs mountpoint */
    char* manifest_file;    /* manifest file containing the transfer list */
};

typedef struct _unifyfs_stage unifyfs_stage_t;

static inline void unifyfs_stage_print(unifyfs_stage_t* ctx)
{
    printf("== unifyfs stage context ==\n"
           "rank = %d\n"
           "total ranks = %d\n"
           "checksum = %d\n"
           "mode = %d\n"
           "should_we_mount_unifyfs = %d\n"
           "mountpoint = %s\n"
           "manifest file = %s\n",
           ctx->rank,
           ctx->total_ranks,
           ctx->checksum,
           ctx->mode,
           ctx->should_we_mount_unifyfs,
           ctx->mountpoint,
           ctx->manifest_file);
}

/**
 * @brief transfer files specified in @ctx
 *
 * @param ctx unifyfs_stage_t data transfer context
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_stage_transfer(unifyfs_stage_t* ctx);

extern int verbose;
extern int rank;
extern int total_ranks;

#endif /* __UNIFYFS_STAGE_H */
