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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
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

#ifndef CRUISE_SYSIO_H
#define CRUISE_SYSIO_H

#include "cruise-internal.h"

/* read count bytes info buf from file starting at offset pos,
 * returns number of bytes actually read in retcount,
 * retcount will be less than count only if an error occurs
 * or end of file is reached */
int cruise_fd_read(int fd, off_t pos, void* buf, size_t count, size_t* retcount);

/* write count bytes from buf into file starting at offset pos,
 * allocates new bytes and updates file size as necessary,
 * fills any gaps with zeros */
int cruise_fd_write(int fd, off_t pos, const void* buf, size_t count);
int cruise_fd_logreadlist(read_req_t *read_req, int count);
int compare_read_req(const void *a, const void *b);
int compare_index_entry(const void *a, const void *b);

#endif /* CRUISE_SYSIO_H */
