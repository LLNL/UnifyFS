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
#include <stdint.h>
#include "unifycr_log.h"
#include "unifycr_request_manager.h"
#include "unifycr_const.h"
#include "unifycr_global.h"
#include "unifycr_metadata.h"
#include "unifycr_sock.h"

/* One request manager thread is created for each client that a
 * delegator serves.  The main thread of the delegator assigns
 * work to the request manager thread to retrieve data and send
 * it back to the client.
 *
 * To start, given a read request from the client (via rpc)
 * the handler function on the main delegator first queries the
 * key/value store using the given file id and byte range to obtain
 * the meta data on the physical location of the file data.  This
 * meta data provides the host delegator rank, the app/client
 * ids that specify the log file on the remote delegator, the
 * offset within the log file and the length of data.  The rpc
 * handler function sorts the meta data by host delegator rank,
 * generates read requests, and inserts those into a list on a
 * data structure shared with the request manager (del_req_set).
 *
 * The request manager thread coordinates with the main thread
 * using a lock and a condition variable to protect the shared data
 * structure and impose flow control.  When assigned work, the
 * request manager thread packs and sends request messages to
 * service manager threads on remote delegators via MPI send.
 * It waits for data to be sent back, and unpacks the read replies
 * in each message into a shared memory buffer for the client.
 * When the shared memory is full or all data has been received,
 * it signals the client process to process the read replies.
 * It iterates with the client until all incoming read replies
 * have been transferred. */

static void print_send_msgs(send_msg_t* send_metas,
                            int msg_cnt)
{
    int i;
    for (i = 0; i < msg_cnt; i++) {
        LOGDBG("src_offset:%zu, byte_cnt:%zu",
               send_metas[i].src_offset,
               send_metas[i].length);
    }
}

static void print_remote_del_reqs(int app_id, int cli_id,
                                  del_req_stat_t* del_req_stat)
{
    int i;
    for (i = 0; i < del_req_stat->del_cnt; i++) {
        LOGDBG("remote_delegator:%d, req_cnt:%d",
               del_req_stat->req_stat[i].del_id,
               del_req_stat->req_stat[i].req_cnt);
    }
}

#if 0 // NOT CURRENTLY USED
static void print_recv_msg(int app_id, int cli_id,
                           int thrd_id,
                           shm_meta_t* msg)
{
    LOGDBG("recv_msg: app_id:%d, cli_id:%d, thrd_id:%d, "
           "fid:%d, offset:%ld, len:%ld",
           app_id, cli_id, thrd_id, msg->src_fid,
           msg->offset, msg->length);
}
#endif

/* order read requests by destination delegator rank */
static int compare_delegators(const void* a, const void* b)
{
    const send_msg_t* msg_a = a;
    const send_msg_t* msg_b = b;
    int rank_a = msg_a->dest_delegator_rank;
    int rank_b = msg_b->dest_delegator_rank;

    if (rank_a == rank_b) {
        return 0;
    } else if (rank_a < rank_b) {
        return -1;
    } else {
        return 1;
    }
}

/************************
 * These functions are called by the rpc handler to assign work
 * to the request manager thread
 ***********************/

/* given an app_id, client_id and global file id,
 * compute and return file size for specified file
 */
int rm_cmd_filesize(
    int app_id,    /* app_id for requesting client */
    int client_id, /* client_id for requesting client */
    int gfid,      /* global file id of read request */
    size_t* outsize) /* output file size */
{
    /* get pointer to app structure for this app id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* get thread id for this client */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* look up thread control structure */
    thrd_ctrl_t* thrd_ctrl =
        (thrd_ctrl_t*)arraylist_get(thrd_list, thrd_id);

    /* wait for lock for shared data structures holding requests
     * and condition variable */
    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* set offset and length to request *all* key/value pairs
     * for this file */
    size_t offset = 0;

    /* want to pick the highest integer offset value a file
     * could have here */
    // TODO: would like to unsed max for unsigned long, but
    // that fails to return any keys for some reason
    size_t length = (SIZE_MAX >> 1) - 1;

    /* get the locations of all the read requests from the
     * key-value store*/
    int rc = meta_read_get(app_id, client_id, thrd_id, 0,
                           gfid, offset, length, thrd_ctrl->del_req_set);

    /* compute our file size by iterating over each file
     * segment and taking the max logical offset */
    int i;
    size_t filesize = 0;
    for (i = 0; i < thrd_ctrl->del_req_set->num; i++) {
        /* get pointer to next send_msg structure */
        send_msg_t* msg = &(thrd_ctrl->del_req_set->msg_meta[i]);

        /* get last byte offset for this segment of the file */
        size_t last_offset = msg->src_offset + msg->length;

        /* update our filesize if this offset is bigger than the current max */
        if (last_offset > filesize) {
            filesize = last_offset;
        }
    }

    /* done updating shared variables, release the lock */
    pthread_mutex_unlock(&thrd_ctrl->thrd_lock);

    *outsize = filesize;
    return rc;
}

