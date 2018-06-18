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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <limits.h>
#include <aio.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "log.h"
#include "unifycr_sock.h"
#include "unifycr_service_manager.h"
#include "unifycr_const.h"
#include "unifycr_global.h"
#include "arraylist.h"

service_msgs_t service_msgs;
task_set_t read_task_set;
rank_ack_task_t rank_ack_task;

arraylist_t *pended_reads;
arraylist_t *pended_sends;
arraylist_t *send_buf_lst;

int sm_sockfd = -1;
int wait_time = 0;
int dbg_rank = 0;
int sm_rc = 0;

long burst_data_sz;
long dbg_sending_cnt = 0;
long dbg_sent_cnt = 0;

char req_msg_buf[REQ_BUF_LEN];
char *mem_buf;

/**
* Service the read requests received from the requesting delegators
*/

void *sm_service_reads(void *ctx)
{
    int return_code, irecv_flag = 0;
    int rc, cmd, flag = 0;

    rc = sm_init_socket();
    if (rc < 0) {
        sm_rc = (int)UNIFYCR_ERROR_SOCKET;
        return (void *)&sm_rc;
    }

    dbg_rank = glb_rank;

    MPI_Status status;
    MPI_Request request;

    mem_buf = malloc(READ_BUF_SZ);
    memset(mem_buf, 0, READ_BUF_SZ);

    long bursty_interval = MAX_BURSTY_INTERVAL / 10;

    burst_data_sz = 0;

    read_task_set.read_tasks =
        (read_task_t *)malloc(MAX_META_PER_SEND * sizeof(read_task_t));
    read_task_set.num = 0;
    read_task_set.cap = MAX_META_PER_SEND;

    service_msgs.msg =
        (send_msg_t *)malloc(MAX_META_PER_SEND * sizeof(send_msg_t));
    service_msgs.num = 0;
    service_msgs.cap = MAX_META_PER_SEND;

    pended_reads = arraylist_create();
    if (pended_reads == NULL) {
        sm_rc = (int)UNIFYCR_ERROR_NOMEM;
        return (void *)&sm_rc;
    }

    pended_sends = arraylist_create();
    if (pended_sends == NULL) {
        sm_rc = (int)UNIFYCR_ERROR_NOMEM;
        return (void *)&sm_rc;
    }

    send_buf_lst = arraylist_create();
    if (send_buf_lst == NULL) {
        sm_rc = (int)UNIFYCR_ERROR_NOMEM;
        return (void *)&sm_rc;
    }

    rank_ack_task.num = 0;
    rank_ack_task.ack_metas =
        (rank_ack_meta_t *)malloc(glb_size * MAX_NUM_CLIENTS
                                  * sizeof(rank_ack_meta_t));

    int i;
    for (i = 0; i < glb_size * MAX_NUM_CLIENTS; i++) {
        rank_ack_task.ack_metas[i].ack_list = NULL;
        rank_ack_task.ack_metas[i].rank_id = -1;
        rank_ack_task.ack_metas[i].thrd_id = -1;
        rank_ack_task.ack_metas[i].src_sz = 0;
        rank_ack_task.ack_metas[i].start_cursor = 0;
    }

    /*received message format: cmd, req_num, a list of read
     * requests*/
    while (!flag) {
        MPI_Irecv(req_msg_buf, REQ_BUF_LEN, MPI_CHAR,
                  MPI_ANY_SOURCE, CLI_DATA_TAG,
                  MPI_COMM_WORLD, &request);

        return_code = MPI_Test(&request,
                               &irecv_flag, &status);
        if (return_code != MPI_SUCCESS) {
            sm_rc = (int)UNIFYCR_ERROR_RECV;
            return (void *)&sm_rc;
        }

        /*
         * keep receiving the read request
         * until the end of a anticipated
         * bursty behavior
         * */
        while (!irecv_flag) {
            if (bursty_interval > MIN_SLEEP_INTERVAL) {
                usleep(SLEEP_INTERVAL); /* wait an interval */
                wait_time += SLEEP_INTERVAL;
            }


            /* a bursty behavior is considered end when
             * wait time is larger than BURSTY_INTERVAL
             * */
            if (wait_time >= bursty_interval ||
                bursty_interval <= MIN_SLEEP_INTERVAL) {
                if (service_msgs.num != 0) {
                    rc = sm_cluster_reads(&read_task_set, &service_msgs);
                    if (rc != 0) {
                        sm_rc = rc;
                        return (void *)&sm_rc;
                    }

                    if (glb_rank == glb_rank) {
                        //  print_service_msgs(&service_msgs);
                    }

                    if (glb_rank == dbg_rank) {
                        //  print_task_set(&read_task_set, &service_msgs);
                    }
                    rc = sm_read_send_pipe(&read_task_set,
                                           &service_msgs, &rank_ack_task);
                    if (rc != 0) {
                        sm_rc = rc;
                        return (void *)&sm_rc;
                    }

                    service_msgs.num = 0;
                    read_task_set.num = 0;

                    if (burst_data_sz >= LARGE_BURSTY_DATA) {
                        bursty_interval = MAX_BURSTY_INTERVAL;
                    } else {
                        bursty_interval =
                            (long)((double)burst_data_sz / 1048576) * SLEEP_SLICE_PER_UNIT;

                    }
                    burst_data_sz = 0;
                }
                wait_time = 0;


            }

            return_code = MPI_Test(&request, &irecv_flag, &status);
            if (return_code != MPI_SUCCESS) {
                sm_rc = (int)UNIFYCR_ERROR_RECV;
                return (void *)&sm_rc;
            }

        }

        /*
         * Reinterpret memory type of req_msg_buf as an integer array
         * to extract service command from message header.
         */
        union rmb { int *i; char *c; };
        cmd = *(((union rmb)req_msg_buf).i);
        if (cmd == XFER_COMM_DATA)  {
            sm_decode_msg(req_msg_buf);
        }

        wait_time = 0;
        irecv_flag = 0;

        if (cmd == XFER_COMM_EXIT) {
            sm_exit();
            flag = 1;
        }

    }

    return (void *)&sm_rc;
}

