/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2018, UT-Battelle, LLC.
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

#include <assert.h>
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

//extern struct pollfd poll_set[MAX_NUM_CLIENTS];

struct timespec shm_wait_tm;

/**
* send the read requests to the remote delegators
* @param sock_id: which socket in the poll_set received
* the application's requests
* @param req_cnt: number of read requests
* @return success/error code
*/
int rm_read_remote_data(int sock_id, size_t req_cnt)
{
    int rc;
    cli_req_t *client_request;
    unifycr_key_t *unifycr_keys;
    unifycr_keyval_t *keyvals;
    int *unifycr_key_lens;
    int num_vals;
    int i;

    int app_id = invert_sock_ids[sock_id];
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    int client_id = app_config->client_ranks[sock_id];
    int dbg_rank = app_config->dbg_ranks[sock_id];

    int thrd_id = app_config->thrd_idxs[sock_id];
    thrd_ctrl_t *thrd_ctrl = (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    client_request = (cli_req_t *)(app_config->shm_req_bufs[client_id]);

    // allocate key storage
    // TODO: might want to get this from a memory pool
    unifycr_keys = calloc(req_num, sizeof(unifycr_key_t));
    if (unifycr_keys == NULL) {
        // this is a fatal error
        // TODO: we need better error handling
        fprintf(stderr, "Error allocating buffer in %s\n", __FILE__);
         exit(-1);
    }
    unifycr_key_lens = calloc(req_num, sizeof(int));
    if (unifycr_key_lens == NULL) {
        // this is a fatal error
        // TODO: we need better error handling
        fprintf(stderr, "Error allocating buffer in %s\n", __FILE__);
         exit(-1);
    }

    /* get keys from client request
     * The loop is creating a tuple of keys for each read request. The tuple
     * defines the start and the end offset of the read request. The
     * implementation of mdhim will return all key-value pairs that fall within
     * the range of this tuple.
     * TODO: make this a function
     * TODO: this is specific to the mdhim in the source tree and not portable
     *       to other KV-stores. This needs to be reviseted to utilize some
     *       other mechanism to retrieve all relevant key-value pairs from the
     *       KV-store.
     */
    for (i = 0; i < req_num; i++) {
        unifycr_keys[2 * i].fid = client_request[i].fid;
        unifycr_keys[2 * i].offset = client_request[i].offset;
        unifycr_key_lens[2 * i] = sizeof(unifycr_key_t);
        unifycr_keys[2 * i + 1].fid = client_request[i].fid;
        unifycr_keys[2 * i + 1].offset =
            client_request[i].offset + client_request[i].length - 1;
        unifycr_key_lens[2 * i + 1] = sizeof(unifycr_key_t);
    }

    rc = unifycr_get_fvals(req_num * 2, unifycr_keys, unifycr_key_lens,
                           &num_vals, &keyvals);

    // set up the thread_control delegator request set
    // TODO: make this a function
    for (i = 0; i < num_vals; i++) {
        send_msg_t meta = thrd_ctrl->del_req_set->msg_meta[i];

        meta.dest_offset = keyvals[i].val.addr;
        meta.dest_delegator_rank = keyvals[i].val.delegator_id;
        meta.length = keyvals[i].val.len;

        memcpy(&meta.dest_app_id, (char *) &(keyvals[i].val.app_rank_id),
               sizeof(int));
        memcpy(&meta.dest_client_id, (char *) &(keyvals[i].val.app_rank_id)
               + sizeof(int), sizeof(int));

        meta.src_app_id = app_id;
        meta.src_cli_id = client_id;

        meta.src_offset = keyvals[i].key.offset;
        meta.src_delegator_rank = glb_rank;
        meta.src_fid = keyvals[i].key.fid;
        meta.src_dbg_rank = dbg_rank;
        meta.src_thrd = thrd_id;
    }

    /*
     * group together the read requests
     * to be sent to the same delegators.
     * */
    qsort(thrd_ctrl->del_req_set->msg_meta,
          thrd_ctrl->del_req_set->num,
          sizeof(send_msg_t), compare_delegators);
//  print_send_msgs(thrd_ctrl->del_req_set->msg_meta,
//  thrd_ctrl->del_req_set->num, dbg_rank);

    /* calculate the number of read requests
     * to be sent to each delegator */

    thrd_ctrl->del_req_stat->req_stat[0].req_cnt = 1;
    thrd_ctrl->del_req_stat->req_stat[0].del_id =
        thrd_ctrl->del_req_set->msg_meta[0].dest_delegator_rank;

    int i, del_ndx = 0;
    for (i = 1; i < thrd_ctrl->del_req_set->num; i++) {
        if (thrd_ctrl->del_req_set->msg_meta[i].dest_delegator_rank
            == thrd_ctrl->del_req_set->msg_meta[i - 1].dest_delegator_rank) {
            thrd_ctrl->del_req_stat->req_stat[del_ndx].req_cnt++;
        } else {
            del_ndx++;
            thrd_ctrl->del_req_stat->req_stat[del_ndx].req_cnt = 1;
            thrd_ctrl->del_req_stat->req_stat[del_ndx].del_id =
                thrd_ctrl->del_req_set->msg_meta[i].dest_delegator_rank;

        }
    }

    thrd_ctrl->del_req_stat->del_cnt = del_ndx + 1;

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
* @param req_cnt: number of read requests
* @param *tot_sz: the total data size to read in these
*  packed read requests
* @return success/error code
*/
int rm_pack_send_requests(char *req_msg_buf,
                          send_msg_t *send_metas,
                          size_t req_cnt,
                          size_t *tot_sz)
{

    /* tot_sz records the aggregate data size
     * requested in this transfer */

    char *ptr = req_msg_buf;
    memset(ptr, 0, REQ_BUF_LEN);

    /*send format: cmd, req_cnt*/
    int cmd = XFER_COMM_DATA;
    memcpy(ptr, &cmd, sizeof(cmd));
    ptr += sizeof(cmd);
    memcpy(ptr, &req_cnt, sizeof(req_cnt));
    ptr += sizeof(req_cnt);

    size_t send_size = 0;
    size_t i;
    for (i = 0; i < req_cnt; i++) {
        send_size += send_metas[i].length;
    }
    *tot_sz += send_size;

    memcpy(ptr, send_metas, (req_cnt * sizeof(send_msg_t)));
    ptr += (req_cnt * sizeof(send_msg_t));

    size_t msg_size = ptr - req_msg_buf;
    assert(msg_size <= REQ_BUF_LEN);
    return (int)msg_size;
}

/**
* send the read requests to the
* remote delegators
* @return success/error code
*/
int rm_send_remote_requests(thrd_ctrl_t *thrd_ctrl,
                            int thrd_tag,
                            size_t *tot_sz)
{

    /* ToDo: Transfer the message in multiple
     * rounds when total size > the size of
     * send_msg_buf
     * */

    *tot_sz = 0;

    int i = 0;
    int msg_cursor = 0;

    while (i < thrd_ctrl->del_req_stat->del_cnt) {
        size_t cnt = thrd_ctrl->del_req_stat->req_stat[i].req_cnt;

        int packed_size = rm_pack_send_requests(thrd_ctrl->del_req_msg_buf,
                                                &(thrd_ctrl->del_req_set->msg_meta[msg_cursor]),
                                                cnt, tot_sz);
        MPI_Send(thrd_ctrl->del_req_msg_buf, packed_size, MPI_CHAR,
                 thrd_ctrl->del_req_stat->req_stat[i].del_id,
                 CLI_DATA_TAG, MPI_COMM_WORLD);
        msg_cursor += cnt;
        i++;
    }

    return UNIFYCR_SUCCESS;
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

        size_t tot_sz;
        rc = rm_send_remote_requests(thrd_ctrl, thrd_id, &tot_sz);
        if (rc != UNIFYCR_SUCCESS) {
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
                              int sock_id,
                              size_t tot_sz)
{

    int rc = UNIFYCR_SUCCESS;

    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);
    int thrd_id = app_config->thrd_idxs[sock_id];
    thrd_ctrl_t *thrd_ctrl =
        (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);


    int irecv_flag[RECV_BUF_CNT];
    MPI_Request recv_req[RECV_BUF_CNT];
    MPI_Status status;

    int i;
    for (i = 0; i < RECV_BUF_CNT; i++) {
        irecv_flag[i] = 0;
        recv_req[i] = 0;
    }

    /*
     * ToDo: something wrong happens and tot_sz keeps larger
     * than 0, handle this exception.
     * */

    shm_wait_tm.tv_sec = 0;
    shm_wait_tm.tv_nsec = SHM_WAIT_INTERVAL;

    while (tot_sz > 0) {
        for (i = 0; i < RECV_BUF_CNT; i++) {
            memset(thrd_ctrl->del_recv_msg_buf[i], 0, RECV_BUF_LEN);
            MPI_Irecv(thrd_ctrl->del_recv_msg_buf[i], RECV_BUF_LEN,
                      MPI_CHAR, MPI_ANY_SOURCE,
                      SER_DATA_TAG + thrd_id,
                      MPI_COMM_WORLD, &recv_req[i]);
        }

        int recv_counter = 0;
        while (tot_sz > 0) {
            for (i = 0; i < RECV_BUF_CNT; i++) {
                if (irecv_flag[i] == 0) {
                    int return_code = MPI_Test(&recv_req[i],
                                               &irecv_flag[i], &status);
                    if (return_code != MPI_SUCCESS) {
                        return (int)UNIFYCR_ERROR_RM_RECV;
                    }

                    if (irecv_flag[i] != 0) {
                        rc = rm_process_received_msg(app_id, sock_id,
                                                 thrd_ctrl->del_recv_msg_buf[i],
                                                     &tot_sz);
                        if (rc != UNIFYCR_SUCCESS) {
                            return rc;
                        }
                        recv_counter++;
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

    /* notify client it's time to consume received data */
    rc = sock_notify_cli(sock_id, COMM_DIGEST);
    if (rc != 0) {
        return rc;
    }

    /* wait for client to consume all received data */
    int client_id = app_config->client_ranks[sock_id];
    shm_hdr_t *recv_hdr = (shm_hdr_t *)(app_config->shm_recv_bufs[client_id]);
    while (recv_hdr->sz != 0) {
        nanosleep(&shm_wait_tm, NULL);
    }

    return UNIFYCR_SUCCESS;
}

/**
*
* parse the received message, and deliver to the client
* @param app_id: client's application id
* @param sock_id: socket index in the poll_set for that client
* @param recv_msg_buf: buffer of packed read requests and data
* @param ptr_tot_sz: total data size to receive
* @return success/error code
*/
int rm_process_received_msg(int app_id, int sock_id,
                            char *recv_msg_buf, size_t *tot_sz)
{

    int rc;
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);
    int client_id = app_config->client_ranks[sock_id];

    /*
     * format of recv_msg_buf:
     *   msg_cnt, data_msg_t, msg_data[] [, data_msg_t, msg_data[], ...]
     */
    int recv_cursor = 0;
    int msg_cnt = *(int *)recv_msg_buf;
    recv_cursor += sizeof(msg_cnt);
    data_msg_t *recv_msg = (data_msg_t *)(recv_msg_buf + recv_cursor);

    char* shm_recv_buf = app_config->shm_recv_bufs[client_id];
    shm_hdr_t *shm_recv_hdr = (shm_hdr_t *) shm_recv_buf;

    char* shm_msg_base = shm_recv_buf + sizeof(shm_hdr_t);
    shm_meta_t *shm_msg;

    int j;
    for (j = 0; j < msg_cnt; j++) {
        size_t used_sz = sizeof(shm_hdr_t) + shm_recv_hdr->sz;
        size_t data_sz = sizeof(shm_meta_t) + recv_msg->length;
        assert(data_sz < app_config->recv_buf_sz);
        if ((used_sz + data_sz) > app_config->recv_buf_sz) {
            /* client-side receive buffer is full,
             * wait until the client reads all the data */
            rc = sock_notify_cli(sock_id, COMM_READ);
            if (rc != 0) {
                return rc;
            }
            while (shm_recv_hdr->sz != 0) {
                nanosleep(&shm_wait_tm, NULL);
            } /* wait until client digests the data */
        }

        /* fill the next message in the shared buffer */
        shm_msg = (shm_meta_t *)(shm_msg_base + shm_recv_hdr->sz);
        shm_msg->src_fid = recv_msg->src_fid;
        shm_msg->offset = recv_msg->src_offset;
        shm_msg->length = recv_msg->length;
        memcpy((void *)(shm_msg + 1), (void *)(recv_msg + 1),
               recv_msg->length);

        *tot_sz -= recv_msg->length;
        shm_recv_hdr->sz += data_sz;
        shm_recv_hdr->cnt++;

        /* the message buffer may contain a list of messages */
        recv_cursor += sizeof(data_msg_t) + recv_msg->length;
        recv_msg = (data_msg_t *)(recv_msg_buf + recv_cursor);
    }

    return UNIFYCR_SUCCESS;
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
                     size_t msg_cnt, int dbg_rank)
{
    size_t i;
    for (i = 0; i < msg_cnt; i++) {
        LOG(LOG_DBG,
            "print_send_msgs: rank:%d, msg_cnt:%zu src_offset[%zu]:%zu\n",
            dbg_rank, msg_cnt, i, send_metas[i].src_offset);
    }
}

void print_remote_del_reqs(int app_id, int cli_id, int dbg_rank,
                           del_req_stat_t *del_req_stat)
{
    int i;
    for (i = 0; i < del_req_stat->del_cnt; i++) {
        /* TODO - this is probably missing an array reference based on i */
        LOG(LOG_DBG, "remote: rank:%d, remote_delegator:%d, req_cnt:%d===\n",
            dbg_rank, del_req_stat->req_stat->del_id,
            del_req_stat->req_stat->req_cnt);
        fflush(stdout);
    }
}

void print_recv_msg(int app_id, int cli_id, int dbg_rank,
                    int thrd_id, shm_meta_t *msg)
{
    LOG(LOG_DBG, "recv_msg: rank:%d, app_id:%d, client:%d, thrd:%d, "
        "shm_meta(fid=%d, offset=%zu, len=%zu)\n",
        dbg_rank, app_id, cli_id, thrd_id,
        msg->src_fid, msg->offset, msg->length);
}
