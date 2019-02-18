/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "unifycr_log.h"
#include "unifycr_const.h"

/* TODO: same function exists in client code, move this to common */
/* creates a shared memory of given size under specified name,
 * returns address of new shared memory if successful,
 * returns NULL on error  */
void* unifycr_shm_alloc(const char* name, size_t size)
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

/* unmaps shared memory region from memory, and releases it,
 * caller should provide the address of a pointer to the region
 * in paddr, sets paddr to NULL on return,
 * returns UNIFYCR_SUCCESS on success */
int unifycr_shm_free(const char* name, size_t size, void** paddr)
{
    /* check that we got an address (to something) */
    if (paddr == NULL) {
        return UNIFYCR_FAILURE;
    }

    /* get address of shared memory region */
    void* addr = *paddr;

    /* if we have a pointer, try to munmap and unlink it */
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

        /* release our reference to the shared memory region */
        errno = 0;
        rc = shm_unlink(name);
        if (rc == -1) {
            int err = errno;
            if (ENOENT != err) {
                /* failed to remove shared memory */
                LOGERR("Failed to unlink shared memory %s errno=%d (%s)",
                       name, err, strerror(err));
            }
            /* not fatal, so keep going */
        }
    }

    /* set caller's pointer to NULL */
    *paddr = NULL;

    return UNIFYCR_SUCCESS;
}
