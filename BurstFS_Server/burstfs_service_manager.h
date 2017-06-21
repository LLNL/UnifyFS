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

#ifndef BURSTFS_SERVICE_MANAGER_H
#define BURSTFS_SERVICE_MANAGER_H
#include <mpi.h>
#include "burstfs_global.h"
#include "arraylist.h"
#include <aio.h>

typedef struct {
	long src_fid;
	long src_offset;
	long length;
	char *addr;
}ack_meta_t;

typedef struct {
	MPI_Request req;
	MPI_Status stat;
	int src_rank;
	int src_thrd;
}ack_stat_t;

typedef struct {
	int start_idx;
	int end_idx;
	int size;
	int app_id;
	int cli_id;
	int arrival_time;
}read_task_t;

typedef struct {
	read_task_t *read_tasks;
	int num;
	int cap;
}task_set_t;

typedef struct {
	send_msg_t *msg;
	int num;
	int cap;
} service_msgs_t;

typedef struct {
	struct aiocb read_cb;
	int index;
	int start_pos;
	int end_pos;
	char *mem_pos;
}pended_read_t;

typedef struct {
	arraylist_t *ack_list;
	int rank_id;
	int thrd_id;
	int src_cli_rank;
	long src_sz;
	int start_cursor;
}rank_ack_meta_t;

typedef struct {
	rank_ack_meta_t *ack_metas;
	int num;
}rank_ack_task_t;

int insert_ack_meta(rank_ack_task_t *rank_ack_task,\
		ack_meta_t *ptr_ack, int pos, int rank_id, int thrd_id, \
			int src_cli_rank);
int compare_rank_thrd(int src_rank, int src_thrd, \
		int cmp_rank, int cmp_thrd);
void *sm_service_reads(void *ctx);
int sm_decode_msg(char *recv_msg_buf);
int sm_classfy_reads(service_msgs_t *service_msgs);
int sm_ack_reads(service_msgs_t *service_msgs);
int compare_send_msg(void *a, void *b);
int sm_wait_until_digested(task_set_t *read_task_set,\
		service_msgs_t *service_msgs,\
			rank_ack_task_t *read_ack_task);
int sm_ack_remote_delegator(rank_ack_meta_t *ptr_ack_meta);
int batch_ins_to_ack_lst(rank_ack_task_t *rank_ack_task,
		read_task_t *read_task,\
		service_msgs_t *service_msgs,\
			char  *mem_addr, int start_offset, int end_offset);
int sm_read_send_pipe(task_set_t *read_task_set,\
		service_msgs_t *service_msgs, \
		rank_ack_task_t *rank_ack_task);
int sm_init_socket();
int ins_to_ack_lst(rank_ack_task_t *rank_ack_task, \
		char *mem_addr, service_msgs_t *service_msgs,\
			int index, long src_offset, long len);
int find_ack_meta(rank_ack_task_t *rank_ack_task,\
		int src_rank, int src_thrd);
int sm_cluster_reads(task_set_t *read_task_set,\
		service_msgs_t *service_msgs);
void reset_read_tasks(task_set_t *read_task_set,\
		service_msgs_t *service_msgs, int index);
int compare_read_task(void *a, void *b);
int sm_exit();
void print_task_set(task_set_t *read_task_set,\
		service_msgs_t * service_msgs);
void print_service_msgs(service_msgs_t *service_msgs);
void reset_read_tasks(task_set_t *read_task_set,\
		service_msgs_t *service_msgs, int index);
void print_task_set(task_set_t *read_task_set,\
		service_msgs_t * service_msgs);
void print_pended_reads(arraylist_t *pended_reads);
void print_pended_sends(arraylist_t *pended_sends);
void print_ack_meta(rank_ack_task_t *rank_ack_tasks);
int compare_ack_stat(void *a, void *b);
#endif
