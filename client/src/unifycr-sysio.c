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

#include "unifycr-sysio.h"

#include <aio.h>

/* -------------------
 * define external variables
 * --------------------*/
extern int unifycr_spilloverblock;
extern int unifycr_use_spillover;
extern int dbgrank;


void debug_log_client_req(const char* ctx,
                          read_req_t *req)
{
    if (req != NULL) {
        fprintf(stderr,
                "[UNIFYCR DEBUG] @%s - "
                "read_req(fid=%d, offset=%zu, length=%zu, buf=%p)\n",
                ctx, req->fid, req->offset, req->length, req->buf);
        fflush(stderr);
    }
}

/* ---------------------------------------
 * POSIX wrappers: paths
 * --------------------------------------- */

int UNIFYCR_WRAP(access)(const char *path, int mode)
{
    /* determine whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* check if path exists */
        if (unifycr_get_fid_from_path(path) < 0) {
            DEBUG("access: unifycr_get_id_from path failed, returning -1, %s\n",
                  path);
            errno = ENOENT;
            return -1;
        }

        /* currently a no-op */
        DEBUG("access: path intercepted, returning 0, %s\n", path);
        return 0;
    } else {
        DEBUG("access: calling MAP_OR_FAIL, %s\n", path);
        MAP_OR_FAIL(access);
        int ret = UNIFYCR_REAL(access)(path, mode);
        DEBUG("access: returning __real_access %d,%s\n", ret, path);
        return ret;
    }
}

int UNIFYCR_WRAP(mkdir)(const char *path, mode_t mode)
{
    /* Support for directories is very limited at this time
     * mkdir simply puts an entry into the filelist for the
     * requested directory (assuming it does not exist)
     * It doesn't check to see if parent directory exists */

    /* determine whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* check if it already exists */
        if (unifycr_get_fid_from_path(path) >= 0) {
            errno = EEXIST;
            return -1;
        }

        /* add directory to file list */
        int fid = unifycr_fid_create_directory(path);
        if (fid < 0)
            return -1;
        else
            return 0;
    } else {
        MAP_OR_FAIL(mkdir);
        int ret = UNIFYCR_REAL(mkdir)(path, mode);
        return ret;
    }
}

int UNIFYCR_WRAP(rmdir)(const char *path)
{
    /* determine whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* check if the mount point itself is being deleted */
        if (! strcmp(path, unifycr_mount_prefix)) {
            errno = EBUSY;
            return -1;
        }

        /* check if path exists */
        int fid = unifycr_get_fid_from_path(path);
        if (fid < 0) {
            errno = ENOENT;
            return -1;
        }

        /* is it a directory? */
        if (! unifycr_fid_is_dir(fid)) {
            errno = ENOTDIR;
            return -1;
        }

        /* is it empty? */
        if (! unifycr_fid_is_dir_empty(path)) {
            errno = ENOTEMPTY;
            return -1;
        }

        /* remove the directory from the file list */
        int ret = unifycr_fid_unlink(fid);
        if (ret < 0)
            return -1;
        else
            return 0;
    } else {
        MAP_OR_FAIL(rmdir);
        int ret = UNIFYCR_REAL(rmdir)(path);
        return ret;
    }
}

int UNIFYCR_WRAP(rename)(const char *oldpath, const char *newpath)
{
    /* TODO: allow oldpath / newpath to split across memfs and normal
     * linux fs, which means we'll need to do a read / write */

    /* check whether the old path is in our file system */
    if (unifycr_intercept_path(oldpath)) {
        /* for now, we can only rename within our file system */
        if (! unifycr_intercept_path(newpath)) {
            /* ERROR: can't yet rename across file systems */
            errno = EXDEV;
            return -1;
        }

        /* verify that we really have a file by the old name */
        int fid = unifycr_get_fid_from_path(oldpath);
        DEBUG("orig file in position %d\n", fid);
        if (fid < 0) {
            /* ERROR: oldname does not exist */
            DEBUG("Couldn't find entry for %s in UNIFYCR\n", oldpath);
            errno = ENOENT;
            return -1;
        }

        /* verify that we don't already have a file by the new name */
        if (unifycr_get_fid_from_path(newpath) < 0) {
            /* check that new name is within bounds */
            size_t newpathlen = strlen(newpath) + 1;
            if (newpathlen > UNIFYCR_MAX_FILENAME) {
                errno = ENAMETOOLONG;
                return -1;
            }

            /* finally overwrite the old name with the new name */
            DEBUG("Changing %s to %s\n",
                  (char *)&unifycr_filelist[fid].filename, newpath);
            strcpy((void *)&unifycr_filelist[fid].filename, newpath);
        } else {
            /* ERROR: new name already exists */
            DEBUG("File %s exists\n", newpath);
            errno = EEXIST;
            return -1;
        }
        return 0;
    } else {
        /* for now, we can only rename within our file system */
        if (unifycr_intercept_path(newpath)) {
            /* ERROR: can't yet rename across file systems */
            errno = EXDEV;
            return -1;
        }

        /* both files are normal linux files, delegate to system call */
        MAP_OR_FAIL(rename);
        int ret = UNIFYCR_REAL(rename)(oldpath, newpath);
        return ret;
    }
}

int UNIFYCR_WRAP(truncate)(const char *path, off_t length)
{
    /* determine whether we should intercept this path or not */
    if (unifycr_intercept_path(path)) {
        /* lookup the fd for the path */
        int fid = unifycr_get_fid_from_path(path);

        if (fid < 0) {
            /* ERROR: file does not exist */
            DEBUG("Couldn't find entry for %s in UNIFYCR\n", path);
            errno = ENOENT;
            return -1;
        }

        /* truncate the file */
        int rc = unifycr_fid_truncate(fid, length);
        if (rc != UNIFYCR_SUCCESS) {
            DEBUG("unifycr_fid_truncate failed for %s in UNIFYCR\n", path);
            errno = EIO;
            return -1;
        }

        return 0;
    } else {
        MAP_OR_FAIL(truncate);
        int ret = UNIFYCR_REAL(truncate)(path, length);
        return ret;
    }
}

