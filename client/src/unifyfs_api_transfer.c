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

/* this avoids a #include of <unifyfs.h> */
extern int unifyfs_transfer_file(const char* src,
                                 const char* dst,
                                 int parallel);

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
        return EINVAL;
    }

    unifyfs_transfer_request* req;
    for (size_t i = 0; i < nreqs; i++) {
        req = reqs + i;
        req->state = UNIFYFS_IOREQ_STATE_IN_PROGRESS;

        /* check for a valid transfer mode */
        switch (req->mode) {
        case UNIFYFS_TRANSFER_MODE_COPY:
        case UNIFYFS_TRANSFER_MODE_MOVE:
            break;
        default:
            req->result.error = EINVAL;
            req->result.rc = UNIFYFS_FAILURE;
            req->state = UNIFYFS_IOREQ_STATE_COMPLETED;
            continue;
        }

        int rc = unifyfs_transfer_file(req->src_path, req->dst_path,
                                       req->use_parallel);
        if (rc) {
            /* unifyfs_transfer_file() returns a negative error code */
            req->result.error = -rc;
            req->result.rc = UNIFYFS_FAILURE;
        } else {
            req->result.error = 0;
            req->result.rc = UNIFYFS_SUCCESS;

            if (req->mode == UNIFYFS_TRANSFER_MODE_MOVE) {
                /* successful copy, now remove source */
                errno = 0;
                rc = unlink(req->src_path);
                if (rc) {
                    req->result.error = errno;
                    req->result.rc = UNIFYFS_FAILURE;
                }
            }
        }

        req->state = UNIFYFS_IOREQ_STATE_COMPLETED;
    }

    return UNIFYFS_SUCCESS;
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

    size_t i, n_done;
    while (1) {
        n_done = 0;
        for (i = 0; i < nreqs; i++) {
            unifyfs_transfer_request* req = reqs + i;
            if ((req->state == UNIFYFS_IOREQ_STATE_CANCELED) ||
                (req->state == UNIFYFS_IOREQ_STATE_COMPLETED)) {
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
