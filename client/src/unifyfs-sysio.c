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

#include "unifyfs-internal.h"
#include "unifyfs-sysio.h"
#include "margo_client.h"
#include "ucr_read_builder.h"
#include "seg_tree.h"

/* -------------------
 * define external variables
 * --------------------*/

extern int unifyfs_spilloverblock;
extern int unifyfs_use_spillover;

#define MAX(a, b) (a > b ? a : b)

/* ---------------------------------------
 * POSIX wrappers: paths
 * --------------------------------------- */

int UNIFYFS_WRAP(access)(const char* path, int mode)
{
    /* determine whether we should intercept this path */
    if (unifyfs_intercept_path(path)) {
        /* check if path exists */
        if (unifyfs_get_fid_from_path(path) < 0) {
            LOGDBG("access: unifyfs_get_id_from path failed, returning -1, %s",
                   path);
            errno = ENOENT;
            return -1;
        }

        /* currently a no-op */
        LOGDBG("access: path intercepted, returning 0, %s", path);
        return 0;
    } else {
        LOGDBG("access: calling MAP_OR_FAIL, %s", path);
        MAP_OR_FAIL(access);
        int ret = UNIFYFS_REAL(access)(path, mode);
        LOGDBG("access: returning __real_access %d, %s", ret, path);
        return ret;
    }
}

int UNIFYFS_WRAP(mkdir)(const char* path, mode_t mode)
{
    /* Support for directories is very limited at this time
     * mkdir simply puts an entry into the filelist for the
     * requested directory (assuming it does not exist)
     * It doesn't check to see if parent directory exists */

    /* determine whether we should intercept this path */
    if (unifyfs_intercept_path(path)) {
        /* check if it already exists */
        if (unifyfs_get_fid_from_path(path) >= 0) {
            errno = EEXIST;
            return -1;
        }

        /* add directory to file list */
        int ret = unifyfs_fid_create_directory(path);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to create the directory,
             * set errno and return */
            errno = unifyfs_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(mkdir);
        int ret = UNIFYFS_REAL(mkdir)(path, mode);
        return ret;
    }
}

int UNIFYFS_WRAP(rmdir)(const char* path)
{
    /* determine whether we should intercept this path */
    if (unifyfs_intercept_path(path)) {
        /* check if the mount point itself is being deleted */
        if (!strcmp(path, unifyfs_mount_prefix)) {
            errno = EBUSY;
            return -1;
        }

        /* check if path exists */
        int fid = unifyfs_get_fid_from_path(path);
        if (fid < 0) {
            errno = ENOENT;
            return -1;
        }

        /* is it a directory? */
        if (!unifyfs_fid_is_dir(fid)) {
            errno = ENOTDIR;
            return -1;
        }

        /* is it empty? */
        if (!unifyfs_fid_is_dir_empty(path)) {
            errno = ENOTEMPTY;
            return -1;
        }

        /* remove the directory from the file list */
        int ret = unifyfs_fid_unlink(fid);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to remove the directory,
             * set errno and return */
            errno = unifyfs_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(rmdir);
        int ret = UNIFYFS_REAL(rmdir)(path);
        return ret;
    }
}

int UNIFYFS_WRAP(rename)(const char* oldpath, const char* newpath)
{
    /* TODO: allow oldpath / newpath to split across memfs and normal
     * linux fs, which means we'll need to do a read / write */

    /* check whether the old path is in our file system */
    if (unifyfs_intercept_path(oldpath)) {
        /* for now, we can only rename within our file system */
        if (!unifyfs_intercept_path(newpath)) {
            /* ERROR: can't yet rename across file systems */
            errno = EXDEV;
            return -1;
        }

        /* verify that we really have a file by the old name */
        int fid = unifyfs_get_fid_from_path(oldpath);
        if (fid < 0) {
            /* ERROR: oldname does not exist */
            LOGDBG("Couldn't find entry for %s in UNIFYFS", oldpath);
            errno = ENOENT;
            return -1;
        }
        LOGDBG("orig file in position %d", fid);

        /* check that new name is within bounds */
        size_t newpathlen = strlen(newpath) + 1;
        if (newpathlen > UNIFYFS_MAX_FILENAME) {
            errno = ENAMETOOLONG;
            return -1;
        }

        /* TODO: rename should replace existing file atomically */

        /* verify that we don't already have a file by the new name */
        int newfid = unifyfs_get_fid_from_path(newpath);
        if (newfid >= 0) {
            /* something exists in newpath, need to delete it */
            int ret = UNIFYFS_WRAP(unlink)(newpath);
            if (ret == -1) {
                /* failed to unlink */
                errno = EBUSY;
                return -1;
            }
        }

        /* finally overwrite the old name with the new name */
        LOGDBG("Changing %s to %s",
               (char*)&unifyfs_filelist[fid].filename, newpath);
        strcpy((void*)&unifyfs_filelist[fid].filename, newpath);

        /* success */
        return 0;
    } else {
        /* for now, we can only rename within our file system */
        if (unifyfs_intercept_path(newpath)) {
            /* ERROR: can't yet rename across file systems */
            errno = EXDEV;
            return -1;
        }

        /* both files are normal linux files, delegate to system call */
        MAP_OR_FAIL(rename);
        int ret = UNIFYFS_REAL(rename)(oldpath, newpath);
        return ret;
    }
}

