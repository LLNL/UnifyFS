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

//extern struct pollfd poll_set[MAX_NUM_CLIENTS];

/* read function for one requested extent,
 * called from rpc handler to fill shared data structures
 * with read requests to be handled by the delegator thread
 * returns before requests are handled
 */
int rm_read_remote_data(int app_id, int client_id, int gfid, long offset, long length)
{
    /* rank to print debug messages */
    int dbg_rank = -1;

    /* get pointer to app structure for this app id */
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    /* get thread id for this client */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* look up thread control structure */
    thrd_ctrl_t *thrd_ctrl = (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    /* lock for shared memory holding requests and condition variable */
    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* get the locations of all the read requests from the key-value store*/
    int rc = meta_read_get(app_id, client_id, thrd_id, 0, gfid, offset, length,
                        thrd_ctrl->del_req_set);

    /* group together the read requests
     * to be sent to the same delegators. */
    qsort(thrd_ctrl->del_req_set->msg_meta,
          thrd_ctrl->del_req_set->num,
          sizeof(send_msg_t), compare_delegators);

    /* debug print */
    print_send_msgs(thrd_ctrl->del_req_set->msg_meta,
                    thrd_ctrl->del_req_set->num, dbg_rank);

    /* get pointer to list of delegator stat objects to record
     * delegator rank and count of requests for each delegator */
    del_req_stat_t *req_stat = thrd_ctrl->del_req_stat->req_stat;

    /* get pointer to send message structures, one for each request */
    send_msg_t *msg_meta = thrd_ctrl->del_req_set->msg_meta;

    /* record rank of first delegator we'll send to */
    req_stat[0].del_id =
        thrd_ctrl->del_req_set->msg_meta[0].dest_delegator_rank;

    /* initialize request count for first delegator to 1 */
    req_stat[0].req_cnt = 1;

    /* iterate over read requests and count number of requests
     * to be sent to each delegator */
    int del_cnt = 0;
    int i;
    for (i = 1; i < thrd_ctrl->del_req_set->num; i++) {
        int cur_rank  = msg_meta[i    ].dest_delegator_rank;
        int prev_rank = msg_meta[i - 1].dest_delegator_rank;
        if (cur_rank == prev_rank) {
            /* another message for the current delegator */
            req_stat[del_cnt].req_cnt++;
        } else {
            /* got a new delegator, set the rank */
            req_stat[del_cnt].del_id = msg_meta[i].dest_delegator_rank;

            /* initialize the request count */
            req_stat[del_cnt].req_cnt = 1;

            /* increment the delegator count */
            del_cnt++;
        }
    }
    del_cnt++;

    /* record number of delegators we'll send to */
    thrd_ctrl->del_req_stat->del_cnt = del_cnt;

    /* debug print */
    print_remote_del_reqs(app_id, thrd_id, dbg_rank,
                          thrd_ctrl->del_req_stat);

    /* wake up the service thread for the requesting client */
    if (! thrd_ctrl->has_waiting_delegator) {
        /* delegator thread is not waiting, but we are in critical
         * section, we just added requests so we must wait for delegator
         * to signal us that it's reached the critical section before
         * we escaple so we don't overwrite these requests before it
         * has had a chance to process them */
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* delegator thread has signaled us that it's now waiting,
         * so signal it to go ahead and then release the lock,
         * so it can start */
        thrd_ctrl->has_waiting_dispatcher = 0;
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    } else {
        /* have a delegator thread waiting on condition variable,
         * signal it to begin processing the requests we just added */
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }

    /* done updating shared variables, release the lock */
    pthread_mutex_unlock(&thrd_ctrl->thrd_lock);

    return rc;
}

/**
* send the read requests to the
* remote delegators
* @param sock_id: which socket in the poll_set received
* the application's requests
* @param req_num: number of read requests
* @return success/error code
*/
int rm_mread_remote_data(int app_id, int client_id, int gfid, int req_num, void* buffer)
{
    int rc;

    //int app_id = invert_sock_ids[sock_id];
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    int thrd_id = app_config->thrd_idxs[client_id];
    thrd_ctrl_t *thrd_ctrl = (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* get the locations of all the read requests from the key-value store*/
	printf("calling meta_batch_get with req_num: %d, thrd_id: %d\n", req_num, thrd_id);
    rc = meta_batch_get(app_id, client_id, thrd_id, 0, buffer, req_num,
                        thrd_ctrl->del_req_set);

	printf("completed meta_batch_get\n");
    /*
     * group together the read requests
     * to be sent to the same delegators.
     * */
    qsort(thrd_ctrl->del_req_set->msg_meta,
          thrd_ctrl->del_req_set->num,
          sizeof(send_msg_t), compare_delegators);
    print_send_msgs(thrd_ctrl->del_req_set->msg_meta,
                    thrd_ctrl->del_req_set->num, dbg_rank);
    thrd_ctrl->del_req_stat->req_stat[0].req_cnt = 1;



    int i, del_cnt = 0;
    thrd_ctrl->del_req_stat->req_stat[0].del_id =
        thrd_ctrl->del_req_set->msg_meta[0].dest_delegator_rank;

	printf("calculate the number of read requests to be sent to each delegator\n");

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

    print_remote_del_reqs(app_id, thrd_id, dbg_rank,
                          thrd_ctrl->del_req_stat);

    printf("wake up the service thread for the requesting client\n");
    /*wake up the service thread for the requesting client*/
    if (!thrd_ctrl->has_waiting_delegator) {
		printf("has waiting delegator\n");
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);
		printf("has waiting dispatcherr\n");
        thrd_ctrl->has_waiting_dispatcher = 0;
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
		printf("signaled\n");
    } else {
		printf("does not have waiting delegator\n");
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }
	printf("woked\n");
    pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
	printf("unlocked\n");
    return rc;

}

#ifdef RPC
int rm_read_remote_data(int app_id, int thrd_id, int gfid, int req_num)
{
    int rc;

    //int app_id = invert_sock_ids[sock_id];
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

  	//int client_id = app_config->client_ranks[sock_id];
    //int dbg_rank = app_config->dbg_ranks[sock_id];
    int dbg_rank = 1;

    int thrd_id = app_config->thrd_idxs[client_id];
    client_ctrl_t *client_ctrl = (client_ctrl_t *)arraylist_get(client_list, thrd_id);

    //pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* get the locations of all the read requests from the key-value store*/
	printf("calling meta_batch_get\n");
    rc = meta_batch_get(app_id, client_id, thrd_id, 0,
                        app_config->shm_req_bufs[client_id], req_num,
                        client_ctrl->del_req_set);

	printf("completed meta_batch_get\n");
    /*
     * group together the read requests
     * to be sent to the same delegators.
     * */
    qsort(client_ctrl->del_req_set->msg_meta,
          client_ctrl->del_req_set->num,
          sizeof(send_msg_t), compare_delegators);
    print_send_msgs(client_ctrl->del_req_set->msg_meta,
                    client_ctrl->del_req_set->num, dbg_rank);
    client_ctrl->del_req_stat->req_stat[0].req_cnt = 1;



    int i, del_cnt = 0;
    client_ctrl->del_req_stat->req_stat[0].del_id =
        client_ctrl->del_req_set->msg_meta[0].dest_delegator_rank;
	printf("calculate the number of read requests to be sent to each delegator\n");

    /* calculate the number of read requests
     * to be sent to each delegator*/
    for (i = 1; i < client_ctrl->del_req_set->num; i++) {
        if (client_ctrl->del_req_set->msg_meta[i].dest_delegator_rank
            == client_ctrl->del_req_set->msg_meta[i - 1].dest_delegator_rank) {
            client_ctrl->del_req_stat->req_stat[del_cnt].req_cnt++;
        } else {
            del_cnt++;
            client_ctrl->del_req_stat->req_stat[del_cnt].req_cnt = 1;
            client_ctrl->del_req_stat->req_stat[del_cnt].del_id =
                client_ctrl->del_req_set->msg_meta[i].dest_delegator_rank;

        }
    }
    del_cnt++;

    client_ctrl->del_req_stat->del_cnt = del_cnt;

    print_remote_del_reqs(app_id, client_id, dbg_rank,
                          client_ctrl->del_req_stat);

    printf("wake up the service thread for the requesting client\n");
    /*wake up the service thread for the requesting client*/
    if (!thrd_ctrl->has_waiting_delegator) {
		printf("has waiting delegator\n");
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);
        thrd_ctrl->has_waiting_dispatcher = 0;
		printf("has waiting dispatcher\n");
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
		printf("signaled\n");
    } else {
		printf("does not have waiting delegator\n");
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }
	printf("woked\n");
    pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
	printf("unlocked\n");
    return rc;

}
#endif

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

    /* send format: cmd, req_num, {sequence of send_meta_t requests} */

    /* get pointer to start of send buffer */
    char *ptr = req_msg_buf;

    /* pack command */
    int cmd = XFER_COMM_DATA;
    memcpy(ptr, &cmd, sizeof(int));
    ptr += sizeof(int);

    /* pack request count */
    memcpy(ptr, &req_num, sizeof(int));
    ptr += sizeof(int);

    /* pack each request into the send buffer,
     * total up incoming bytes as we go */
    int i;
    long bytes = 0;
    for (i = 0; i < req_num; i++) {
        /* copy request into buffer */
        memcpy(ptr, &(send_metas[i]), sizeof(send_msg_t));
        ptr += sizeof(send_msg_t);

        /* accumulate data size of this request */
        bytes += send_metas[i].length;
    }

    /* increment running total size of data bytes */
    (*tot_sz) += bytes;

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

    /* use this variable to total up number of incoming data bytes */
    *tot_sz = 0;

    int msg_cursor = 0;
    for (i = 0; i < thrd_ctrl->del_req_stat->del_cnt; i++) {
        printf("request data transfer for client_id: %d, src_fid: %d, dest_offset %ld, src_offset %ld, length %ld\n", thrd_ctrl->del_req_set->msg_meta[msg_cursor].dest_client_id, thrd_ctrl->del_req_set->msg_meta[msg_cursor].src_fid, thrd_ctrl->del_req_set->msg_meta[msg_cursor].dest_offset, thrd_ctrl->del_req_set->msg_meta[msg_cursor].src_offset, thrd_ctrl->del_req_set->msg_meta[msg_cursor].length);

        /* get pointer to start of send buffer */
        char* sendbuf = thrd_ctrl->del_req_msg_buf;

        /* pointer to start of requests */
        send_msg_t *req = &(thrd_ctrl->del_req_set->msg_meta[msg_cursor]);

        /* number of requests */
        int req_num = thrd_ctrl->del_req_stat->req_stat[i].req_cnt;

        /* pack requests into send buffer, get size of packed data */
        int packed_size = rm_pack_send_requests(sendbuf, req, req_num, tot_sz);

        /* get rank of target delegator */
        int del_rank = thrd_ctrl->del_req_stat->req_stat[i].del_id;

        /* send requests */
        MPI_Send(sendbuf, packed_size, MPI_CHAR,
            del_rank, CLI_DATA_TAG, MPI_COMM_WORLD);

        /* advance to requests for next delegator */
        msg_cursor += req_num;
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
    /* get app id and sock id from argument */
    cli_signature_t *my_sig = arg;
    int app_id  = my_sig->app_id;
    int sock_id = my_sig->sock_id;

    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    int thrd_id = app_config->thrd_idxs[sock_id];
    thrd_ctrl_t *thrd_ctrl =
        (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    while (1) {
        /* grab lock */
        pthread_mutex_lock(&thrd_ctrl->thrd_lock);

        /* inform dispatcher that we're waiting for work
         * inside the critical section */
        thrd_ctrl->has_waiting_delegator = 1;

        /* if dispatcher is waiting on us, signal it to go ahead */
        if (thrd_ctrl->has_waiting_dispatcher == 1) {
            pthread_cond_signal(&thrd_ctrl->thrd_cond);
        }

        /* release lock and wait to be signaled by dispatcher */
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* set flag to indicate we're no longer waiting */
        thrd_ctrl->has_waiting_delegator = 0;

        /* go do work ... */

        /* release lock and bail out if we've been told to exit */
        if (thrd_ctrl->exit_flag == 1) {
            pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
            break;
        }

        /* this will hold the total number of bytes we expect
         * to come in, compute in send, subtracted in receive */
        long tot_sz = 0;

        /* send read requests to remote servers */
        int rc = rm_send_remote_requests(thrd_ctrl, thrd_id, &tot_sz);
        if (rc != UNIFYCR_SUCCESS) {
            /* release lock and exit if we hit an error */
            pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
            return NULL;
        }

        /* wait for data to come back from servers */
        rc = rm_receive_remote_message(app_id, sock_id, tot_sz);
        if (rc != UNIFYCR_SUCCESS) {
            /* release lock and exit if we hit an error */
            pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
            return NULL;
        }

        /* release lock */
        pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
    }

    return NULL;
}

/* signal the client process for it to start processing read
 * data */
static int client_signal(int app_id, int sock_id, int flag)
{
    /* signal client on socket */
//    int rc = sock_notify_cli(sock_id, COMM_DIGEST);
//    return rc;

    /* lookup our data structure for this app id */
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    /* get local rank of client based on socket */
    int client_id = app_config->client_ranks[sock_id];

    /* get pointer to flag in shared memory */
    volatile int *ptr_flag = (volatile int *)app_config->shm_recv_bufs[client_id];

    /* set flag to 1 to signal client */
    *ptr_flag = flag;

    /* TODO: MEM_FLUSH */

    return UNIFYCR_SUCCESS;
}

/* wait until client has processed all read data */
static int client_wait(int app_id, int sock_id)
{
    /* specify time to sleep between checking flag in shared
     * memory indicating client has processed data */
    struct timespec shm_wait_tm;
    shm_wait_tm.tv_sec  = 0;
    shm_wait_tm.tv_nsec = SHM_WAIT_INTERVAL;

    /* lookup our data structure for this app id */
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    /* get local rank of client based on socket */
    int client_id = app_config->client_ranks[sock_id];

    /* get pointer to flag in shared memory */
    volatile int *ptr_flag = (volatile int *)app_config->shm_recv_bufs[client_id];

    /* TODO: MEM_FETCH */

    /* wait for client to set flag to 0 */
    while (*ptr_flag != 0) {
        /* not there yet, sleep for a while */
        nanosleep(&shm_wait_tm, NULL);
        /* TODO: MEM_FETCH */
    }

    return UNIFYCR_SUCCESS;
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
    /* assume we'll succeed */
    int rc = ULFS_SUCCESS;

    /* lookup our data structure for this app id */
    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    /* get thread control structure for this socket */
    int thrd_id = app_config->thrd_idxs[sock_id];
    thrd_ctrl_t *thrd_ctrl =
        (thrd_ctrl_t *)arraylist_get(thrd_list, thrd_id);

    int irecv_flag[RECV_BUF_CNT] = {0};
    MPI_Request recv_req[RECV_BUF_CNT] = {0};

    /*
     * ToDo: something wrong happens and tot_sz keeps larger
     * than 0, handle this exception.
     * */

    while (tot_sz > 0) {
        /* post a receive for each thread? */
        int i;
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
                    /* receive pending, test this flag */
                    MPI_Status status;
                    int mpi_rc = MPI_Test(&recv_req[i],
                                           &irecv_flag[i], &status);
                    if (mpi_rc != MPI_SUCCESS) {
                        return (int)UNIFYCR_ERROR_RM_RECV;
                    }

                    /* check whether it has come in */
                    if (irecv_flag[i] != 0) {
                        /* got a message, get pointer to message buffer */
                        char* buf = thrd_ctrl->del_recv_msg_buf[i];

                        /* unpack the data into client shared memory */
                        int tmp_rc = rm_process_received_msg(
                            app_id, sock_id, buf, &tot_sz);
                        if (tmp_rc != ULFS_SUCCESS) {
                            rc = tmp_rc;
                        }

                        /* update count of received messages */
                        recv_counter++;
                    }
                }
            }

            if (recv_counter == RECV_BUF_CNT) {
                /* all outstanding receives accounted for,
                 * reset flags */
                for (i = 0; i < RECV_BUF_CNT; i++) {
                    irecv_flag[i] = 0;
                }
                break;
            }
        }
    }

    /* signal client that we're now done writing data (flag=2) */
    client_signal(app_id, sock_id, 2);

    /* wait for client to read data */
    client_wait(app_id, sock_id);

    return rc;
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
    /* assume we'll succeed in processing the message */
    int rc = UNIFYCR_SUCCESS;

    app_config_t *app_config =
        (app_config_t *)arraylist_get(app_config_list, app_id);

    int client_id = app_config->client_ranks[sock_id];

    /* get pointer to shared memory buffer for this client */
    char *shmbuf = (char *) app_config->shm_recv_bufs[client_id];

    /* first two ints in shared memory record size consumed
     * and number of messages respectively */

    /* ptr_size and ptr_num point to the
     * size and num information of the
     * client-side shared memory */
    int *ptr_flag = (int *)shmbuf;
    shmbuf += sizeof(int);

    int *ptr_size = (int *)shmbuf;
    shmbuf += sizeof(int);

    int *ptr_num = (int *)shmbuf;
    shmbuf += sizeof(int);

    /* read these from shared memory because they may not be zero? */
    int shm_offset = *ptr_size;
    int shm_count  = *ptr_num;

    /* format of recv_msg_buf:
     * num, {src_app_id, src_cli_id, src_fid, src_offset, src_length} */

    /* get pointer to start of receive buffer */
    char *msgptr = recv_msg_buf;

    /* extract number of read requests in this message */
    int num = *(int *)msgptr;
    msgptr += sizeof(int);

    /* unpack each read reply */
    int j;
    for (j = 0; j < num; j++) {
        /* point to first read reply in message */
        recv_msg_t *msg = (recv_msg_t *)msgptr;
        msgptr += sizeof(recv_msg_t);

        /* compute max byte that will be consumed by copying
         * data for this message into shared memory buffer */
        size_t msg_size  = sizeof(shm_meta_t) + msg->length;
        size_t need_size = 2 * sizeof(int) + shm_offset + msg_size;

        /* check that there is space for this message */
        if (need_size > app_config->recv_buf_sz) {
            /* client-side receive buffer is full,
             * inform client to start reading */
            client_signal(app_id, sock_id, 1);

            /* wait for client to read data */
            client_wait(app_id, sock_id);

            /* TODO: MEM_FETCH */

            /* refresh our packing values now that client
             * has processed entries */
            shm_offset = *ptr_size;
            shm_count  = *ptr_num;
        }

        /* fill the next message in the shared buffer */
        shm_count++;

        /* TODO: we should probably add a field to track errors */

        /* copy in header for this read request */
        shm_meta_t *shmmsg = (shm_meta_t *) (shmbuf + shm_offset);
        shmmsg->src_fid = msg->src_fid;
        shmmsg->offset  = msg->src_offset;
        shmmsg->length  = msg->length;
        shm_offset += sizeof(shm_meta_t);

        /* copy data for this read request */
        memcpy(shmbuf + shm_offset, msgptr, msg->length);
        shm_offset += msg->length;
        msgptr     += msg->length;

        /* decrement number of bytes processed from total */
        *ptr_tot_sz -= msg->length;
    }

    /* record total bytes and number of read requests in buffer */
    *ptr_size = shm_offset;
    *ptr_num  = shm_count;

    return rc;
}

int rm_init(int size)
{
    /*
    req_dels_stat.stat =
            (delegator_stat_t *)malloc(sizeof(delegator_stat_t));
    if (!req_dels_stat.stat)
        return (int)UNIFYCR_ERROR_RM_INIT;
        */
    return ULFS_SUCCESS;
}

int compare_delegators(const void *a, const void *b)
{
    const send_msg_t *ptr_a = a;
    const send_msg_t *ptr_b = b;

    if (ptr_a->dest_delegator_rank > ptr_b->dest_delegator_rank)
        return 1;

    if (ptr_a->dest_delegator_rank < ptr_b->dest_delegator_rank)
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
