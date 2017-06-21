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

#ifndef BURSTFS_METADATA_H
#define BURSTFS_METADATA_H
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "mdhim.h"
#include "indexes.h"
#include "arraylist.h"
#include "burstfs_const.h"
#include "burstfs_global.h"

#define DEF_META_PATH "/l/ssd/"
#define MANIFEST_FILE_NAME "mdhim_manifest_"
#define DEF_DB_NAME "burstfs_db"
#define DEF_SERVER_RATIO 1
#define DEF_RANGE_SZ 1048576

typedef struct {
	unsigned long fid;
	unsigned long offset;
}burstfs_key_t;

typedef struct {
	unsigned long delegator_id;
	unsigned long len;
	unsigned long addr;
	unsigned long app_rank_id; /*include both app and rank id*/
}burstfs_val_t;

typedef struct {
	off_t file_pos;
	off_t mem_pos;
	size_t length;
	int fid;
}burstfs_index_t;

typedef struct {
	int fid;
	long offset;
	long length;
}cli_req_t;

extern arraylist_t *ulfs_keys;
extern arraylist_t *ulfs_vals;
extern arraylist_t *ulfs_metas;

int meta_sanitize();
int meta_init_store();
void print_bget_indices(int app_id, int cli_id, \
		send_msg_t *index_set, int tot_num);
int meta_process_fsync (int sock_id);
int meta_batch_get(int app_id, int client_id,\
		int thrd_id, int dbg_rank, char *shm_reqbuf, int num,\
			msg_meta_t* del_req_set);
int meta_init_indices();
int meta_free_indices();
void print_fsync_indices(burstfs_key_t **burstfs_keys,\
		burstfs_val_t **burstfs_vals, long num_entries);
int meta_process_attr_set(char *ptr_cmd, int sock_id);
int meta_process_attr_get(char *buf, int sock_id,\
		burstfs_file_attr_t *ptr_attr_val);

#endif
