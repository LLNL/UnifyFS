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

/* Create and open a new file in UnifyFS */
unifyfs_rc unifyfs_create(unifyfs_handle fshdl,
                          const int flags,
                          const char* filepath,
                          unifyfs_gfid* gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)
        || (NULL == gfid)) {
        return EINVAL;
    }
    *gfid = UNIFYFS_INVALID_GFID;
    return UNIFYFS_ERROR_NYI;
}

/* Open an existing file in UnifyFS */
unifyfs_rc unifyfs_open(unifyfs_handle fshdl,
                        const int flags,
                        const char* filepath,
                        unifyfs_gfid* gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)
        || (NULL == gfid)) {
        return EINVAL;
    }
    *gfid = UNIFYFS_INVALID_GFID;
    return UNIFYFS_ERROR_NYI;
}

/* Close an open file in UnifyFS */
unifyfs_rc unifyfs_close(unifyfs_handle fshdl,
                         const unifyfs_gfid gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (UNIFYFS_INVALID_GFID == gfid)) {
        return EINVAL;
    }
    return UNIFYFS_ERROR_NYI;
}

/* Synchronize client writes with server */
unifyfs_rc unifyfs_sync(unifyfs_handle fshdl,
                        const unifyfs_gfid gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (UNIFYFS_INVALID_GFID == gfid)) {
        return EINVAL;
    }
    return UNIFYFS_ERROR_NYI;
}

/* Get global file status */
unifyfs_rc unifyfs_stat(unifyfs_handle fshdl,
                        const unifyfs_gfid gfid,
                        unifyfs_status* st)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (UNIFYFS_INVALID_GFID == gfid)
        || (NULL == st)) {
        return EINVAL;
    }
    return UNIFYFS_ERROR_NYI;
}

/* Global lamination - no further writes to file are permitted */
unifyfs_rc unifyfs_laminate(unifyfs_handle fshdl,
                            const char* filepath)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)) {
        return EINVAL;
    }
    return UNIFYFS_ERROR_NYI;
}

/* Remove an existing file from UnifyFS */
unifyfs_rc unifyfs_remove(unifyfs_handle fshdl,
                          const char* filepath)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)) {
        return EINVAL;
    }
    return UNIFYFS_ERROR_NYI;
}
