/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <unistd.h> // usleep

#include "unifyfs_api_internal.h"
#include "client_read.h"


/*
 * Private Methods
 */

static int process_gfid_writes(unifyfs_client* client,
                               unifyfs_io_request* wr_reqs,
                               size_t n_reqs)
{
    int ret = UNIFYFS_SUCCESS;

    size_t i;
    for (i = 0; i < n_reqs; i++) {
        unifyfs_io_request* req = wr_reqs + i;

        int fid = unifyfs_fid_from_gfid(client, req->gfid);
        if (-1 == fid) {
            req->state = UNIFYFS_REQ_STATE_COMPLETED;
            req->result.error = EINVAL;
            continue;
        }

        if (req->op == UNIFYFS_IOREQ_OP_ZERO) {
            /* TODO: support OP_ZERO in a more efficient manner */
            req->user_buf = calloc(1, req->nbytes);
            if (NULL == req->user_buf) {
                ret = ENOMEM;
                continue;
            }
        }

        /* write user buffer to file */
        int rc = unifyfs_fid_write(client, fid, req->offset, req->user_buf,
                                   req->nbytes, &(req->result.count));
        if (rc != UNIFYFS_SUCCESS) {
            req->result.error = rc;
        }
        req->state = UNIFYFS_REQ_STATE_COMPLETED;

        if (req->op == UNIFYFS_IOREQ_OP_ZERO) {
            /* cleanup allocated OP_ZERO buffer */
            free(req->user_buf);
            req->user_buf = NULL;
        }
    }

    return ret;
}

static int process_gfid_truncates(unifyfs_client* client,
                                  unifyfs_io_request* tr_reqs,
                                  size_t n_reqs)
{
    int ret = UNIFYFS_SUCCESS;

    size_t i;
    for (i = 0; i < n_reqs; i++) {
        unifyfs_io_request* req = tr_reqs + i;

        int fid = unifyfs_fid_from_gfid(client, req->gfid);
        if (-1 == fid) {
            req->state = UNIFYFS_REQ_STATE_COMPLETED;
            req->result.error = EINVAL;
        }

        int rc = unifyfs_fid_truncate(client, fid, req->offset);
        if (rc != UNIFYFS_SUCCESS) {
            req->result.error = rc;
        }
        req->state = UNIFYFS_REQ_STATE_COMPLETED;
    }

    return ret;
}

static int process_gfid_syncs(unifyfs_client* client,
                              unifyfs_io_request* s_reqs,
                              size_t n_reqs)
{
    int ret = UNIFYFS_SUCCESS;
    int rc;
    int data_sync_completed = 0;
    size_t i;
    for (i = 0; i < n_reqs; i++) {
        unifyfs_io_request* req = s_reqs + i;

        if (req->op == UNIFYFS_IOREQ_OP_SYNC_META) {
            int fid = unifyfs_fid_from_gfid(client, req->gfid);
            if (-1 == fid) {
                req->state = UNIFYFS_REQ_STATE_COMPLETED;
                req->result.error = EINVAL;
            }

            rc = unifyfs_fid_sync_extents(client, fid);
            if (rc != UNIFYFS_SUCCESS) {
                req->result.error = rc;
            }
            req->state = UNIFYFS_REQ_STATE_COMPLETED;
        } else if (req->op == UNIFYFS_IOREQ_OP_SYNC_DATA) {
            /* logio_sync covers all files' data - only do it once */
            if (!data_sync_completed) {
                rc = unifyfs_logio_sync(client->state.logio_ctx);
                if (UNIFYFS_SUCCESS != rc) {
                    req->result.error = rc;
                } else {
                    data_sync_completed = 1;
                }
            }
            req->state = UNIFYFS_REQ_STATE_COMPLETED;
        }
    }

    return ret;
}


/*
 * Public Methods
 */

