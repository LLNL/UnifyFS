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

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file LICENSE.CRUISE
 */

#ifndef UNIFYFS_H
#define UNIFYFS_H

#include <limits.h>
#include <stddef.h>      // size_t
#include <sys/types.h>   // off_t

#include "unifyfs_const.h"

#ifdef __cplusplus
extern "C" {
#endif

/* linked list of chunk information given to an external library wanting
 * to RDMA out a file from UNIFYFS */
typedef struct {
    off_t chunk_id;
    int location;
    void* chunk_mr;
    off_t spillover_offset;
    struct chunk_list_t* next;
} chunk_list_t;

/*data structures defined for unifyfs********************/

typedef struct {
    char hostname[UNIFYFS_MAX_HOSTNAME];
    int rank;
} name_rank_pair_t;

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
