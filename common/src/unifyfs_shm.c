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

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "unifyfs_const.h"
#include "unifyfs_log.h"
#include "unifyfs_shm.h"

/* Allocate a shared memory region with given name and size,
 * and map it into memory.
 * Returns a pointer to shm_context for region if successful,
 * or NULL on error */
shm_context* unifyfs_shm_alloc(const char* name, size_t size)
{
    int ret;

    /* open shared memory file */
    errno = 0;
    int fd = shm_open(name, O_RDWR | O_CREAT, 0770);
    if (fd == -1) {
        /* failed to open shared memory */
        LOGERR("Failed to open shared memory %s (%s)",
               name, strerror(errno));
        return NULL;
    }

    /* set size of shared memory region */
#ifdef HAVE_POSIX_FALLOCATE
    do { /* this loop handles syscall interruption for large allocations */
        int try_count = 0;
        ret = posix_fallocate(fd, 0, size);
        if (ret != 0) {
            /* failed to set size shared memory */
            try_count++;
            if ((ret != EINTR) || (try_count >= 5)) {
                LOGERR("posix_fallocate failed for %s (%s)",
                    name, strerror(ret));
                close(fd);
                return NULL;
            }
        }
    } while (ret != 0);
#else
    errno = 0;
    ret = ftruncate(fd, size);
    if (ret == -1) {
        /* failed to set size of shared memory */
        LOGERR("ftruncate failed for %s (%s)",
               name, strerror(errno));
        close(fd);
        return NULL;
    }
#endif

    /* map shared memory region into address space */
    errno = 0;
    void* addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED,
                      fd, 0);
    if (addr == MAP_FAILED) {
        /* failed to open shared memory */
        LOGERR("Failed to mmap shared memory %s (%s)",
               name, strerror(errno));
        close(fd);
        return NULL;
    }

    /* safe to close file descriptor now */
    errno = 0;
    ret = close(fd);
    if (ret == -1) {
        /* failed to close shared memory */
        LOGERR("Failed to close shared memory fd %d (%s)",
               fd, strerror(errno));

        /* not fatal, so keep going */
    }

    /* return pointer to new shm_context */
    shm_context* ctx = (shm_context*) calloc(1, sizeof(shm_context));
    if (NULL != ctx) {
        snprintf(ctx->name, sizeof(ctx->name), "%s", name);
        ctx->addr = addr;
        ctx->size = size;
    }
    return ctx;
}

/* Unmaps shared memory region and frees its context.
 * The shm_context pointer is set to NULL on success.
 * Returns UNIFYFS_SUCCESS on success, or error code */
int unifyfs_shm_free(shm_context** pctx)
{
    /* check that we got an address (to something) */
    if (pctx == NULL) {
        return EINVAL;
    }

    shm_context* ctx = *pctx;
    if (ctx == NULL) {
        return EINVAL;
    }

    /* get address of shared memory region */
    void* addr = ctx->addr;

    /* if we have a pointer, try to munmap it */
    if (addr != NULL) {
        /* unmap shared memory from memory space */
        errno = 0;
        int rc = munmap(addr, ctx->size);
        if (rc == -1) {
            /* failed to unmap shared memory */
            int err = errno;
            LOGERR("Failed to unmap shared memory %s (%s)",
                   ctx->name, strerror(err));

            /* not fatal, so keep going */
        }
    }

    /* free shmem context structure */
    free(ctx);

    /* set caller's pointer to NULL */
    *pctx = NULL;

    return UNIFYFS_SUCCESS;
}

/* Unlinks file used to attach to a shared memory region.
 * Once unlinked, no other processes may attach.
 * Returns UNIFYFS_SUCCESS on success, or error code. */
int unifyfs_shm_unlink(shm_context* ctx)
{
    /* check context pointer */
    if (ctx == NULL) {
        return EINVAL;
    }

    /* unlink file naming the shared memory region */
    errno = 0;
    int rc = shm_unlink(ctx->name);
    if (rc == -1) {
        int err = errno;
        if (ENOENT != err) {
            /* failed to remove shared memory */
            LOGERR("Failed to unlink shared memory %s (%s)",
                   ctx->name, strerror(err));
        }
        /* not fatal, so keep going */
    }

    return UNIFYFS_SUCCESS;
}