int UNIFYCR_WRAP(unlink)(const char *path)
{
    /* determine whether we should intercept this path or not */
    if (unifycr_intercept_path(path)) {
        /* get file id for path name */
        int fid = unifycr_get_fid_from_path(path);
        if (fid < 0) {
            /* ERROR: file does not exist */
            DEBUG("Couldn't find entry for %s in UNIFYCR\n", path);
            errno = ENOENT;
            return -1;
        }

        /* check that it's not a directory */
        if (unifycr_fid_is_dir(fid)) {
            /* ERROR: is a directory */
            DEBUG("Attempting to unlink a directory %s in UNIFYCR\n", path);
            errno = EISDIR;
            return -1;
        }

        /* delete the file */
        unifycr_fid_unlink(fid);

        return 0;
    } else {
        MAP_OR_FAIL(unlink);
        int ret = UNIFYCR_REAL(unlink)(path);
        return ret;
    }
}

int UNIFYCR_WRAP(remove)(const char *path)
{
    /* determine whether we should intercept this path or not */
    if (unifycr_intercept_path(path)) {
        /* get file id for path name */
        int fid = unifycr_get_fid_from_path(path);
        if (fid < 0) {
            /* ERROR: file does not exist */
            DEBUG("Couldn't find entry for %s in UNIFYCR\n", path);
            errno = ENOENT;
            return -1;
        }

        /* check that it's not a directory */
        if (unifycr_fid_is_dir(fid)) {
            /* TODO: shall be equivalent to rmdir(path) */
            /* ERROR: is a directory */
            DEBUG("Attempting to remove a directory %s in UNIFYCR\n", path);
            errno = EISDIR;
            return -1;
        }

        /* shall be equivalent to unlink(path) */
        /* delete the file */
        unifycr_fid_unlink(fid);

        return 0;
    } else {
        MAP_OR_FAIL(remove);
        int ret = UNIFYCR_REAL(remove)(path);
        return ret;
    }
}

int UNIFYCR_WRAP(stat)(const char *path, struct stat *buf)
{
    int ret = 0;
    int fid = -1;
    int gfid = -1;
    int found_local = 0;
    int found_global = 0;
    unifycr_file_attr_t gfattr = { 0, };

    DEBUG("stat was called for %s....\n", path);

    if (unifycr_intercept_path(path)) {
        if (!buf) {
            errno = EFAULT;
            return -1;
        }

        gfid = unifycr_generate_gfid(path);
        fid = unifycr_get_fid_from_path(path);

        found_global =
            (unifycr_get_global_file_meta(gfid, &gfattr) == UNIFYCR_SUCCESS);
        found_local = (fid >= 0);

        if (!found_global) {
            /* the local entry is obsolete and should be discarded. */
            if (found_local)
                unifycr_fid_unlink(fid); /* this always returns success */

            errno = ENOENT;
            return -1;
        }

        *buf = gfattr.file_attr;

        return 0;
    } else {
        MAP_OR_FAIL(stat);
        ret = UNIFYCR_REAL(stat)(path, buf);
        return ret;
    }
}

int UNIFYCR_WRAP(fstat)(int fd, struct stat *buf)
{
    int ret = 0;

    DEBUG("fstat was called for fd: %d....\n", fd);

    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    /* get the file id for this file descriptor */
    int fid = unifycr_get_fid_from_fd(fd);

    if (fid < 0) {
        errno = EBADF;
        return -1;
    }

    ret = unifycr_fid_stat(fid, buf);
    if (ret < 0)
        errno = EBADF;

    return ret;
}

/*
 * NOTE on __xstat(2), __lxstat(2), and __fxstat(2)
 * The additional parameter vers shall be 3 or the behavior of these functions
 * is undefined. (ISO POSIX(2003))
 *
 * from /sys/stat.h, it seems that we need to test if vers being _STAT_VER,
 * instead of using the absolute value 3.
 */

int UNIFYCR_WRAP(__xstat)(int vers, const char *path, struct stat *buf)
{
    int ret = 0;
    int fid = -1;
    int gfid = -1;
    int found_local = 0;
    int found_global = 0;
    unifycr_file_attr_t gfattr = { 0, };


    DEBUG("xstat was called for %s....\n", path);
    if (unifycr_intercept_path(path)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }

        if (!buf) {
            errno = EFAULT;
            return -1;
        }

        gfid = unifycr_generate_gfid(path);
        fid = unifycr_get_fid_from_path(path);

        found_global =
            (unifycr_get_global_file_meta(gfid, &gfattr) == UNIFYCR_SUCCESS);
        found_local = (fid >= 0);

        if (!found_global) {
            /* the local entry is obsolete and should be discarded. */
            if (found_local)
                unifycr_fid_unlink(fid); /* this always returns success */

            errno = ENOENT;
            return -1;
        }

        *buf = gfattr.file_attr;

        return 0;
    } else {
        MAP_OR_FAIL(__xstat);
        ret = UNIFYCR_REAL(__xstat)(vers, path, buf);
        return ret;
    }
}

int UNIFYCR_WRAP(__lxstat)(int vers, const char *path, struct stat *buf)
{
    int ret = 0;
    int fid = -1;
    int gfid = -1;
    int found_local = 0;
    int found_global = 0;
    unifycr_file_attr_t gfattr = { 0, };

    DEBUG("lxstat was called for %s....\n", path);
    if (unifycr_intercept_path(path)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }
        if (!buf) {
            errno = EFAULT;
            return -1;
        }

        gfid = unifycr_generate_gfid(path);
        fid = unifycr_get_fid_from_path(path);

        found_global =
            (unifycr_get_global_file_meta(gfid, &gfattr) == UNIFYCR_SUCCESS);
        found_local = (fid >= 0);

        if (!found_global) {
            /* the local entry is obsolete and should be discarded. */
            if (found_local)
                unifycr_fid_unlink(fid); /* this always returns success */

            errno = ENOENT;
            return -1;
        }

        *buf = gfattr.file_attr;

        return 0;
    } else {
        MAP_OR_FAIL(__lxstat);
        ret = UNIFYCR_REAL(__lxstat)(vers, path, buf);
        return ret;
    }
}