/* Dispatch an array of I/O requests */
unifyfs_rc unifyfs_dispatch_io(unifyfs_handle fshdl,
                               const size_t nreqs,
                               unifyfs_io_request* reqs)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }

    if (0 == nreqs) {
        return UNIFYFS_SUCCESS;
    } else if (NULL == reqs) {
        return EINVAL;
    }

    unifyfs_client* client = fshdl;

    unifyfs_io_request* req;

    /* determine counts of various operations */
    size_t n_read = 0;
    size_t n_write = 0;
    size_t n_trunc = 0;
    size_t n_sync = 0;
    for (size_t i = 0; i < nreqs; i++) {
        req = reqs + i;

        /* set initial request result and state */
        req->state = UNIFYFS_REQ_STATE_INVALID;
        req->result.error = UNIFYFS_SUCCESS;
        req->result.count = 0;
        req->result.rc = 0;

        switch (req->op) {
        case UNIFYFS_IOREQ_NOP:
            break;
        case UNIFYFS_IOREQ_OP_READ:
            n_read++;
            break;
        case UNIFYFS_IOREQ_OP_WRITE:
        case UNIFYFS_IOREQ_OP_ZERO:
            n_write++;
            break;
        case UNIFYFS_IOREQ_OP_SYNC_DATA:
        case UNIFYFS_IOREQ_OP_SYNC_META:
            n_sync++;
            break;
        case UNIFYFS_IOREQ_OP_TRUNC:
            n_trunc++;
            break;
        default:
            LOGERR("invalid ioreq operation");
            req->result.error = EINVAL;
            return EINVAL;
        }
    }

    /* construct per-op requests arrays */
    read_req_t* rd_reqs = NULL;
    if (n_read) {
        rd_reqs = (read_req_t*) calloc(n_read, sizeof(read_req_t));
        if (NULL == rd_reqs) {
            return ENOMEM;
        }
    }
    unifyfs_io_request* wr_reqs = NULL;
    if (n_write) {
        wr_reqs = (unifyfs_io_request*)
            calloc(n_write, sizeof(unifyfs_io_request));
        if (NULL == wr_reqs) {
            return ENOMEM;
        }
    }
    unifyfs_io_request* tr_reqs = NULL;
    if (n_trunc) {
        tr_reqs = (unifyfs_io_request*)
            calloc(n_trunc, sizeof(unifyfs_io_request));
        if (NULL == tr_reqs) {
            return ENOMEM;
        }
    }
    unifyfs_io_request* s_reqs = NULL;
    if (n_sync) {
        s_reqs = (unifyfs_io_request*)
            calloc(n_sync, sizeof(unifyfs_io_request));
        if (NULL == s_reqs) {
            return ENOMEM;
        }
    }

    size_t i;
    size_t rd_ndx = 0;
    size_t wr_ndx = 0;
    size_t tr_ndx = 0;
    size_t s_ndx = 0;
    for (i = 0; i < nreqs; i++) {
        req = reqs + i;
        req->state = UNIFYFS_REQ_STATE_IN_PROGRESS;
        switch (req->op) {
        case UNIFYFS_IOREQ_NOP:
            break;
        case UNIFYFS_IOREQ_OP_READ: {
            read_req_t* rd_req = rd_reqs + rd_ndx++;
            rd_req->gfid    = req->gfid;
            rd_req->offset  = req->offset;
            rd_req->length  = req->nbytes;
            rd_req->nread   = 0;
            rd_req->errcode = 0;
            rd_req->buf     = req->user_buf;
            rd_req->cover_begin_offset = (size_t)-1;
            rd_req->cover_end_offset   = (size_t)-1;
            break;
        }
        case UNIFYFS_IOREQ_OP_WRITE:
        case UNIFYFS_IOREQ_OP_ZERO: {
            unifyfs_io_request* wr_req = wr_reqs + wr_ndx++;
            *wr_req = *req;
            break;
        }
        case UNIFYFS_IOREQ_OP_SYNC_DATA:
        case UNIFYFS_IOREQ_OP_SYNC_META: {
            unifyfs_io_request* s_req = s_reqs + s_ndx++;
            *s_req = *req;
            break;
        }
        case UNIFYFS_IOREQ_OP_TRUNC: {
            unifyfs_io_request* tr_req = tr_reqs + tr_ndx++;
            *tr_req = *req;
            break;
        }
        default:
            break;
        }
    }

    /* process reads */
    int rc = process_gfid_reads(client, rd_reqs, (int)n_read);
    if (rc != UNIFYFS_SUCCESS) {
        /* error encountered while issuing reads */
        for (i = 0; i < n_read; i++) {
            read_req_t* rd_req = rd_reqs + i;
            rd_req->errcode = rc;
        }
    }

    /* process writes */
    rc = process_gfid_writes(client, wr_reqs, n_write);
    if (rc != UNIFYFS_SUCCESS) {
        /* error encountered while issuing writes */
        for (i = 0; i < n_write; i++) {
            unifyfs_io_request* wr_req = wr_reqs + i;
            wr_req->result.error = rc;
        }
    }

    /* process truncates */
    rc = process_gfid_truncates(client, tr_reqs, n_trunc);
    if (rc != UNIFYFS_SUCCESS) {
        /* error encountered while issuing writes */
        for (i = 0; i < n_trunc; i++) {
            unifyfs_io_request* tr_req = tr_reqs + i;
            tr_req->result.error = rc;
        }
    }

    /* process syncs */
    rc = process_gfid_syncs(client, s_reqs, n_sync);
    if (rc != UNIFYFS_SUCCESS) {
        /* error encountered while issuing writes */
        for (i = 0; i < n_sync; i++) {
            unifyfs_io_request* s_req = s_reqs + i;
            s_req->result.error = rc;
        }
    }

    /* update ioreq state */
    rd_ndx = 0;
    wr_ndx = 0;
    tr_ndx = 0;
    s_ndx = 0;
    for (i = 0; i < nreqs; i++) {
        req = reqs + i;
        req->state = UNIFYFS_REQ_STATE_COMPLETED;
        switch (req->op) {
        case UNIFYFS_IOREQ_NOP:
            break;
        case UNIFYFS_IOREQ_OP_READ: {
            read_req_t* rd_req = rd_reqs + rd_ndx++;
            req->result.count = rd_req->nread;
            req->result.error = rd_req->errcode;
            break;
        }
        case UNIFYFS_IOREQ_OP_WRITE:
        case UNIFYFS_IOREQ_OP_ZERO: {
            unifyfs_io_request* wr_req = wr_reqs + wr_ndx++;
            *req = *wr_req;
            break;
        }
        case UNIFYFS_IOREQ_OP_SYNC_DATA:
        case UNIFYFS_IOREQ_OP_SYNC_META: {
            unifyfs_io_request* s_req = s_reqs + s_ndx++;
            *req = *s_req;
            break;
        }
        case UNIFYFS_IOREQ_OP_TRUNC: {
            unifyfs_io_request* tr_req = tr_reqs + tr_ndx++;
            *req = *tr_req;
            break;
        }
        default:
            break;
        }
    }
    if (rd_reqs) free(rd_reqs);
    if (wr_reqs) free (wr_reqs);
    if (tr_reqs) free (tr_reqs);
    if (s_reqs) free(s_reqs);

    return UNIFYFS_SUCCESS;
}

