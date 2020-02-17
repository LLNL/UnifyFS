/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef __UNIFYFS_RPC_TYPES_H
#define __UNIFYFS_RPC_TYPES_H

#include <time.h>
#include <margo.h>
#include <mercury.h>
#include <mercury_proc_string.h>
#include <mercury_types.h>
#include "unifyfs_meta.h"

/* need to transfer timespec structs */
typedef struct timespec sys_timespec_t;
MERCURY_GEN_STRUCT_PROC(sys_timespec_t,
                        ((uint64_t)(tv_sec))
                        ((uint64_t)(tv_nsec)))

/* encode/decode unifyfs_file_attr_t */
static inline
hg_return_t hg_proc_unifyfs_file_attr_t(hg_proc_t proc, void* _attr)
{
    int i = 0;
    unifyfs_file_attr_t* attr = (unifyfs_file_attr_t *) _attr;
    hg_return_t ret = HG_SUCCESS;

    switch (hg_proc_get_op(proc))
    {
    case HG_ENCODE:
        ret = hg_proc_int32_t(proc, &attr->gfid);
        for (i = 0; i < UNIFYFS_MAX_FILENAME; i++) {
            ret |= hg_proc_int8_t(proc, &(attr->filename[i]));
        }
        ret |= hg_proc_int32_t(proc, &attr->mode);
        ret |= hg_proc_int32_t(proc, &attr->uid);
        ret |= hg_proc_int32_t(proc, &attr->gid);
        ret |= hg_proc_uint64_t(proc, &attr->size);
        ret |= hg_proc_sys_timespec_t(proc, &attr->atime);
        ret |= hg_proc_sys_timespec_t(proc, &attr->mtime);
        ret |= hg_proc_sys_timespec_t(proc, &attr->ctime);
        ret |= hg_proc_uint32_t(proc, &attr->is_laminated);
        if (ret != HG_SUCCESS) {
            ret = HG_PROTOCOL_ERROR;
        }
        break;

    case HG_DECODE:
        ret = hg_proc_int32_t(proc, &attr->gfid);
        for (i = 0; i < UNIFYFS_MAX_FILENAME; i++) {
            ret |= hg_proc_int8_t(proc, &(attr->filename[i]));
        }
        ret |= hg_proc_int32_t(proc, &attr->mode);
        ret |= hg_proc_int32_t(proc, &attr->uid);
        ret |= hg_proc_int32_t(proc, &attr->gid);
        ret |= hg_proc_uint64_t(proc, &attr->size);
        ret |= hg_proc_sys_timespec_t(proc, &attr->atime);
        ret |= hg_proc_sys_timespec_t(proc, &attr->mtime);
        ret |= hg_proc_sys_timespec_t(proc, &attr->ctime);
        ret |= hg_proc_uint32_t(proc, &attr->is_laminated);
        if (ret != HG_SUCCESS) {
            ret = HG_PROTOCOL_ERROR;
        }
        break;

    case HG_FREE:
    default:
        /* nothing */
        break;
    }

    return ret;
}

#endif /* __UNIFYFS_RPC_TYPES_H */
