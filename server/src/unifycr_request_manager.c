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
 * Copyright (c) 2017, Florida State University. Contribuions from
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

#include <mpi.h>
#include <poll.h>
#include <time.h>
#include <string.h>
#include "log.h"
#include "unifycr_request_manager.h"
#include "unifycr_const.h"
#include "unifycr_global.h"
#include "unifycr_metadata.h"
#include "unifycr_sock.h"

extern struct pollfd poll_set[MAX_NUM_CLIENTS];

struct timespec shm_wait_tm;

/**
* send the read requests to the
* remote delegators
* @param sock_id: which socket in the poll_set received
* the application's requests
* @param req_num: number of read requests
* @return success/error code
*/
int rm_read_remote_data(int sock_id, int req_num)
{

    int rc;

    int app_id = invert_sock_ids[sock_id];
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    int client_id = app_config->client_ranks[sock_id];
    int dbg_rank = app_config->dbg_ranks[sock_id];

    int thrd_id = app_config->thrd_idxs[sock_id];
    thrd_ctrl_t *thrd_ctrl = (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* get the locations of all the read requests from the key-value store*/
    rc = meta_batch_get(app_id, client_id, thrd_id, dbg_rank,
                        app_config->shm_req_bufs[client_id], req_num,
                        thrd_ctrl->del_req_set);

    /*
     * group together the read requests
     * to be sent to the same delegators.
     * */
    qsort(thrd_ctrl->del_req_set->msg_meta,
          thrd_ctrl->del_req_set->num,
          sizeof(send_msg_t), compare_delegators);
//  print_send_msgs(thrd_ctrl->del_req_set->msg_meta,
//  thrd_ctrl->del_req_set->num, dbg_rank);
    thrd_ctrl->del_req_stat->req_stat[0].req_cnt = 1;



    int i, del_cnt = 0;
    thrd_ctrl->del_req_stat->req_stat[0].del_id =
        thrd_ctrl->del_req_set->msg_meta[0].dest_delegator_rank;

    /* calculate the number of read requests
     * to be sent to each delegator*/
    for (i = 1; i < thrd_ctrl->del_req_set->num; i++) {
        if (thrd_ctrl->del_req_set->msg_meta[i].dest_delegator_rank
            == thrd_ctrl->del_req_set->msg_meta[i - 1].dest_delegator_rank) {
            thrd_ctrl->del_req_stat->req_stat[del_cnt].req_cnt++;
        } else {
            del_cnt++;
            thrd_ctrl->del_req_stat->req_stat[del_cnt].req_cnt = 1;
            thrd_ctrl->del_req_stat->req_stat[del_cnt].del_id =
                thrd_ctrl->del_req_set->msg_meta[i].dest_delegator_rank;

        }
    }
    del_cnt++;

    thrd_ctrl->del_req_stat->del_cnt = del_cnt;

    // print_remote_del_reqs(app_id, client_id, dbg_rank,
    //thrd_ctrl->del_req_stat);

    /*wake up the service thread for the requesting client*/
    if (!thrd_ctrl->has_waiting_delegator) {
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);
        thrd_ctrl->has_waiting_dispatcher = 0;
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    } else {
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }
    pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
    return rc;

}

/**
* pack the the requests to be sent to the same
* delegator.
* ToDo: pack and send multiple rounds if the
* total request sizes is larger than REQ_BUF_LEN
* @param rank: source rank that sends the requests
* @param req_msg_buf: request buffer
* @param req_num: number of read requests
* @param *tot_sz: the total data size to read in these
*  packed read requests
* @return success/error code
*/
int rm_pack_send_requests(char *req_msg_buf,
                          send_msg_t *send_metas, int req_num,
                          long *tot_sz)
{

    /* tot_sz records the aggregate data size
     * requested in this transfer */
    char *ptr = req_msg_buf;

    /*send format: cmd, req_num*/
    int cmd = XFER_COMM_DATA;
    memcpy(ptr, &cmd, sizeof(int));
    memcpy(ptr + sizeof(int), &(req_num),
           sizeof(int));

    long send_size = 0;
    ptr += 2 * sizeof(int);

    int i;
    for (i = 0; i < req_num; i++) {
        send_size += send_metas[i].length;
        memcpy(ptr, &(send_metas[i]), sizeof(send_msg_t));
        ptr += sizeof(send_msg_t);

    }

    /*note: copy tot_size*/
    (*tot_sz) += send_size;

    return (int)(ptr - req_msg_buf);
}

