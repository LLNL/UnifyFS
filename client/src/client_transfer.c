/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "client_transfer.h"


static const char* invalid_str = "INVALID";

static const char* mode_copy_str = "COPY";
static const char* mode_move_str = "MOVE";
static const char* transfer_mode_str(unifyfs_transfer_mode mode)
{
    switch (mode) {
    case UNIFYFS_TRANSFER_MODE_COPY:
        return mode_copy_str;
    case UNIFYFS_TRANSFER_MODE_MOVE:
        return mode_move_str;
    default:
        return invalid_str;
    }
    return NULL;
}

static const char* state_canceled_str = "CANCELED";
static const char* state_completed_str = "COMPLETED";
static const char* state_inprogress_str = "IN-PROGRESS";
static const char* transfer_state_str(unifyfs_req_state state)
{
    switch (state) {
    case UNIFYFS_REQ_STATE_IN_PROGRESS:
        return state_inprogress_str;
    case UNIFYFS_REQ_STATE_CANCELED:
        return state_canceled_str;
    case UNIFYFS_REQ_STATE_COMPLETED:
        return state_completed_str;
    default:
        return invalid_str;
    }
    return NULL;
}

#define debug_print_transfer_req(req) \
do { \
    if (NULL != (req)) { \
        LOGDBG("transfer_req[%p] src=%s, dst=%s, mode=%s, parallel=%d" \
               " - id=%d, state=%s, result=%d, errcode=%d (%s)", \
               (req), (req)->src_path, (req)->dst_path, \
               transfer_mode_str((req)->mode), (req)->use_parallel, \
               (req)->_reqid, transfer_state_str((req)->state), \
               (req)->result.rc, (req)->result.error, \
               unifyfs_rc_enum_description((req)->result.error)); \
    } \
} while (0)

/* compute the arraylist index for the given request id. we use
 * modulo operator to reuse slots in the list */
static inline
unsigned int id_to_list_index(unifyfs_client* client,
                              unsigned int id)
{
    unsigned int capacity = (unsigned int)
        arraylist_capacity(client->active_transfers);
    return id % capacity;
}

/* Create a new transfer status for the given transfer request */
int client_create_transfer(unifyfs_client* client,
                           unifyfs_transfer_request* req,
                           bool src_in_unify)
{
    if ((NULL == client) || (NULL == client->active_transfers)) {
        LOGERR("client->active_transfers is NULL");
        return UNIFYFS_FAILURE;
    }

    if (NULL == req) {
        LOGERR("transfer req is NULL");
        return EINVAL;
    }

    pthread_mutex_lock(&(client->sync));

    int active_count = arraylist_size(client->active_transfers);
    if (active_count == arraylist_capacity(client->active_transfers)) {
        /* already at full capacity for outstanding reads */
        LOGWARN("too many outstanding client transfers");
        pthread_mutex_unlock(&(client->sync));
        return UNIFYFS_FAILURE;
    }

    /* generate an id that doesn't conflict with another active mread */
    unsigned int transfer_id, req_ndx;
    void* existing;
    do {
        transfer_id = client->transfer_id_generator++;
        req_ndx = id_to_list_index(client, transfer_id);
        existing = arraylist_get(client->active_transfers, req_ndx);
    } while (existing != NULL);

    client_transfer_status* transfer = calloc(1, sizeof(*transfer));
    if (NULL == transfer) {
        LOGERR("failed to allocate transfer status struct");
        pthread_mutex_unlock(&(client->sync));
        return ENOMEM;
    }
    transfer->client = client;
    transfer->req = req;
    transfer->complete = 0;
    transfer->src_in_unify = src_in_unify;
    ABT_mutex_create(&(transfer->sync));

    int rc = arraylist_insert(client->active_transfers,
                              (int)req_ndx, (void*)transfer);
    if (rc != 0) {
        ABT_mutex_free(&(transfer->sync));
        free(transfer);
        pthread_mutex_unlock(&(client->sync));
        return rc;
    }

    pthread_mutex_unlock(&(client->sync));

    req->_reqid = transfer_id;
    debug_print_transfer_req(req);

    return UNIFYFS_SUCCESS;
}

/* Remove the transfer status */
client_transfer_status* client_get_transfer(unifyfs_client* client,
                                            unsigned int transfer_id)
{
    if ((NULL == client) || (NULL == client->active_transfers)) {
        LOGERR("client->active_transfers is NULL");
        return NULL;
    }

    pthread_mutex_lock(&(client->sync));

    int list_index = (int) id_to_list_index(client, transfer_id);
    void* list_item = arraylist_get(client->active_transfers, list_index);
    if (list_item == NULL) {
        LOGERR("client->active_transfers index=%d is NULL", list_index);
    }

    pthread_mutex_unlock(&(client->sync));

    client_transfer_status* transfer = list_item;
    return transfer;
}

/* Check if the transfer has completed */
bool client_check_transfer_complete(client_transfer_status* transfer)
{
    if ((NULL == transfer) || (NULL == transfer->req)) {
        LOGERR("transfer is NULL");
        return false;
    }

    unifyfs_transfer_request* req = transfer->req;
    //debug_print_transfer_req(req);

    bool is_complete = false;

    switch (req->state) {
    case UNIFYFS_REQ_STATE_IN_PROGRESS:
        ABT_mutex_lock(transfer->sync);
        is_complete = (transfer->complete == 1);
        ABT_mutex_unlock(transfer->sync);
        break;
    case UNIFYFS_REQ_STATE_CANCELED:
    case UNIFYFS_REQ_STATE_COMPLETED:
        is_complete = true;
        break;
    default:
        break;
    }

    return is_complete;
}