int UNIFYFS_WRAP(truncate)(const char* path, off_t length)
{
    /* determine whether we should intercept this path or not */
    if (unifyfs_intercept_path(path)) {
        /* get global file id for path */
        int gfid = unifyfs_generate_gfid(path);

        /* before we truncate, sync any data cached this file id */
        int ret = unifyfs_sync(gfid);
        if (ret != UNIFYFS_SUCCESS) {
            /* sync failed for some reason, set errno and return error */
            errno = unifyfs_err_map_to_errno(ret);
            return -1;
        }

        /* get file id for path name */
        int fid = unifyfs_get_fid_from_path(path);
        if (fid >= 0) {
            /* got the file locally, use fid_truncate the file */
            int rc = unifyfs_fid_truncate(fid, length);
            if (rc != UNIFYFS_SUCCESS) {
                errno = EIO;
                return -1;
            }
        } else {
            /* invoke truncate rpc */
            int rc = invoke_client_truncate_rpc(gfid, length);
            if (rc != UNIFYFS_SUCCESS) {
                LOGDBG("truncate rpc failed %s in UNIFYFS", path);
                errno = EIO;
                return -1;
            }
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(truncate);
        int ret = UNIFYFS_REAL(truncate)(path, length);
        return ret;
    }
}

int UNIFYFS_WRAP(unlink)(const char* path)
{
    /* determine whether we should intercept this path or not */
    if (unifyfs_intercept_path(path)) {
        /* get file id for path name */
        int fid = unifyfs_get_fid_from_path(path);
        if (fid < 0) {
            /* ERROR: file does not exist */
            LOGDBG("Couldn't find entry for %s in UNIFYFS", path);
            errno = ENOENT;
            return -1;
        }

        /* check that it's not a directory */
        if (unifyfs_fid_is_dir(fid)) {
            /* ERROR: is a directory */
            LOGDBG("Attempting to unlink a directory %s in UNIFYFS", path);
            errno = EISDIR;
            return -1;
        }

        /* delete the file */
        int ret = unifyfs_fid_unlink(fid);
        if (ret != UNIFYFS_SUCCESS) {
            errno = unifyfs_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(unlink);
        int ret = UNIFYFS_REAL(unlink)(path);
        return ret;
    }
}

int UNIFYFS_WRAP(remove)(const char* path)
{
    /* determine whether we should intercept this path or not */
    if (unifyfs_intercept_path(path)) {
        /* get file id for path name */
        int fid = unifyfs_get_fid_from_path(path);
        if (fid < 0) {
            /* ERROR: file does not exist */
            LOGDBG("Couldn't find entry for %s in UNIFYFS", path);
            errno = ENOENT;
            return -1;
        }

        /* check that it's not a directory */
        if (unifyfs_fid_is_dir(fid)) {
            /* TODO: shall be equivalent to rmdir(path) */
            /* ERROR: is a directory */
            LOGDBG("Attempting to remove a directory %s in UNIFYFS", path);
            errno = EISDIR;
            return -1;
        }

        /* shall be equivalent to unlink(path) */
        /* delete the file */
        int ret = unifyfs_fid_unlink(fid);
        if (ret != UNIFYFS_SUCCESS) {
            errno = unifyfs_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(remove);
        int ret = UNIFYFS_REAL(remove)(path);
        return ret;
    }
}

/* Get global file meta data with accurate file size */
static int unifyfs_get_meta_with_size(int gfid, unifyfs_file_attr_t* pfattr)
{
    /* lookup global meta data for this file */
    int ret = unifyfs_get_global_file_meta(gfid, pfattr);
    if (ret != UNIFYFS_SUCCESS) {
        LOGDBG("get metadata rpc failed");
        return ret;
    }

    /* if file is laminated, we assume the file size in the meta
     * data is already accurate, if not, look up the current file
     * size with an rpc */
    if (!pfattr->is_laminated) {
        /* lookup current global file size */
        size_t filesize;
        ret = invoke_client_filesize_rpc(gfid, &filesize);
        if (ret == UNIFYFS_SUCCESS) {
            /* success, we have a file size value */
            pfattr->size = (uint64_t) filesize;
        } else {
            /* failed to get file size for some reason */
            LOGDBG("filesize rpc failed");
            return ret;
        }
    }

    return UNIFYFS_SUCCESS;
}

/* The main stat call for all the *stat() functions */
static int __stat(const char* path, struct stat* buf)
{
    /* check that caller gave us a buffer to write to */
    if (!buf) {
        /* forgot buffer for stat */
        LOGDBG("invalid stat buffer");
        errno = EINVAL;
        return -1;
    }

    /* clear the user buffer */
    memset(buf, 0, sizeof(*buf));

    /* get global file id for given path */
    int gfid = unifyfs_generate_gfid(path);

    /* get stat information for file */
    unifyfs_file_attr_t fattr;
    int ret = unifyfs_get_meta_with_size(gfid, &fattr);
    if (ret != UNIFYFS_SUCCESS) {
        errno = EIO;
        return -1;
    }

    /* update local file metadata (if applicable) */
    int fid = unifyfs_get_fid_from_path(path);
    if (fid != -1) {
        unifyfs_fid_update_file_meta(fid, &fattr);
    }

    /* copy attributes to stat struct */
    unifyfs_file_attr_to_stat(&fattr, buf);

    if (fid >= 0) { /* If we have a local file */
        /*
         * For debugging and testing purposes, we hijack st_rdev to store our
         * local size and log size.  We also assume the stat struct is
         * the 64-bit variant.  The values are stored as:
         *
         * st_rdev = log_size << 32 | local_size;
         *
         */
        buf->st_rdev  = (unifyfs_fid_log_size(fid) << 32);
        buf->st_rdev |= (unifyfs_fid_local_size(fid) & 0xFFFFFFFF);
    }

    return 0;
}

int UNIFYFS_WRAP(stat)(const char* path, struct stat* buf)
{
    LOGDBG("stat was called for %s", path);
    if (unifyfs_intercept_path(path)) {
        return __stat(path, buf);
    } else {
        MAP_OR_FAIL(stat);
        return UNIFYFS_REAL(stat)(path, buf);
    }
}

int UNIFYFS_WRAP(fstat)(int fd, struct stat* buf)
{
    LOGDBG("fstat was called for fd: %d", fd);

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        int fid = unifyfs_get_fid_from_fd(fd);
        const char* path = unifyfs_path_from_fid(fid);
        return __stat(path, buf);
    } else {
        MAP_OR_FAIL(fstat);
        return UNIFYFS_REAL(fstat)(fd, buf);
    }
}

/*
 * NOTE on __xstat(2), __lxstat(2), and __fxstat(2)
 * The additional parameter vers shall be 3 or the behavior of these functions
 * is undefined. (ISO POSIX(2003))
 *
 * from /sys/stat.h, it seems that we need to test if vers being _STAT_VER,
 * instead of using the absolute value 3.
 */

#ifdef HAVE___XSTAT
int UNIFYFS_WRAP(__xstat)(int vers, const char* path, struct stat* buf)
{
    LOGDBG("xstat was called for %s", path);

    if (unifyfs_intercept_path(path)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }
        return __stat(path, buf);
    } else {
        MAP_OR_FAIL(__xstat);
        int ret = UNIFYFS_REAL(__xstat)(vers, path, buf);
        return ret;
    }
}
#endif

#ifdef HAVE___LXSTAT
int UNIFYFS_WRAP(__lxstat)(int vers, const char* path, struct stat* buf)
{
    LOGDBG("lxstat was called for %s", path);

    if (unifyfs_intercept_path(path)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }
        return __stat(path, buf);
    } else {
        MAP_OR_FAIL(__lxstat);
        return UNIFYFS_REAL(__lxstat)(vers, path, buf);
    }
}
#endif

#ifdef HAVE___FXSTAT
int UNIFYFS_WRAP(__fxstat)(int vers, int fd, struct stat* buf)
{
    LOGDBG("fxstat was called for fd %d", fd);

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }

        int fid = unifyfs_get_fid_from_fd(fd);
        const char* path = unifyfs_path_from_fid(fid);
        return __stat(path, buf);
    } else {
        MAP_OR_FAIL(__fxstat);
        return UNIFYFS_REAL(__fxstat)(vers, fd, buf);
    }
}
#endif

/* ---------------------------------------
 * POSIX wrappers: file descriptors
 * --------------------------------------- */

/*
 * Read 'count' bytes info 'buf' from file starting at offset 'pos'.
 *
 * Returns number of bytes actually read, or -1 on error, in which
 * case errno will be set.
 */
ssize_t unifyfs_fd_read(int fd, off_t pos, void* buf, size_t count)
{
    /* get the file id for this file descriptor */
    int fid = unifyfs_get_fid_from_fd(fd);
    if (fid < 0) {
        errno = EBADF;
        return -1;
    }

    /* it's an error to read from a directory */
    if (unifyfs_fid_is_dir(fid)) {
        /* TODO: note that read/pread can return this, but not fread */
        errno = EISDIR;
        return -1;
    }

    /* check that file descriptor is open for read */
    unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
    if (!filedesc->read) {
        errno = EBADF;
        return -1;
    }

    /* TODO: is it safe to assume that off_t is bigger than size_t? */
    /* check that we don't overflow the file length */
    if (unifyfs_would_overflow_offt(pos, (off_t) count)) {
        errno = EOVERFLOW;
        return -1;
    }

    /* TODO: check that file is open for reading */

    /* check that we don't try to read past the end of the file */
    off_t lastread = pos + (off_t) count;
    off_t filesize = unifyfs_fid_logical_size(fid);
    if (filesize < lastread) {
        /* adjust count so we don't read past end of file */
        if (filesize > pos) {
            /* read all bytes until end of file */
            count = (size_t)(filesize - pos);
        } else {
            /* pos is already at or past the end of the file */
            count = 0;
        }
    }

    /* if we don't read any bytes, return success */
    if (count == 0) {
        LOGDBG("returning EOF");
        return 0;
    }

    /* fill in read request */
    read_req_t req;
    req.gfid    = unifyfs_gfid_from_fid(fid);
    req.offset  = (size_t) pos;
    req.length  = count;
    req.nread   = 0;
    req.errcode = UNIFYFS_SUCCESS;
    req.buf     = buf;

    /* execute read operation */
    ssize_t retcount;
    int ret = unifyfs_fd_logreadlist(&req, 1);
    if (ret != UNIFYFS_SUCCESS) {
        /* failed to issue read operation */
        errno = EIO;
        retcount = -1;
    } else if (req.errcode != UNIFYFS_SUCCESS) {
        /* read executed, but failed */
        errno = EIO;
        retcount = -1;
    } else {
        /* success, get number of bytes read from read request field */
        retcount = (ssize_t) req.nread;

        /* update file pointer position */
        filedesc->pos += (off_t) retcount;
    }

    return retcount;
}

/*
 * Write 'count' bytes from 'buf' into file starting at offset' pos'.
 * Allocates new bytes and updates file size as necessary.  It is assumed
 * that 'pos' is actually where you want to write, and so O_APPEND behavior
 * is ignored.  Fills any gaps with zeros
 */
