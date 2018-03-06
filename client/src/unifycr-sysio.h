/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
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
 * This file is part of UnifyCR.
 * For details, see https://github.com/llnl/unifycr
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

#ifndef UNIFYCR_SYSIO_H
#define UNIFYCR_SYSIO_H

#include "unifycr-internal.h"
/* ---------------------------------------
 * POSIX wrappers: paths
 * --------------------------------------- */

UNIFYCR_DECL(access, int, (const char *pathname, int mode));
UNIFYCR_DECL(mkdir, int, (const char *path, mode_t mode));
UNIFYCR_DECL(rmdir, int, (const char *path));
UNIFYCR_DECL(unlink, int, (const char *path));
UNIFYCR_DECL(remove, int, (const char *path));
UNIFYCR_DECL(rename, int, (const char *oldpath, const char *newpath));
UNIFYCR_DECL(truncate, int, (const char *path, off_t length));
UNIFYCR_DECL(stat, int, (const char *path, struct stat *buf));
UNIFYCR_DECL(__lxstat, int, (int vers, const char *path, struct stat *buf));
UNIFYCR_DECL(__xstat, int, (int vers, const char *path, struct stat *buf));

/* ---------------------------------------
 * POSIX wrappers: file descriptors
 * --------------------------------------- */

UNIFYCR_DECL(creat, int, (const char *path, mode_t mode));
UNIFYCR_DECL(creat64, int, (const char *path, mode_t mode));
UNIFYCR_DECL(open, int, (const char *path, int flags, ...));
UNIFYCR_DECL(open64, int, (const char *path, int flags, ...));
UNIFYCR_DECL(read, ssize_t, (int fd, void *buf, size_t count));
UNIFYCR_DECL(write, ssize_t, (int fd, const void *buf, size_t count));
UNIFYCR_DECL(readv, ssize_t, (int fd, const struct iovec *iov, int iovcnt));
UNIFYCR_DECL(writev, ssize_t, (int fd, const struct iovec *iov, int iovcnt));
UNIFYCR_DECL(pread, ssize_t, (int fd, void *buf, size_t count, off_t offset));
UNIFYCR_DECL(pread64, ssize_t, (int fd, void *buf, size_t count,
                                off64_t offset));
UNIFYCR_DECL(pwrite, ssize_t, (int fd, const void *buf, size_t count,
                               off_t offset));
UNIFYCR_DECL(pwrite64, ssize_t, (int fd, const void *buf, size_t count,
                                 off64_t offset));
UNIFYCR_DECL(posix_fadvise, int, (int fd, off_t offset, off_t len, int advice));
UNIFYCR_DECL(lseek, off_t, (int fd, off_t offset, int whence));
UNIFYCR_DECL(lseek64, off64_t, (int fd, off64_t offset, int whence));
UNIFYCR_DECL(ftruncate, int, (int fd, off_t length));
UNIFYCR_DECL(fsync, int, (int fd));
UNIFYCR_DECL(fdatasync, int, (int fd));
UNIFYCR_DECL(flock, int, (int fd, int operation));
UNIFYCR_DECL(mmap, void *, (void *addr, size_t length, int prot, int flags,
                            int fd, off_t offset));
UNIFYCR_DECL(mmap64, void *, (void *addr, size_t length, int prot, int flags,
                              int fd, off64_t offset));
UNIFYCR_DECL(munmap, int, (void *addr, size_t length));
UNIFYCR_DECL(msync, int, (void *addr, size_t length, int flags));
UNIFYCR_DECL(__fxstat, int, (int vers, int fd, struct stat *buf));
UNIFYCR_DECL(close, int, (int fd));
/*UNIFYCR_DECL(lio_listio, ssize_t, (int mode,\
   struct aiocb *const aiocb_list[], \
                      int nitems, struct sigevent *sevp));*/

/* read count bytes info buf from file starting at offset pos,
 * returns number of bytes actually read in retcount,
 * retcount will be less than count only if an error occurs
 * or end of file is reached */
int unifycr_fd_read(int fd, off_t pos, void *buf, size_t count,
                    size_t *retcount);

/* write count bytes from buf into file starting at offset pos,
 * allocates new bytes and updates file size as necessary,
 * fills any gaps with zeros */
int unifycr_fd_write(int fd, off_t pos, const void *buf, size_t count);
int unifycr_fd_logreadlist(read_req_t *read_req, int count);
int compare_read_req(const void *a, const void *b);
int compare_index_entry(const void *a, const void *b);

#endif /* UNIFYCR_SYSIO_H */