/**
* Cluster read requests based on file
* offset and ages; Each log file is uniquely
* identified by client-side app_id and client_id, so
* from these two, we can locate the target
* log file (generated by the client-side program).
* @param: service_msgs: a list of received messages
* @param: read_task_set: a list of read tasks containing
* the clustered read requests
* return success/error
*/

int sm_cluster_reads(task_set_t *read_task_set,
                     service_msgs_t *service_msgs)
{
    int i;
    qsort(service_msgs->msg, service_msgs->num,
          sizeof(send_msg_t), compare_send_msg);

    read_task_set->num = 0;
    reset_read_tasks(read_task_set, service_msgs, 0);

    for (i = 1; i < service_msgs->num; i++) {
        /*reading on a different local log file*/

        if (read_task_set->read_tasks[read_task_set->num].app_id !=
            service_msgs->msg[i].dest_app_id ||
            read_task_set->read_tasks[read_task_set->num].cli_id !=
            service_msgs->msg[i].dest_client_id) {
            read_task_set->num++;
            reset_read_tasks(read_task_set, service_msgs, i);

        } else {

            /*contiguous from the last offset*/
            if (service_msgs->msg[i - 1].dest_offset
                + service_msgs->msg[i - 1].length ==
                service_msgs->msg[i].dest_offset) {
                /* if no larger than read_block_size, proceed*/
                /* ToDO: the size of individual read should be smaller
                 * than read_block_size, if read size is larger it
                 * needs to be split into the unit of READ_BLOCK_SIZE
                 * */
                if (read_task_set->read_tasks[read_task_set->num].size
                    + service_msgs->msg[i].length <= READ_BLOCK_SIZE) {
                    read_task_set->read_tasks[read_task_set->num].end_idx = i;
                    read_task_set->read_tasks[read_task_set->num].size +=
                        service_msgs->msg[i].length;
                    if (service_msgs->msg[i].arrival_time <
                        read_task_set->read_tasks[read_task_set->num].arrival_time) {
                        read_task_set->read_tasks[read_task_set->num].arrival_time
                            = service_msgs->msg[i].arrival_time;
                    }
                } else {
                    /* if larger than read block size, start a new read task,
                     * here the data size requested by individual read request
                     * should be smaller than read_block_size. The larger one
                     * has already been split by the initiator
                     * */
                    read_task_set->num++;
                    reset_read_tasks(read_task_set, service_msgs, i);
                }

            } else { /*not contiguous from the last offset*/
                read_task_set->num++;
                reset_read_tasks(read_task_set, service_msgs, i);
            }
        }
    }
    read_task_set->num++;
    return ULFS_SUCCESS;
}