int unifyfs_fd_write(int fd, off_t pos, const void* buf, size_t count)
{
    /* get the file id for this file descriptor */
    int fid = unifyfs_get_fid_from_fd(fd);
    if (fid < 0) {
        return UNIFYFS_ERROR_BADF;
    }

    /* it's an error to write to a directory */
    if (unifyfs_fid_is_dir(fid)) {
        return UNIFYFS_ERROR_INVAL;
    }

    /* check that file descriptor is open for write */
    unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
    if (!filedesc->write) {
        return UNIFYFS_ERROR_BADF;
    }

    /* TODO: is it safe to assume that off_t is bigger than size_t? */
    /* check that our write won't overflow the length */
    if (unifyfs_would_overflow_offt(pos, (off_t) count)) {
        /* TODO: want to return EFBIG here for streams */
        return UNIFYFS_ERROR_OVERFLOW;
    }

    /* get current log size before extending the log */
    off_t logsize = unifyfs_fid_log_size(fid);

    /* compute size log will be after we append data */
    off_t newlogsize = logsize + count;

    /* allocate storage space to hold data for this write */
    int extend_rc = unifyfs_fid_extend(fid, newlogsize);
    if (extend_rc != UNIFYFS_SUCCESS) {
        return extend_rc;
    }

    /* finally write specified data to file */
    int write_rc = unifyfs_fid_write(fid, pos, buf, count);
    if (write_rc == 0) {
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
        meta->needs_sync = 1;
        meta->local_size = MAX(meta->local_size, pos + count);
        meta->log_size = newlogsize;
    }
    return write_rc;
}

int UNIFYFS_WRAP(creat)(const char* path, mode_t mode)
{
    /* equivalent to open(path, O_WRONLY|O_CREAT|O_TRUNC, mode) */

    /* check whether we should intercept this path */
    if (unifyfs_intercept_path(path)) {
        /* TODO: handle relative paths using current working directory */

        /* create the file */
        int fid;
        off_t pos;
        int rc = unifyfs_fid_open(path, O_WRONLY | O_CREAT | O_TRUNC, mode, &fid, &pos);
        if (rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_err_map_to_errno(rc);
            return -1;
        }

        /* allocate a free file descriptor value */
        int fd = unifyfs_stack_pop(unifyfs_fd_stack);
        if (fd < 0) {
            /* ran out of file descriptors */
            errno = EMFILE;
            return -1;
        }

        /* set file id and file pointer, flags include O_WRONLY */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        filedesc->fid   = fid;
        filedesc->pos   = pos;
        filedesc->read  = 0;
        filedesc->write = 1;
        LOGDBG("UNIFYFS_open generated fd %d for file %s", fd, path);

        /* don't conflict with active system fds that range from 0 - (fd_limit) */
        int ret = fd + unifyfs_fd_limit;
        return ret;
    } else {
        MAP_OR_FAIL(creat);
        int ret = UNIFYFS_REAL(creat)(path, mode);
        return ret;
    }
}

int UNIFYFS_WRAP(creat64)(const char* path, mode_t mode)
{
    /* check whether we should intercept this path */
    if (unifyfs_intercept_path(path)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
                __FILE__, __LINE__);
        errno = ENOTSUP;
        return -1;
    } else {
        MAP_OR_FAIL(creat64);
        int ret = UNIFYFS_REAL(creat64)(path, mode);
        return ret;
    }
}

int UNIFYFS_WRAP(open)(const char* path, int flags, ...)
{
    /* if O_CREAT is set, we should also have some mode flags */
    int mode = 0;
    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, int);
        va_end(arg);
    }

    /* determine whether we should intercept this path */
    int ret;
    if (unifyfs_intercept_path(path)) {
        /* TODO: handle relative paths using current working directory */

        /* create the file */
        int fid;
        off_t pos;
        int rc = unifyfs_fid_open(path, flags, mode, &fid, &pos);
        if (rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_err_map_to_errno(rc);
            return -1;
        }

        /* allocate a free file descriptor value */
        int fd = unifyfs_stack_pop(unifyfs_fd_stack);
        if (fd < 0) {
            /* ran out of file descriptors */
            errno = EMFILE;
            return -1;
        }

        /* set file id and file pointer */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        filedesc->fid   = fid;
        filedesc->pos   = pos;
        filedesc->read  = ((flags & O_RDONLY) == O_RDONLY)
                          || ((flags & O_RDWR) == O_RDWR);
        filedesc->write = ((flags & O_WRONLY) == O_WRONLY)
                          || ((flags & O_RDWR) == O_RDWR);
        filedesc->append = ((flags & O_APPEND));
        LOGDBG("UNIFYFS_open generated fd %d for file %s", fd, path);

        /* don't conflict with active system fds that range from 0 - (fd_limit) */
        ret = fd + unifyfs_fd_limit;
        return ret;
    } else {
        MAP_OR_FAIL(open);
        if (flags & O_CREAT) {
            ret = UNIFYFS_REAL(open)(path, flags, mode);
        } else {
            ret = UNIFYFS_REAL(open)(path, flags);
        }
        return ret;
    }
}

#ifdef HAVE_OPEN64
int UNIFYFS_WRAP(open64)(const char* path, int flags, ...)
{
    /* if O_CREAT is set, we should also have some mode flags */
    int mode = 0;
    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, int);
        va_end(arg);
    }

    /* check whether we should intercept this path */
    int ret;
    if (unifyfs_intercept_path(path)) {
        /* Call open wrapper with LARGEFILE flag set*/
        if (flags & O_CREAT) {
            ret = UNIFYFS_WRAP(open)(path, flags | O_LARGEFILE, mode);
        } else {
            ret = UNIFYFS_WRAP(open)(path, flags | O_LARGEFILE);
        }
    } else {
        MAP_OR_FAIL(open64);
        if (flags & O_CREAT) {
            ret = UNIFYFS_REAL(open64)(path, flags, mode);
        } else {
            ret = UNIFYFS_REAL(open64)(path, flags);
        }
    }

    return ret;
}
#endif

int UNIFYFS_WRAP(__open_2)(const char* path, int flags, ...)
{
    int ret;

    LOGDBG("__open_2 was called for path %s", path);

    /* if O_CREAT is set, we should also have some mode flags */
    int mode = 0;
    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, int);
        va_end(arg);
    }

    /* check whether we should intercept this path */
    if (unifyfs_intercept_path(path)) {
        LOGDBG("__open_2 was intercepted for path %s", path);

        /* Call open wrapper */
        if (flags & O_CREAT) {
            ret = UNIFYFS_WRAP(open)(path, flags, mode);
        } else {
            ret = UNIFYFS_WRAP(open)(path, flags);
        }
    } else {
        MAP_OR_FAIL(open);
        if (flags & O_CREAT) {
            ret = UNIFYFS_REAL(open)(path, flags, mode);
        } else {
            ret = UNIFYFS_REAL(open)(path, flags);
        }
    }

    return ret;
}

off_t UNIFYFS_WRAP(lseek)(int fd, off_t offset, int whence)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* TODO: check that fd is actually in use */

        /* get the file id for this file descriptor */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            /* bad file descriptor */
            errno = EBADF;
            return (off_t)(-1);
        }

        /* lookup meta to get file size */
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
        if (meta == NULL) {
            /* bad file descriptor */
            errno = EBADF;
            return (off_t)(-1);
        }

        /* get file descriptor for fd */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);

        /* get current file position */
        off_t current_pos = filedesc->pos;

        /* TODO: support SEEK_DATA and SEEK_HOLE? */

        /* compute final file position */
        switch (whence) {
        case SEEK_SET:
            /* seek to offset */
            current_pos = offset;
            break;
        case SEEK_CUR:
            /* seek to current position + offset */
            current_pos += offset;
            break;
        case SEEK_END:
            /* seek to EOF + offset */
            current_pos = unifyfs_fid_logical_size(fid);
            break;
        default:
            errno = EINVAL;
            return (off_t)(-1);
        }

        /* set and return final file position */
        filedesc->pos = current_pos;
        return current_pos;
    } else {
        MAP_OR_FAIL(lseek);
        off_t ret = UNIFYFS_REAL(lseek)(fd, offset, whence);
        return ret;
    }
}

off64_t UNIFYFS_WRAP(lseek64)(int fd, off64_t offset, int whence)
{
    /* check whether we should intercept this file descriptor */
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        if (sizeof(off_t) == sizeof(off64_t)) {
            /* off_t and off64_t are the same size,
             * delegate to lseek warpper */
            off64_t ret = (off64_t)UNIFYFS_WRAP(lseek)(
                              origfd, (off_t) offset, whence);
            return ret;
        } else {
            /* ERROR: fn not yet supported */
            fprintf(stderr, "Function not yet supported @ %s:%d\n",
                    __FILE__, __LINE__);
            errno = ENOTSUP;
            return (off64_t)(-1);
        }
    } else {
        MAP_OR_FAIL(lseek64);
        off64_t ret = UNIFYFS_REAL(lseek64)(fd, offset, whence);
        return ret;
    }
}

