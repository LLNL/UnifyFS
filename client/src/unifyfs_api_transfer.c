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


#include "unifyfs_api_internal.h"
#include "client_transfer.h"

/*
 * Public Methods
 */

/* Dispatch an array of transfer requests */
unifyfs_rc unifyfs_dispatch_transfer(unifyfs_handle fshdl,
                                     const size_t nreqs,
                                     unifyfs_transfer_request* reqs)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }

    if (nreqs == 0) {
        return UNIFYFS_SUCCESS;
    } else if (NULL == reqs) {
        /* non-zero req count, but NULL reqs pointer */
        return EINVAL;
    }

    unifyfs_client* client = fshdl;
    size_t n_reqs = nreqs;
    return client_submit_transfers(client, reqs, n_reqs);
}

/* Cancel an array of transfer requests */
unifyfs_rc unifyfs_cancel_transfer(unifyfs_handle fshdl,
                                   const size_t nreqs,
                                   unifyfs_transfer_request* reqs)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }

    if (0 == nreqs) {
        return UNIFYFS_SUCCESS;
    } else if (NULL == reqs) {
        return EINVAL;
    }

    for (size_t i = 0; i < nreqs; i++) {
        unifyfs_transfer_request* req = reqs + i;
        if (req->state != UNIFYFS_REQ_STATE_COMPLETED) {
            req->state = UNIFYFS_REQ_STATE_CANCELED;

            /* TODO: cancel the transfer */
        }
    }

    /* not actually canceling the transfer yet */
    return UNIFYFS_ERROR_NYI;
}

/* Wait for an array of transfer requests to be completed/canceled */
unifyfs_rc unifyfs_wait_transfer(unifyfs_handle fshdl,
                                 const size_t nreqs,
                                 unifyfs_transfer_request* reqs,
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

    unifyfs_client* client = fshdl;
    unifyfs_transfer_request* req;
    client_transfer_status* transfer;
    size_t i, n_done;
    int max_loop = 6000;
    int loop_cnt = 0;
    do {
        n_done = 0;
        for (i = 0; i < nreqs; i++) {
            req = reqs + i;
            transfer = client_get_transfer(client, req->_reqid);
            if ((NULL != transfer) &&
                client_check_transfer_complete(transfer)) {
                LOGDBG("checked - complete");
                n_done++;
                client_cleanup_transfer(client, transfer);
            } else if ((req->state == UNIFYFS_REQ_STATE_CANCELED) ||
                       (req->state == UNIFYFS_REQ_STATE_COMPLETED)) {
                /* this handles the case where we have already cleaned the
                 * transfer status in a prior loop iteration */
                n_done++;
                LOGDBG("state - complete");
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

        /* TODO: we probably need a timeout mechanism to prevent an infinite
         *       loop when something goes wrong and the transfer status never
         *       gets updated. For now, just using a hardcoded maximum loop
         *       iteration count that roughly equates to 10 min (6000 sec) */
        loop_cnt++;
        usleep(100000); /* sleep 100 ms */
    } while (loop_cnt < max_loop);

    if (loop_cnt == max_loop) {
        return ETIMEDOUT;
    }

    return UNIFYFS_SUCCESS;
}