int UNIFYCR_WRAP(__fxstat)(int vers, int fd, struct stat *buf)
{
    int ret = 0;

    DEBUG("fxstat was called for fd %d....\n", fd);

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }

        if (fd < 0) {
            errno = EBADF;
            return -1;
        }

        if (!buf) {
            errno = EINVAL;
            return -1;
        }

        int fid = unifycr_get_fid_from_fd(fd);

        if (fid < 0)
            return UNIFYCR_ERROR_BADF;

        ret = unifycr_fid_stat(fid, buf);
        if (ret < 0)
            errno = ENOENT;

        return ret;
    } else {
        MAP_OR_FAIL(__fxstat);
        ret = UNIFYCR_REAL(__fxstat)(vers, fd, buf);
        return ret;
    }
}


/* ---------------------------------------
 * POSIX wrappers: file descriptors
 * --------------------------------------- */

/* read count bytes info buf from file starting at offset pos,
 * returns number of bytes actually read in retcount,
 * retcount will be less than count only if an error occurs
 * or end of file is reached */
int unifycr_fd_read(int fd, off_t pos, void *buf, size_t count,
                    size_t *retcount)
{
    /* get the file id for this file descriptor */
    int fid = unifycr_get_fid_from_fd(fd);
    if (fid < 0) {
        return UNIFYCR_ERROR_BADF;
    }

    /* it's an error to read from a directory */
    if (unifycr_fid_is_dir(fid)) {
        /* TODO: note that read/pread can return this, but not fread */
        return UNIFYCR_ERROR_ISDIR;
    }

    /* check that file descriptor is open for read */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
    if (! filedesc->read) {
        return UNIFYCR_ERROR_BADF;
    }

    /* TODO: is it safe to assume that off_t is bigger than size_t? */
    /* check that we don't overflow the file length */
    if (unifycr_would_overflow_offt(pos, (off_t) count)) {
        return UNIFYCR_ERROR_OVERFLOW;
    }

    /* TODO: check that file is open for reading */

    /* check that we don't try to read past the end of the file */
    off_t lastread = pos + (off_t) count;
    off_t filesize = unifycr_fid_size(fid);
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

    /* record number of bytes that we'll actually read */
    *retcount = count;

    /* if we don't read any bytes, return success */
    if (count == 0) {
        return UNIFYCR_SUCCESS;
    }

    /* read data from file */
    int read_rc = unifycr_fid_read(fid, pos, buf, count);
    return read_rc;
}

/* write count bytes from buf into file starting at offset pos,
 * allocates new bytes and updates file size as necessary,
 * fills any gaps with zeros */
int unifycr_fd_write(int fd, off_t pos, const void *buf, size_t count)
{

    /* get the file id for this file descriptor */
    int fid = unifycr_get_fid_from_fd(fd);
    if (fid < 0) {
        return UNIFYCR_ERROR_BADF;
    }

    /* it's an error to write to a directory */
    if (unifycr_fid_is_dir(fid)) {
        return UNIFYCR_ERROR_INVAL;
    }

    /* check that file descriptor is open for write */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
    if (! filedesc->write) {
        return UNIFYCR_ERROR_BADF;
    }

    /* check that our write won't overflow the length */
    if (unifycr_would_overflow_offt(pos, (off_t) count)) {
        /* TODO: want to return EFBIG here for streams */
        return UNIFYCR_ERROR_OVERFLOW;
    }

    /* TODO: check that file is open for writing */

    /* get current file size before extending the file */
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);
    off_t filesize = meta->size;
    off_t newend = pos + (off_t)count;

    /* extend file size and allocate chunks if needed */
    int extend_rc = unifycr_fid_extend(fid, newend);
    if (extend_rc != UNIFYCR_SUCCESS) {
        return extend_rc;
    }

    /* finally write specified data to file */
    int write_rc = unifycr_fid_write(fid, pos, buf, count);
    if (write_rc == 0) {
        if (newend > filesize) {
            meta->size = newend;
        }
        if (meta->storage == FILE_STORAGE_LOGIO) {
            meta->log_size = pos + count;
        } else if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
            /* fill any new bytes between old size and pos with zero values */
            if (filesize < pos) {
                off_t gap_size = pos - filesize;
                int zero_rc = unifycr_fid_write_zero(fid, filesize, gap_size);
                if (zero_rc != UNIFYCR_SUCCESS) {
                    return zero_rc;
                }
            }
        }
    }
    return write_rc;
}

int UNIFYCR_WRAP(creat)(const char *path, mode_t mode)
{
    /* equivalent to open(path, O_WRONLY|O_CREAT|O_TRUNC, mode) */

    /* check whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* TODO: handle relative paths using current working directory */

        /* create the file */
        int fid;
        off_t pos;
        int rc = unifycr_fid_open(path, O_WRONLY | O_CREAT | O_TRUNC, mode, &fid, &pos);
        if (rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(rc);
            return -1;
        }

        /* TODO: allocate a free file descriptor and associate it with fid */
        /* set in_use flag and file pointer, flags include O_WRONLY */
        unifycr_fd_t *filedesc = &(unifycr_fds[fid]);
        filedesc->pos   = pos;
        filedesc->read  = 0;
        filedesc->write = 1;
        DEBUG("UNIFYCR_open generated fd %d for file %s\n", fid, path);

        /* don't conflict with active system fds that range from 0 - (fd_limit) */
        int ret = fid + unifycr_fd_limit;
        return ret;
    } else {
        MAP_OR_FAIL(creat);
        int ret = UNIFYCR_REAL(creat)(path, mode);
        return ret ;
    }
}