/* Cancel an array of I/O requests */
unifyfs_rc unifyfs_cancel_io(unifyfs_handle fshdl,
                             const size_t nreqs,
                             unifyfs_io_request* reqs)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }

    if (0 == nreqs) {
        return UNIFYFS_SUCCESS;
    } else if (NULL == reqs) {
        return EINVAL;
    }

    return UNIFYFS_ERROR_NYI;
}

/* Wait for an array of I/O requests to be completed/canceled */
unifyfs_rc unifyfs_wait_io(unifyfs_handle fshdl,
                           const size_t nreqs,
                           unifyfs_io_request* reqs,
                           const int waitall)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }

    if (0 == nreqs) {
        return UNIFYFS_SUCCESS;
    } else if (NULL == reqs) {
        return EINVAL;
    }

    size_t i, n_done;
    while (1) {
        n_done = 0;
        for (i = 0; i < nreqs; i++) {
            unifyfs_io_request* req = reqs + i;
            if ((req->state == UNIFYFS_REQ_STATE_CANCELED) ||
                (req->state == UNIFYFS_REQ_STATE_COMPLETED)) {
                n_done++;
            }
        }
        if (waitall) {
            /* for waitall, all reqs must be done to finish */
            if (n_done == nreqs) {
                break;
            }
        } else if (n_done) {
            /* at least one req is done */
            break;
        }
        usleep(1000); /* sleep 1 ms */
    }

    return UNIFYFS_SUCCESS;
}


