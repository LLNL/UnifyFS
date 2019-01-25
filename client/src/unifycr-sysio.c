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
#include "unifycr-internal.h"
#include "unifycr_client.h"
#include "ucr_read_builder.h"

/* -------------------
 * define external variables
 * --------------------*/

extern int unifycr_spilloverblock;
extern int unifycr_use_spillover;

/* ---------------------------------------
 * POSIX wrappers: paths
 * --------------------------------------- */

int UNIFYCR_WRAP(access)(const char* path, int mode)
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

int UNIFYCR_WRAP(mkdir)(const char* path, mode_t mode)
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
        int ret = unifycr_fid_create_directory(path);
        if (ret != UNIFYCR_SUCCESS) {
            /* failed to create the directory,
             * set errno and return */
            errno = unifycr_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(mkdir);
        int ret = UNIFYCR_REAL(mkdir)(path, mode);
        return ret;
    }
}

int UNIFYCR_WRAP(rmdir)(const char* path)
{
    /* determine whether we should intercept this path */
    if (unifycr_intercept_path(path)) {
        /* check if the mount point itself is being deleted */
        if (!strcmp(path, unifycr_mount_prefix)) {
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
        if (!unifycr_fid_is_dir(fid)) {
            errno = ENOTDIR;
            return -1;
        }

        /* is it empty? */
        if (!unifycr_fid_is_dir_empty(path)) {
            errno = ENOTEMPTY;
            return -1;
        }

        /* remove the directory from the file list */
        int ret = unifycr_fid_unlink(fid);
        if (ret != UNIFYCR_SUCCESS) {
            /* failed to remove the directory,
             * set errno and return */
            errno = unifycr_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(rmdir);
        int ret = UNIFYCR_REAL(rmdir)(path);
        return ret;
    }
}

int UNIFYCR_WRAP(rename)(const char* oldpath, const char* newpath)
{
    /* TODO: allow oldpath / newpath to split across memfs and normal
     * linux fs, which means we'll need to do a read / write */

    /* check whether the old path is in our file system */
    if (unifycr_intercept_path(oldpath)) {
        /* for now, we can only rename within our file system */
        if (!unifycr_intercept_path(newpath)) {
            /* ERROR: can't yet rename across file systems */
            errno = EXDEV;
            return -1;
        }

        /* verify that we really have a file by the old name */
        int fid = unifycr_get_fid_from_path(oldpath);
        if (fid < 0) {
            /* ERROR: oldname does not exist */
            DEBUG("Couldn't find entry for %s in UNIFYCR\n", oldpath);
            errno = ENOENT;
            return -1;
        }
        DEBUG("orig file in position %d\n", fid);

        /* check that new name is within bounds */
        size_t newpathlen = strlen(newpath) + 1;
        if (newpathlen > UNIFYCR_MAX_FILENAME) {
            errno = ENAMETOOLONG;
            return -1;
        }

        /* TODO: rename should replace existing file atomically */

        /* verify that we don't already have a file by the new name */
        int newfid = unifycr_get_fid_from_path(newpath);
        if (newfid >= 0) {
            /* something exists in newpath, need to delete it */
            int ret = UNIFYCR_WRAP(unlink)(newpath);
            if (ret == -1) {
                /* failed to unlink */
                errno = EBUSY;
                return -1;
            }
        }

        /* finally overwrite the old name with the new name */
        DEBUG("Changing %s to %s\n",
              (char*)&unifycr_filelist[fid].filename, newpath);
        strcpy((void*)&unifycr_filelist[fid].filename, newpath);

        /* success */
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

int UNIFYCR_WRAP(truncate)(const char* path, off_t length)
{
    /* determine whether we should intercept this path or not */
    if (unifycr_intercept_path(path)) {
        /* lookup the fid for the path */
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

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(truncate);
        int ret = UNIFYCR_REAL(truncate)(path, length);
        return ret;
    }
}

int UNIFYCR_WRAP(unlink)(const char* path)
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
        int ret = unifycr_fid_unlink(fid);
        if (ret != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(unlink);
        int ret = UNIFYCR_REAL(unlink)(path);
        return ret;
    }
}

int UNIFYCR_WRAP(remove)(const char* path)
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
        int ret = unifycr_fid_unlink(fid);
        if (ret != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(ret);
            return -1;
        }

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(remove);
        int ret = UNIFYCR_REAL(remove)(path);
        return ret;
    }
}

int UNIFYCR_WRAP(stat)(const char* path, struct stat* buf)
{

    DEBUG("stat was called for %s....\n", path);

    if (unifycr_intercept_path(path)) {
        /* check that caller gave us a buffer to write to */
        if (!buf) {
            errno = EFAULT;
            return -1;
        }

        /* look for local file id for path */
        int fid = unifycr_get_fid_from_path(path);
        int found_local = (fid >= 0);

        /* get global file id for path */
        int gfid = unifycr_generate_gfid(path);

        /* look for stat struct on global file */
        unifycr_file_attr_t gfattr = { 0, };
        int ret = unifycr_get_global_file_meta(fid, gfid, &gfattr);
        int found_global = (ret == UNIFYCR_SUCCESS);

        if (!found_global) {
            /* the local entry is obsolete and should be discarded. */
            if (found_local) {
                /* delete local file */
                ret = unifycr_fid_unlink(fid);
                if (ret != UNIFYCR_SUCCESS) {
                    /* failed to delete local copy */
                    errno = unifycr_err_map_to_errno(ret);
                    return -1;
                }
            }

            /* no stat structure exists for global file */
            errno = ENOENT;
            return -1;
        }

        /* copy stat structure to user's buffer */
        *buf = gfattr.file_attr;

        /* success */
        return 0;
    } else {
        MAP_OR_FAIL(stat);
        int ret = UNIFYCR_REAL(stat)(path, buf);
        return ret;
    }
}

int UNIFYCR_WRAP(fstat)(int fd, struct stat* buf)
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
    if (ret < 0) {
        errno = EBADF;
    }

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

int UNIFYCR_WRAP(__xstat)(int vers, const char* path, struct stat* buf)
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
            (unifycr_get_global_file_meta(fid, gfid, &gfattr) == UNIFYCR_SUCCESS);
        found_local = (fid >= 0);

        if (!found_global) {
            /* the local entry is obsolete and should be discarded. */
            if (found_local) {
                unifycr_fid_unlink(fid);    /* this always returns success */
            }

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

int UNIFYCR_WRAP(__lxstat)(int vers, const char* path, struct stat* buf)
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
            (unifycr_get_global_file_meta(fid, gfid, &gfattr) == UNIFYCR_SUCCESS);
        found_local = (fid >= 0);

        if (!found_global) {
            /* the local entry is obsolete and should be discarded. */
            if (found_local) {
                unifycr_fid_unlink(fid);    /* this always returns success */
            }

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

int UNIFYCR_WRAP(__fxstat)(int vers, int fd, struct stat* buf)
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

        if (fid < 0) {
            return UNIFYCR_ERROR_BADF;
        }

        ret = unifycr_fid_stat(fid, buf);
        if (ret < 0) {
            errno = ENOENT;
        }

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
int unifycr_fd_read(int fd, off_t pos, void* buf, size_t count,
                    size_t* retcount)
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
    unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
    if (!filedesc->read) {
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
int unifycr_fd_write(int fd, off_t pos, const void* buf, size_t count)
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
    unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
    if (!filedesc->write) {
        return UNIFYCR_ERROR_BADF;
    }

    /* TODO: is it safe to assume that off_t is bigger than size_t? */
    /* check that our write won't overflow the length */
    if (unifycr_would_overflow_offt(pos, (off_t) count)) {
        /* TODO: want to return EFBIG here for streams */
        return UNIFYCR_ERROR_OVERFLOW;
    }

    /* TODO: check that file is open for writing */

    /* get current file size before extending the file */
    off_t filesize = unifycr_fid_size(fid);

    /* compute new position based on storage type */
    off_t newpos;
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
    if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
        /* extend file size and allocate chunks if needed */
        newpos = pos + (off_t) count;
        int extend_rc = unifycr_fid_extend(fid, newpos);
        if (extend_rc != UNIFYCR_SUCCESS) {
            return extend_rc;
        }

        /* fill any new bytes between old size and pos with zero values */
        if (filesize < pos) {
            off_t gap_size = pos - filesize;
            int zero_rc = unifycr_fid_write_zero(fid, filesize, gap_size);
            if (zero_rc != UNIFYCR_SUCCESS) {
                return zero_rc;
            }
        }
    } else if (meta->storage == FILE_STORAGE_LOGIO) {
        newpos = filesize + (off_t)count;
        int extend_rc = unifycr_fid_extend(fid, newpos);
        if (extend_rc != UNIFYCR_SUCCESS) {
            return extend_rc;
        }
    } else {
        return UNIFYCR_ERROR_IO;
    }

    /* finally write specified data to file */
    int write_rc = unifycr_fid_write(fid, pos, buf, count);

    if (meta->storage == FILE_STORAGE_LOGIO) {
        unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
        if (write_rc == 0) {
            meta->size = newpos;
            meta->log_size = pos + count;
        }
    }
    return write_rc;
}

int UNIFYCR_WRAP(creat)(const char* path, mode_t mode)
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

        /* allocate a free file descriptor value */
        int fd = unifycr_stack_pop(unifycr_fd_stack);
        if (fd < 0) {
            /* ran out of file descriptors */
            errno = EMFILE;
            return -1;
        }

        /* set file id and file pointer, flags include O_WRONLY */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        filedesc->fid   = fid;
        filedesc->pos   = pos;
        filedesc->read  = 0;
        filedesc->write = 1;
        DEBUG("UNIFYCR_open generated fd %d for file %s\n", fd, path);

        /* don't conflict with active system fds that range from 0 - (fd_limit) */
        int ret = fd + unifycr_fd_limit;
        return ret;
    } else {
        MAP_OR_FAIL(creat);
        int ret = UNIFYCR_REAL(creat)(path, mode);
        return ret ;
    }
}