/**
* Read and send the data via pipelined read, copy and send
* @param: read_task_set: a list of reading task
* @param: service_msgs: a list of received read requests
* @param: a list of send tasks for acknowledging the
* requesting delegators
* return success/error
*/
int sm_read_send_pipe(task_set_t *read_task_set,
                      service_msgs_t *service_msgs,
                      rank_ack_task_t *rank_ack_task)
{

    qsort(read_task_set->read_tasks, read_task_set->num,
          sizeof(read_task_t), compare_read_task);

    int tmp_app_id, tmp_cli_id, start_idx, tmp_fd;

    app_config_t *app_config;
    long tmp_offset, buf_cursor = 0;


    int i;

    for (i = 0; i < read_task_set->num; i++) {
        if (buf_cursor + read_task_set->read_tasks[i].size > READ_BUF_SZ) {
            // wait until all are digested once the read buffer is full
            sm_wait_until_digested(read_task_set,
                                   service_msgs, rank_ack_task);
            buf_cursor = 0;
        }

        tmp_app_id = read_task_set->read_tasks[i].app_id;
        tmp_cli_id = read_task_set->read_tasks[i].cli_id;
        app_config = arraylist_get(app_config_list, tmp_app_id);
        start_idx = read_task_set->read_tasks[i].start_idx;
        tmp_offset = service_msgs->msg[start_idx].dest_offset;
        tmp_fd = service_msgs->msg[start_idx].src_fid;

        /*requested data in read_task is totally in shared memory */
        if (tmp_offset + read_task_set->read_tasks[i].size
            <= app_config->data_size) {
            memcpy(mem_buf + buf_cursor,
                   app_config->shm_superblocks[tmp_cli_id]
                   + app_config->data_offset + tmp_offset,
                   read_task_set->read_tasks[i].size);
            burst_data_sz += read_task_set->read_tasks[i].size;
            // put to the network list

            batch_ins_to_ack_lst(rank_ack_task, &read_task_set->read_tasks[i],
                                 service_msgs, mem_buf + buf_cursor, 0,
                                 read_task_set->read_tasks[i].size - 1);
            buf_cursor += read_task_set->read_tasks[i].size;

        } else {
            /*part of the requested data is in shared memory*/
            if (tmp_offset < app_config->data_size) {
                long sz_from_mem = app_config->data_size - tmp_offset;
                memcpy(mem_buf + buf_cursor,
                       app_config->shm_superblocks[tmp_cli_id]
                       + app_config->data_offset + tmp_offset,
                       sz_from_mem);
                burst_data_sz += sz_from_mem;
                // link to ack list
                batch_ins_to_ack_lst(rank_ack_task, &read_task_set->read_tasks[i],
                                     service_msgs, mem_buf + buf_cursor, 0,
                                     sz_from_mem - 1);
                buf_cursor += sz_from_mem;

                long sz_from_ssd = read_task_set->read_tasks[i].size - sz_from_mem;

                /*post read*/
                tmp_fd = app_config->spill_log_fds[tmp_cli_id];
                int rc = pread(tmp_fd, mem_buf + buf_cursor, sz_from_ssd,
                               0);
                if (rc < 0) {
                    return (int)UNIFYCR_ERROR_READ;
                }

                buf_cursor += rc;
                burst_data_sz += sz_from_ssd;

            } else { /*all requested data in the current read task are on ssd*/
                tmp_fd = app_config->spill_log_fds[tmp_cli_id];

                pended_read_t *ptr =
                    (pended_read_t *)malloc(sizeof(pended_read_t));
                ptr->start_pos = 0;
                ptr->end_pos = read_task_set->read_tasks[i].size - 1;
                ptr->index = i;
                memset(&ptr->read_cb, 0, sizeof(struct aiocb));
                ptr->read_cb.aio_fildes = tmp_fd;
                ptr->read_cb.aio_buf = mem_buf + buf_cursor;
                ptr->read_cb.aio_offset =
                    tmp_offset - app_config->data_size;
                ptr->read_cb.aio_nbytes = read_task_set->read_tasks[i].size;
                ptr->mem_pos = mem_buf + buf_cursor;

                arraylist_add(pended_reads, ptr);

                int rc = aio_read(&ptr->read_cb);
                if (rc < 0) {
                    return (int)UNIFYCR_ERROR_READ;
                }
                burst_data_sz += read_task_set->read_tasks[i].size;
                buf_cursor += read_task_set->read_tasks[i].size;

            }
        }

    }

    sm_wait_until_digested(read_task_set,
                           service_msgs, rank_ack_task);
    buf_cursor = 0;

    return ULFS_SUCCESS;
}