/* read function for one requested extent,
 * called from rpc handler to fill shared data structures
 * with read requests to be handled by the delegator thread
 * returns before requests are handled
 */
int rm_cmd_read(
    int app_id,    /* app_id for requesting client */
    int client_id, /* client_id for requesting client */
    int gfid,      /* global file id of read request */
    size_t offset, /* logical file offset of read request */
    size_t length) /* number of bytes to read */
{
    /* get pointer to app structure for this app id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* get thread id for this client */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* client app rank for debugging */
    int cli_rank = app_config->dbg_ranks[client_id];

    /* look up thread control structure */
    thrd_ctrl_t* thrd_ctrl =
        (thrd_ctrl_t*)arraylist_get(thrd_list, thrd_id);

    /* wait for lock for shared data structures holding requests
     * and condition variable */
    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* get the locations of all the read requests from the
     * key-value store*/
    int rc = meta_read_get(app_id, client_id, thrd_id, cli_rank,
                           gfid, offset, length, thrd_ctrl->del_req_set);

    /* sort read requests to be sent to the same delegators. */
    qsort(thrd_ctrl->del_req_set->msg_meta,
          thrd_ctrl->del_req_set->num,
          sizeof(send_msg_t), compare_delegators);

    /* debug print */
    print_send_msgs(thrd_ctrl->del_req_set->msg_meta,
                    thrd_ctrl->del_req_set->num);

    /* get pointer to list of delegator stat objects to record
     * delegator rank and count of requests for each delegator */
    per_del_stat_t* req_stat = thrd_ctrl->del_req_stat->req_stat;

    /* get pointer to send message structures, one for each request */
    send_msg_t* msg_meta = thrd_ctrl->del_req_set->msg_meta;

    /* record rank of first delegator we'll send to */
    req_stat[0].del_id = msg_meta[0].dest_delegator_rank;

    /* initialize request count for first delegator to 1 */
    req_stat[0].req_cnt = 1;

    /* iterate over read requests and count number of requests
     * to be sent to each delegator */
    int del_cnt = 0;
    int i;
    for (i = 1; i < thrd_ctrl->del_req_set->num; i++) {
        int cur_rank = msg_meta[i].dest_delegator_rank;
        int prev_rank = msg_meta[i-1].dest_delegator_rank;
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

    /* record total number of delegators we'll send requests to */
    thrd_ctrl->del_req_stat->del_cnt = del_cnt;

    /* debug print */
    print_remote_del_reqs(app_id, thrd_id, thrd_ctrl->del_req_stat);

    /* wake up the request manager thread for the requesting client */
    if (!thrd_ctrl->has_waiting_delegator) {
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
* send the read requests to the remote delegators
*
* @param app_id: application id
* @param client_id: client id for requesting process
* @param gfid: global file id
* @param req_num: number of read requests
* @param reqbuf: read requests buffer
* @return success/error code
*/
int rm_cmd_mread(int app_id, int client_id, int gfid,
                 size_t req_num, void* reqbuf)
{
    int rc;
    int dbg_rank = -1;

    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    int thrd_id = app_config->thrd_idxs[client_id];
    thrd_ctrl_t* thrd_ctrl = (thrd_ctrl_t*)arraylist_get(thrd_list, thrd_id);

    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* get the locations of all the read requests from the key-value store*/
    LOGDBG("calling meta_batch_get with req_num:%zu, thrd_id:%d",
           req_num, thrd_id);
    rc = meta_batch_get(app_id, client_id, thrd_id, 0,
                        reqbuf, req_num,
                        thrd_ctrl->del_req_set);
    LOGDBG("completed meta_batch_get, rc=%d", rc);

    /*
     * group together the read requests
     * to be sent to the same delegators.
     * */
    qsort(thrd_ctrl->del_req_set->msg_meta,
          thrd_ctrl->del_req_set->num,
          sizeof(send_msg_t), compare_delegators);
    print_send_msgs(thrd_ctrl->del_req_set->msg_meta,
                    thrd_ctrl->del_req_set->num);
    thrd_ctrl->del_req_stat->req_stat[0].req_cnt = 1;

    int i, del_cnt = 0;
    thrd_ctrl->del_req_stat->req_stat[0].del_id =
        thrd_ctrl->del_req_set->msg_meta[0].dest_delegator_rank;

    /* calculate the number of read requests
     * to be sent to each delegator*/
    for (i = 1; i < thrd_ctrl->del_req_set->num; i++) {
        if (thrd_ctrl->del_req_set->msg_meta[i].dest_delegator_rank ==
            thrd_ctrl->del_req_set->msg_meta[i - 1].dest_delegator_rank) {
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

    print_remote_del_reqs(app_id, thrd_id, thrd_ctrl->del_req_stat);

    LOGDBG("wake up the service thread for the requesting client");
    /*wake up the service thread for the requesting client*/
    if (!thrd_ctrl->has_waiting_delegator) {
        LOGDBG("has waiting delegator");
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);
        LOGDBG("has waiting dispatcherr");
        thrd_ctrl->has_waiting_dispatcher = 0;
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
        LOGDBG("signaled");
    } else {
        LOGDBG("does not have waiting delegator");
        pthread_cond_signal(&thrd_ctrl->thrd_cond);
    }
    LOGDBG("woked");
    pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
    LOGDBG("unlocked");

    return rc;
}

/* function called by main thread to instruct
 * resource manager thread to exit,
 * returns UNIFYCR_SUCCESS on success */
int rm_cmd_exit(thrd_ctrl_t* thrd_ctrl)
{
    /* grab the lock */
    pthread_mutex_lock(&thrd_ctrl->thrd_lock);

    /* if delegator thread is not waiting in critical
     * section, let's wait on it to come back */
    if (!thrd_ctrl->has_waiting_delegator) {
        /* delegator thread is not in critical section,
         * tell it we've got something and signal it */
        thrd_ctrl->has_waiting_dispatcher = 1;
        pthread_cond_wait(&thrd_ctrl->thrd_cond, &thrd_ctrl->thrd_lock);

        /* we're no longer waiting */
        thrd_ctrl->has_waiting_dispatcher = 0;
    }

    /* inform delegator thread that it's time to exit */
    thrd_ctrl->exit_flag = 1;

    /* free storage holding shared data structures */
    free(thrd_ctrl->del_req_set);
    free(thrd_ctrl->del_req_stat->req_stat);
    free(thrd_ctrl->del_req_stat);

    /* signal delegator thread */
    pthread_cond_signal(&thrd_ctrl->thrd_cond);

    /* release the lock */
    pthread_mutex_unlock(&thrd_ctrl->thrd_lock);

    /* wait for delegator thread to exit */
    void* status;
    pthread_join(thrd_ctrl->thrd, &status);

    return UNIFYCR_SUCCESS;
}

/************************
 * These functions define the logic of the request manager thread
 ***********************/

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
static int rm_pack_send_requests(
    char* req_msg_buf,      /* pointer to buffer to pack requests into */
    send_msg_t* send_metas, /* request objects to be packed */
    int req_cnt,            /* number of requests */
    size_t* tot_sz)         /* total data payload size we're requesting */
{
    /* tot_sz records the aggregate data size
     * requested in this transfer */

    /* send format:
     *   (int) cmd - specifies type of message (XFER_COMM_DATA)
     *   (int) req_num - number of requests in message
     *   {sequence of send_meta_t requests} */

    /* get pointer to start of send buffer */
    char* ptr = req_msg_buf;
    memset(ptr, 0, REQ_BUF_LEN);

    /* pack command */
    int cmd = XFER_COMM_DATA;
    memcpy(ptr, &cmd, sizeof(int));
    ptr += sizeof(int);

    /* pack request count */
    memcpy(ptr, &req_cnt, sizeof(int));
    ptr += sizeof(int);

    /* pack each request into the send buffer,
     * total up incoming bytes as we go */
    int i;
    size_t bytes = 0;
    for (i = 0; i < req_cnt; i++) {
        /* accumulate data size of this request */
        bytes += send_metas[i].length;
    }

    /* copy requests into buffer */
    memcpy(ptr, send_metas, (req_cnt * sizeof(send_msg_t)));
    ptr += (req_cnt * sizeof(send_msg_t));

    /* increment running total size of data bytes */
    (*tot_sz) += bytes;

    /* return number of bytes used to pack requests */
    size_t packed_size = ptr - req_msg_buf;
    return (int)packed_size;
}

/**
* send the read requests to the remote delegator service managers
* @return success/error code
*/
static int rm_send_remote_requests(
    thrd_ctrl_t* thrd_ctrl, /* lists delegators and read requests */
    size_t* tot_sz)         /* returns total data payload to be read */
{
    int i = 0;

    /* ToDo: Transfer the message in multiple
     * rounds when total size > the size of
     * send_msg_buf
     * */

    /* use this variable to total up number of incoming data bytes */
    *tot_sz = 0;

    /* get pointer to send buffer */
    char* sendbuf = thrd_ctrl->del_req_msg_buf;

    /* get pointer to start of read request array,
     * and initialize index to point to first element */
    send_msg_t* msgs = thrd_ctrl->del_req_set->msg_meta;
    int msg_cursor = 0;

    /* iterate over each delegator we need to send requests to */
    for (i = 0; i < thrd_ctrl->del_req_stat->del_cnt; i++) {
        /* pointer to start of requests for this delegator */
        send_msg_t* reqs = msgs + msg_cursor;

        /* number of requests for this delegator */
        int req_num = thrd_ctrl->del_req_stat->req_stat[i].req_cnt;

        /* pack requests into send buffer, get size of packed data,
         * increase total number of data payload we will get back */
        int packed_size = rm_pack_send_requests(sendbuf, reqs,
                                                req_num, tot_sz);

        /* get rank of target delegator */
        int del_rank = thrd_ctrl->del_req_stat->req_stat[i].del_id;

        /* send requests */
        MPI_Send(sendbuf, packed_size, MPI_BYTE,
                 del_rank, CLI_DATA_TAG, MPI_COMM_WORLD);

        /* advance to requests for next delegator */
        msg_cursor += req_num;
    }

    return ULFS_SUCCESS;
}

/* signal the client process for it to start processing read
 * data in shared memory */
static int client_signal(int app_id, int client_id, int flag)
{
    /* signal client on socket */
//    int rc = sock_notify_cli(client_id, COMM_DIGEST);
//    return rc;

    /* lookup our data structure for this app id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* get pointer to flag in shared memory */
    volatile int* ptr_flag =
        (volatile int*)app_config->shm_recv_bufs[client_id];

    /* set flag to 1 to signal client */
    *ptr_flag = flag;

    /* TODO: MEM_FLUSH */

    return UNIFYCR_SUCCESS;
}

/* wait until client has processed all read data in shared memory */
static int client_wait(int app_id, int client_id)
{
    /* specify time to sleep between checking flag in shared
     * memory indicating client has processed data */
    struct timespec shm_wait_tm;
    shm_wait_tm.tv_sec  = 0;
    shm_wait_tm.tv_nsec = SHM_WAIT_INTERVAL;

    /* lookup our data structure for this app id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* get pointer to flag in shared memory */
    volatile int* ptr_flag =
        (volatile int*)app_config->shm_recv_bufs[client_id];

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
* parse the read replies from message received from service manager,
* deliver replies back to client
*
* @param app_id: client's application id
* @param client_id: socket index in the poll_set
* for that client
* @param recv_msg_buf: buffer for received message
*  packed read requests
* @param ptr_tot_sz: total data size to receive
* @return success/error code
*/
static int rm_process_received_msg(
    int app_id,         /* client app id to get shared memory */
    int client_id,      /* client_id to client */
    char* recv_msg_buf, /* pointer to receive buffer */
    size_t* ptr_tot_sz) /* decrements total data received */
{
    /* assume we'll succeed in processing the message */
    int rc = UNIFYCR_SUCCESS;

    /* look up client app config based on client id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* format of read replies in shared memory
     *   (int) flag - used for signal between delegator and client
     *   (int) size - bytes consumed for shared memory read replies
     *   (int) num  - number of read replies
     *   {sequence of shm_meta_t} - read replies */

    /* number of bytes in header (3 ints right now) */
    size_t header_size = 3 * sizeof(int);

    /* get pointer to shared memory buffer for this client */
    char* shmbuf = (char*) app_config->shm_recv_bufs[client_id];

    /* get pointer to flag in shared memory that we'll set
     * to signal to client that data is ready */
    //int* ptr_flag = (int*)shmbuf;
    shmbuf += sizeof(int);

    /* get pointer to slot in shared memory to write bytes
     * consumed by read replies */
    int* ptr_size = (int*)shmbuf;
    shmbuf += sizeof(int);

    /* get pointer to slot in shared memory to write number
     * of read replies */
    int* ptr_num = (int*)shmbuf;
    shmbuf += sizeof(int);

    /* read current size and count from shared memory
     * because they may not be zero? */
    int shm_offset = *ptr_size;
    int shm_count  = *ptr_num;

    /* format of recv_msg_buf:
     *   (int) num - number of read replies packed in message
     *   {sequence of recv_msg_t containing read replies} */

    /* get pointer to start of receive buffer */
    char* msgptr = recv_msg_buf;

    /* extract number of read requests in this message */
    int num = *(int*)msgptr;
    msgptr += sizeof(int);

    /* unpack each read reply */
    int j;
    for (j = 0; j < num; j++) {
        /* point to first read reply in message */
        recv_msg_t* msg = (recv_msg_t*)msgptr;
        msgptr += sizeof(recv_msg_t);

        /* compute max byte that will be consumed by copying
         * data for this message into shared memory buffer */
        size_t msg_size  = sizeof(shm_meta_t) + msg->length;
        size_t need_size = header_size + shm_offset + msg_size;

        /* check that there is space for this message */
        if (need_size > app_config->recv_buf_sz) {
            /* client-side receive buffer is full,
             * inform client to start reading */
            client_signal(app_id, client_id, 1);

            /* wait for client to read data */
            client_wait(app_id, client_id);

            /* TODO: MEM_FETCH */

            /* refresh our packing values now that client
             * has processed entries */
            shm_offset = *ptr_size;
            shm_count  = *ptr_num;
        }

        /* fill the next message in the shared buffer */
        shm_count++;

        /* TODO: we should probably add a field to track errors */

        /* get pointer in shared memory for next read reply */
        shm_meta_t* shmmsg = (shm_meta_t*)(shmbuf + shm_offset);

        /* copy in header for this read request */
        shmmsg->src_fid = msg->src_fid;
        shmmsg->offset  = msg->src_offset;
        shmmsg->length  = msg->length;
        shmmsg->errcode = msg->errcode;
        shm_offset += sizeof(shm_meta_t);

        /* copy data for this read request */
        memcpy(shmbuf + shm_offset, msgptr, msg->length);
        shm_offset += msg->length;

        /* advance to next read reply in message buffer */
        msgptr += msg->length;

        /* decrement number of bytes processed from total */
        *ptr_tot_sz -= msg->length;
    }

    /* record total bytes and number of read requests in buffer */
    *ptr_size = shm_offset;
    *ptr_num  = shm_count;

    return rc;
}

/**
* receive the requested data returned from service managers
* as a result of the read requests we sent to them
*
* @param app_id: app id for incoming data
* @param client_id: client id for incoming data
* @param tot_sz: total data size to receive (excludes header bytes)
* @return success/error code
*/
static int rm_receive_remote_message(
    thrd_ctrl_t* thrd_ctrl, /* contains pointer to receive buffer */
    size_t tot_sz)          /* number of incoming data payload bytes */
{
    /* assume we'll succeed */
    int rc = ULFS_SUCCESS;

    /* get app id and client id that we'll be serving,
     * app id associates thread with a namespace (mountpoint)
     * the client id associates the thread with a particular
     * client process id */
    int app_id    = thrd_ctrl->app_id;
    int client_id = thrd_ctrl->client_id;

    /* lookup our data structure for this app id */
    app_config_t* app_config =
        (app_config_t*)arraylist_get(app_config_list, app_id);

    /* get thread id for this client (used for MPI tags) */
    int thrd_id = app_config->thrd_idxs[client_id];

    /* service manager will incorporate our thread id in tag,
     * to distinguish between target request manager threads */
    int tag = SER_DATA_TAG + thrd_id;

    /* array of MPI_Request objects for window of posted receives */
    MPI_Request recv_req[RECV_BUF_CNT] = {MPI_REQUEST_NULL};

    /* get number of receives to post and size of each buffer */
    int recv_buf_cnt = RECV_BUF_CNT;
    int recv_buf_len = (int) RECV_BUF_LEN;

    /* post a window of receive buffers for incoming data */
    int i;
    for (i = 0; i < recv_buf_cnt; i++) {
        /* post buffer for incoming receive */
        MPI_Irecv(thrd_ctrl->del_recv_msg_buf[i], recv_buf_len, MPI_BYTE,
                  MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, &recv_req[i]);
    }

    /* spin until we have received all incoming data */
    while (tot_sz > 0) {
        /* wait for any receive to come in */
        int index;
        MPI_Status status;
        MPI_Waitany(recv_buf_cnt, recv_req, &index, &status);

        /* got a new message, get pointer to message buffer */
        char* buf = thrd_ctrl->del_recv_msg_buf[index];

        /* unpack the data into client shared memory,
         * this will internally signal client and wait
         * for data to be processed if shared memory
         * buffer is filled */
        int tmp_rc = rm_process_received_msg(
                         app_id, client_id, buf, &tot_sz);
        if (tmp_rc != ULFS_SUCCESS) {
            rc = tmp_rc;
        }

        /* done processing, repost this receive buffer */
        MPI_Irecv(thrd_ctrl->del_recv_msg_buf[index], recv_buf_len, MPI_BYTE,
                  MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, &recv_req[index]);
    }

    /* cancel posted MPI receives */
    for (i = 0; i < recv_buf_cnt; i++) {
        MPI_Status status;
        MPI_Cancel(&recv_req[i]);
        MPI_Wait(&recv_req[i], &status);
    }

    /* signal client that we're now done writing data (flag=2) */
    client_signal(app_id, client_id, 2);

    /* wait for client to read data */
    client_wait(app_id, client_id);

    return rc;
}

/**
* entry point for request manager thread, one thread is created
* for each client process, client informs thread of a set of read
* requests, thread retrieves data for client and notifies client
* when data is ready
*
* delegate the read requests for the delegator thread's client. Each
* delegator thread handles one connection to one client-side rank.
*
* @param arg: pointer to control structure for the delegator thread
*
* @return NULL
*/
void* rm_delegate_request_thread(void* arg)
{
    /* get pointer to our thread control structure */
    thrd_ctrl_t* thrd_ctrl = (thrd_ctrl_t*) arg;

    /* loop forever to handle read requests from the client,
     * new requests are added to a list on a shared data structure
     * with main thread, new items inserted by the rpc handler */
    while (1) {
        /* grab lock */
        pthread_mutex_lock(&thrd_ctrl->thrd_lock);

        /* inform dispatcher that we're waiting for work
         * inside the critical section */
        thrd_ctrl->has_waiting_delegator = 1;

        /* if dispatcher is waiting on us, signal it to go ahead,
         * this coordination ensures that we'll be the next thread
         * to grab the lock after the dispatcher has assigned us
         * some work (rather than the dispatcher grabbing the lock
         * and assigning yet more work) */
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
        size_t tot_sz = 0;

        /* send read requests to remote servers */
        int rc = rm_send_remote_requests(thrd_ctrl, &tot_sz);
        if (rc != UNIFYCR_SUCCESS) {
            /* release lock and exit if we hit an error */
            pthread_mutex_unlock(&thrd_ctrl->thrd_lock);
            return NULL;
        }

        /* wait for data to come back from servers */
        rc = rm_receive_remote_message(thrd_ctrl, tot_sz);
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
