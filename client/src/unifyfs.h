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

#ifndef UNIFYFS_H
#define UNIFYFS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "UNFY" in ASCII */
//#define UNIFYFS_SUPER_MAGIC (0x554E4659)

/* "UnifyFS!" in ASCII */
#define UNIFYFS_SUPER_MAGIC (0x556E696679465321)

/**
 * Mount UnifyFS file system at given prefix
 *
 * @param prefix  mountpoint prefix
 * @param rank    client rank within application
 * @param size    the number of application clients
 *
 * @return success/error code
 */
int unifyfs_mount(const char prefix[], int rank, size_t size);

/**
 * Unmount the UnifyFS file system
 *
 * @return success/error code
 */
int unifyfs_unmount(void);


/* Enumeration to control transfer mode */
enum {
    UNIFYFS_TRANSFER_SERIAL = 0,
    UNIFYFS_TRANSFER_PARALLEL = 1,
};

/**
 * Transfer a single file between UnifyFS and another file system.
 * Either @src or @dst (not both) should specify a path within
 * the UnifyFS namespace prefixed by the mountpoint, e.g., /unifyfs/..
 *
 * @param src   source file path
 * @param dst   destination file path
 * @param mode  transfer mode
 *
 * @return 0 on success, negative errno otherwise.
 */
int unifyfs_transfer_file(const char* src,
                          const char* dst,
                          int mode);

static inline
int unifyfs_transfer_file_serial(const char* src, const char* dst)
{
    return unifyfs_transfer_file(src, dst, UNIFYFS_TRANSFER_SERIAL);
}

static inline
int unifyfs_transfer_file_parallel(const char* src, const char* dst)
{
    return unifyfs_transfer_file(src, dst, UNIFYFS_TRANSFER_PARALLEL);
}


#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_H */