#ifdef HAVE_POSIX_FADVISE
int UNIFYFS_WRAP(posix_fadvise)(int fd, off_t offset, off_t len, int advice)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* check that the file descriptor is valid */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return errno;
        }

        /* process advice from caller */
        switch (advice) {
        case POSIX_FADV_NORMAL:
        case POSIX_FADV_SEQUENTIAL:
        /* can use this hint for a better compression strategy */
        case POSIX_FADV_RANDOM:
        case POSIX_FADV_NOREUSE:
        case POSIX_FADV_WILLNEED:
        /* with the spill-over case, we can use this hint to
         * to better manage the in-memory parts of a file. On
         * getting this advice, move the chunks that are on the
         * spill-over device to the in-memory portion
         */
        case POSIX_FADV_DONTNEED:
            /* similar to the previous case, but move contents from memory
             * to the spill-over device instead.
             */

            /* ERROR: fn not yet supported */
            fprintf(stderr, "Function not yet supported @ %s:%d\n",
                    __FILE__, __LINE__);
            errno = ENOTSUP;
            return errno;
        default:
            /* this function returns the errno itself, not -1 */
            errno = EINVAL;
            return errno;
        }

        /* just a hint so return success even if we don't do anything */
        return 0;
    } else {
        MAP_OR_FAIL(posix_fadvise);
        int ret = UNIFYFS_REAL(posix_fadvise)(fd, offset, len, advice);
        return ret;
    }
}
#endif

ssize_t UNIFYFS_WRAP(read)(int fd, void* buf, size_t count)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* get file id */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* get pointer to file descriptor structure */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* execute read */
        ssize_t ret = unifyfs_fd_read(fd, filedesc->pos, buf, count);
        return ret;
    } else {
        MAP_OR_FAIL(read);
        ssize_t ret = UNIFYFS_REAL(read)(fd, buf, count);
        return ret;
    }
}

/* TODO: find right place to msync spillover mapping */
ssize_t UNIFYFS_WRAP(write)(int fd, const void* buf, size_t count)
{
    LOGDBG("write was called for fd %d", fd);
    size_t ret;
    off_t pos;

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        if (filedesc->append) {
            /*
             * With O_APPEND we always write to the end, despite the current
             * file position.
             */
            int fid = unifyfs_get_fid_from_fd(fd);
            pos = unifyfs_fid_logical_size(fid);
        } else {
            pos = filedesc->pos;
        }

        /* write data to file */
        int write_rc = unifyfs_fd_write(fd, pos, buf, count);
        if (write_rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_err_map_to_errno(write_rc);
            return (ssize_t)(-1);
        }
        ret = count;

        /* update file position */
        filedesc->pos = pos + count;
    } else {
        MAP_OR_FAIL(write);
        ret = UNIFYFS_REAL(write)(fd, buf, count);
    }

    return ret;
}

ssize_t UNIFYFS_WRAP(readv)(int fd, const struct iovec* iov, int iovcnt)
{
    ssize_t ret;

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        ssize_t rret;
        int i;
        ret = 0;
        for (i = 0; i < iovcnt; i++) {
            rret = UNIFYFS_WRAP(read)(fd, (void*)iov[i].iov_base,
                                      iov[i].iov_len);
            if (-1 == rret) {
                return -1;
            } else if (0 == rret) {
                return ret;
            } else {
                ret += rret;
            }
        }
        return ret;
    } else {
        MAP_OR_FAIL(readv);
        ret = UNIFYFS_REAL(readv)(fd, iov, iovcnt);
        return ret;
    }
}

ssize_t UNIFYFS_WRAP(writev)(int fd, const struct iovec* iov, int iovcnt)
{
    ssize_t ret;

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        ssize_t wret;
        int i;
        ret = 0;
        for (i = 0; i < iovcnt; i++) {
            wret = UNIFYFS_WRAP(write)(fd, (const void*)iov[i].iov_base,
                                       iov[i].iov_len);
            if (-1 == wret) {
                return -1;
            } else {
                ret += wret;
                if ((size_t)wret != iov[i].iov_len) {
                    return ret;
                }
            }
        }
        return ret;
    } else {
        MAP_OR_FAIL(writev);
        ret = UNIFYFS_REAL(writev)(fd, iov, iovcnt);
        return ret;
    }
}

#ifdef HAVE_LIO_LISTIO
int UNIFYFS_WRAP(lio_listio)(int mode, struct aiocb* const aiocb_list[],
                             int nitems, struct sigevent* sevp)
{
    /* TODO - support for LIO_NOWAIT mode */

    read_req_t* reqs = calloc(nitems, sizeof(read_req_t));
    if (NULL == reqs) {
        errno = ENOMEM; // EAGAIN?
        return -1;
    }

    int ret = 0;
    int reqcnt = 0;
    int i, fd, fid, ndx, rc;
    struct aiocb* cbp;

    for (i = 0; i < nitems; i++) {
        cbp = aiocb_list[i];
        fd = cbp->aio_fildes;
        switch (cbp->aio_lio_opcode) {
        case LIO_WRITE: {
            ssize_t wret;
            wret = UNIFYFS_WRAP(pwrite)(fd, (const void*)cbp->aio_buf,
                                        cbp->aio_nbytes, cbp->aio_offset);
            if (-1 == wret) {
                AIOCB_ERROR_CODE(cbp) = errno;
            } else {
                AIOCB_ERROR_CODE(cbp) = 0;
                AIOCB_RETURN_VAL(cbp) = wret;
            }
            break;
        }
        case LIO_READ: {
            if (unifyfs_intercept_fd(&fd)) {
                /* get local file id for this request */
                fid = unifyfs_get_fid_from_fd(fd);
                if (fid < 0) {
                    AIOCB_ERROR_CODE(cbp) = EINVAL;
                } else {
                    reqs[reqcnt].gfid    = unifyfs_gfid_from_fid(fid);
                    reqs[reqcnt].offset  = (size_t)(cbp->aio_offset);
                    reqs[reqcnt].length  = cbp->aio_nbytes;
                    reqs[reqcnt].nread   = 0;
                    reqs[reqcnt].errcode = EINPROGRESS;
                    reqs[reqcnt].buf     = (char*)(cbp->aio_buf);
                    reqcnt++;
                }
            } else {
                ssize_t rret;
                rret = UNIFYFS_WRAP(pread)(fd, (void*)cbp->aio_buf,
                                           cbp->aio_nbytes, cbp->aio_offset);
                if (-1 == rret) {
                    AIOCB_ERROR_CODE(cbp) = errno;
                } else {
                    AIOCB_ERROR_CODE(cbp) = 0;
                    AIOCB_RETURN_VAL(cbp) = rret;
                }
            }
            break;
        }
        default: // LIO_NOP
            break;
        }
    }

    if (reqcnt) {
        rc = unifyfs_fd_logreadlist(reqs, reqcnt);
        if (rc != UNIFYFS_SUCCESS) {
            /* error reading data */
            ret = -1;
        }
        /* update aiocb fields to record error status and return value */
        ndx = 0;
        for (i = 0; i < reqcnt; i++) {
            char* buf = reqs[i].buf;
            for (; ndx < nitems; ndx++) {
                cbp = aiocb_list[ndx];
                if ((char*)(cbp->aio_buf) == buf) {
                    AIOCB_ERROR_CODE(cbp) = reqs[i].errcode;
                    if (0 == reqs[i].errcode) {
                        AIOCB_RETURN_VAL(cbp) = reqs[i].length;
                    }
                    break; // continue outer loop
                }
            }
        }
    }

    free(reqs);

    if (-1 == ret) {
        errno = EIO;
    }
    return ret;
}
#endif

/* order by file id then by file position */
static int compare_index_entry(const void* a, const void* b)
{
    const unifyfs_index_t* ptr_a = a;
    const unifyfs_index_t* ptr_b = b;

    if (ptr_a->gfid != ptr_b->gfid) {
        if (ptr_a->gfid < ptr_b->gfid) {
            return -1;
        } else {
            return 1;
        }
    }

    if (ptr_a->file_pos == ptr_b->file_pos) {
        return 0;
    } else if (ptr_a->file_pos < ptr_b->file_pos) {
        return -1;
    } else {
        return 1;
    }
}

/* order by file id then by offset */
static int compare_read_req(const void* a, const void* b)
{
    const read_req_t* ptr_a = a;
    const read_req_t* ptr_b = b;

    if (ptr_a->gfid != ptr_b->gfid) {
        if (ptr_a->gfid < ptr_b->gfid) {
            return -1;
        } else {
            return 1;
        }
    }

    if (ptr_a->offset == ptr_b->offset) {
        return 0;
    } else if (ptr_a->offset < ptr_b->offset) {
        return -1;
    } else {
        return 1;
    }
}