int UNIFYCR_WRAP(creat64)(const char *path, mode_t mode)
{
    /* check whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
        return -1;
    } else {
        MAP_OR_FAIL(creat64);
        int ret = UNIFYCR_REAL(creat64)(path, mode);
        return ret;
    }
}

int UNIFYCR_WRAP(open)(const char *path, int flags, ...)
{
    int ret;
    /* if O_CREAT is set, we should also have some mode flags */
    int mode = 0;
    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, int);
        va_end(arg);
    }
    /* determine whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* TODO: handle relative paths using current working directory */

        /* create the file */
        int fid;
        off_t pos;
        int rc = unifycr_fid_open(path, flags, mode, &fid, &pos);
        if (rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(rc);
            return -1;
        }

        /* TODO: allocate a free file descriptor and associate it with fid */
        /* set in_use flag and file pointer */
        unifycr_fd_t *filedesc = &(unifycr_fds[fid]);
        filedesc->pos = pos;
        filedesc->read = ((flags & O_RDONLY) == O_RDONLY)
                         || ((flags & O_RDWR) == O_RDWR);
        filedesc->write = ((flags & O_WRONLY) == O_WRONLY)
                          || ((flags & O_RDWR) == O_RDWR);
        DEBUG("UNIFYCR_open generated fd %d for file %s\n", fid, path);

        /* don't conflict with active system fds that range from 0 - (fd_limit) */
        ret = fid + unifycr_fd_limit;
        return ret;
    } else {
        MAP_OR_FAIL(open);
        if (flags & O_CREAT) {
            ret = UNIFYCR_REAL(open)(path, flags, mode);
        } else {
            ret = UNIFYCR_REAL(open)(path, flags);
        }
        return ret;
    }
}

int UNIFYCR_WRAP(open64)(const char *path, int flags, ...)
{
    int ret;

    /* if O_CREAT is set, we should also have some mode flags */
    int mode = 0;
    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, int);
        va_end(arg);
    }

    /* check whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* Call open wrapper with LARGEFILE flag set*/
        if (flags & O_CREAT) {
            ret = UNIFYCR_WRAP(open)(path, flags | O_LARGEFILE, mode);
        } else {
            ret = UNIFYCR_WRAP(open)(path, flags | O_LARGEFILE);
        }
    } else {
        MAP_OR_FAIL(open64);
        if (flags & O_CREAT) {
            ret = UNIFYCR_REAL(open64)(path, flags, mode);
        } else {
            ret = UNIFYCR_REAL(open64)(path, flags);
        }
    }

    return ret;
}

off_t UNIFYCR_WRAP(lseek)(int fd, off_t offset, int whence)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* TODO: check that fd is actually in use */

        /* get the file id for this file descriptor */
        int fid = unifycr_get_fid_from_fd(fd);

        /* check that file descriptor is good */
        unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);
        if (meta == NULL) {
            /* bad file descriptor */
            errno = EBADF;
            return (off_t) (-1);
        }

        /* get file descriptor for fd */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);

        /* get current file position */
        off_t current_pos = filedesc->pos;

        /* compute final file position */
        DEBUG("seeking from %ld\n", current_pos);
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
            current_pos = meta->size + offset;
            break;
        default:
            errno = EINVAL;
            return (off_t) (-1);
        }
        DEBUG("seeking to %ld\n", current_pos);

        /* set and return final file position */
        filedesc->pos = current_pos;
        return current_pos;
    } else {
        MAP_OR_FAIL(lseek);
        off_t ret = UNIFYCR_REAL(lseek)(fd, offset, whence);
        return ret;
    }
}

off64_t UNIFYCR_WRAP(lseek64)(int fd, off64_t offset, int whence)
{
    int tmpfd = fd;

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&tmpfd)) {
        if (sizeof(off_t) == sizeof(off64_t))
            return (off64_t) UNIFYCR_WRAP(lseek) (fd, (off_t) offset, whence);
        else {
            /* ERROR: fn not yet supported */
            fprintf(stderr, "Function not yet supported @ %s:%d\n",
                    __FILE__, __LINE__);
            errno = EBADF;
            return (off64_t) (-1);
        }
    } else {
        MAP_OR_FAIL(lseek64);
        off64_t ret = UNIFYCR_REAL(lseek64)(fd, offset, whence);
        return ret;
    }
}

int UNIFYCR_WRAP(posix_fadvise)(int fd, off_t offset, off_t len, int advice)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* check that the file descriptor is valid */
        int fid = unifycr_get_fid_from_fd(fd);
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
            fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
            break;
        default:
            /* this function returns the errno itself, not -1 */
            errno = EINVAL;
            return errno;
        }

        /* just a hint so return success even if we don't do anything */
        return 0;
    } else {
        MAP_OR_FAIL(posix_fadvise);
        int ret = UNIFYCR_REAL(posix_fadvise)(fd, offset, len, advice);
        return ret;
    }
}

ssize_t UNIFYCR_WRAP(read)(int fd, void *buf, size_t count)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fd);
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);

        if (meta == NULL || filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t) (-1);
        }

        if (filedesc->pos >= meta->size)
            return 0;   /* EOF */

        /* read data from file */
        read_req_t tmp_req;
        tmp_req.buf = buf;
        tmp_req.fid = fd;
        tmp_req.length = count;
        tmp_req.offset = (size_t) filedesc->pos;

        /*
         * this returns error code, which is zero for successful cases.
         */
        size_t retcount;
        int ret = unifycr_fd_logreadlist(&tmp_req, 1);
        if (ret)
            retcount = 0;
        else
            retcount = tmp_req.length;

        /* update position */
        filedesc->pos += (off_t) retcount;
        /* return number of bytes read */
        return (ssize_t) retcount;
    } else {
        MAP_OR_FAIL(read);
        ssize_t ret = UNIFYCR_REAL(read)(fd, buf, count);
        return ret;
    }
}

/* TODO: find right place to msync spillover mapping */
ssize_t UNIFYCR_WRAP(write)(int fd, const void *buf, size_t count)
{
    ssize_t ret;

    DEBUG("write was called for fd %d....\n", fd);

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t) (-1);
        }

        /* write data to file */
        int write_rc = unifycr_fd_write(fd, filedesc->pos, buf, count);

        if (write_rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(write_rc);
            return (ssize_t) (-1);
        }
        ret = count;

        /* update file position */
        filedesc->pos += ret;
    } else {
        MAP_OR_FAIL(write);
        ret = UNIFYCR_REAL(write)(fd, buf, count);
    }

    return ret;
}

