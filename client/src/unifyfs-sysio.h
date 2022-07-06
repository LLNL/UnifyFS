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
#include "unifyfs_wrap.h"

#include "unifyfs-dirops.h"

#define AIOCB_ERROR_CODE(cbp) (cbp->__error_code)
#define AIOCB_RETURN_VAL(cbp) (cbp->__return_value)

/* ---------------------------------------
 * POSIX wrappers: paths
 * --------------------------------------- */

/* file and directory operations */
UNIFYFS_DECL(access, int, (const char* pathname, int mode));
UNIFYFS_DECL(chmod, int, (const char* path, mode_t mode));
UNIFYFS_DECL(__lxstat, int, (int vers, const char* path, struct stat* buf));
UNIFYFS_DECL(remove, int, (const char* path));
UNIFYFS_DECL(rename, int, (const char* oldpath, const char* newpath));
UNIFYFS_DECL(stat, int, (const char* path, struct stat* buf));
UNIFYFS_DECL(statfs, int, (const char* path, struct statfs* fsbuf));
UNIFYFS_DECL(__xstat, int, (int vers, const char* path, struct stat* buf));

/* directory operations */
UNIFYFS_DECL(chdir, int, (const char* path));
UNIFYFS_DECL(__getcwd_chk, char*, (char* path, size_t, size_t));
UNIFYFS_DECL(get_current_dir_name, char*, (void));
UNIFYFS_DECL(getcwd, char*, (char* path, size_t));
UNIFYFS_DECL(getwd, char*, (char* path));
UNIFYFS_DECL(mkdir, int, (const char* path, mode_t mode));
UNIFYFS_DECL(rmdir, int, (const char* path));

/* file operations */
UNIFYFS_DECL(creat, int, (const char* path, mode_t mode));
UNIFYFS_DECL(creat64, int, (const char* path, mode_t mode));
UNIFYFS_DECL(__open_2, int, (const char* path, int flags, ...));
UNIFYFS_DECL(open, int, (const char* path, int flags, ...));
UNIFYFS_DECL(open64, int, (const char* path, int flags, ...));
UNIFYFS_DECL(truncate, int, (const char* path, off_t length));
UNIFYFS_DECL(unlink, int, (const char* path));

/* ---------------------------------------
 * POSIX wrappers: file descriptors
 * --------------------------------------- */

/* I/O operations */
UNIFYFS_DECL(fsync, int, (int fd));
UNIFYFS_DECL(fdatasync, int, (int fd));
UNIFYFS_DECL(ftruncate, int, (int fd, off_t length));
UNIFYFS_DECL(lio_listio, int, (int mode, struct aiocb* const aiocb_list[],
                               int nitems, struct sigevent* sevp));
UNIFYFS_DECL(lseek, off_t, (int fd, off_t offset, int whence));
UNIFYFS_DECL(lseek64, off64_t, (int fd, off64_t offset, int whence));
UNIFYFS_DECL(mmap, void*, (void* addr, size_t length, int prot, int flags,
                           int fd, off_t offset));
UNIFYFS_DECL(mmap64, void*, (void* addr, size_t length, int prot, int flags,
                             int fd, off64_t offset));
UNIFYFS_DECL(msync, int, (void* addr, size_t length, int flags));
UNIFYFS_DECL(munmap, int, (void* addr, size_t length));
UNIFYFS_DECL(pread, ssize_t, (int fd, void* buf, size_t count, off_t offset));
UNIFYFS_DECL(pread64, ssize_t, (int fd, void* buf, size_t count,
                                off64_t offset));
UNIFYFS_DECL(pwrite, ssize_t, (int fd, const void* buf, size_t count,
                               off_t offset));
UNIFYFS_DECL(pwrite64, ssize_t, (int fd, const void* buf, size_t count,
                                 off64_t offset));
UNIFYFS_DECL(read, ssize_t, (int fd, void* buf, size_t count));
UNIFYFS_DECL(readv, ssize_t, (int fd, const struct iovec* iov, int iovcnt));
UNIFYFS_DECL(write, ssize_t, (int fd, const void* buf, size_t count));
UNIFYFS_DECL(writev, ssize_t, (int fd, const struct iovec* iov, int iovcnt));

/* inspection/control operations */
UNIFYFS_DECL(close, int, (int fd));
UNIFYFS_DECL(dup, int, (int fd));
UNIFYFS_DECL(dup2, int, (int fd, int desired_fd));
UNIFYFS_DECL(fchdir, int, (int fd));
UNIFYFS_DECL(fchmod, int, (int fd, mode_t mode));
UNIFYFS_DECL(flock, int, (int fd, int operation));
UNIFYFS_DECL(fstat, int, (int fd, struct stat* buf));
UNIFYFS_DECL(fstatfs, int, (int fd, struct statfs* fsbuf));
UNIFYFS_DECL(__fxstat, int, (int vers, int fd, struct stat* buf));
UNIFYFS_DECL(posix_fadvise, int, (int fd, off_t offset, off_t len, int advice));

/*
 * Read 'count' bytes info 'buf' from file starting at offset 'pos'.
 * Returns UNIFYFS_SUCCESS and sets number of bytes actually read in bytes
 * on success.  Otherwise returns error code on error.
 */
int unifyfs_fd_read(
    int fd,       /* file descriptor to read from */
    off_t pos,    /* offset within file to read from */
    void* buf,    /* buffer to hold data */
    size_t count, /* number of bytes to read */
    size_t* nread /* number of bytes read */
);

/*
 * Write 'count' bytes from 'buf' into file starting at offset 'pos'.
 * Returns UNIFYFS_SUCCESS and sets number of bytes actually written in bytes
 * on success.  Otherwise returns error code on error.
 */
int unifyfs_fd_write(
    int fd,          /* file descriptor to write to */
    off_t pos,       /* offset within file to write to */
    const void* buf, /* buffer holding data to write */
    size_t count,    /* number of bytes to write */
    size_t* nwritten /* number of bytes written */
);

#endif /* UNIFYFS_SYSIO_H */
