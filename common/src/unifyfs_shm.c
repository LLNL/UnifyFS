/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */
#include <config.h>

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "unifyfs_log.h"
#include "unifyfs_const.h"

/* Creates a shared memory of given size under specified name.
 * Returns address of new shared memory if successful.
 * Returns NULL on error. */
void* unifyfs_shm_alloc(const char* name, size_t size)
{
    int ret;

    /* open shared memory file */
    errno = 0;
    int fd = shm_open(name, O_RDWR | O_CREAT, 0770);
    if (fd == -1) {
        /* failed to open shared memory */
        LOGERR("Failed to open shared memory %s errno=%d (%s)",
               name, errno, strerror(errno));
        return NULL;
    }

    /* set size of shared memory region */
#ifdef HAVE_POSIX_FALLOCATE
    ret = posix_fallocate(fd, 0, size);
    if (ret != 0) {
        /* failed to set size shared memory */
        errno = ret;
        LOGERR("posix_fallocate failed for %s errno=%d (%s)",
               name, errno, strerror(errno));
        close(fd);
        return NULL;
    }
#else
    errno = 0;
    ret = ftruncate(fd, size);
    if (ret == -1) {
        /* failed to set size of shared memory */
        LOGERR("ftruncate failed for %s errno=%d (%s)",
               name, errno, strerror(errno));
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
        LOGERR("Failed to mmap shared memory %s errno=%d (%s)",
               name, errno, strerror(errno));
        close(fd);
        return NULL;
    }

    /* safe to close file descriptor now */
    errno = 0;
    ret = close(fd);
    if (ret == -1) {
        /* failed to open shared memory */
        LOGERR("Failed to mmap shared memory %s errno=%d (%s)",
               name, errno, strerror(errno));

        /* not fatal, so keep going */
    }

    /* return address */
    return addr;
}

/* Unmaps shared memory region from memory.
 * Caller should provide the address of a pointer to the region
 * in paddr.  Sets paddr to NULL on return.
 * Returns UNIFYFS_SUCCESS on success. */
int unifyfs_shm_free(const char* name, size_t size, void** paddr)
{
    /* check that we got an address (to something) */
    if (paddr == NULL) {
        return UNIFYFS_FAILURE;
    }

    /* get address of shared memory region */
    void* addr = *paddr;

    /* if we have a pointer, munmap the region, when all procs
     * have unmapped the region, the OS will free the memory */
    if (addr != NULL) {
        /* unmap shared memory from memory space */
        errno = 0;
        int rc = munmap(addr, size);
        if (rc == -1) {
            /* failed to unmap shared memory */
            LOGERR("Failed to unmap shared memory %s errno=%d (%s)",
                   name, errno, strerror(errno));

            /* not fatal, so keep going */
        }
    }

    /* set caller's pointer to NULL */
    *paddr = NULL;

    return UNIFYFS_SUCCESS;
}

/* Unlinks file used to attach to a shared memory region.
 * Once unlinked, no other processes may attach.
 * Returns UNIFYFS_SUCCESS on success. */
int unifyfs_shm_unlink(const char* name)
{
    /* delete associated file if told to unlink */
    errno = 0;
    int rc = shm_unlink(name);
    if (rc == -1) {
        int err = errno;
        if (ENOENT != err) {
            /* failed to remove shared memory */
            LOGERR("Failed to unlink shared memory %s errno=%d (%s)",
                   name, err, strerror(err));
        }
        /* not fatal, so keep going */
    }

    return UNIFYFS_SUCCESS;
}