ssize_t UNIFYCR_WRAP(readv)(int fd, const struct iovec *iov, int iovcnt)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
        errno = EBADF;
        return -1;
    } else {
        MAP_OR_FAIL(readv);
        ssize_t ret = UNIFYCR_REAL(readv)(fd, iov, iovcnt);
        return ret;
    }
}

ssize_t UNIFYCR_WRAP(writev)(int fd, const struct iovec *iov, int iovcnt)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
        errno = EBADF;
        return -1;
    } else {
        MAP_OR_FAIL(writev);
        ssize_t ret = UNIFYCR_REAL(writev)(fd, iov, iovcnt);
        return ret;
    }
}

int UNIFYCR_WRAP(lio_listio)(int mode, struct aiocb *const aiocb_list[],
                             int nitems, struct sigevent *sevp)
{
    int ret, i;
    read_req_t *read_reqs = calloc(nitems, sizeof(read_req_t));
    if (NULL == read_reqs) {
        errno = ENOMEM;
        return -1;
    }

    size_t unifycr_fd_cnt = 0;
    for (i = 0; i < nitems; i++) {
        int fd = aiocb_list[i]->aio_fildes;
        if (unifycr_intercept_fd(&fd)) {
            if ((aiocb_list[i]->aio_lio_opcode != LIO_READ) ||
                (mode != LIO_WAIT)) {
                //does not support write operation currently
                free(read_reqs);
                errno = ENOTSUP;
                return -1;
            }
            read_req_t *req = read_reqs + unifycr_fd_cnt;
            req->fid = fd;
            req->buf = (char *)aiocb_list[i]->aio_buf;
            req->length = aiocb_list[i]->aio_nbytes;
            req->offset = (size_t) aiocb_list[i]->aio_offset;
            unifycr_fd_cnt++;
        }
    }

    if (unifycr_fd_cnt > 0) {
        if (unifycr_fd_cnt < (size_t)nitems) {
            // TODO: handle subset of list that were not UnifyCR fds
        }
        ret = unifycr_fd_logreadlist(read_reqs, unifycr_fd_cnt);
    } else {
        MAP_OR_FAIL(lio_listio);
        ret = UNIFYCR_REAL(lio_listio)(mode, aiocb_list, nitems, sevp);
    }

    free(read_reqs);
    if (ret) {
        errno = EIO;
    }
    return ret;
}

int compare_index_entry(const void *a, const void *b)
{
    const unifycr_index_t *ptr_a = a;
    const unifycr_index_t *ptr_b = b;

    if (ptr_a->fid != ptr_b->fid) {
        if (ptr_a->fid < ptr_b->fid)
            return -1;
        else
            return 1;
    }

    if (ptr_a->file_pos == ptr_b->file_pos)
        return 0;
    else if (ptr_a->file_pos < ptr_b->file_pos)
        return -1;
    else
        return 1;
}

int compare_read_req(const void *a, const void *b)
{
    const read_req_t *ptr_a = a;
    const read_req_t *ptr_b = b;

    if (ptr_a->fid != ptr_b->fid) {
        if (ptr_a->fid < ptr_b->fid)
            return -1;
        else
            return 1;
    }

    if (ptr_a->offset == ptr_b->offset)
        return 0;
    else if (ptr_a->offset < ptr_b->offset)
        return -1;
    else
        return 1;
}

ssize_t unifycr_locate_req(read_req_t *read_reqs, size_t count,
                           read_req_t *match_req)
{
    if (count == 0) {
        return -1;
    } else if (count == 1) {
        return 0;
    } else if (count == 2) {
        if (compare_read_req(match_req, &read_reqs[1]) < 0) {
            return 0;
        }
        return 1;
    }

    ssize_t left = 0;
    ssize_t right = count - 1;
    ssize_t mid = count / 2;

    // simple binary search
    int cmp;
    int found = 0;
    while ((left + 1) < right) {
        cmp = compare_read_req(match_req, &read_reqs[mid]);
        if (cmp == 0)
            return mid;
        else if (cmp > 0)
            left = mid;
        else
            right = mid;
        mid = (left + right) / 2;
    }

    if (compare_read_req(match_req, &read_reqs[left]) < 0) {
        if (left == 0)
            return 0;
        else
            return left - 1;
    } else if (compare_read_req(match_req, &read_reqs[right]) < 0) {
        return right - 1;
    } else {
        return right;
    }
}

/*
 * given a read request, split it into multiple indices whose range is equal or
 * smaller than slice_range size
 * @param cur_read_req: the read request to split
 * @param slice_range: the slice size of the key-value store
 * @return rd_req_set: the set of split read requests
 * */
int unifycr_split_read_requests(read_req_t *cur_read_req,
                                read_req_set_t *rd_req_set,
                                size_t slice_range)
{
    size_t cur_read_start = cur_read_req->offset;
    size_t cur_read_end = cur_read_req->offset + cur_read_req->length - 1;

    size_t cur_slice_start = (cur_read_req->offset / slice_range) * slice_range;
    size_t cur_slice_end = cur_slice_start + slice_range - 1;

    memset(rd_req_set, 0, sizeof(read_req_set_t));
    // previous memset has side effect of: rd_req_set->count = 0;

    if (cur_read_end <= cur_slice_end) {
        /*
        cur_slice_start                                   cur_slice_end
                         cur_read_start     cur_read_end
        */
        rd_req_set->read_reqs[rd_req_set->count] = *cur_read_req;
        rd_req_set->count++;
    } else {
        /*
        cur_slice_s          cur_slice_e|next_slice_s          next_slice_e
                   cur_read_s                        cur_read_e
        */

        // debug_log_client_req("splitting current request", cur_read_req);

        // add first portion (tail of current slice)
        rd_req_set->read_reqs[rd_req_set->count] = *cur_read_req;
        rd_req_set->read_reqs[rd_req_set->count].length =
            cur_slice_end - cur_read_start + 1;
        rd_req_set->count++;

        do {
            cur_slice_start = cur_slice_end + 1;
            cur_slice_end = cur_slice_start + slice_range - 1;
            if (cur_read_end <= cur_slice_end)
                break;

            // add full slice
            rd_req_set->read_reqs[rd_req_set->count].fid = cur_read_req->fid;
            rd_req_set->read_reqs[rd_req_set->count].offset = cur_slice_start;
            rd_req_set->read_reqs[rd_req_set->count].length = slice_range;
            rd_req_set->count++;
        } while (1);

        // add last portion (head of current slice)
        rd_req_set->read_reqs[rd_req_set->count].fid = cur_read_req->fid;
        rd_req_set->read_reqs[rd_req_set->count].offset = cur_slice_start;
        rd_req_set->read_reqs[rd_req_set->count].length =
            cur_read_end - cur_slice_start + 1;
        rd_req_set->count++;
    }

    return 0;
}

