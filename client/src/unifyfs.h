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

int unifyfs_mount(const char prefix[], int rank, size_t size,
                  int l_app_id);
int unifyfs_unmount(void);

/**
 * @brief transfer a single file between unifyfs and other file system. either
 * @src or @dst should (not both) specify a unifyfs pathname, i.e., /unifyfs/..
 *
 * @param src source file path
 * @param dst destination file path
 * @param parallel parallel transfer if set (parallel=1)
 *
 * @return 0 on success, negative errno otherwise.
 */
int unifyfs_transfer_file(const char* src, const char* dst, int parallel);

static inline
int unifyfs_transfer_file_serial(const char* src, const char* dst)
{
    return unifyfs_transfer_file(src, dst, 0);
}

static inline
int unifyfs_transfer_file_parallel(const char* src, const char* dst)
{
    return unifyfs_transfer_file(src, dst, 1);
}


#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_H */
