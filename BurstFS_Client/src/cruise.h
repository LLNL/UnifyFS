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

#ifndef CRUISE_H
#define CRUISE_H

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
/* TODO: namespace C */

/* linked list of chunk information given to an external library wanting
 * to RDMA out a file from CRUISE */
typedef struct {
    off_t chunk_id;
    int location;
    void *chunk_mr;
    off_t spillover_offset;
    struct chunk_list_t *next;
} chunk_list_t;

/*data structures defined for burstfs********************/

#define MMAP_OPEN_FLAG O_RDWR|O_CREAT
#define MMAP_OPEN_MODE 00777

typedef struct {
	char hostname[CRUISE_MAX_FILENAME];
	int rank;
}name_rank_pair_t;

static int get_del_cnt();
static int compare_int(void *a, void *b);
static int compare_name_rank_pair(const void *a, const void *b);
static int find_rank_idx(int rank,\
		int *local_rank_lst, int local_rank_cnt);
static int burstfs_init_socket(int proc_id,\
		int l_num_procs_per_node, \
			int l_num_del_per_node);
static int CountTasksPerNode(int rank, int numTasks);
static int burstfs_init_req_shm(int local_rank_idx, int app_id);
int burstfs_mount(const char prefix[], int rank, size_t size,\
		int l_app_id, int subtype);
static int burstfs_init_recv_shm(int local_rank_idx, int app_id);
static int burstfs_init_req_shm(int local_rank_idx, int app_id);
static int burstfs_sync_to_del();
static int cruise_get_global_fid(const char *path, int *gfid);
static int get_global_file_meta(int gfid, burstfs_fattr_t **file_meta);
static int set_global_file_meta(burstfs_fattr_t *f_meta);
static int ins_file_meta(burstfs_fattr_buf_t *ptr_f_meta_log,\
		burstfs_fattr_t *ins_fattr);
int compare_fattr(void *a, void *b);


/* mount memfs at some prefix location */
int cruise_mount(const char prefix[], size_t size, int rank);

/* get information about the chunk data region
 * for external async libraries to register during their init */
size_t cruise_get_data_region(void **ptr);

/* get a list of chunks for a given file (useful for RDMA, etc.) */
chunk_list_t* cruise_get_chunk_list(char* path);

/* debug function to print list of chunks constituting a file
 * and to test above function*/
void cruise_print_chunk_list(char* path);

#endif /* CRUISE_H */