/*
 * coalesce contiguous read requests and
 * split the read requests whose size is larger than
 * unifycr_key_slice_range into the ones smaller
 * than unifycr_key_slice range
 * @param read_reqs: a list of read requests
 * @param count: number of read requests
 * @param unifycr_key_slice_range: slice size of distributed
 * key-value store
 * @return rd_req_set: the coalesced read requests
 *
 * */
int unifycr_coalesce_read_reqs(read_req_t *read_reqs, size_t count,
                               size_t slice_range,
                               read_req_set_t *rd_req_set)
{
    size_t i, j;
    read_req_set_t tmp_req_set;

    rd_req_set->count = 0;

    for (i = 0; i < count; i++) {
        unifycr_split_read_requests(read_reqs+i, &tmp_req_set,
                                    slice_range);

        j = 0;
        if (rd_req_set->count > 0) {
            read_req_t *first = &(tmp_req_set.read_reqs[0]);
            read_req_t *prev = &(rd_req_set->read_reqs[rd_req_set->count - 1]);
            if (prev->fid == first->fid) {
                if ((prev->offset + prev->length) == first->offset) {
                    /* only coalesce when in same slice */
                    if ((prev->offset / slice_range)
                        == (first->offset / slice_range)) {
                        // debug_log_client_req("coalesce to", prev);
                        // debug_log_client_req("coalesce from", first);
                        prev->length += first->length;
                        j++;
                    }

                }
            }
        }

        for (; j < tmp_req_set.count; j++) {
            rd_req_set->read_reqs[rd_req_set->count] = tmp_req_set.read_reqs[j];
            rd_req_set->count++;
        }
    }

    return 0;
}

/*
 * match the received read request with the client's read requests
 * @param read_reqs: a list of read requests
 * @param count: number of read requests
 * @param match_req: received read request to match
 * @return error code
 *
 * */

int unifycr_match_received_ack(read_req_t *read_reqs, size_t count,
                               read_req_t *match_req)
{
    read_req_t match_start = *match_req;
    read_req_t match_end = *match_req;
    match_end.offset += match_end.length - 1;

    ssize_t first_pos = unifycr_locate_req(read_reqs, count, &match_start);
    ssize_t last_pos = unifycr_locate_req(read_reqs, count, &match_end);

    if (first_pos == -1)
        return -1;

    /* NOTE: 's' is start of match_req, 'e' is end of match_req */
    if (first_pos == 0) {
        if (compare_read_req(&match_start, &read_reqs[0]) < 0) {
            // match starts before first client request
            /*
             *               ************       ***********
             *       s
             * */
            return -1;
        }
    }

    read_req_t first_req_start, first_req_end;

    first_req_start = read_reqs[first_pos];
    first_req_end = read_reqs[first_pos];
    first_req_end.offset += first_req_end.length - 1;

    if (compare_read_req(&match_start, &first_req_start) >= 0 &&
        compare_read_req(&match_end, &first_req_end) <= 0) {
        // match is fully within first client request
        /*              req_s       req_e
         *              *****************           *************
         *                     s  e
         * */
        off_t copy_offset = match_start.offset - first_req_start.offset;
        memcpy(first_req_start.buf + copy_offset, match_req->buf,
               match_req->length);
        return 0;
    }

    read_req_t last_req_start, last_req_end;

    last_req_start = read_reqs[last_pos];
    last_req_end = read_reqs[last_pos];
    last_req_end.offset += last_req_end.length - 1;

    if (compare_read_req(&match_start, &first_req_start) >= 0 &&
        compare_read_req(&match_end, &last_req_end) <= 0) {
        // match starts after first client req and ends before last client req

        size_t i;
        for (i = first_pos + 1; i <= last_pos; i++) {
            if ((read_reqs[i - 1].offset + read_reqs[i - 1].length)
                != read_reqs[i].offset) {
                /* TODO: noncontiguous read requests not yet supported */
                return -1;
            }
        }

        /* fill covered portion of first client req */
        off_t copy_offset = match_start.offset - first_req_start.offset;
        size_t len = (size_t)(first_req_end.offset - match_start.offset + 1);
        memcpy(first_req_start.buf + copy_offset, match_req->buf, len);

        /* fill all req buffers in the middle */
        for (i = first_pos + 1; i < last_pos; i++) {
            memcpy(read_reqs[i].buf, match_req->buf + len, read_reqs[i].length);
            len += read_reqs[i].length;
        }

        /* fill covered portion of last client req */
        len = (size_t)(match_end.offset - last_req_start.offset + 1);
        memcpy(last_req_start.buf, match_req->buf, len);

        return 0;
    }

    /* TODO: unhandled case */
    return -1;
}

/*
 * get data for a list of read requests from the delegator
 * @param read_req: a list of read requests (with internal fids)
 * @param count: number of read requests
 * @return error code
 *
 * */