int UNIFYCR_WRAP(creat64)(const char* path, mode_t mode)
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

int UNIFYCR_WRAP(open)(const char* path, int flags, ...)
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

        /* allocate a free file descriptor value */
        int fd = unifycr_stack_pop(unifycr_fd_stack);
        if (fd < 0) {
            /* ran out of file descriptors */
            errno = EMFILE;
            return -1;
        }

        /* set file id and file pointer */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        filedesc->fid   = fid;
        filedesc->pos   = pos;
        filedesc->read  = ((flags & O_RDONLY) == O_RDONLY)
                          || ((flags & O_RDWR) == O_RDWR);
        filedesc->write = ((flags & O_WRONLY) == O_WRONLY)
                          || ((flags & O_RDWR) == O_RDWR);
        DEBUG("UNIFYCR_open generated fd %d for file %s\n", fd, path);

        /* don't conflict with active system fds that range from 0 - (fd_limit) */
        ret = fd + unifycr_fd_limit;
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

int UNIFYCR_WRAP(open64)(const char* path, int flags, ...)
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

int UNIFYCR_WRAP(__open_2)(const char* path, int flags, ...)
{
    int ret;

    DEBUG("__open_2 was called for path %s....\n", path);

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
        DEBUG("__open_2 was intercepted for path %s....\n", path);

        /* Call open wrapper */
        if (flags & O_CREAT) {
            ret = UNIFYCR_WRAP(open)(path, flags, mode);
        } else {
            ret = UNIFYCR_WRAP(open)(path, flags);
        }
    } else {
        MAP_OR_FAIL(open);
        if (flags & O_CREAT) {
            ret = UNIFYCR_REAL(open)(path, flags, mode);
        } else {
            ret = UNIFYCR_REAL(open)(path, flags);
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
        if (fid < 0) {
            /* bad file descriptor */
            errno = EBADF;
            return (off_t)(-1);
        }

        /* lookup meta to get file size */
        unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
        if (meta == NULL) {
            /* bad file descriptor */
            errno = EBADF;
            return (off_t)(-1);
        }

        /* get file descriptor for fd */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);

        /* get current file position */
        off_t current_pos = filedesc->pos;

        /* TODO: support SEEK_DATA and SEEK_HOLE? */

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
            return (off_t)(-1);
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
    /* check whether we should intercept this file descriptor */
    int origfd = fd;
    if (unifycr_intercept_fd(&fd)) {
        if (sizeof(off_t) == sizeof(off64_t)) {
            /* off_t and off64_t are the same size,
             * delegate to lseek warpper */
            off64_t ret = (off64_t)UNIFYCR_WRAP(lseek)(
                              origfd, (off_t) offset, whence);
            return ret;
        } else {
            /* ERROR: fn not yet supported */
            fprintf(stderr, "Function not yet supported @ %s:%d\n",
                    __FILE__, __LINE__);
            errno = EBADF;
            return (off64_t)(-1);
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

ssize_t UNIFYCR_WRAP(read)(int fd, void* buf, size_t count)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get file id */
        int fid = unifycr_get_fid_from_fd(fd);
        if (fid < 0) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* get pointer to file descriptor structure */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
        if (meta == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* check for end of file */
        if (filedesc->pos >= meta->size) {
            return 0;   /* EOF */
        }

        /* assume we'll succeed in read */
        size_t retcount = count;

        read_req_t tmp_req;
        tmp_req.fid     = fid;
        tmp_req.offset  = filedesc->pos;
        tmp_req.length  = count;
        tmp_req.errcode = UNIFYCR_SUCCESS;
        tmp_req.buf     = buf;

        /*
         * this returns error code, which is zero for successful cases.
         */
        int ret = unifycr_fd_logreadlist(&tmp_req, 1);

        if (ret != UNIFYCR_SUCCESS) {
            /* error reading data */
            errno = EIO;
            retcount = -1;
        } else if (tmp_req.errcode != UNIFYCR_SUCCESS) {
            /* error reading data */
            errno = EIO;
            retcount = -1;
        } else {
            /* success, update position */
            filedesc->pos += (off_t) retcount;
        }

        /* return number of bytes read */
        return (ssize_t) retcount;
    } else {
        MAP_OR_FAIL(read);
        ssize_t ret = UNIFYCR_REAL(read)(fd, buf, count);
        return ret;
    }
}

/* TODO: find right place to msync spillover mapping */
ssize_t UNIFYCR_WRAP(write)(int fd, const void* buf, size_t count)
{
    ssize_t ret;

    DEBUG("write was called for fd %d....\n", fd);

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* write data to file */
        int write_rc = unifycr_fd_write(fd, filedesc->pos, buf, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(write_rc);
            return (ssize_t)(-1);
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

ssize_t UNIFYCR_WRAP(readv)(int fd, const struct iovec* iov, int iovcnt)
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

ssize_t UNIFYCR_WRAP(writev)(int fd, const struct iovec* iov, int iovcnt)
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

int UNIFYCR_WRAP(lio_listio)(int mode, struct aiocb* const aiocb_list[],
                             int nitems, struct sigevent* sevp)
{
    /* TODO: missing pass through for non-intercepted lio_listio */

    /* TODO: require all elements in list to refer to unify files? */

    read_req_t* reqs = malloc(nitems * sizeof(read_req_t));

    int i;
    for (i = 0; i < nitems; i++) {
        if (aiocb_list[i]->aio_lio_opcode != LIO_READ) {
            //does not support write operation currently
            free(reqs);
            errno = EIO;
            return -1;
        }

        int fd = aiocb_list[i]->aio_fildes;
        if (unifycr_intercept_fd(&fd)) {
            /* get local file id for this request */
            int fid = unifycr_get_fid_from_fd(fd);
            if (fid < 0) {
                /* TODO: need to set error code on each element */
                errno = EIO;
                free(reqs);
                return -1;
            }

            reqs[i].fid     = fid;
            reqs[i].offset  = aiocb_list[i]->aio_offset;
            reqs[i].length  = aiocb_list[i]->aio_nbytes;
            reqs[i].errcode = UNIFYCR_SUCCESS;
            reqs[i].buf     = (char*)aiocb_list[i]->aio_buf;
        }
    }

    int ret = unifycr_fd_logreadlist(reqs, nitems);

    for (i = 0; i < nitems; i++) {
        /* TODO: update fields to record error status
         * see /usr/include/aio.h,
         * update __error_code and __return_val fields? */
    }

    free(reqs);

    return ret;
}

/* order by file id then by file position */
static int compare_index_entry(const void* a, const void* b)
{
    const unifycr_index_t* ptr_a = a;
    const unifycr_index_t* ptr_b = b;

    if (ptr_a->fid > ptr_b->fid) {
        return 1;
    }

    if (ptr_a->fid < ptr_b->fid) {
        return -1;
    }

    if (ptr_a->file_pos > ptr_b->file_pos) {
        return 1;
    }

    if (ptr_a->file_pos < ptr_b->file_pos) {
        return -1;
    }

    return 0;
}

/* order by file id then by offset */
static int compare_read_req(const void* a, const void* b)
{
    const read_req_t* ptr_a = a;
    const read_req_t* ptr_b = b;

    if (ptr_a->fid > ptr_b->fid) {
        return 1;
    }

    if (ptr_a->fid < ptr_b->fid) {
        return -1;
    }

    if (ptr_a->offset > ptr_b->offset) {
        return 1;
    }

    if (ptr_a->offset < ptr_b->offset) {
        return -1;
    }

    return 0;
}

/* returns index into read_req of item whose offset is
 * just below offset of target item (if one exists) */
static int unifycr_locate_req(read_req_t* read_req, int count,
                              read_req_t* match_req)
{
    /* if list is empty, indicate that there is valid starting request */
    if (count == 0) {
        return -1;
    }

    /* if we only have one item, return its index */
    if (count == 1) {
        return 0;
    }

    /* if we have two items, return index to item that must come before */
    if (count == 2) {
        if (compare_read_req(match_req, &read_req[1]) < 0) {
            /* second item is clearly bigger, so try first */
            return 0;
        }

        /* second item is less than or equal to target */
        return 1;
    }

    /* execute binary search comparing target to list of requests */

    int left  = 0;
    int right = count - 1;
    int mid   = (left + right) / 2;

    /* iterate until we find an exact match or have cut the list
     * to just two items */
    int found = 0;
    while (left + 1 < right) {
        /* bail out if we find an exact match */
        if (compare_read_req(match_req, &read_req[mid]) == 0) {
            /* found exact match */
            found = 1;
            break;
        }

        /* if target if bigger than mid, set left bound to mid */
        if (compare_read_req(match_req, &read_req[mid]) > 0) {
            left = mid;
        }

        /* if target is smaller than mid, set right bounds to mid */
        if (compare_read_req(match_req, &read_req[mid]) < 0) {
            right = mid;
        }

        /* update middle index */
        mid = (left + right) / 2;
    }

    /* return index of exact match if found one */
    if (found == 1) {
        return mid;
    }

    /* got two items, let's pick one */
    if (compare_read_req(match_req, &read_req[left]) < 0) {
        /* target is smaller than left item,
         * return index to left of left item if we can */
        if (left == 0) {
            /* at left most item, so return this index */
            return 0;
        }
        return left - 1;
    } else {
        if (compare_read_req(match_req, &read_req[right]) < 0) {
            /* target is smaller than right item,
             * return index of item one less than right */
            return right - 1;
        } else {
            /* target is greater or equal to right item */
            return right;
        }
    }
}

/*
 * given an read request, split it into multiple indices whose range
 * is equal or smaller than slice_range size
 * @param cur_read_req: the read request to split
 * @param slice_range: the slice size of the key-value store
 * @return out_set: the set of split read requests
 * */
static int unifycr_split_read_requests(read_req_t* req,
                                       read_req_set_t* out_set,
                                       long slice_range)
{
    /* compute offset of first and last byte in request */
    long req_start = req->offset;
    long req_end   = req->offset + req->length - 1;

    /* compute offset of first and lasy byte of slice
     * that contains first byte of request */
    long slice_start = (req->offset / slice_range) * slice_range;
    long slice_end   = slice_start + slice_range - 1;

    /* initialize request count in output set */
    int count = 0;

    if (req_end <= slice_end) {
        /* slice fully contains request
         *
         * slice_start                         slice_end
         *                req_start     req_end
         *
         */
        out_set->read_reqs[count] = *req;
        count++;
    } else {
        /* read request spans multiple slices
         *
         * slice_start       slice_end  next_slice_start      next_slice_end
         *           req_start                          req_end
         *
         */

        /* account for leading bytes in read request in first slice */
        out_set->read_reqs[count] = *req;
        out_set->read_reqs[count].length = slice_end - req_start + 1;
        count++;

        /* advance to next slice */
        slice_start = slice_end + 1;
        slice_end   = slice_start + slice_range - 1;

        /* account for all middle slices */
        while (1) {
            if (req_end <= slice_end) {
                /* found the slice that contains end byte in read request */
                break;
            }

            /* full slice is contained in read request */
            out_set->read_reqs[count].fid     = req->fid;
            out_set->read_reqs[count].offset  = slice_start;
            out_set->read_reqs[count].length  = slice_range;
            out_set->read_reqs[count].errcode = UNIFYCR_SUCCESS;
            count++;

            /* advance to next slice */
            slice_start = slice_end + 1;
            slice_end   = slice_start + slice_range - 1;
        }

        /* account for bytes in final slice */
        out_set->read_reqs[count].fid     = req->fid;
        out_set->read_reqs[count].offset  = slice_start;
        out_set->read_reqs[count].length  = req_end - slice_start + 1;
        out_set->read_reqs[count].errcode = UNIFYCR_SUCCESS;
        count++;
    }

    /* set size of output set */
    out_set->count = count;

    return 0;
}

/*
 * coalesce read requests refering to contiguous data within a given
 * file id, and split read requests whose size is larger than
 * unifycr_key_slice_range into more requests that are smaller
 *
 * Note: a series of read requests that have overlapping spans
 * will prevent merging of contiguous ranges, this should still
 * function, but performance may be lost
 *
 * @param read_req: a list of read requests
 * @param count: number of read requests
 * @param tmp_set: a temporary read requests buffer
 * to hold the intermediate result
 * @param unifycr_key_slice_range: slice size of distributed
 * key-value store
 * @return out_set: the coalesced read requests
 *
 * */
static int unifycr_coalesce_read_reqs(read_req_t* read_req, int count,
                                      read_req_set_t* tmp_set, long slice_range,
                                      read_req_set_t* out_set)
{
    /* initialize output and temporary sets */
    out_set->count = 0;
    tmp_set->count = 0;

    int i;
    int out_idx = 0;
    for (i = 0; i < count; i++) {
        /* index into temp set */
        int tmp_idx = 0;

        /* split this read request into parts based on slice range
         * store resulting requests in tmp_set */
        unifycr_split_read_requests(&read_req[i], tmp_set, slice_range);

        /* look to merge last item in output set with first item
         * in split requests */
        if (out_idx > 0) {
            /* get pointer to last item in out_set */
            read_req_t* out_req = &(out_set->read_reqs[out_idx - 1]);

            /* get pointer to first item in tmp_set */
            read_req_t* tmp_req = &(tmp_set->read_reqs[0]);

            /* look to merge these items if they are contiguous */
            if (out_req->fid == tmp_req->fid &&
                    out_req->offset + out_req->length == tmp_req->offset) {
                /* refers to contiguous range in the same file,
                 * coalesce if also in the same slice */
                uint64_t cur_slice = out_req->offset / slice_range;
                uint64_t tmp_slice = tmp_req->offset / slice_range;
                if (cur_slice == tmp_slice) {
                    /* just increase length to merge */
                    out_req->length += tmp_req->length;

                    /* bump offset into tmp set array */
                    tmp_idx++;
                }
            }
        }

        /* tack on remaining items from tmp set into output set */
        for (; tmp_idx < tmp_set->count; tmp_idx++) {
            out_set->read_reqs[out_idx] = tmp_set->read_reqs[tmp_idx];
            out_set->count++;
            out_idx++;
        }
    }

    return 0;
}

/*
 * match the received read_requests with the
 * client's read requests
 * @param read_req: a list of read requests
 * @param count: number of read requests
 * @param match_req: received read request to match
 * @return error code
 *
 * */
static int unifycr_match_received_ack(read_req_t* read_req, int count,
                                      read_req_t* match_req)
{
    /* given fid, offset, and length of match_req that holds read reply,
     * identify which read request this belongs to in read_req array,
     * then copy data to user buffer */

    /* create a request corresponding to the first byte in read reply */
    read_req_t match_start = *match_req;

    /* create a request corresponding to last byte in read reply */
    read_req_t match_end = *match_req;
    match_end.offset += match_end.length - 1;

    /* find index of read request that contains our first byte */
    int start_pos = unifycr_locate_req(read_req, count, &match_start);

    /* find index of read request that contains our last byte */
    int end_pos = unifycr_locate_req(read_req, count, &match_end);

    /* could not find a valid read request in read_req array */
    if (start_pos == -1) {
        return UNIFYCR_FAILURE;
    }

    /* s: start of match_req, e: end of match_req */

    if (start_pos == 0) {
        if (compare_read_req(&match_start, &read_req[0]) < 0) {
            /* starting offset in read reply comes before lowest
             * offset in read requests, consider this to be an error
             *
             *   ************    ***********         *************
             * s
             *
             * */
            return UNIFYCR_FAILURE;
        }
    }

    /* create read request corresponding to first byte of first read request */
    read_req_t first_start = read_req[start_pos];

    /* create read request corresponding to last byte of first read request */
    read_req_t first_end = read_req[start_pos];
    first_end.offset += first_end.length - 1;

    /* check whether read reply is fully contained by first read request */
    if (compare_read_req(&match_start, &first_start) >= 0 &&
            compare_read_req(&match_end,   &first_end)   <= 0) {
        /* read reply is fully contained within first read request
         *
         * first_s   first_e
         * *****************           *************
         *        s  e
         *
         * */

        /* copy data to user buffer if no error */
        if (match_req->errcode == UNIFYCR_SUCCESS) {
            /* compute buffer location to copy data */
            size_t offset = (size_t)(match_start.offset - first_start.offset);
            char* buf = first_start.buf + offset;

            /* copy data to user buffer */
            memcpy(buf, match_req->buf, match_req->length);

            return UNIFYCR_SUCCESS;
        } else {
            /* hit an error during read, so record this fact
             * in user's original read request */
            read_req[start_pos].errcode = match_req->errcode;
            return UNIFYCR_FAILURE;
        }
    }

    /* define read request for offset of first byte in last read request */
    read_req_t last_start = read_req[end_pos];

    /* define read request for offset of last byte in last read request */
    read_req_t last_end = read_req[end_pos];
    last_end.offset += last_end.length - 1;

    /* determine whether read reply is contained in a range of read requests */
    if (compare_read_req(&match_start, &first_start) >= 0 &&
            compare_read_req(&match_end,   &last_end)    <= 0) {
        /* read reply spans multiple read requests
         *
         *  first_s   first_e  req_s req_e  req_s req_e  last_s    last_e
         *  *****************  ***********  ***********  ****************
         *          s                                              e
         *
         * */

        /* check that read requests from start_pos to end_pos
         * define a contiguous set of bytes */
        int i;
        for (i = start_pos + 1; i <= end_pos; i++) {
            if (read_req[i - 1].offset + read_req[i - 1].length != read_req[i].offset) {
                /* read requests are noncontiguous, so we returned data
                 * covering a hole in the original array of read requests, error */
                return UNIFYCR_FAILURE;
            }
        }

        /* read requests are contiguous, fill all buffers in middle */

        if (match_req->errcode == UNIFYCR_SUCCESS) {
            /* get pointer to start of read reply data */
            char* ptr = match_req->buf;

            /* compute position in user buffer to copy data */
            size_t offset = (size_t)(match_start.offset - first_start.offset);
            char* buf = first_start.buf + offset;

            /* compute number of bytes to copy into first read request */
            size_t length =
                (size_t)(first_end.offset - match_start.offset + 1);

            /* copy data into user buffer for first read request */
            memcpy(buf, ptr, length);
            ptr += length;

            /* copy data for middle read requests */
            for (i = start_pos + 1; i < end_pos; i++) {
                memcpy(read_req[i].buf, ptr, read_req[i].length);
                ptr += read_req[i].length;
            }

            /* compute bytes for last read request */
            length = (size_t)(match_end.offset - last_start.offset + 1);

            /* copy data into user buffer for last read request */
            memcpy(last_start.buf, ptr, length);
            ptr += length;

            return UNIFYCR_SUCCESS;
        } else {
            /* hit an error during read, update errcode in user's
             * original read request from start to end inclusive */
            for (i = start_pos; i <= end_pos; i++) {
                read_req[i].errcode = match_req->errcode;
            }
            return UNIFYCR_FAILURE;
        }
    }

    /* could not find a matching read request, return an error */
    return UNIFYCR_FAILURE;
}

/* notify our delegator that the shared memory buffer
 * is now clear and ready to hold more read data */
static int delegator_signal()
{
    int rc = UNIFYCR_SUCCESS;

    /* set shm flag to 0 to signal delegator we're done */
    volatile int* ptr_flag = (volatile int*)shm_recv_buf;
    *ptr_flag = 0;

    /* TODO: MEM_FLUSH */

    return rc;
}

/* wait for delegator to inform us that shared memory buffer
 * is filled with read data */
static int delegator_wait()
{
#if 0
    /* wait for signal on socket */
    cmd_fd.events = POLLIN | POLLPRI;
    cmd_fd.revents = 0;
    rc = poll(&cmd_fd, 1, -1);

    /* check that we got something good */
    if (rc == 0) {
        if (cmd_fd.revents != 0) {
            if (cmd_fd.revents == POLLIN) {
                return UNIFYCR_SUCCESS;
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
    volatile int* ptr_flag = (volatile int*)shm_recv_buf;

    /* TODO: MEM_FETCH */

    /* wait for server to set flag to non-zero */
    while (*ptr_flag == 0) {
        /* not there yet, sleep for a while */
        nanosleep(&shm_wait_tm, NULL);
        /* TODO: MEM_FETCH */
    }

    return UNIFYCR_SUCCESS;
}

/* copy read data from shared memory buffer to user buffers from read
 * calls, sets done=1 on return when delegator informs us it has no
 * more data */
static int process_read_data(read_req_t* read_req, int count, int* done)
{
    /* assume we'll succeed */
    int rc = UNIFYCR_SUCCESS;

    /* get pointer to start of shared memory buffer */
    char* shmptr = (char*)shm_recv_buf;

    /* extract pointer to flag */
    int* ptr_flag = (int*)shmptr;
    shmptr += sizeof(int);

    /* extract pointer to size */
    int* ptr_size = (int*)shmptr;
    shmptr += sizeof(int);

    /* extract number of read replies in buffer */
    int* ptr_num = (int*)shmptr;
    shmptr += sizeof(int);
    int num = *ptr_num;

    /* process each of our replies */
    int j;
    for (j = 0; j < num; j++) {
        /* get pointer to current read reply header */
        shm_meta_t* msg = (shm_meta_t*)shmptr;
        shmptr += sizeof(shm_meta_t);

        /* define request object */
        read_req_t req;
        req.fid     = msg->src_fid;
        req.offset  = msg->offset;
        req.length  = msg->length;
        req.errcode = msg->errcode;

        /* get pointer to data */
        req.buf = shmptr;
        shmptr += msg->length;

        /* process this read reply, identify which application read
         * request this reply goes to and copy data to user buffer */
        int tmp_rc = unifycr_match_received_ack(read_req, count, &req);
        if (tmp_rc != UNIFYCR_SUCCESS) {
            rc = UNIFYCR_FAILURE;
        }
    }

    /* set done flag if there is no more data */
    if (*ptr_flag == 2) {
        *done = 1;
    };

    /* zero out size and count in recv buffer */
    *ptr_size = 0;
    *ptr_num  = 0;

    return rc;
}

/*
 * get data for a list of read requests from the
 * delegator
 * @param read_req: a list of read requests
 * @param count: number of read requests
 * @return error code
 *
 * */
int unifycr_fd_logreadlist(read_req_t* read_req, int count)
{
    int i;
    int tot_sz = 0;
    int rc = UNIFYCR_SUCCESS;
    int num = 0;
    int* ptr_size = NULL;
    int* ptr_num = NULL;

    /* Adjust length for fitting the EOF. */
    for (i = 0; i < count; i++) {
        /* get pointer to read request */
        read_req_t* req = &read_req[i];

        /* get metadata for this file */
        unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(req->fid);
        if (meta == NULL) {
            return UNIFYCR_ERROR_BADF;
        }

        /* compute last byte of read request */
        off_t last_offset = (off_t)req->offset + (off_t)req->length;
        if (last_offset > meta->size) {
            /* shorten the request to read just up to end */
            req->length = meta->size - req->offset;
        }
    }

    /*
     * Todo: When the number of read requests exceed the
     * request buffer, split list io into multiple bulk
     * sends and transfer in bulks
     * */

    /* convert local fid to global fid */
    unifycr_file_attr_t tmp_meta_entry;
    unifycr_file_attr_t* ptr_meta_entry;
    for (i = 0; i < count; i++) {
        /* look for global meta data for this local file id */
        tmp_meta_entry.fid = read_req[i].fid;
        ptr_meta_entry = (unifycr_file_attr_t*)bsearch(&tmp_meta_entry,
                         unifycr_fattrs.meta_entry, *unifycr_fattrs.ptr_num_entries,
                         sizeof(unifycr_file_attr_t), compare_fattr);

        /* replace local file id with global file id in request */
        if (ptr_meta_entry != NULL) {
            read_req[i].fid = ptr_meta_entry->gfid;
        } else {
            /* failed to find gfid for this request */
            return UNIFYCR_ERROR_BADF;
        }
    }

    /* order read request by increasing file id, then increasing offset */
    qsort(read_req, count, sizeof(read_req_t), compare_read_req);

    /* coalesce the contiguous read requests */
    unifycr_coalesce_read_reqs(read_req, count,
                               &tmp_read_req_set, unifycr_key_slice_range,
                               &read_req_set);

    shm_meta_t* tmp_sh_meta;

    /* prepare our shared memory buffer for delegator */
    delegator_signal();

    if (read_req_set.count > 1) {
        /* got multiple read requests,
         * build up a flat buffer to include them all */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);

        /* create request vector */
        unifycr_Extent_vec_start(&builder);

        /* fill in values for each request entry */
        for (i = 0; i < read_req_set.count; i++) {
            unifycr_Extent_vec_push_create(&builder,
                                           read_req_set.read_reqs[i].fid,
                                           read_req_set.read_reqs[i].offset,
                                           read_req_set.read_reqs[i].length);
        }

        /* complete the array */
        unifycr_Extent_vec_ref_t extents = unifycr_Extent_vec_end(&builder);
        unifycr_ReadRequest_create_as_root(&builder, extents);
        //unifycr_ReadRequest_end_as_root(&builder);

        /* allocate our buffer to be sent */
        size_t size;
        void* buffer = flatcc_builder_finalize_buffer(&builder, &size);
        assert(buffer);

        flatcc_builder_clear(&builder);

        /* invoke read rpc here */
        unifycr_client_mread_rpc_invoke(&unifycr_rpc_context, app_id,
            local_rank_idx, ptr_meta_entry->gfid, read_req_set.count,
            size, buffer);

        free(buffer);
    } else {
        /* got a single read request */
        int gfid = ptr_meta_entry->gfid;
        uint64_t offset = read_req_set.read_reqs[0].offset;
        uint64_t length = read_req_set.read_reqs[0].length;
        unifycr_client_read_rpc_invoke(&unifycr_rpc_context, app_id,
            local_rank_idx, gfid, offset, length);
    }

    /*
     * ToDo: Exception handling when some of the requests
     * are missed
     * */

    int done = 0;
    while (!done) {
        delegator_wait();

        int tmp_rc = process_read_data(read_req, count, &done);
        if (tmp_rc != UNIFYCR_SUCCESS) {
            rc = UNIFYCR_FAILURE;
        }

        delegator_signal();
    }

    return rc;
}

ssize_t UNIFYCR_WRAP(pread)(int fd, void* buf, size_t count, off_t offset)
{
    /* equivalent to read(), except that it shall read from a given
     * position in the file without changing the file pointer */

    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get file id */
        int fid = unifycr_get_fid_from_fd(fd);
        if (fid < 0) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* get file descriptor structure */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* get pointer to file descriptor structure */
        unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
        if (meta == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* check for end of file */
        if (offset >= meta->size) {
            return 0;
        }

        /* assume we'll succeed in read */
        size_t retcount = count;

        read_req_t tmp_req;
        tmp_req.fid     = fid;
        tmp_req.offset  = offset;
        tmp_req.length  = count;
        tmp_req.errcode = UNIFYCR_SUCCESS;
        tmp_req.buf     = buf;

        int ret = unifycr_fd_logreadlist(&tmp_req, 1);

        if (ret != UNIFYCR_SUCCESS) {
            /* error reading data */
            errno = EIO;
            retcount = -1;
        } else if (tmp_req.errcode != UNIFYCR_SUCCESS) {
            /* error reading data */
            errno = EIO;
            retcount = -1;
        }

        /* return number of bytes read */
        return (ssize_t) retcount;
    } else {
        MAP_OR_FAIL(pread);
        ssize_t ret = UNIFYCR_REAL(pread)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYCR_WRAP(pread64)(int fd, void* buf, size_t count, off64_t offset)
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

ssize_t UNIFYCR_WRAP(pwrite)(int fd, const void* buf, size_t count,
                             off_t offset)
{
    /* equivalent to write(), except that it writes into a given
     * position without changing the file pointer */
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* write data to file */
        int write_rc = unifycr_fd_write(fd, offset, buf, count);
        if (write_rc != UNIFYCR_SUCCESS) {
            errno = unifycr_err_map_to_errno(write_rc);
            return (ssize_t)(-1);
        }

        /* return number of bytes read */
        return (ssize_t) count;
    } else {
        MAP_OR_FAIL(pwrite);
        ssize_t ret = UNIFYCR_REAL(pwrite)(fd, buf, count, offset);
        return ret;
    }
}

ssize_t UNIFYCR_WRAP(pwrite64)(int fd, const void* buf, size_t count,
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
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        if (!filedesc->write) {
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

/* get the gfid for use in fsync wrapper
 * TODO: maybe move this somewhere else */
uint32_t get_gfid(int fid)
{
    unifycr_file_attr_t target;
    target.fid = fid;

    const void* entries = unifycr_fattrs.meta_entry;
    size_t num  = *unifycr_fattrs.ptr_num_entries;
    size_t size = sizeof(unifycr_file_attr_t);

    unifycr_file_attr_t* entry = (unifycr_file_attr_t*)bsearch(
        &target, entries, num, size, compare_fattr);

    uint32_t gfid;
    if (entry != NULL) {
        gfid = (uint32_t)entry->gfid;
    } else {
        return -1;
    }

    return gfid;
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

        /* if using spill over, fsync spillover data to disk */
        if (unifycr_use_spillover) {
            int ret = __real_fsync(unifycr_spilloverblock);
            if (ret != 0) {
                /* error, need to set errno appropriately,
                 * we called the real fsync which should
                 * have already set errno to something reasonable */
                return -1;
            }
        }

        /* if using LOGIO, call fsync rpc */
        unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
        if (meta->storage == FILE_STORAGE_LOGIO) {
            /* invoke fsync rpc to register index metadata with server */
            uint32_t gfid = get_gfid(fid);
            unifycr_client_fsync_rpc_invoke(&unifycr_rpc_context,
                                            app_id,
                                            local_rank_idx,
                                            gfid);
        }
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
void* UNIFYCR_WRAP(mmap)(void* addr, size_t length, int prot, int flags,
                         int fd, off_t offset)
{
    /* check whether we should intercept this file descriptor */
    if (unifycr_intercept_fd(&fd)) {
        /* for now, tell user that we can't support mmap,
         * we'll need to track assigned memory region so that
         * we can identify our files on msync and munmap */
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
            __FILE__, __LINE__);
        errno = ENODEV;
        return MAP_FAILED;

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
        void* ret = UNIFYCR_REAL(mmap)(addr, length, prot, flags, fd, offset);
        return ret;
    }
}

int UNIFYCR_WRAP(munmap)(void* addr, size_t length)
{
#if 0
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = EINVAL;
    return -1;
#endif
    MAP_OR_FAIL(munmap);
    int ret = UNIFYCR_REAL(munmap)(addr, length);
    return ret;
}

int UNIFYCR_WRAP(msync)(void* addr, size_t length, int flags)
{
    /* TODO: need to keep track of all the mmaps that are linked to
     * a given file before this function can be implemented */
#if 0
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = EINVAL;
    return -1;
#endif
    MAP_OR_FAIL(msync);
    int ret = UNIFYCR_REAL(msync)(addr, length, flags);
    return ret;
}

void* UNIFYCR_WRAP(mmap64)(void* addr, size_t length, int prot, int flags,
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
        void* ret = UNIFYCR_REAL(mmap64)(addr, length, prot, flags, fd, offset);
        return ret;
    }
}

int UNIFYCR_WRAP(close)(int fd)
{
    /* check whether we should intercept this file descriptor */
    int origfd = fd;
    if (unifycr_intercept_fd(&fd)) {
        DEBUG("closing fd %d\n", fd);

        /* TODO: what to do if underlying file has been deleted? */

        /* check that fd is actually in use */
        int fid = unifycr_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return -1;
        }

        /* get file descriptor for this file */
        unifycr_fd_t* filedesc = unifycr_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            errno = EBADF;
            return -1;
        }

        /* if file was opened for writing, fsync it */
        if (filedesc->write) {
            UNIFYCR_WRAP(fsync)(origfd);
        }

        /* close the file id */
        int close_rc = unifycr_fid_close(fid);
        if (close_rc != UNIFYCR_SUCCESS) {
            errno = EIO;
            return -1;
        }

        /* reinitialize file descriptor to indicate that
         * it is no longer associated with a file,
         * not technically needed but may help catch bugs */
        unifycr_fd_init(fd);

        /* add file descriptor back to free stack */
        unifycr_stack_push(unifycr_fd_stack, fd);

        return 0;
    } else {
        MAP_OR_FAIL(close);
        int ret = UNIFYCR_REAL(close)(fd);
        return ret;
    }
}
