/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
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
 * This file is part of UnifyFS.
 * For details, see https://github.com/llnl/unifyfs
 * Please read https://github.com/llnl/unifyfs/LICENSE for full license text.
 */

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of UNIFYFS.
 * For details, see https://github.com/hpc/unifyfs
 * Please also read this file LICENSE.UNIFYFS
 */

#ifndef UNIFYFS_SYSIO_H
#define UNIFYFS_SYSIO_H

#include "unifyfs-internal.h"

#define AIOCB_ERROR_CODE(cbp) (cbp->__error_code)
#define AIOCB_RETURN_VAL(cbp) (cbp->__return_value)

/* ---------------------------------------
 * POSIX wrappers: paths
 * --------------------------------------- */

UNIFYFS_DECL(access, int, (const char* pathname, int mode));
UNIFYFS_DECL(mkdir, int, (const char* path, mode_t mode));
UNIFYFS_DECL(rmdir, int, (const char* path));
UNIFYFS_DECL(unlink, int, (const char* path));
UNIFYFS_DECL(remove, int, (const char* path));
UNIFYFS_DECL(rename, int, (const char* oldpath, const char* newpath));
UNIFYFS_DECL(truncate, int, (const char* path, off_t length));
UNIFYFS_DECL(stat, int, (const char* path, struct stat* buf));
UNIFYFS_DECL(fstat, int, (int fd, struct stat* buf));
UNIFYFS_DECL(__xstat, int, (int vers, const char* path, struct stat* buf));
UNIFYFS_DECL(__lxstat, int, (int vers, const char* path, struct stat* buf));
UNIFYFS_DECL(__fxstat, int, (int vers, int fd, struct stat* buf));

/* ---------------------------------------
 * POSIX wrappers: file descriptors
 * --------------------------------------- */

UNIFYFS_DECL(creat, int, (const char* path, mode_t mode));
UNIFYFS_DECL(creat64, int, (const char* path, mode_t mode));
UNIFYFS_DECL(open, int, (const char* path, int flags, ...));
UNIFYFS_DECL(open64, int, (const char* path, int flags, ...));
UNIFYFS_DECL(__open_2, int, (const char* path, int flags, ...));
UNIFYFS_DECL(read, ssize_t, (int fd, void* buf, size_t count));
UNIFYFS_DECL(write, ssize_t, (int fd, const void* buf, size_t count));
UNIFYFS_DECL(readv, ssize_t, (int fd, const struct iovec* iov, int iovcnt));
UNIFYFS_DECL(writev, ssize_t, (int fd, const struct iovec* iov, int iovcnt));
UNIFYFS_DECL(pread, ssize_t, (int fd, void* buf, size_t count, off_t offset));
UNIFYFS_DECL(pread64, ssize_t, (int fd, void* buf, size_t count,
                                off64_t offset));
UNIFYFS_DECL(pwrite, ssize_t, (int fd, const void* buf, size_t count,
                               off_t offset));
UNIFYFS_DECL(pwrite64, ssize_t, (int fd, const void* buf, size_t count,
                                 off64_t offset));
UNIFYFS_DECL(posix_fadvise, int, (int fd, off_t offset, off_t len, int advice));
UNIFYFS_DECL(lseek, off_t, (int fd, off_t offset, int whence));
UNIFYFS_DECL(lseek64, off64_t, (int fd, off64_t offset, int whence));
UNIFYFS_DECL(ftruncate, int, (int fd, off_t length));
UNIFYFS_DECL(fsync, int, (int fd));
UNIFYFS_DECL(fdatasync, int, (int fd));
UNIFYFS_DECL(flock, int, (int fd, int operation));
UNIFYFS_DECL(mmap, void*, (void* addr, size_t length, int prot, int flags,
                           int fd, off_t offset));
UNIFYFS_DECL(mmap64, void*, (void* addr, size_t length, int prot, int flags,
                             int fd, off64_t offset));
UNIFYFS_DECL(munmap, int, (void* addr, size_t length));
UNIFYFS_DECL(msync, int, (void* addr, size_t length, int flags));
UNIFYFS_DECL(__fxstat, int, (int vers, int fd, struct stat* buf));
UNIFYFS_DECL(close, int, (int fd));
UNIFYFS_DECL(lio_listio, int, (int mode, struct aiocb* const aiocb_list[],
                               int nitems, struct sigevent* sevp));

/*
 * Read 'count' bytes info 'buf' from file starting at offset 'pos'.
 * Returns number of bytes actually read, or -1 on error, in which
 * case errno will be set.
 */
ssize_t unifyfs_fd_read(int fd, off_t pos, void* buf, size_t count);

/* write count bytes from buf into file starting at offset pos,
 * allocates new bytes and updates file size as necessary,
 * fills any gaps with zeros */
int unifyfs_fd_write(int fd, off_t pos, const void* buf, size_t count);
int unifyfs_fd_logreadlist(read_req_t* read_req, int count);

#include "unifyfs-dirops.h"

#endif /* UNIFYFS_SYSIO_H */