int unifycr_fd_logreadlist(read_req_t *read_reqs, size_t count)
{
    size_t i;
    size_t tot_sz = 0;
    int rc;

#if 0 // TODO - revisit when we have a valid global view of file size
    /*
     * Adjust length for fitting the EOF.
     */
    for (i = 0; i < count; i++) {
        read_req_t *req = read_reqs + i;
        unifycr_filemeta_t *meta = NULL;
        meta = unifycr_get_meta_from_fid(req->fid);
        if (NULL == meta)
            return -1;

        /* TODO - The following is broken, local meta doesn't
         * have correct size to compute EOF */
        if ((req->offset + req->length) > meta->size) {
            req->length = meta->size - req->offset;
            assert((req->length > 0) && (req->length <= meta->size));
        }
    }
#endif

    /*
     * Todo: When the number of read requests exceed the
     * request buffer, split list io into multiple bulk
     * sends and transfer in bulks
     * */

    /*convert local fid to global fid*/
    unifycr_file_attr_t tmp_meta_entry;
    unifycr_file_attr_t *ptr_meta_entry;
    for (i = 0; i < count; i++) {
        tmp_meta_entry.fid = read_reqs[i].fid;

        ptr_meta_entry = (unifycr_file_attr_t *)
            bsearch(&tmp_meta_entry, unifycr_fattrs.meta_entry,
                    *unifycr_fattrs.ptr_num_entries,
                    sizeof(unifycr_file_attr_t), compare_fattr);
        if (ptr_meta_entry != NULL) {
            read_reqs[i].fid = ptr_meta_entry->gfid;
        }
    }

    /*coalesce the contiguous read requests*/
    read_req_set_t read_req_set;
    memset(&read_req_set, 0, sizeof(read_req_set_t));
    qsort(read_reqs, count, sizeof(read_req_t), compare_read_req);
    unifycr_coalesce_read_reqs(read_reqs, count, unifycr_key_slice_range,
                               &read_req_set);

    shm_hdr_t *req_hdr = (shm_hdr_t *) shm_req_buf;
    req_hdr->cnt = read_req_set.count;
    req_hdr->sz = (req_hdr->cnt * sizeof(shm_meta_t));

    shm_meta_t *meta_req_base = (shm_meta_t *)
        (shm_req_buf + sizeof(shm_hdr_t));

    for (i = 0; i < read_req_set.count; i++) {
        shm_meta_t *meta_req = meta_req_base + i;
        size_t len = read_req_set.read_reqs[i].length;
        meta_req->src_fid = read_req_set.read_reqs[i].fid;
        meta_req->offset = read_req_set.read_reqs[i].offset;
        meta_req->length = len;
        tot_sz += len;
    }

    shm_hdr_t *recv_hdr = (shm_hdr_t *) shm_recv_buf;
    recv_hdr->sz = 0;
    recv_hdr->cnt = 0;

    int cmd = COMM_READ;

    memset(cmd_buf, 0, sizeof(cmd_buf));
    memcpy(cmd_buf, &cmd, sizeof(cmd));
    memcpy(cmd_buf + sizeof(cmd), &(read_req_set.count),
           sizeof(read_req_set.count));

    __real_write(cmd_fd.fd, cmd_buf, sizeof(cmd_buf));

    while (tot_sz > 0) {
        cmd_fd.events = POLLIN | POLLPRI;
        cmd_fd.revents = 0;

        rc = poll(&cmd_fd, 1, -1);
        if (rc > 0) {
            if (cmd_fd.revents == POLLIN) {
                ssize_t rret = __real_read(cmd_fd.fd, cmd_buf, sizeof(cmd_buf));
                if (rret < 0)
                    return UNIFYCR_ERROR_READ;
                else if (rret == 0) /* domain socket was closed */
                    return UNIFYCR_ERROR_SOCK_DISCONNECT;

                /* QUESTION: do we need to check contents of cmd_buf? */

                int sh_cursor = sizeof(shm_hdr_t);
                shm_meta_t *shm_req;
                size_t j;
                size_t cnt = recv_hdr->cnt; /* must cache, modified in loop */
                int error_cnt = 0;
                for (j = 0; j < cnt; j++) {
                    shm_req = (shm_meta_t *)(shm_recv_buf + sh_cursor);
                    sh_cursor += sizeof(shm_meta_t);

                    size_t len = shm_req->length;

                    read_req_t rreq;
                    rreq.fid = shm_req->src_fid;
                    rreq.offset = shm_req->offset;
                    rreq.length = len;
                    rreq.buf = shm_recv_buf + sh_cursor;

                    rc = unifycr_match_received_ack(read_reqs, count, &rreq);
                    if (rc == 0) {
                        sh_cursor += len;
                        tot_sz -= len;
                        recv_hdr->sz -= (sizeof(shm_meta_t) + len);
                        recv_hdr->cnt--;
                    } else {
                        /* no match, update error count and continue */
                        error_cnt++;
                    }
                }
                if (error_cnt > 0) {
                    /* TODO: Exception handling when requests are missed */
                    return -1;
                }
            }
        }
    }

    return (int)UNIFYCR_SUCCESS;
}

ssize_t UNIFYCR_WRAP(pread)(int fd, void *buf, size_t count, off_t offset)
{
    /* equivalent to read(), except that it shall read from a given
     * position in the file without changing the file pointer */

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fd);
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);

        if (meta == NULL || filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t) (-1);
        }

        if (offset >= meta->size)
            return 0;

        size_t retcount;
        read_req_t tmp_req;

        tmp_req.buf = buf;
        tmp_req.fid = fd;
        tmp_req.length = count;
        tmp_req.offset = offset;

        int ret = unifycr_fd_logreadlist(&tmp_req, 1);

        if (ret)
            retcount = 0;
        else
            retcount = tmp_req.length;

        /* return number of bytes read */
        return (ssize_t) retcount;
    } else {
        MAP_OR_FAIL(pread);
        ssize_t ret = UNIFYCR_REAL(pread)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYCR_WRAP(pread64)(int fd, void *buf, size_t count, off64_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
        errno = EBADF;
        return -1;
    } else {
        MAP_OR_FAIL(pread64);
        ssize_t ret = UNIFYCR_REAL(pread64)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYCR_WRAP(pwrite)(int fd, const void *buf, size_t count,
                             off_t offset)
{
    /* equivalent to write(), except that it writes into a given
     * position without changing the file pointer */
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t) (-1);
        }

        /* write data to file */
        int write_rc = unifycr_fd_write(fd, offset, buf, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(write_rc);
            return (ssize_t) (-1);
        }

        /* return number of bytes read */
        return (ssize_t) count;
    } else {
        MAP_OR_FAIL(pwrite);
        ssize_t ret = UNIFYCR_REAL(pwrite)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYCR_WRAP(pwrite64)(int fd, const void *buf, size_t count,
                               off64_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* ERROR: fn not yet supported */
        return -1;
    } else {
        MAP_OR_FAIL(pwrite64);
        ssize_t ret = UNIFYCR_REAL(pwrite64)(fd, buf, count, offset);
        return ret;
    }
}