/**
* Wait until all data are read and sent
* @param: read_task_set: a list of reading task
* @param: service_msgs: a list of received read requests
* @param: a list of send tasks for acknowledging the
* requesting delegators
* return success/error
*/
int sm_wait_until_digested(task_set_t *read_task_set,
                           service_msgs_t *service_msgs,
                           rank_ack_task_t *read_ack_task)
{
    int i, ret_code;
    pended_read_t *cur_pended_read;

    int *ptr_flags = NULL;

    if (arraylist_size(pended_reads) != 0) {
        ptr_flags = (int *)malloc(sizeof(int)
                                  * arraylist_size(pended_reads));
        memset(ptr_flags, 0, sizeof(int)
               * arraylist_size(pended_reads));
    }

    long counter = 0;
    while (1) {
        if (counter == arraylist_size(pended_reads)) {
            break;
        }
        for (i = 0; i < arraylist_size(pended_reads); i++) {
            cur_pended_read = (pended_read_t *)arraylist_get(pended_reads, i);
            if (aio_error(&cur_pended_read->read_cb)
                != EINPROGRESS && ptr_flags[i] != 1) {
                counter++;
                ptr_flags[i] = 1;
                ret_code = aio_return(&cur_pended_read->read_cb);
                if (ret_code > 0) {
                    // add to ack_list
                    batch_ins_to_ack_lst(read_ack_task,
                                         &read_task_set->read_tasks[cur_pended_read->index],
                                         service_msgs, cur_pended_read->mem_pos,
                                         cur_pended_read->start_pos, cur_pended_read->end_pos);

                } else {
                }
            }
        }

    }

    if (ptr_flags != NULL) {
        free(ptr_flags);
    }

    arraylist_reset(pended_reads);
    if (glb_rank == glb_rank) {
        //  print_ack_meta(read_ack_task);
    }

    /*Send back the remaining acks*/
    rank_ack_meta_t *ptr_ack_meta = NULL;
    long tmp_sz;
    for (i = 0; i < read_ack_task->num; i++) {
        ptr_ack_meta = &read_ack_task->ack_metas[i];
        tmp_sz = arraylist_size(ptr_ack_meta->ack_list);
        if (ptr_ack_meta->start_cursor < tmp_sz) {
            ret_code = sm_ack_remote_delegator(ptr_ack_meta);
            if (ret_code != ULFS_SUCCESS) {
                return ret_code;
            }
        }
    }

    if (arraylist_size(pended_sends) != 0) {
        ptr_flags = (int *)malloc(sizeof(int)
                                  * arraylist_size(pended_sends));
        memset(ptr_flags, 0, sizeof(int)
               * arraylist_size(pended_sends));
    }

    //  print_pended_sends(pended_sends);
    /*wait until all acks are sent*/
    counter = 0;
    ack_stat_t *ptr_ack_stat;
    while (1) {
        if (counter == arraylist_size(pended_sends)) {
            break;
        }
        for (i = 0; i < arraylist_size(pended_sends); i++) {
            if (ptr_flags[i] == 0) {
                ptr_ack_stat = arraylist_get(pended_sends, i);
                ret_code = MPI_Test(&ptr_ack_stat->req,
                                    &ptr_flags[i], &ptr_ack_stat->stat);
                if (ret_code != MPI_SUCCESS) {
                    return (int)UNIFYCR_ERROR_RECV;
                }

                if (ptr_flags[i] != 0) {
                    dbg_sent_cnt++;
                    counter++;
                }
            }
        }
    }

    if (arraylist_size(pended_sends) != 0) {
        free(ptr_flags);
    }

    for (i = 0; i < read_ack_task->num; i++) {
        ptr_ack_meta = &(read_ack_task->ack_metas[i]);
        ptr_ack_meta->start_cursor = 0;
        arraylist_reset(ptr_ack_meta->ack_list);
        ptr_ack_meta->rank_id = -1;
        ptr_ack_meta->thrd_id = -1;
        ptr_ack_meta->src_sz = 0;
    }
    read_ack_task->num = 0;
    arraylist_reset(pended_sends);
    send_buf_lst->size = 0;
    return ULFS_SUCCESS;
}



/*
 * insert the data read for each read_task_t to ack_list,
 * data will be transferred back lafter.
 * */
int batch_ins_to_ack_lst(rank_ack_task_t *rank_ack_task,
                         read_task_t *read_task,
                         service_msgs_t *service_msgs,
                         char  *mem_addr, int start_offset, int end_offset)
{
    /* search for the starting service msgs for
     * a given region of read_task_t identified by
     * start_offset and size*/

    int i = 0;
    int cur_offset = 0;

    /*                          read_task
     *    read_task_t: -----------******************------------
     *    service_msgs:[    ],[*********],[***], [******],[      ]
     *                      start_msg               end_msg
     *
     * */
    while (1) {
        if (service_msgs->msg[read_task->start_idx + i].length
            + cur_offset >= start_offset) {
            break;
        }
        cur_offset +=
            service_msgs->msg[read_task->start_idx + i].length;
        i++;
    }

    // pack to a ack_meta
    int partial_skip_len = start_offset - cur_offset;
    /*after finding the starting service msgs and insert it to the ack list,
     * continue to add the next ones across the region of the read_task
     * */
    int mem_pos = 0;

    int first = 1, tmp_len;
    long tmp_offset;

    while (1) {
        if (cur_offset + service_msgs->msg[read_task->start_idx + i].length
            >= end_offset + 1) {
            break;
        }

        if (first == 1) {
            tmp_len =
                service_msgs->msg[read_task->start_idx + i].length
                - partial_skip_len;
            tmp_offset = service_msgs->msg[read_task->start_idx + i].src_offset
                         + partial_skip_len;
            first = 0;
        } else {
            tmp_len = service_msgs->msg[read_task->start_idx + i].length;
            tmp_offset = service_msgs->msg[read_task->start_idx + i].src_offset;
        }

        ins_to_ack_lst(rank_ack_task,
                       mem_addr + mem_pos, service_msgs,
                       read_task->start_idx + i, tmp_offset,
                       tmp_len);
        mem_pos += tmp_len;
        cur_offset += tmp_len;
        i++;

    }

    /*insert part of the last message*/
    if (mem_pos < end_offset - start_offset + 1) {
        long src_offset = service_msgs->msg[read_task->start_idx + i].src_offset;
        long len = end_offset - start_offset + 1 - mem_pos;
        ins_to_ack_lst(rank_ack_task,
                       mem_addr + mem_pos, service_msgs,
                       read_task->start_idx + i, src_offset, len);
    }

    return ULFS_SUCCESS;
}

