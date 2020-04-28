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

#include "unifyfs_api.h"


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
    return UNIFYFS_ERROR_NYI;
}

/* Cancel an array of I/O requests */
unifyfs_rc unifyfs_cancel_io(unifyfs_handle fshdl,
                             const size_t nreqs,
                             unifyfs_io_request* reqs)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
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
    return UNIFYFS_ERROR_NYI;
}
