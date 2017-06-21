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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
* Please read https://github.com/llnl/burstfs/LICENSE for full license text.
*/

#ifndef BURSTFS_INIT_H
#define BURSTFS_INIT_H
#include <pthread.h>
#include "burstfs_const.h"

/*
 * structure that records the information of
 * each application launched by srun.
 * */
typedef struct {
	char hostname[ULFS_MAX_FILENAME];
	int rank;
}name_rank_pair_t;

typedef struct {
	pthread_t thrd;
	pthread_cond_t	thrd_cond;
}thread_ctrl_t;

static int CountTasksPerNode(int rank, int numTasks);
static int compare_name_rank_pair(const void *a, const void *b);
static int compare_int(void *a, void *b);
static int find_rank_idx(int my_rank,\
		int *local_rank_lst, int local_rank_cnt);
static int burstfs_exit();
#endif