/*
 * send back ack to the remote delegator
 * */

int sm_ack_remote_delegator(rank_ack_meta_t *ptr_ack_meta)
{
    long i;
    int send_sz = sizeof(int), len;

    len = arraylist_size(ptr_ack_meta->ack_list)
          - ptr_ack_meta->start_cursor;

    char *send_msg_buf = NULL;
    if (send_buf_lst->size == send_buf_lst->cap) {
        send_msg_buf = (char *)malloc(SEND_BLOCK_SIZE);
        send_buf_lst->elems = (void **)realloc(send_buf_lst->elems,
                                               2 * sizeof(void *) * send_buf_lst->cap);
        for (i = 0; i < 2 * send_buf_lst->cap; i++) {
            send_buf_lst->elems[i] = NULL;
        }
        send_buf_lst->cap = 2 * send_buf_lst->cap;
        send_buf_lst->elems[send_buf_lst->size] = send_msg_buf;
        send_buf_lst->size++;
    } else {
        if (send_buf_lst->elems[send_buf_lst->size] == NULL) {
            send_buf_lst->elems[send_buf_lst->size]
                = malloc(SEND_BLOCK_SIZE);
        }
        send_msg_buf = send_buf_lst->elems[send_buf_lst->size];
        send_buf_lst->size++;
    }

    ack_meta_t *ptr_meta_cursor;
    for (i = ptr_ack_meta->start_cursor;
         i < arraylist_size(ptr_ack_meta->ack_list);
         i++) {
        ptr_meta_cursor =
            (ack_meta_t *)arraylist_get(ptr_ack_meta->ack_list, i);

        memcpy(send_msg_buf + send_sz, &(ptr_meta_cursor->src_fid),
               sizeof(long));
        send_sz += sizeof(long);

        memcpy(send_msg_buf + send_sz, &(ptr_meta_cursor->src_offset),
               sizeof(long));
        send_sz += sizeof(long);

        memcpy(send_msg_buf + send_sz, &(ptr_meta_cursor->length),
               sizeof(long));
        send_sz += sizeof(long);

        memcpy(send_msg_buf + send_sz, ptr_meta_cursor->addr,
               ptr_meta_cursor->length);
        send_sz += ptr_meta_cursor->length;
    }

    memcpy(send_msg_buf, &len, sizeof(int));

    ack_stat_t *ack_stat = (ack_stat_t *)malloc(sizeof(ack_stat_t));
    ack_stat->src_rank = ptr_ack_meta->rank_id;
    ack_stat->src_thrd = ptr_ack_meta->thrd_id;
    MPI_Isend(send_msg_buf, send_sz, MPI_CHAR,
              ptr_ack_meta->rank_id,
              SER_DATA_TAG + ptr_ack_meta->thrd_id,
              MPI_COMM_WORLD, &(ack_stat->req));
    arraylist_add(pended_sends, ack_stat);
    dbg_sending_cnt++;

    return ULFS_SUCCESS;
}

int compare_ack_stat(const void *a, const void *b)
{
    const ack_stat_t *ptr_a = a;
    const ack_stat_t *ptr_b = b;

    if (ptr_a->src_rank > ptr_b->src_rank)
        return 1;

    if (ptr_a->src_rank < ptr_b->src_rank)
        return -1;

    if (ptr_a->src_thrd > ptr_b->src_thrd)
        return 1;

    if (ptr_a->src_thrd < ptr_b->src_thrd)
        return -1;

    return 0;
}

/*
 * insert a message to an entry of ack list corresponding to its destination
 * delegator.
 * @param: rank_ack_task: a list of ack for each (rank, thrd) pair
 * @param: mem_addr: address of data to be acked in mem_pool
 * @param service_msgs and index: identifies msg to be inserted to ack_lst
 * @param src_offset: offset of the requested segment on the logical
 *  file (not the physical log file on SSD. E.g. for N-1 pattern, logical
 *  offset is the offset on the shared file)
 * @param len: length of the message
 * */