/* notify our delegator that the shared memory buffer
 * is now clear and ready to hold more read data */
static void delegator_signal(void)
{
    LOGDBG("receive buffer now empty");

    /* set shm flag to signal delegator we're done */
    shm_header_t* hdr = (shm_header_t*)shm_recv_buf;
    hdr->state = SHMEM_REGION_EMPTY;

    /* TODO: MEM_FLUSH */
}

/* wait for delegator to inform us that shared memory buffer
 * is filled with read data */
static int delegator_wait(void)
{
    int rc = (int)UNIFYFS_SUCCESS;

#if defined(UNIFYFS_USE_DOMAIN_SOCKET)
    /* wait for signal on socket */
    cmd_fd.events = POLLIN | POLLPRI;
    cmd_fd.revents = 0;
    rc = poll(&cmd_fd, 1, -1);

    /* check that we got something good */
    if (rc == 0) {
        if (cmd_fd.revents != 0) {
            if (cmd_fd.revents == POLLIN) {
                return UNIFYFS_SUCCESS;
            } else {
                printf("poll returned %d; error: %s\n", rc,  strerror(errno));
            }
        } else {
            printf("poll returned %d; error: %s\n", rc,  strerror(errno));
        }
    } else {
        printf("poll returned %d; error: %s\n", rc,  strerror(errno));
    }
#endif

    /* specify time to sleep between checking flag in shared
     * memory indicating server has produced */
    struct timespec shm_wait_tm;
    shm_wait_tm.tv_sec  = 0;
    shm_wait_tm.tv_nsec = SHM_WAIT_INTERVAL;

    /* get pointer to flag in shared memory */
    shm_header_t* hdr = (shm_header_t*)shm_recv_buf;

    /* wait for server to set flag to non-zero */
    int max_sleep = 5000000; // 5s
    volatile int* vip = (volatile int*)&(hdr->state);
    while (*vip == SHMEM_REGION_EMPTY) {
        /* not there yet, sleep for a while */
        nanosleep(&shm_wait_tm, NULL);
        /* TODO: MEM_FETCH */
        max_sleep--;
        if (0 == max_sleep) {
            LOGERR("timed out waiting for non-empty");
            rc = (int)UNIFYFS_ERROR_SHMEM;
            break;
        }
    }

    return rc;
}

/* copy read data from shared memory buffer to user buffers from read
 * calls, sets done=1 on return when delegator informs us it has no
 * more data */
static int process_read_data(read_req_t* read_reqs, int count, int* done)
{
    /* assume we'll succeed */
    int rc = UNIFYFS_SUCCESS;

    /* get pointer to start of shared memory buffer */
    shm_header_t* shm_hdr = (shm_header_t*)shm_recv_buf;
    char* shmptr = ((char*)shm_hdr) + sizeof(shm_header_t);

    /* get number of read replies in shared memory */
    size_t num = shm_hdr->meta_cnt;

    /* process each of our read replies */
    size_t i;
    for (i = 0; i < num; i++) {
        /* get pointer to current read reply header */
        shm_meta_t* rep = (shm_meta_t*)shmptr;
        shmptr += sizeof(shm_meta_t);

        /* get pointer to data */
        char* rep_buf = shmptr;
        shmptr += rep->length;

        /* get start and end offset of reply */
        size_t rep_start = rep->offset;
        size_t rep_end   = rep->offset + rep->length;

        /* iterate over each of our read requests */
        size_t j;
        for (j = 0; j < count; j++) {
            /* get pointer to read request */
            read_req_t* req = &read_reqs[j];

            /* skip if this request if not the same file */
            if (rep->gfid != req->gfid) {
                /* request and reply are for different files */
                continue;
            }

            /* same file, now get start and end offsets
             * of this read request */
            size_t req_start = req->offset;
            size_t req_end   = req->offset + req->length;

            /* test whether reply overlaps with request,
             * overlap if:
             *   start of reply comes before the end of request
             * AND
             *   end of reply comes after the start of request */
            int overlap = (rep_start < req_end && rep_end > req_start);
            if (!overlap) {
                /* reply does not overlap with this request */
                continue;
            }

            /* this reply overlaps with the request, check that
             * we didn't get an error */
            if (rep->errcode != UNIFYFS_SUCCESS) {
                /* TODO: should we look for the reply with an errcode
                 * with the lowest start offset? */

                /* read reply has an error, mark the read request
                 * as also having an error, then quit processing */
                req->errcode = rep->errcode;
                continue;
            }

            /* otherwise, we have an error-free, overlapping reply
             * for this request, copy data into request buffer */

            /* start of overlapping segment is the maximum of
             * reply and request start offsets */
            size_t start = rep_start;
            if (req_start > start) {
                start = req_start;
            }

            /* end of overlapping segment is the mimimum of
             * reply and request end offsets */
            size_t end = rep_end;
            if (req_end < end) {
                end = req_end;
            }

            /* compute length of overlapping segment */
            size_t length = end - start;

            /* get number of bytes from start of reply and request
             * buffers to the start of the overlap region */
            size_t rep_offset = start - rep_start;
            size_t req_offset = start - req_start;

            /* if we have a gap, fill with zeros */
            size_t gap_start = req_start + req->nread;
            if (start > gap_start) {
                size_t gap_length = start - gap_start;
                char* req_ptr = req->buf + req->nread;
                memset(req_ptr, 0, gap_length);
            }

            /* copy data from reply buffer into request buffer */
            char* req_ptr = req->buf + req_offset;
            char* rep_ptr = rep_buf  + rep_offset;
            memcpy(req_ptr, rep_ptr, length);

            /* update max number of bytes we have written to in the
             * request buffer */
            size_t nread = end - req_start;
            if (nread > req->nread) {
                req->nread = nread;
            }
        }
    }

    /* set done flag if there is no more data */
    if (shm_hdr->state == SHMEM_REGION_DATA_COMPLETE) {
        *done = 1;
    }

    return rc;
}

/* This uses information in the extent map for a file on the client to
 * complete any read requests.  It only complets a request if it contains
 * all of the data.  Otherwise the request is copied to the list of
 * requests to be handled by the server. */