/* Remove the transfer status */
int client_cleanup_transfer(unifyfs_client* client,
                           client_transfer_status* transfer)
{
    if ((NULL == client) || (NULL == client->active_transfers)) {
        LOGERR("client->active_transfers is NULL");
        return UNIFYFS_FAILURE;
    }

    if ((NULL == transfer) || (NULL == transfer->req)) {
        LOGERR("transfer status or request is NULL");
        return EINVAL;
    }

    unifyfs_transfer_request* req = transfer->req;
    debug_print_transfer_req(req);

    if ((req->state == UNIFYFS_REQ_STATE_COMPLETED) &&
        (req->mode == UNIFYFS_TRANSFER_MODE_MOVE)) {
        /* successful copy, now remove source */
        if (transfer->src_in_unify) {
            unifyfs_remove(client, req->src_path);
        } else {
            LOGWARN("Not removing non-UnifyFS source file %s for "
                    "UNIFYFS_TRANSFER_MODE_MOVE", req->src_path);
        }
    }

    int ret = UNIFYFS_SUCCESS;

    pthread_mutex_lock(&(client->sync));

    int list_index = (int) id_to_list_index(client, req->_reqid);
    void* list_item = arraylist_remove(client->active_transfers, list_index);
    if (list_item == (void*)transfer) {
        ABT_mutex_free(&(transfer->sync));
        free(transfer);
    } else {
        LOGERR("mismatch on client->active_transfers index=%d", list_index);
        ret = UNIFYFS_FAILURE;
    }

    pthread_mutex_unlock(&(client->sync));

    return ret;
}

/* Update the transfer status for the client (app_id + client_id)
 * transfer request (transfer_id) using the given error_code, transfer
 * size, and transfer time */
int client_complete_transfer(unifyfs_client* client,
                             int transfer_id,
                             int error_code,
                             size_t transfer_size_bytes,
                             double transfer_time_seconds)
{
    if (NULL == client) {
        LOGERR("NULL client");
        return EINVAL;
    }

    client_transfer_status* transfer = client_get_transfer(client,
                                                           transfer_id);
    if (NULL == transfer) {
        LOGERR("failed to find client transfer with id=%d", transfer_id);
        return EINVAL;
    }

    unifyfs_transfer_request* req = transfer->req;
    if (NULL == req) {
        LOGERR("found transfer status, but request is NULL - internal error");
        return UNIFYFS_FAILURE;
    }

    /* update the request status */
    ABT_mutex_lock(transfer->sync);
    req->result.error = error_code;
    req->result.file_size_bytes = transfer_size_bytes;
    req->result.transfer_time_seconds = transfer_time_seconds;
    req->state = UNIFYFS_REQ_STATE_COMPLETED;
    transfer->complete = 1;
    ABT_mutex_unlock(transfer->sync);

    return UNIFYFS_SUCCESS;
}

int client_submit_transfers(unifyfs_client* client,
                            unifyfs_transfer_request* t_reqs,
                            size_t n_reqs)
{
    int ret = UNIFYFS_SUCCESS;
    int rc;

    for (size_t i = 0; i < n_reqs; i++) {
        unifyfs_transfer_request* req = t_reqs + i;

        /* check for a valid transfer mode */
        switch (req->mode) {
        case UNIFYFS_TRANSFER_MODE_COPY:
        case UNIFYFS_TRANSFER_MODE_MOVE:
            break;
        default:
            req->result.error = EINVAL;
            req->result.rc = UNIFYFS_FAILURE;
            req->result.file_size_bytes = 0;
            req->result.transfer_time_seconds = 0.0;
            req->state = UNIFYFS_REQ_STATE_COMPLETED;
            continue;
        }

        const char* src = req->src_path;
        const char* dst = req->dst_path;
        int parallel = req->use_parallel;

        bool src_in_unify = is_unifyfs_path(client, src);
        bool dst_in_unify = is_unifyfs_path(client, dst);

        /* either src or dst must be within client namespace, but not both */
        if ((!src_in_unify && !dst_in_unify) ||
            (src_in_unify && dst_in_unify)) {
            rc = EINVAL;
        } else {
            req->state = UNIFYFS_REQ_STATE_IN_PROGRESS;
            rc = client_create_transfer(client, req, src_in_unify);
            if (UNIFYFS_SUCCESS == rc) {
                if (src_in_unify) {
                    int gfid = unifyfs_generate_gfid(src);
                    rc = invoke_client_transfer_rpc(client, req->_reqid,
                                                    gfid, parallel, dst);
                } else {
                    /* need to create dest file and copy all source data */
                    rc = UNIFYFS_ERROR_NYI;
                }
            }
        }

        if (rc != UNIFYFS_SUCCESS) {
            req->result.error = rc;
            req->result.rc = UNIFYFS_FAILURE;
            req->result.file_size_bytes = 0;
            req->result.transfer_time_seconds = 0.0;
            req->state = UNIFYFS_REQ_STATE_COMPLETED;
            ret = UNIFYFS_FAILURE;
        }
    }

    return ret;
}
