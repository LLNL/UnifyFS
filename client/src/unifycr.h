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

#ifndef UNIFYCR_H
#define UNIFYCR_H

#include <limits.h>
#include <stddef.h>      // size_t
#include <sys/types.h>   // off_t

#include "unifycr_const.h"

#ifdef __cplusplus
extern "C" {
#endif

/* linked list of chunk information given to an external library wanting
 * to RDMA out a file from UNIFYCR */
typedef struct {
    off_t chunk_id;
    int location;
    void* chunk_mr;
    off_t spillover_offset;
    struct chunk_list_t* next;
} chunk_list_t;

/*data structures defined for unifycr********************/

typedef struct {
    char hostname[UNIFYCR_MAX_HOSTNAME];
    int rank;
} name_rank_pair_t;

int unifycr_mount(const char prefix[], int rank, size_t size,
                  int l_app_id);
int unifycr_unmount(void);
int compare_fattr(const void* a, const void* b);

/**
 * @brief transfer a single file between unifycr and other file system. either
 * @src or @dst should (not both) specify a unifycr pathname, i.e., /unifycr/..
 *
 * @param src source file path
 * @param dst destination file path
 *
 * @return 0 on success, negative errno otherwise.
 */
int unifycr_transfer_file(const char* src, const char* dst);


#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYCR_H */
