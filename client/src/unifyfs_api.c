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

/* Initialize client's use of UnifyFS */
// TODO: replace unifyfs_mount()
unifyfs_rc unifyfs_initialize(const char* mountpoint,
                              const unifyfs_options* opts,
                              unifyfs_handle* fshdl)
{
    if (NULL == fshdl) {
        return EINVAL;
    }
    *fshdl = UNIFYFS_INVALID_HANDLE;
    return UNIFYFS_ERROR_NYI;
}

/* Finalize client's use of UnifyFS */
// TODO: replace unifyfs_unmount()
unifyfs_rc unifyfs_finalize(unifyfs_handle fshdl)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }
    return UNIFYFS_ERROR_NYI;
}