int ins_to_ack_lst(rank_ack_task_t *rank_ack_task,
                   char *mem_addr, service_msgs_t *service_msgs,
                   int index, long src_offset, long len)
{
    ack_meta_t *ptr_ack = (ack_meta_t *)malloc(sizeof(ack_meta_t));

    int rc = 0;
    ptr_ack->addr = mem_addr;
    ptr_ack->length = len;
    ptr_ack->src_fid = service_msgs->msg[index].src_fid;

    /*the src_offset might start from any position in the message, so
     * make it a separate parameter*/
    ptr_ack->src_offset = src_offset;


    int src_thrd = service_msgs->msg[index].src_thrd;;
    int src_rank = service_msgs->msg[index].src_delegator_rank;
    int src_dbg_rank = service_msgs->msg[index].src_dbg_rank;

    /*after setting the ack for this message, link it
     * to a ack list based on its destination.
     * */
    rank_ack_meta_t *ptr_ack_meta = NULL;

    /*find the entry with the same destination (rank, thrd) pair*/

    int pos = find_ack_meta(rank_ack_task,
                            src_rank, src_thrd);
    if (rank_ack_task->ack_metas[pos].rank_id != src_rank ||
        rank_ack_task->ack_metas[pos].thrd_id != src_thrd) {
        rc = insert_ack_meta(rank_ack_task, ptr_ack, pos,
                             src_rank, src_thrd, src_dbg_rank);

    } else { /*found the corresponding entry for ack*/
        ptr_ack_meta = &rank_ack_task->ack_metas[pos];
        long num_entries = arraylist_size(ptr_ack_meta->ack_list);
        long num_to_ack = num_entries - ptr_ack_meta->start_cursor;

        if (ptr_ack_meta->src_sz + ptr_ack->length
            + (num_to_ack + 1) * sizeof(ack_meta_t) > SEND_BLOCK_SIZE) {
            ptr_ack_meta->src_sz = ptr_ack->length;
            rc = sm_ack_remote_delegator(ptr_ack_meta);
            arraylist_add(ptr_ack_meta->ack_list, ptr_ack);
            /*start_cursor records the starting ack for the next send*/
            ptr_ack_meta->start_cursor += num_to_ack;
            if (rc < 0) {
                return (int)UNIFYCR_ERROR_SEND;
            }
        } else {
            ptr_ack_meta->src_sz += ptr_ack->length;
            arraylist_add(ptr_ack_meta->ack_list, ptr_ack);
        }
    }

    return ULFS_SUCCESS;

}

int insert_ack_meta(rank_ack_task_t *rank_ack_task,
                    ack_meta_t *ptr_ack, int pos, int rank_id, int thrd_id,
                    int src_cli_rank)
{

    if (pos == rank_ack_task->num) {
        rank_ack_task->ack_metas[rank_ack_task->num].ack_list
            = arraylist_create();
        rank_ack_task->ack_metas[rank_ack_task->num].rank_id = rank_id;
        rank_ack_task->ack_metas[rank_ack_task->num].thrd_id = thrd_id;
        rank_ack_task->ack_metas[rank_ack_task->num].src_cli_rank = src_cli_rank;
        if (rank_ack_task->ack_metas[rank_ack_task->num].ack_list == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }

        rank_ack_task->ack_metas[rank_ack_task->num].src_sz
            = ptr_ack->length;
        rank_ack_task->ack_metas[rank_ack_task->num].start_cursor = 0;
        arraylist_add(rank_ack_task->ack_metas[rank_ack_task->num].ack_list,
                      ptr_ack);
        rank_ack_task->num++;
        return ULFS_SUCCESS;
    }

    int i;
    for (i = rank_ack_task->num - 1; i >= pos; i--) {
        rank_ack_task->ack_metas[i + 1] =  rank_ack_task->ack_metas[i];
    }
    rank_ack_task->ack_metas[pos].ack_list
        = arraylist_create();
    rank_ack_task->ack_metas[pos].rank_id = rank_id;
    rank_ack_task->ack_metas[pos].thrd_id = thrd_id;
    rank_ack_task->ack_metas[pos].src_cli_rank = src_cli_rank;

    if (rank_ack_task->ack_metas[pos].ack_list == NULL) {
        return (int)UNIFYCR_ERROR_NOMEM;
    }

    rank_ack_task->ack_metas[pos].src_sz = ptr_ack->length;
    rank_ack_task->ack_metas[pos].start_cursor = 0;
    arraylist_add(rank_ack_task->ack_metas[pos].ack_list,
                  ptr_ack);
    rank_ack_task->num++;
    return ULFS_SUCCESS;

}

int find_ack_meta(rank_ack_task_t *rank_ack_task,
                  int src_rank, int src_thrd)
{
    int left = 0, right = rank_ack_task->num - 1;
    int mid = (left + right) / 2;

    if (rank_ack_task->num == 0) {
        return 0;
    }

    if (rank_ack_task->num == 1) {
        if (compare_rank_thrd(src_rank, src_thrd,
                              rank_ack_task->ack_metas[0].rank_id,
                              rank_ack_task->ack_metas[0].thrd_id) == 0) {
            return 0;
        }
    }

    while (right > left + 1) {
        if (compare_rank_thrd(src_rank, src_thrd,
                              rank_ack_task->ack_metas[mid].rank_id,
                              rank_ack_task->ack_metas[mid].thrd_id) > 0) {
            left = mid;
            mid = (left + right) / 2;
        } else {
            if (compare_rank_thrd(src_rank, src_thrd,
                                  rank_ack_task->ack_metas[mid].rank_id,
                                  rank_ack_task->ack_metas[mid].thrd_id) < 0) {
                right = mid;
                mid = (left + right) / 2;
            } else {
                return mid;
            }
        }
    }

    if (compare_rank_thrd(src_rank, src_thrd,
                          rank_ack_task->ack_metas[left].rank_id,
                          rank_ack_task->ack_metas[left].thrd_id) == 0) {
        return left;
    }

    if (compare_rank_thrd(src_rank, src_thrd,
                          rank_ack_task->ack_metas[right].rank_id,
                          rank_ack_task->ack_metas[right].thrd_id) == 0) {
        return right;
    }

    if (right == rank_ack_task->num - 1) {
        if (compare_rank_thrd(src_rank, src_thrd,
                              rank_ack_task->ack_metas[right].rank_id,
                              rank_ack_task->ack_metas[right].thrd_id) > 0) {
            return rank_ack_task->num;
        }
    }

    if (left == 0) {
        if (compare_rank_thrd(src_rank, src_thrd,
                              rank_ack_task->ack_metas[left].rank_id,
                              rank_ack_task->ack_metas[left].thrd_id) < 0) {
            return 0;
        }
    }

    return right;

}