/**
* send the read requests to the
* remote delegators
* @return success/error code
*/
int rm_send_remote_requests(thrd_ctrl_t *thrd_ctrl,
                            int thrd_tag, long *tot_sz)
{

    int i = 0;
    /* ToDo: Transfer the message in multiple
     * rounds when total size > the size of
     * send_msg_buf
     * */

    *tot_sz = 0;

    int msg_cursor = 0;
    while (i < thrd_ctrl->del_req_stat->del_cnt) {
        int packed_size = rm_pack_send_requests(thrd_ctrl->del_req_msg_buf,
                                                &(thrd_ctrl->del_req_set->msg_meta[msg_cursor]),
                                                thrd_ctrl->del_req_stat->req_stat[i].req_cnt,
                                                tot_sz);
        MPI_Send(thrd_ctrl->del_req_msg_buf, packed_size, MPI_CHAR,
                 thrd_ctrl->del_req_stat->req_stat[i].del_id,
                 CLI_DATA_TAG, MPI_COMM_WORLD);
        msg_cursor +=
            thrd_ctrl->del_req_stat->req_stat[i].req_cnt;
        i++;

    }

    return ULFS_SUCCESS;
}

/**
* delegate the read requests
* for the delegator thread's client. Each
* delegator thread handles one connection to
* one client-side rank.
* @param arg: the signature of the delegator thread,
* marked by its client's app_id and the socket id.
* @return NULL
*/
void *rm_delegate_request_thread(void *arg)
{
    cli_signature_t *my_sig = arg;
    int app_id = my_sig->app_id;
    int sock_id = my_sig->sock_id;

    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);
    int thrd_id = app_config->thrd_idxs[sock_id];
    thrd_ctrl_t *thrd_ctrl =
        (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    while (1) {
        int rc;

        pthread_mutex_lock(&thrd_ctrl->thrd_lock);
        thrd_ctrl->has_waiting_delegator = 1;
        if (thrd_ctrl->has_waiting_dispatcher == 1) {
            pthread_cond_signal(&thrd_ctrl->thrd_cond);
        }
        pthread_cond_wait(&thrd_ctrl->thrd_cond,
                          &thrd_ctrl->thrd_lock);
        thrd_ctrl->has_waiting_delegator = 0;
        if (thrd_ctrl->exit_flag == 1) {
            pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
            break;
        }

        long tot_sz = 0;
        rc = rm_send_remote_requests(thrd_ctrl,
                                     thrd_id, &tot_sz);
        if (rc != ULFS_SUCCESS) {
            pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
            return NULL;
        }

        rc = rm_receive_remote_message(app_id, sock_id, tot_sz);
        if (rc != 0) {
            pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
            return NULL;
        }
        pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
    }
    return NULL;
}

/**
*
* receive the requested data returned as a result of
* the delegated read requests
* @param app_id:
* @param sock_id:
* @param tot_sz: the total data size to receive
* @return success/error code
*/
int rm_receive_remote_message(int app_id,
                              int sock_id, long tot_sz)
{

    int rc = ULFS_SUCCESS;

    long dbg_tot_recv = 0;
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);
    int thrd_id = app_config->thrd_idxs[sock_id];
    thrd_ctrl_t *thrd_ctrl =
        (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);


    int irecv_flag[RECV_BUF_CNT] = {0};
    MPI_Request recv_req[RECV_BUF_CNT] = {0};
    MPI_Status status;

    /*
     * ToDo: something wrong happens and tot_sz keeps larger
     * than 0, handle this exception.
     * */

    shm_wait_tm.tv_sec = 0;
    shm_wait_tm.tv_nsec = SHM_WAIT_INTERVAL;

    while (tot_sz > 0) {
        int i, return_code;

        for (i = 0; i < RECV_BUF_CNT; i++) {
            MPI_Irecv(thrd_ctrl->del_recv_msg_buf[i], RECV_BUF_LEN,
                      MPI_CHAR, MPI_ANY_SOURCE,
                      SER_DATA_TAG + thrd_id,
                      MPI_COMM_WORLD, &recv_req[i]);
        }

        int recv_counter = 0;
        while (tot_sz > 0) {
            for (i = 0; i < RECV_BUF_CNT; i++) {
                if (irecv_flag[i] == 0) {
                    return_code = MPI_Test(&recv_req[i],
                                           &irecv_flag[i], &status);
                    if (return_code != MPI_SUCCESS) {
                        return ULFS_ERROR_RM_RECV;
                    }

                    if (irecv_flag[i] != 0) {

                        rc = rm_process_received_msg(app_id,
                                                     sock_id, thrd_ctrl->del_recv_msg_buf[i],
                                                     &tot_sz);
                        if (rc != ULFS_SUCCESS) {
                            return rc;
                        }
                        recv_counter++;
                        dbg_tot_recv++;

                    }
                }
            }

            if (recv_counter == RECV_BUF_CNT) {
                for (i = 0; i < RECV_BUF_CNT; i++) {
                    irecv_flag[i] = 0;
                }
                recv_counter = 0;
                break;
            }

        }

    }

    /*purify shared receive buffer*/
    rc = sock_notify_cli(sock_id, COMM_DIGEST);
    if (rc != 0) {
        return rc;
    }

    int client_id = app_config->client_ranks[sock_id];

    int *ptr_size = (int *)app_config->shm_recv_bufs[client_id];
    while (*ptr_size != 0) {
        nanosleep(&shm_wait_tm, NULL);
    }

    return ULFS_SUCCESS;
}