static void service_local_reqs(
    read_req_t* read_reqs,   /* list of input read requests */
    int count,               /* number of input read requests */
    read_req_t* local_reqs,  /* list to copy requests completed by client */
    read_req_t* server_reqs, /* list to copy requests to be handled by server */
    int* out_count)          /* number of items copied to server list */
{
    /* this will track the total number of requests we're passing
     * on to the server */
    int local_count  = 0;
    int server_count = 0;

    /* iterate over each input read request, satisfy it locally if we can
     * otherwise copy request into output list that the server will handle
     * for us */
    int i;
    for (i = 0; i < count; i++) {
        /* get current read request */
        read_req_t* req = &read_reqs[i];

        /* skip any request that's already completed or errored out,
         * we pass those requests on to server */
        if (req->nread >= req->length || req->errcode != UNIFYFS_SUCCESS) {
            /* copy current request into list of requests
             * that we'll ask server for */
            memcpy(&server_reqs[server_count], req, sizeof(read_req_t));
            server_count++;
            continue;
        }

        /* get gfid, start, and length of this request */
        int gfid         = req->gfid;
        size_t req_start = req->offset;
        size_t req_end   = req->offset + req->length;

        /* lookup local extents if we have them */
        int fid = unifyfs_fid_from_gfid(gfid);

        /* move to next request if we can't find the matching fid */
        if (fid < 0) {
            /* copy current request into list of requests
             * that we'll ask server for */
            memcpy(&server_reqs[server_count], req, sizeof(read_req_t));
            server_count++;
            continue;
        }

        /* get pointer to extents for this file */
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
        struct seg_tree* extents = &meta->extents;

        /* lock the extent tree for reading */
        seg_tree_rdlock(extents);

        /* identify whether we can satisfy this full request
         * or not, assume we can */
        int have_local = 1;

        /* this will point to the offset of the next byte we
         * need to account for */
        size_t expected_start = req_start;

        /* iterate over extents we have for this file,
         * and check that there are no holes in coverage,
         * we search for a starting extent using a range
         * of just the very first byte that we need */
        struct seg_tree_node* first;
        first = seg_tree_find_nolock(extents, req_start, req_start);
        struct seg_tree_node* next = first;
        while (next != NULL && next->start < req_end) {
            if (expected_start >= next->start) {
                /* this extent has the next byte we expect,
                 * bump up to the first byte past the end
                 * of this extent */
                expected_start = next->end + 1;
            } else {
                /* there is a gap between extents so we're missing
                 * some bytes */
                have_local = 0;
                break;
            }

            /* get the next element in the tree */
            next = seg_tree_iter(extents, next);
        }

        /* check that we account for the full request
         * up until the last byte */
        if (expected_start < req_end) {
            /* missing some bytes at the end of the request */
            have_local = 0;
        }

        /* if we can't fully satisfy the request, copy request to
         * output array, so it can be passed on to server */
        if (!have_local) {
            /* copy current request into list of requests
             * that we'll ask server for */
            memcpy(&server_reqs[server_count], req, sizeof(read_req_t));
            server_count++;

            /* release lock before we go to next request */
            seg_tree_unlock(extents);

            continue;
        }

        /* otherwise we can copy the data locally, iterate
         * over the extents and copy data into request buffer,
         * again search for a starting extent using a range
         * of just the very first byte that we need */
        next = first;
        while (next != NULL && next->start < req_end) {
            /* get start and end of this extent (reply) */
            size_t rep_start = next->start;
            size_t rep_end   = next->end + 1;

            /* get the offset into the log */
            size_t pos = next->ptr;

            /* start of overlapping segment is the maximum of
             * reply and request start offsets */
            size_t start = rep_start;
            if (req_start > start) {
                start = req_start;
            }

            /* end of overlapping segment is the mimimum of
             * reply and request end offsets */
            size_t end = rep_end;
            if (req_end < end) {
                end = req_end;
            }

            /* compute length of overlapping segment */
            size_t length = end - start;

            /* get number of bytes from start of reply and request
             * buffers to the start of the overlap region */
            size_t rep_offset = start - rep_start;
            size_t req_offset = start - req_start;

            /* if we have a gap, fill with zeros */
            size_t gap_start = req_start + req->nread;
            if (start > gap_start) {
                size_t gap_length = start - gap_start;
                char* req_ptr = req->buf + req->nread;
                memset(req_ptr, 0, gap_length);
            }

            /* copy data from reply buffer into request buffer */
            char* req_ptr = req->buf + req_offset;

            /* compute total size of shared memory data region */
            size_t chunk_mem_size = unifyfs_max_chunks * unifyfs_chunk_size;

            /* compute number of bytes we'll read from shared memory */
            size_t mem_length = 0;
            if (pos < chunk_mem_size) {
                /* we need to start from memory, assume we'll get it all */
                mem_length = length;
                if (pos + length > chunk_mem_size) {
                    /* amount to read extends past end of memory segment,
                     * compute the number of bytes in memory */
                    mem_length = chunk_mem_size - pos;
                }
            }

            /* any remainder comes from the spill over file */
            size_t spill_length = length - mem_length;

            /* copy data from memory into request buffer */
            if (mem_length > 0) {
                /* pointer to data is starting address of superblock
                 * data region (unify_chunks) + offset within the
                 * superblock to the start of the segment in
                 * reply (pos) + any offset from start of that segment
                 * until the first byte we are asking for (rep_offset) */
                char* mem_ptr = unifyfs_chunks + pos + rep_offset;
                memcpy(req_ptr, mem_ptr, mem_length);
            }

            /* copy data from spill over into request buffer */
            if (spill_length > 0) {
                /* advance in user buffer past any bytes we copied in
                 * from memory */
                char* reqbuf = req_ptr + mem_length;

                /* offset to start of data in spill over file is
                 * position within log (pos) + offset from start of
                 * segment to the first byte in segment that overlaps
                 * with request (rep_offset) + any bytes copied from
                 * memory (mem_length) - the size of the data region
                 * in memory */
                off_t spill_offset = pos + rep_offset + mem_length
                    - chunk_mem_size;

                /* TODO: loop on this if we get a short read
                 * or EIO/EAGAIN */

                /* read data from file into request buffer */
                ssize_t rc = pread(unifyfs_spilloverblock,
                    reqbuf, spill_length, spill_offset);
                if (rc != length) {
                    /* had a problem reading,
                     * set the request error code */
                    req->errcode = UNIFYFS_ERROR_IO;
                }
            }

            /* update max number of bytes we have written to in the
             * request buffer */
            size_t nread = end - req_start;
            if (nread > req->nread) {
                req->nread = nread;
            }

            /* get the next element in the tree */
            next = seg_tree_iter(extents, next);
        }

        /* copy request data to list we completed locally */
        memcpy(&local_reqs[local_count], req, sizeof(read_req_t));
        local_count++;

        /* done reading the tree */
        seg_tree_unlock(extents);
    }

    /* return to user the number of key/values we set */
    *out_count = server_count;

    return;
}

/*
 * get data for a list of read requests from the
 * delegator
 *
 * @param read_reqs: a list of read requests
 * @param count: number of read requests
 * @return error code
 * */