int compare_rank_thrd(int src_rank, int src_thrd,
                      int cmp_rank, int cmp_thrd)
{
    if (src_rank > cmp_rank) {
        return 1;
    }
    if (src_rank < cmp_rank) {
        return -1;
    }
    if (src_thrd > cmp_thrd) {
        return 1;
    }
    if (src_thrd < cmp_thrd) {
        return -1;
    }
    return 0;

}

void reset_read_tasks(task_set_t *read_task_set,
                      service_msgs_t *service_msgs, int index)
{

    read_task_set->read_tasks[read_task_set->num].start_idx = index;
    read_task_set->read_tasks[read_task_set->num].size =
        service_msgs->msg[index].length;
    read_task_set->read_tasks[read_task_set->num].end_idx = index;
    read_task_set->read_tasks[read_task_set->num].app_id =
        service_msgs->msg[index].dest_app_id;
    read_task_set->read_tasks[read_task_set->num].cli_id =
        service_msgs->msg[index].dest_client_id;
    read_task_set->read_tasks[read_task_set->num].arrival_time =
        service_msgs->msg[index].arrival_time;

}

/**
* decode the message received from request_manager
* @param receive buffer that contains the requests
* @return success/error code
*/
int sm_decode_msg(char *recv_msg_buf)
{

    int num_of_msg = *((int *)recv_msg_buf + 1);
    send_msg_t *ptr_msg = (send_msg_t *)(recv_msg_buf
                                         + 2 * sizeof(int));

    if (num_of_msg + service_msgs.num >= service_msgs.cap) {
        service_msgs.msg = (send_msg_t *)realloc(service_msgs.msg,
                           2 * (num_of_msg + service_msgs.num) * sizeof(send_msg_t));
        if (service_msgs.msg == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }

        service_msgs.cap = 2 * (num_of_msg + service_msgs.num);

        read_task_set.read_tasks = (read_task_t *)realloc(read_task_set.read_tasks,
                                   2 * (num_of_msg + service_msgs.num) * sizeof(read_task_t));
        if (read_task_set.read_tasks == NULL) {
            return (int)UNIFYCR_ERROR_NOMEM;
        }

        read_task_set.cap = 2 * (num_of_msg + service_msgs.num);
    }

    int iter = 0;
    while (iter != num_of_msg) {
        service_msgs.msg[service_msgs.num] = ptr_msg[iter];
        service_msgs.msg[service_msgs.num].arrival_time
            = (int)time(NULL);
        service_msgs.num++;
        iter++;
    }
    return ULFS_SUCCESS;
}

int sm_exit()
{
    int rc = ULFS_SUCCESS;
    int i;

    arraylist_free(pended_reads);
    arraylist_free(pended_sends);
    arraylist_free(send_buf_lst);

    free(service_msgs.msg);
    free(read_task_set.read_tasks);

    for (i = 0; i < rank_ack_task.num; i++) {
        arraylist_free(rank_ack_task.ack_metas[i].ack_list);
    }

    close(sm_sockfd);
    return rc;
}

int sm_init_socket()
{
    int rc = -1;
    int len;
    int result;
    struct sockaddr_un serv_addr;
    char tmp_path[UNIFYCR_MAX_FILENAME] = {0};

    sm_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sm_sockfd  < 0) {
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;

    /*which delegator I belong to*/
    snprintf(tmp_path, sizeof(tmp_path), "%s%d",
             DEF_SOCK_PATH, local_rank_idx);

    strcpy(serv_addr.sun_path, tmp_path);
    len = sizeof(serv_addr);
    result = connect(sm_sockfd,
                     (struct sockaddr *)&serv_addr, len);

    /* exit with error if connection is not successful */
    if (result == -1) {
        rc = -1;
        return rc;
    }

    return 0;
}

