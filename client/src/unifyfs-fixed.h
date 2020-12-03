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

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
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

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file LICENSE.CRUISE
 */

#ifndef UNIFYFS_FIXED_H
#define UNIFYFS_FIXED_H

#include "unifyfs-internal.h"

/* rewrite client's shared memory index of file write extents */
off_t unifyfs_rewrite_index_from_seg_tree(unifyfs_filemeta_t* meta);

/* remove/truncate write extents in client metadata */
int truncate_write_meta(unifyfs_filemeta_t* meta, off_t trunc_sz);

/* sync all writes for target file(s) with the server */
int unifyfs_sync(int target_fid);

/* write data to file using log-based I/O */
int unifyfs_fid_logio_write(
    int fid,                  /* file id to write to */
    unifyfs_filemeta_t* meta, /* meta data for file */
    off_t pos,                /* file position to start writing at */
    const void* buf,          /* user buffer holding data */
    size_t count,             /* number of bytes to write */
    size_t* nwritten          /* returns number of bytes written */
);

#endif /* UNIFYFS_FIXED_H */
