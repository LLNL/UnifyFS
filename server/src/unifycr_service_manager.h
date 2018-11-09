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

#ifndef UNIFYCR_SERVICE_MANAGER_H
#define UNIFYCR_SERVICE_MANAGER_H

#include <mpi.h>
#include "unifycr_global.h"
#include "arraylist.h"
#include <aio.h>

typedef struct {
    data_msg_t msg;
    char *addr;
} ack_meta_t;

typedef struct {
    MPI_Request req;
    MPI_Status stat;
    int src_rank;
    int src_thrd;
} ack_stat_t;

typedef struct {
    size_t start_idx;
    size_t end_idx;
    size_t size;
    int app_id;
    int cli_id;
    int arrival_time;
} read_task_t;

typedef struct {
    read_task_t *read_tasks;
    size_t num;
    size_t cap;
} task_set_t;

typedef struct {
    send_msg_t *msg;
    size_t num;
    size_t cap;
} service_msgs_t;

typedef struct {
    struct aiocb read_cb;
    size_t index;
    size_t start_pos;
    size_t end_pos;
    char *mem_pos;
} pended_read_t;

typedef struct {
    arraylist_t *ack_list;
    int start_cursor;
    int rank_id;
    int thrd_id;
    int src_cli_rank;
    size_t src_sz;
} rank_ack_meta_t;

typedef struct {
    rank_ack_meta_t *ack_metas;
    size_t num;
} rank_ack_task_t;

// comparison helper functions
int compare_rank_thrd(int src_rank, int src_thrd,
                      int cmp_rank, int cmp_thrd);
int compare_ack_stat(const void *a, const void *b);
int compare_read_task(const void *a, const void *b);
int compare_send_msg(const void *a, const void *b);

// service manager methods
int sm_exit(void);

int sm_init_socket(void);

void *sm_service_reads(void *ctx);

int sm_decode_msg(char *recv_msgs);

int sm_read_send_pipe(task_set_t *read_task_set,
                      service_msgs_t *service_msgs,
                      rank_ack_task_t *rank_ack_task);

int sm_cluster_reads(task_set_t *read_task_set,
                     service_msgs_t *service_msgs);

int sm_classfy_reads(service_msgs_t *service_msgs);

int sm_ack_reads(service_msgs_t *service_msgs);

int sm_ack_remote_delegator(rank_ack_meta_t *ptr_ack_meta);

int sm_wait_until_digested(task_set_t *read_task_set,
                           service_msgs_t *service_msgs,
                           rank_ack_task_t *read_ack_task);

// helper methods
void reset_read_tasks(task_set_t *read_task_set,
                      service_msgs_t *service_msgs, size_t index);

size_t find_ack_meta(rank_ack_task_t *rank_ack_task,
                     int src_rank, int src_thrd);

int insert_ack_meta(rank_ack_task_t *rank_ack_task,
                    ack_meta_t *ptr_ack, size_t pos,
                    int rank_id, int thrd_id, int src_cli_rank);

int ins_to_ack_lst(rank_ack_task_t *rank_ack_task,
                   char *mem_addr, service_msgs_t *service_msgs,
                   size_t index, size_t src_offset, size_t len);

int batch_ins_to_ack_lst(rank_ack_task_t *rank_ack_task,
                         read_task_t *read_task,
                         service_msgs_t *service_msgs,
                         char *mem_addr,
                         size_t start_offset, size_t end_offset);

// debug print methods
void print_ack_meta(rank_ack_task_t *rank_ack_tasks);
void print_pended_reads(arraylist_t *pended_reads);
void print_pended_sends(arraylist_t *pended_sends);
void print_service_msgs(service_msgs_t *service_msgs);
void print_task_set(task_set_t *read_task_set,
                    service_msgs_t *service_msgs);

#endif