void print_service_msgs(service_msgs_t *service_msgs)
{
    long i;
    send_msg_t *ptr_send_msg;
    for (i = 0; i < service_msgs->num; i++) {
        ptr_send_msg = &service_msgs->msg[i];
        LOG(LOG_DBG, "service_msg:dbg_rank:%d, dest_app_id:%d, dest_client_id:%d, \
            dest_del_rank:%d, dest_offset:%ld, \
            dest_len:%ld, src_app_id:%d, \
            src_cli_id:%d, src_del_rank:%d, \
            src_thrd:%d, src_offset:%ld\n",
            ptr_send_msg->src_dbg_rank,
            ptr_send_msg->dest_app_id, ptr_send_msg->dest_client_id,
            ptr_send_msg->dest_delegator_rank, ptr_send_msg->dest_offset,
            ptr_send_msg->length, ptr_send_msg->src_app_id,
            ptr_send_msg->src_cli_id, ptr_send_msg->src_delegator_rank,
            ptr_send_msg->src_thrd, ptr_send_msg->src_offset);

    }

}


int compare_send_msg(const void *a, const void *b)
{
    const send_msg_t *ptr_a = a;
    const send_msg_t *ptr_b = b;

    if (ptr_a->dest_app_id - ptr_b->dest_app_id > 0)
        return 1;

    if (ptr_a->dest_app_id - ptr_b->dest_app_id < 0)
        return -1;

    if (ptr_a->dest_client_id - ptr_b->dest_client_id > 0)
        return 1;

    if (ptr_a->dest_client_id - ptr_b->dest_client_id < 0)
        return -1;

    if (ptr_a->dest_offset - ptr_b->dest_offset > 0)
        return 1;

    if (ptr_a->dest_offset - ptr_b->dest_offset < 0)
        return -1;

    return 0;
}

int compare_read_task(const void *a, const void *b)
{
    const read_task_t *ptr_a = a;
    const read_task_t *ptr_b = b;

    if (ptr_a->size > ptr_b->size)
        return 1;

    if (ptr_a->size < ptr_b->size)
        return -1;

    if (ptr_a->arrival_time > ptr_b->arrival_time)
        return 1;

    if (ptr_a->arrival_time < ptr_b->arrival_time)
        return -1;

    return 0;
}

void print_task_set(task_set_t *read_task_set,
                    service_msgs_t *service_msgs)
{
    int i;

    read_task_t *ptr_read_task;
    for (i = 0; i < read_task_set->num; i++) {
        ptr_read_task = &read_task_set->read_tasks[i];
        LOG(LOG_DBG, "task_set[%d]'s start_idx is %d, end_idx \
            is %d, arrival time is %d, app_id:%d, cli_id:%d\n",
            i, ptr_read_task->start_idx, ptr_read_task->end_idx,
            ptr_read_task->arrival_time, ptr_read_task->app_id,
            ptr_read_task->cli_id);
    }
}

void print_pended_sends(arraylist_t *pended_sends)
{
    ack_stat_t *all_stats =
        (ack_stat_t *)malloc(sizeof(ack_stat_t)
                             * pended_sends->size);

    ack_stat_t *ptr_stat;
    long i;
    for (i = 0; i < pended_sends->size; i++) {
        ptr_stat = arraylist_get(pended_sends, i);
        all_stats[i].src_rank = ptr_stat->src_rank;
        all_stats[i].src_thrd = ptr_stat->src_thrd;
    }

    qsort(all_stats, pended_sends->size,
          sizeof(ack_stat_t), compare_ack_stat);

    for (i = 0; i < pended_sends->size; i++) {
        LOG(LOG_DBG, "src_rank:%d, src_thrd:%d\n",
            all_stats[i].src_rank, all_stats[i].src_thrd);
    }

    free(all_stats);

}


void print_pended_reads(arraylist_t *pended_reads)
{
    pended_read_t *ptr_pended_read;
    long num = arraylist_size(pended_reads);
    long i;

    int start_pos, end_pos, index;
    for (i = 0; i < num; i++) {
        ptr_pended_read =
            (pended_read_t *)arraylist_get(pended_reads, i);
        start_pos = ptr_pended_read->start_pos;
        end_pos = ptr_pended_read->end_pos;
        index = ptr_pended_read->index;
        LOG(LOG_DBG, "pended_read:start_pos:%d, end_pos:%d, index:%d\n",
            start_pos, end_pos, index);
    }
}

void print_ack_meta(rank_ack_task_t *rank_ack_tasks)
{
    long num = rank_ack_tasks->num;
    long i;

    /* printf("printing ack_meta, num is %d########################\n",
            rank_ack_tasks->num); */
    ack_meta_t *ptr_meta;
    for (i = 0; i < num; i++) {
        long j;
        for (j = 0; j < arraylist_size(rank_ack_tasks->ack_metas[i].ack_list);
             j++) {
            if (rank_ack_tasks->ack_metas[i].src_cli_rank == 3) {
                ptr_meta = arraylist_get(rank_ack_tasks->ack_metas[i].ack_list, j);
                LOG(LOG_DBG, "ack_meta:dbg_rank:%d, fid:%ld, offset:%ld, length:%ld\n",
                    rank_ack_tasks->ack_metas[i].src_cli_rank,
                    ptr_meta->src_fid, ptr_meta->src_offset,
                    ptr_meta->length);
            }
        }

    }
}