int unifyfs_fd_logreadlist(read_req_t* in_reqs, int in_count)
{
    int i;
    int read_rc;

    /* assume we'll succeed */
    int rc = UNIFYFS_SUCCESS;

    /* assume we'll service all requests from the server */
    int count = in_count;
    read_req_t* read_reqs = in_reqs;

    /* TODO: if the file is laminated and we know the file size,
     * we could adjust some reads to not try reading past the EOF */

    /* if the option is enabled to service requests locally, try it,
     * in this case we'll allocate a large array which we split into
     * two, the first half will record requests we completed locally
     * and the second half will store requests to be sent to the server */

    /* this records the pointer to the temp request array if
     * we allocate one, we should free this later if not NULL */
    read_req_t* reqs = NULL;

    /* this will point to the start of the array of requests we
     * complete locally */
    read_req_t* local_reqs = NULL;

    /* attempt to complete requests locally if enabled */
    if (unifyfs_local_extents) {
        /* allocate space to make local and server copies of the requests,
         * each list will be at most in_count long */
        size_t reqs_size = 2 * in_count * sizeof(read_req_t);
        reqs = (read_req_t*) malloc(reqs_size);
        if (reqs == NULL) {
            return UNIFYFS_ERROR_NOMEM;
        }

        /* define pointers to space where we can build our list
         * of requests handled on the client and those left
         * for the server */
        local_reqs = &reqs[0];
        read_reqs  = &reqs[in_count];

        /* service reads from local extent info if we can, this copies
         * completed requests from in_reqs into local_reqs, and it copies
         * any requests that can't be completed locally into the read_reqs
         * to be processed by the server */
        service_local_reqs(in_reqs, in_count, local_reqs, read_reqs, &count);

        /* bail early if we satisfied all requests locally */
        if (count == 0) {
            /* copy completed requests back into user's array */
            memcpy(in_reqs, local_reqs, in_count * sizeof(read_req_t));

            /* free the temporary array */
            free(reqs);
            return rc;
        }
    }

    /* TODO: When the number of read requests exceed the
     * request buffer, split list io into multiple bulk
     * sends and transfer in bulks */

    /* check that we have enough slots for all read requests */
    if (count > UNIFYFS_MAX_READ_CNT) {
        LOGERR("Too many requests to pass to server");
        if (reqs != NULL) {
            free(reqs);
        }
        return read_rc;
    }

    /* order read request by increasing file id, then increasing offset */
    qsort(read_reqs, count, sizeof(read_req_t), compare_read_req);

    /* prepare our shared memory buffer for delegator */
    delegator_signal();

    /* we select different rpcs depending on the number of
     * read requests */
    if (count > 1) {
        /* got multiple read requests,
         * build up a flat buffer to include them all */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);

        /* create request vector */
        unifyfs_Extent_vec_start(&builder);

        /* fill in values for each request entry */
        for (i = 0; i < count; i++) {
            unifyfs_Extent_vec_push_create(&builder,
                read_reqs[i].gfid, read_reqs[i].offset, read_reqs[i].length);
        }

        /* complete the array */
        unifyfs_Extent_vec_ref_t extents = unifyfs_Extent_vec_end(&builder);
        unifyfs_ReadRequest_create_as_root(&builder, extents);
        //unifyfs_ReadRequest_end_as_root(&builder);

        /* allocate our buffer to be sent */
        size_t size = 0;
        void* buffer = flatcc_builder_finalize_buffer(&builder, &size);
        assert(buffer);

        LOGDBG("mread: n_reqs:%d, flatcc buffer (%p) sz:%zu",
               count, buffer, size);

        /* invoke multi-read rpc */
        read_rc = invoke_client_mread_rpc(count, size, buffer);

        /* free flat buffer resources */
        flatcc_builder_clear(&builder);
        free(buffer);
    } else {
        /* got a single read request */
        int gfid      = read_reqs[0].gfid;
        size_t offset = read_reqs[0].offset;
        size_t length = read_reqs[0].length;

        LOGDBG("read: offset:%zu, len:%zu", offset, length);

        /* invoke single read rpc */
        read_rc = invoke_client_read_rpc(gfid, offset, length);
    }

    /* bail out with error if we failed to even start the read */
    if (read_rc != UNIFYFS_SUCCESS) {
        LOGERR("Failed to issue read RPC to server");
        if (reqs != NULL) {
            free(reqs);
        }
        return read_rc;
    }

    /*
     * ToDo: Exception handling when some of the requests
     * are missed
     * */

    /* spin waiting for read data to come back from the server,
     * we process it in batches as it comes in, eventually the
     * server will tell us it's sent us everything it can */
    int done = 0;
    while (!done) {
        int tmp_rc = delegator_wait();
        if (tmp_rc != UNIFYFS_SUCCESS) {
            rc = UNIFYFS_FAILURE;
            done = 1;
        } else {
            tmp_rc = process_read_data(read_reqs, count, &done);
            if (tmp_rc != UNIFYFS_SUCCESS) {
                rc = UNIFYFS_FAILURE;
            }
            delegator_signal();
        }
    }

    /* got all of the data we'll get from the server,
     * check for short reads and whether those short
     * reads are from errors, holes, or the end of the file */
    for (i = 0; i < count; i++) {
        /* get pointer to next read request */
        read_req_t* req = &read_reqs[i];

        /* if we hit an error on our read, nothing else to do */
        if (req->errcode != UNIFYFS_SUCCESS) {
            continue;
        }

        /* if we read all of the bytes, we're done */
        if (req->nread == req->length) {
            continue;
        }

        /* otherwise, we have a short read, check whether there
         * would be a hole after us, in which case we fill the
         * request buffer with zeros */

        /* get file size for this file */
        size_t filesize;
        int ret = invoke_client_filesize_rpc(req->gfid, &filesize);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to get file size */
            req->errcode = ret;
            continue;
        }

        /* get offset of where hole starts */
        size_t gap_start = req->offset + req->nread;

        /* get last offset of the read request */
        size_t req_end = req->offset + req->length;

        /* if file size is larger than last offset we wrote to in
         * read request, then there is a hole we can fill */
        if (filesize > gap_start) {
            /* assume we can fill the full request with zero */
            size_t gap_length = req_end - gap_start;
            if (req_end > filesize) {
                /* request is trying to read past end of file,
                 * so only fill zeros up to end of file */
                gap_length = filesize - gap_start;
            }

            /* copy zeros into request buffer */
            char* req_ptr = req->buf + req->nread;
            memset(req_ptr, 0, gap_length);

            /* update number of bytes read */
            req->nread += gap_length;
        }
    }

    /* if we attempted to service requests from our local extent map,
     * then we need to copy the resulting read requests from the local
     * and server arrays back into the user's original array */
    if (unifyfs_local_extents) {
        /* TODO: would be nice to copy these back into the same order
         * in which we received them. */

        /* copy locally completed requests back into user's array */
        int local_count = in_count - count;
        if (local_count > 0) {
            memcpy(in_reqs, local_reqs, local_count * sizeof(read_req_t));
        }

        /* copy sever completed requests back into user's array */
        if (count > 0) {
            /* skip past any items we copied in from the local requests */
            char* in_ptr = in_reqs + local_count * sizeof(read_req_t);
            memcpy(in_ptr, read_reqs, count * sizeof(read_req_t));
        }

        /* free storage we used for copies of requests */
        if (reqs != NULL) {
            free(reqs);
            reqs = NULL;
        }
    }

    return rc;
}

ssize_t UNIFYFS_WRAP(pread)(int fd, void* buf, size_t count, off_t offset)
{
    /* equivalent to read(), except that it shall read from a given
     * position in the file without changing the file pointer */

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* get file id */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* fill in read request */
        read_req_t req;
        req.gfid    = unifyfs_gfid_from_fid(fid);
        req.offset  = offset;
        req.length  = count;
        req.nread   = 0;
        req.errcode = UNIFYFS_SUCCESS;
        req.buf     = buf;

        /* execute read operation */
        ssize_t retcount;
        int ret = unifyfs_fd_logreadlist(&req, 1);
        if (ret != UNIFYFS_SUCCESS) {
            /* error reading data */
            errno = EIO;
            retcount = -1;
        } else if (req.errcode != UNIFYFS_SUCCESS) {
            /* error reading data */
            errno = EIO;
            retcount = -1;
        } else {
            /* read succeeded, get number of bytes from nread field */
            retcount = (ssize_t) req.nread;
        }

        /* return number of bytes read */
        return retcount;
    } else {
        MAP_OR_FAIL(pread);
        ssize_t ret = UNIFYFS_REAL(pread)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYFS_WRAP(pread64)(int fd, void* buf, size_t count, off64_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        return UNIFYFS_WRAP(pread)(fd, buf, count, (off_t)offset);
    } else {
        MAP_OR_FAIL(pread64);
        ssize_t ret = UNIFYFS_REAL(pread64)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYFS_WRAP(pwrite)(int fd, const void* buf, size_t count,
                             off_t offset)
{
    /* equivalent to write(), except that it writes into a given
     * position without changing the file pointer */
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* write data to file */
        int write_rc = unifyfs_fd_write(fd, offset, buf, count);
        if (write_rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_err_map_to_errno(write_rc);
            return (ssize_t)(-1);
        }

        /* return number of bytes read */
        return (ssize_t) count;
    } else {
        MAP_OR_FAIL(pwrite);
        ssize_t ret = UNIFYFS_REAL(pwrite)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYFS_WRAP(pwrite64)(int fd, const void* buf, size_t count,
                               off64_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        return UNIFYFS_WRAP(pwrite)(fd, buf, count, (off_t)offset);
    } else {
        MAP_OR_FAIL(pwrite64);
        ssize_t ret = UNIFYFS_REAL(pwrite64)(fd, buf, count, offset);
        return ret;
    }
}

int UNIFYFS_WRAP(ftruncate)(int fd, off_t length)
{
    /* check whether we should intercept this file descriptor */
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        /* get the file id for this file descriptor */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return -1;
        }

        /* check that file descriptor is open for write */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        if (!filedesc->write) {
            errno = EBADF;
            return -1;
        }

        /* get global file id for fid */
        int gfid = unifyfs_gfid_from_fid(fid);

        /* before we truncate, sync any data cached this file id */
        int ret = unifyfs_sync(gfid);
        if (ret != UNIFYFS_SUCCESS) {
            /* sync failed for some reason, set errno and return error */
            errno = unifyfs_err_map_to_errno(ret);
            return -1;
        }

        /* truncate the file */
        int rc = unifyfs_fid_truncate(fid, length);
        if (rc != UNIFYFS_SUCCESS) {
            errno = EIO;
            return -1;
        }

        return 0;
    } else {
        MAP_OR_FAIL(ftruncate);
        int ret = UNIFYFS_REAL(ftruncate)(fd, length);
        return ret;
    }
}

int UNIFYFS_WRAP(fsync)(int fd)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* get the file id for this file descriptor */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            LOGERR("Couldn't get fid from fd %d", fd);
            errno = EBADF;
            return -1;
        }

        /* skip this file if no new data has been written to it */
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
        if (!meta->needs_sync) {
            return 0;
        }

        /* invoke fsync rpc to register index metadata with server */
        int gfid = unifyfs_gfid_from_fid(fid);
        int ret = unifyfs_sync(gfid);
        if (ret != UNIFYFS_SUCCESS) {
            /* sync failed for some reason, set errno and return error */
            errno = unifyfs_err_map_to_errno(ret);
            return -1;
        }

        /* update metadata to indicate that data has been synced */
        meta->needs_sync = 0;
        return 0;
    } else {
        MAP_OR_FAIL(fsync);
        int ret = UNIFYFS_REAL(fsync)(fd);
        return ret;
    }
}

int UNIFYFS_WRAP(fdatasync)(int fd)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
                __FILE__, __LINE__);
        errno = ENOTSUP;
        return -1;
    } else {
        MAP_OR_FAIL(fdatasync);
        int ret = UNIFYFS_REAL(fdatasync)(fd);
        return ret;
    }
}

