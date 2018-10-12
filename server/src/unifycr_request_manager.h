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

#ifndef UNIFYCR_REQUEST_MANAGER_H
#define UNIFYCR_REQUEST_MANAGER_H

#include "unifycr_const.h"
#include "unifycr_global.h"
#include "unifycr_meta.h"
#include "arraylist.h"

typedef struct {
    int src_fid;
    long offset;
    long length;
} shm_meta_t; /*metadata format in the shared memory*/


/**
 * synchronize all the indices and file attributes
 * to the key-value store
 * @param sock_id: the connection id in poll_set of the delegator
 * @return success/error code
 */
int rm_process_fsync(int sock_id);

void *rm_delegate_request_thread(void *arg);

int rm_read_remote_data(int sock_id,
                        size_t req_cnt);

int rm_send_remote_requests(thrd_ctrl_t *thrd_ctrl,
                            int thrd_tag,
                            size_t *tot_sz);

int rm_pack_send_requests(char *req_msg_buf,
                          send_msg_t *send_metas,
                          size_t req_cnt,
                          size_t *tot_sz);

int compare_delegators(const void *a,
                       const void *b);

int rm_pack_send_msg(int rank,
                     char *send_msg_buf,
                     send_msg_t *send_metas,
                     size_t meta_cnt,
                     size_t *tot_sz);

int rm_receive_remote_message(int app_id,
                              int sock_id,
                              size_t tot_sz);

void print_recv_msg(int app_id,
                    int cli_id,
                    int dbg_rank,
                    int thrd_id,
                    shm_meta_t *msg);

int rm_process_received_msg(int app_id,
                            int sock_id,
                            char *recv_msg_buf,
                            size_t *ptr_tot_sz);

void print_remote_del_reqs(int app_id,
                           int cli_id,
                           int dbg_rank,
                           del_req_stat_t *del_req_stat);

void print_send_msgs(send_msg_t *send_metas,
                     size_t msg_cnt,
                     int dbg_rank);

#endif