int UNIFYCR_WRAP(ftruncate)(int fd, off_t length)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get the file id for this file descriptor */
        int fid = unifycr_get_fid_from_fd(fd);
        if (fid < 0) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return -1;
        }

        /* check that file descriptor is open for write */
        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
        if (! filedesc->write) {
            errno = EBADF;
            return -1;
        }

        /* truncate the file */
        int rc = unifycr_fid_truncate(fid, length);
        if (rc != UNIFYCR_SUCCESS) {
            errno = EIO;
            return -1;
        }

        return 0;
    } else {
        MAP_OR_FAIL(ftruncate);
        int ret = UNIFYCR_REAL(ftruncate)(fd, length);
        return ret;
    }
}

int UNIFYCR_WRAP(fsync)(int fd)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get the file id for this file descriptor */
        int fid = unifycr_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return -1;
        }

        if (unifycr_use_spillover) {
            int ret = __real_fsync(unifycr_spilloverblock);
            if (ret != 0) {
                return errno;
            }
        }

        /*put indices to key-value store*/
        int cmd = COMM_META_FSYNC;
        int res = -1;

        memset(cmd_buf, 0, sizeof(cmd_buf));
        memcpy(cmd_buf, &cmd, sizeof(int));

        res = __real_write(client_sockfd, cmd_buf, sizeof(cmd_buf));

        if (res != 0) {
            int rc;

            cmd_fd.events = POLLIN | POLLPRI;
            cmd_fd.revents = 0;

            rc = poll(&cmd_fd, 1, -1);
            if (rc == 0) {
                /*time out event*/
            } else if (rc > 0) {
                if (cmd_fd.revents != 0) {
                    if (cmd_fd.revents == POLLIN) {
                        int bytes_read = 0;

                        bytes_read = __real_read(client_sockfd,
                                cmd_buf, sizeof(cmd_buf));
                        if (bytes_read == 0) {
                            return -1;
                        } else {
                            /**/
                            if (*((int *)cmd_buf) != COMM_META_FSYNC ||
                                    *((int *)cmd_buf + 1) != ACK_SUCCESS) {
                                return -1;
                            } else {

                            }
                        }
                    } else {
                        return -1;
                    }
                }

            } else {
                return -1;
            }
        }
        /* TODO: if using spill over we may have some fsyncing to do */

        /* nothing to do in our case */
        return 0;
    } else {
        MAP_OR_FAIL(fsync);
        int ret = UNIFYCR_REAL(fsync)(fd);
        return ret;
    }
}

int UNIFYCR_WRAP(fdatasync)(int fd)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
        errno = EBADF;
        return -1;
    } else {
        MAP_OR_FAIL(fdatasync);
        int ret = UNIFYCR_REAL(fdatasync)(fd);
        return ret;
    }
}

int UNIFYCR_WRAP(flock)(int fd, int operation)
{
    int ret;

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        // KMM I removed the locking code because it was causing
        // hangs
        /*
          -- currently handling the blocking variants only
          switch (operation)
          {
              case LOCK_EX:
                  DEBUG("locking file %d..\n", fid);
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
                  DEBUG("unlocking file %d..\n", fid);
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
        ret = UNIFYCR_REAL(flock)(fd, operation);
        return ret;
    }
}

/* TODO: handle different flags */
void *UNIFYCR_WRAP(mmap)(void *addr, size_t length, int prot, int flags,
                         int fd, off_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get the file id for this file descriptor */
        int fid = unifycr_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return MAP_FAILED;
        }

        /* TODO: handle addr properly based on flags */

        /* allocate memory required to mmap the data if addr is NULL;
         * using posix_memalign instead of malloc to align mmap'ed area
         * to page size */
        if (! addr) {
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
        off_t file_size = unifycr_fid_size(fid);
        if (last_byte > file_size) {
            /* trying to copy past the end of the file, so
             * adjust the total amount to be copied */
            length = (size_t)(file_size - offset);
        }

        /* read data from file */
        int rc = unifycr_fid_read(fid, offset, addr, length);
        if (rc != UNIFYCR_SUCCESS) {
            /* TODO: need to free memory in this case? */
            errno = ENOMEM;
            return MAP_FAILED;
        }

        return addr;
    } else {
        MAP_OR_FAIL(mmap);
        void *ret = UNIFYCR_REAL(mmap)(addr, length, prot, flags, fd, offset);
        return ret;
    }
}

int UNIFYCR_WRAP(munmap)(void *addr, size_t length)
{
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = ENOSYS;
    return ENODEV;
}

int UNIFYCR_WRAP(msync)(void *addr, size_t length, int flags)
{
    /* TODO: need to keep track of all the mmaps that are linked to
     * a given file before this function can be implemented*/
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = ENOSYS;
    return ENOMEM;
}

void *UNIFYCR_WRAP(mmap64)(void *addr, size_t length, int prot, int flags,
                           int fd, off64_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* ERROR: fn not yet supported */
        fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
        errno = ENOSYS;
        return MAP_FAILED;
    } else {
        MAP_OR_FAIL(mmap64);
        void *ret = UNIFYCR_REAL(mmap64)(addr, length, prot, flags, fd, offset);
        return ret;
    }
}

int UNIFYCR_WRAP(close)(int fd)
{
    int user_fd = fd;

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        DEBUG("closing fd %d\n", fd);

        /* TODO: what to do if underlying file has been deleted? */

        /* check that fd is actually in use */
        int fid = unifycr_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return -1;
        }

        unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc->write)
            fsync(user_fd);

        /* close the file id */
        int close_rc = unifycr_fid_close(fid);
        if (close_rc != UNIFYCR_SUCCESS) {
            errno = EIO;
            return -1;
        }

        /* TODO: free file descriptor */

        return 0;
    } else {
        MAP_OR_FAIL(close);
        int ret = UNIFYCR_REAL(close)(fd);
        return ret;
    }
}
