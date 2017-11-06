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
* This file is part of UnifyCR. For details, see https://github.com/llnl/unifycr
* Please read https://github.com/llnl/unifycr/LICENSE for full license text.
*/

/*
* Copyright (c) 2013, Lawrence Livermore National Security, LLC.
* Produced at the Lawrence Livermore National Laboratory.
* code Written by
*   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
*   Kathryn Mohror <kathryn@llnl.gov>
*   Adam Moody <moody20@llnl.gov>
* All rights reserved.
* This file is part of UNIFYCR.
* For details, see https://github.com/hpc/unifycr
* Please also read this file LICENSE.UNIFYCR 
*/

#ifndef UNIFYCR_WRAP_H
#define UNIFYCR_WRAP_H

#include "unifycr-internal.h"

/* ---------------------------------------
 * POSIX wrappers: file descriptors
 * --------------------------------------- */

UNIFYCR_WRAP_DECL(open, int, (const char *path, int flags, ...));
UNIFYCR_WRAP_DECL(write, ssize_t, (int fd, const void *buf, size_t count));
UNIFYCR_WRAP_DECL(lseek, off_t, (int fd, off_t offset, int whence));

#endif /* UNIFYCR_WRAP_H */