/**
*
* parse the received message, and deliver to the
* client
* @param app_id: client's application id
* @param sock_id: socket index in the poll_set
* for that client
* @param recv_msg_buf: buffer for received message
*  packed read requests
* @param ptr_tot_sz: total data size to receive
* @return success/error code
*/
int rm_process_received_msg(int app_id, int sock_id,
                            char *recv_msg_buf, long *ptr_tot_sz)
{

    int rc;
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);
    int client_id = app_config->client_ranks[sock_id];

    /*
     * format of recv_msg_buf: num,
     * src_app_id, src_cli_id,
     * src_fid, src_offset, src_length
     */
    int num = *(int *)recv_msg_buf;
    int recv_cursor = 0;

    /*
     * ptr_size and ptr_num point to the
     * size and num information of the
     * client-side shared memory
     * */
    int *ptr_size = NULL;
    int *ptr_num = NULL;

    recv_msg_t *tmp_recv_msg =
        (recv_msg_t *)(recv_msg_buf + sizeof(int));

    shm_meta_t *tmp_sh_msg;
    ptr_size =
        (int *)app_config->shm_recv_bufs[client_id];
    ptr_num =
        (int *)(app_config->shm_recv_bufs[client_id]
                + sizeof(int));

    int j;
    for (j = 0; j < num; j++) {

        if (*ptr_size + tmp_recv_msg->length + sizeof(shm_meta_t)
            + 2 * sizeof(int) > app_config->recv_buf_sz) {
            /*client-side receive buffer is full,
             * wait until the client reads all the
             * data*/
            rc = sock_notify_cli(sock_id, COMM_READ);

            if (rc != 0) {
                return rc;
            }
            while (*ptr_size != 0) {
                nanosleep(&shm_wait_tm, NULL);
            }/*wait until client digest the data*/
        }

        /*fill the next message in the shared buffer*/
        tmp_sh_msg =
            (shm_meta_t *)(((char *)app_config->shm_recv_bufs[client_id]
                            + *ptr_size) + 2 * sizeof(int));

        tmp_sh_msg->src_fid = tmp_recv_msg->src_fid;
        tmp_sh_msg->offset = tmp_recv_msg->src_offset;
        tmp_sh_msg->length = tmp_recv_msg->length;

        app_config_t *app_config =
            (app_config_t *)arraylist_get(app_config_list, app_id);
        int client_id = app_config->client_ranks[sock_id];

        recv_cursor += sizeof(recv_msg_t);
        *ptr_size += sizeof(shm_meta_t);

        memcpy(2 * sizeof(int)
               + app_config->shm_recv_bufs[client_id] + *ptr_size,
               (void *)tmp_recv_msg,
               tmp_recv_msg->length);

        *ptr_tot_sz -= tmp_recv_msg->length;
        recv_cursor += tmp_recv_msg->length;
        *ptr_size = tmp_recv_msg->length + *ptr_size;
        (*ptr_num)++;

        /*the message buffer may contain a list of messages*/
        tmp_recv_msg = (recv_msg_t *)(recv_msg_buf
                                      + sizeof(int) + recv_cursor);

    }

    return ULFS_SUCCESS;

}

int rm_init(int size)
{
    /*
    req_dels_stat.stat =
            (delegator_stat_t *)malloc(sizeof(delegator_stat_t));
    if (!req_dels_stat.stat)
        return ULFS_ERROR_RM_INIT;
        */
    return ULFS_SUCCESS;
}

int compare_delegators(const void *a, const void *b)
{
    const send_msg_t *ptr_a = a;
    const send_msg_t *ptr_b = b;

    if (ptr_a->dest_delegator_rank - ptr_b->dest_delegator_rank > 0)
        return 1;

    if (ptr_a->dest_delegator_rank - ptr_b->dest_delegator_rank < 0)
        return -1;

    return 0;
}

void print_send_msgs(send_msg_t *send_metas,
                     long msg_cnt, int dbg_rank)
{
    long i;
    for (i = 0; i < msg_cnt; i++) {
        LOG(LOG_DBG, "print_send_msgs:dbg_rank:%d, \
            src_offset:%ld, msg_cnt:%ld\n",
            dbg_rank, send_metas[i].src_offset, msg_cnt);
    }
}

void print_remote_del_reqs(int app_id, int cli_id,
                           int dbg_rank, del_req_stat_t *del_req_stat)
{
    int i;
    for (i = 0; i < del_req_stat->del_cnt; i++) {
        LOG(LOG_DBG, "remote:dbg_rank:%d, remote_delegator:%d, req_cnt:%d===\n",
            dbg_rank, del_req_stat->req_stat->del_id,
            del_req_stat->req_stat->req_cnt);
        fflush(stdout);

    }

}

void print_recv_msg(int app_id,
                    int cli_id, int dbg_rank, int thrd_id, shm_meta_t *msg)
{
    LOG(LOG_DBG, "recv_msg:dbg_rank:%d, app_id:%d, cli_id:%d, thrd_id:%d, \
        fid:%d, offset:%ld, len:%ld\n",
        dbg_rank, app_id, cli_id,  thrd_id, msg->src_fid,
        msg->offset, msg->length);
}