int UNIFYFS_WRAP(flock)(int fd, int operation)
{
    int ret;

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        // KMM I removed the locking code because it was causing
        // hangs
        /*
          -- currently handling the blocking variants only
          switch (operation)
          {
              case LOCK_EX:
                  LOGDBG("locking file %d", fid);
                  ret = pthread_spin_lock(&meta->fspinlock);
                  if ( ret ) {
                      perror("pthread_spin_lock() failed");
                      return -1;
                  }
                  meta->flock_status = EX_LOCKED;
                  break;
              case LOCK_SH:
                  -- not needed for CR; will not be supported,
                  --  update flock_status anyway
                  meta->flock_status = SH_LOCKED;
                  break;
              case LOCK_UN:
                  ret = pthread_spin_unlock(&meta->fspinlock);
                  LOGDBG("unlocking file %d", fid);
                  meta->flock_status = UNLOCKED;
                  break;
              default:
                  errno = EINVAL;
                  return -1;
          }
         */

        return 0;
    } else {
        MAP_OR_FAIL(flock);
        ret = UNIFYFS_REAL(flock)(fd, operation);
        return ret;
    }
}

/* TODO: handle different flags */
void* UNIFYFS_WRAP(mmap)(void* addr, size_t length, int prot, int flags,
                         int fd, off_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* for now, tell user that we can't support mmap,
         * we'll need to track assigned memory region so that
         * we can identify our files on msync and munmap */
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
                __FILE__, __LINE__);
        errno = ENODEV;
        return MAP_FAILED;

#if 0 // TODO - mmap support
        /* get the file id for this file descriptor */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return MAP_FAILED;
        }

        /* TODO: handle addr properly based on flags */

        /* allocate memory required to mmap the data if addr is NULL;
         * using posix_memalign instead of malloc to align mmap'ed area
         * to page size */
        if (!addr) {
            int ret = posix_memalign(&addr, sysconf(_SC_PAGE_SIZE), length);
            if (ret) {
                /* posix_memalign does not set errno */
                if (ret == EINVAL) {
                    errno = EINVAL;
                    return MAP_FAILED;
                }

                if (ret == ENOMEM) {
                    errno = ENOMEM;
                    return MAP_FAILED;
                }
            }
        }

        /* TODO: do we need to extend file if offset+length goes past current end? */

        /* check that we don't copy past the end of the file */
        off_t last_byte = offset + length;
        off_t file_size = unifyfs_fid_size(fid);
        if (last_byte > file_size) {
            /* trying to copy past the end of the file, so
             * adjust the total amount to be copied */
            length = (size_t)(file_size - offset);
        }

        /* read data from file */
        int rc = unifyfs_fid_read(fid, offset, addr, length);
        if (rc != UNIFYFS_SUCCESS) {
            /* TODO: need to free memory in this case? */
            errno = ENOMEM;
            return MAP_FAILED;
        }

        return addr;
#endif
    } else {
        MAP_OR_FAIL(mmap);
        void* ret = UNIFYFS_REAL(mmap)(addr, length, prot, flags, fd, offset);
        return ret;
    }
}

int UNIFYFS_WRAP(munmap)(void* addr, size_t length)
{
#if 0 // TODO - mmap support
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = EINVAL;
    return -1;
#endif
    MAP_OR_FAIL(munmap);
    int ret = UNIFYFS_REAL(munmap)(addr, length);
    return ret;
}

int UNIFYFS_WRAP(msync)(void* addr, size_t length, int flags)
{
#if 0 // TODO - mmap support
    /* TODO: need to keep track of all the mmaps that are linked to
     * a given file before this function can be implemented */
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = EINVAL;
    return -1;
#endif
    MAP_OR_FAIL(msync);
    int ret = UNIFYFS_REAL(msync)(addr, length, flags);
    return ret;
}

void* UNIFYFS_WRAP(mmap64)(void* addr, size_t length, int prot, int flags,
                           int fd, off64_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        return UNIFYFS_WRAP(mmap)(addr, length, prot, flags, fd, (off_t)offset);
    } else {
        MAP_OR_FAIL(mmap64);
        void* ret = UNIFYFS_REAL(mmap64)(addr, length, prot, flags, fd, offset);
        return ret;
    }
}

int UNIFYFS_WRAP(close)(int fd)
{
    /* check whether we should intercept this file descriptor */
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        LOGDBG("closing fd %d", fd);

        /* TODO: what to do if underlying file has been deleted? */

        /* check that fd is actually in use */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return -1;
        }

        /* get file descriptor for this file */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            errno = EBADF;
            return -1;
        }

        /* if file was opened for writing, fsync it */
        if (filedesc->write) {
            UNIFYFS_WRAP(fsync)(origfd);
        }

        /* close the file id */
        int close_rc = unifyfs_fid_close(fid);
        if (close_rc != UNIFYFS_SUCCESS) {
            errno = EIO;
            return -1;
        }

        /* reinitialize file descriptor to indicate that
         * it is no longer associated with a file,
         * not technically needed but may help catch bugs */
        unifyfs_fd_init(fd);

        /* add file descriptor back to free stack */
        unifyfs_stack_push(unifyfs_fd_stack, fd);

        return 0;
    } else {
        MAP_OR_FAIL(close);
        int ret = UNIFYFS_REAL(close)(fd);
        return ret;
    }
}

/* Helper function used by fchmod() and chmod() */
static int __chmod(int fid, mode_t mode)
{
    int ret;

    /* get path for printing debug messages */
    const char* path = unifyfs_path_from_fid(fid);

    /* lookup metadata for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (!meta) {
        LOGDBG("chmod: %s no metadata info", path);
        errno = ENOENT;
        return -1;
    }

    /* Once a file is laminated, you can't modify it in any way */
    if (meta->is_laminated) {
        LOGDBG("chmod: %s is already laminated", path);
        errno = EROFS;
        return -1;
    }

    /* found file, and it's not yet laminated,
     * get the global file id */
    int gfid = unifyfs_gfid_from_fid(fid);

    /* TODO: need to fetch global metadata in case
     * another process has changed it */

    /*
     * If the chmod clears all the existing write bits, then it's a laminate.
     *
     * meta->mode & 0222                  Was at least one write bit set before?
     * ((meta->mode & 0222) & mode) == 0  Will all the write bits be cleared?
     */
    if ((meta->mode & 0222) &&
        (((meta->mode & 0222) & mode) == 0)) {
        /* We're laminating. */
        ret = invoke_client_laminate_rpc(gfid);
        if (ret) {
            LOGERR("chmod: couldn't get the global file size on laminate");
            errno = EIO;
            return -1;
        }
    }

    /* Clear out our old permission bits, and set the new ones in */
    meta->mode = meta->mode & ~0777;
    meta->mode = meta->mode | mode;

    /* update the global meta data to reflect new permissions */
    ret = unifyfs_set_global_file_meta_from_fid(fid, 0);
    if (ret) {
        LOGERR("chmod: can't set global meta entry for %s (fid:%d)",
               path, fid);
        errno = EIO;
        return -1;
    }

    /* read metadata back to pick up file size and laminated flag */
    unifyfs_file_attr_t attr = {0};
    ret = unifyfs_get_global_file_meta(gfid, &attr);
    if (ret) {
        LOGERR("chmod: can't get global meta entry for %s (fid:%d)",
               path, fid);
        errno = EIO;
        return -1;
    }

    /* update global size of file from global metadata */
    unifyfs_fid_update_file_meta(fid, &attr);

    return 0;
}

int UNIFYFS_WRAP(fchmod)(int fd, mode_t mode)
{
    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* TODO: what to do if underlying file has been deleted? */

        /* check that fd is actually in use */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return -1;
        }

        LOGDBG("fchmod: setting fd %d to %o", fd, mode);
        return __chmod(fid, mode);
    } else {
        MAP_OR_FAIL(fchmod);
        int ret = UNIFYFS_REAL(fchmod)(fd, mode);
        return ret;
    }
}

int UNIFYFS_WRAP(chmod)(const char* path, mode_t mode)
{
    /* determine whether we should intercept this path */
    if (unifyfs_intercept_path(path)) {
        /* check if path exists */
        int fid = unifyfs_get_fid_from_path(path);
        if (fid < 0) {
            LOGDBG("chmod: unifyfs_get_id_from path failed, returning -1, %s",
                   path);
            errno = ENOENT;
            return -1;
        }

        LOGDBG("chmod: setting %s to %o", path, mode);
        return __chmod(fid, mode);
    } else {
        MAP_OR_FAIL(chmod);
        int ret = UNIFYFS_REAL(chmod)(path, mode);
        return ret;
    }
}
