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

#ifndef UNIFYCR_INIT_H
#define UNIFYCR_INIT_H
#include <pthread.h>
#include "unifycr_const.h"

/*
 * structure that records the information of
 * each application launched by srun.
 * */
typedef struct {
    char hostname[HOST_NAME_MAX];
    int rank;
} name_rank_pair_t;

typedef struct {
    pthread_t thrd;
    pthread_cond_t  thrd_cond;
} thread_ctrl_t;

static int CountTasksPerNode(int rank, int numTasks);
static int compare_name_rank_pair(const void *a, const void *b);
static int compare_int(const void *a, const void *b);
static int find_rank_idx(int my_rank,
                         int *local_rank_lst, int local_rank_cnt);
static int unifycr_exit();
#endif
