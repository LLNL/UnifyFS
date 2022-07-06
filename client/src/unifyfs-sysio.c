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

#include "unifyfs.h" // for UNIFYFS_SUPER_MAGIC
#include "unifyfs-sysio.h"
#include "margo_client.h"
#include "posix_client.h"
#include "client_read.h"
#include "unifyfs_fid.h"

/* ---------------------------------------
 * POSIX wrappers: paths
 * --------------------------------------- */

int UNIFYFS_WRAP(access)(const char* path, int mode)
{
    /* determine whether we should intercept this path */
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* check if path exists */
        if (unifyfs_fid_from_path(posix_client, upath) < 0) {
            LOGDBG("access: unifyfs_get_id_from path failed, returning -1, %s",
                   upath);
            errno = ENOENT;
            return -1;
        }

        /* currently a no-op */
        LOGDBG("access: path intercepted, returning 0, %s", upath);
        errno = 0;
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* check if it already exists */
        if (unifyfs_fid_from_path(posix_client, upath) >= 0) {
            errno = EEXIST;
            return -1;
        }

        /* add directory to file list */
        int ret = unifyfs_fid_create_directory(posix_client, upath);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to create the directory,
             * set errno and return */
            errno = unifyfs_rc_errno(ret);
            return -1;
        }

        /* success */
        errno = 0;
        return 0;
    } else {
        MAP_OR_FAIL(mkdir);
        int ret = UNIFYFS_REAL(mkdir)(path, mode);
        return ret;
    }
}

/* Function for shared logic between rmdir, remove, and unlink wrappers.
 *
 * Remove the target at upath based on the input file mode indicated by mask.
 * If mask == 0 (e.g., remove wrapper made request), the target doesn't need
 * any mode specific checks before attempting to remove.
 * */
static int _unifyfs_remove(const char* upath, mode_t mask)
{
    /* check if the mount point itself is being deleted */
    if (!strcmp(upath, unifyfs_mount_prefix)) {
        return EBUSY;
    }

    /* check if path exists locally */
    int fid = unifyfs_fid_from_path(posix_client, upath);
    if (fid >= 0) {
        /* found path locally */
        int is_dir = unifyfs_fid_is_dir(posix_client, fid);

        /* is it a directory? */
        if (is_dir) {
            /* check if remove request was made for a regular file */
            if (mask & S_IFREG) {
                /* ERROR: unlink likely made this request but path is a dir */
                LOGDBG("Attempting to unlink a directory %s in UNIFYFS", upath);
                return EISDIR;
            }
            /* remove/rmdir likely made this request (mask & (0 | S_IFDIR)) */

            /* is it empty? */
            if (!unifyfs_fid_is_dir_empty(posix_client, upath)) {
                /* ERROR: is a directory, but isn't empty */
                LOGDBG("Attempting to remove non-empty dir %s in UNIFYFS",
                        upath);
                return ENOTEMPTY;
            }
        } else { /* not a directory */
            /* check if remove request was for a directory */
            if (mask & S_IFDIR) {
                /* ERROR: rmdir likely made this request but path not a dir */
                LOGDBG("Attempting to rmdir a non-dir %s in UNIFYFS", upath);
                return ENOTDIR;
            }
        }

        /* remove the target from the file list */
        int ret = unifyfs_fid_unlink(posix_client, fid);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to remove the target,
             * set errno and return */
            LOGDBG("remove failed on %s in UNIFYFS", upath);
            return unifyfs_rc_errno(ret);
        }
    } else {
        /* path doesn't exist locally, but may exist globally */
        int gfid = unifyfs_generate_gfid(upath);
        unifyfs_file_attr_t attr = {0};

        int ret = unifyfs_get_global_file_meta(posix_client, gfid, &attr);
        if (ret != UNIFYFS_SUCCESS) {
            /* ERROR: path doesn't exist locally or globally */
            LOGDBG("Couldn't find entry for %s in UNIFYFS", upath);
            return ENOENT;
        }

        /* is it a directory? */
        if (attr.mode & S_IFDIR) {
            /* check if remove request was for a regular file */
            if (mask & S_IFREG) {
                /* ERROR: unlink likely made this request but path is a dir */
                LOGDBG("Attempting to unlink a directory %s in UNIFYFS", upath);
                return EISDIR;
            }

            /* Current directory structure assumes all directories are empty.
             * If wanting to enforce directory structure, will need logic to
             * globally check whether a directory is empty.
             * Possibly an rpc to check if_dir and if_empty at once to avoid
             * status change between rpc calls.*/

        } else { /* not a directory */
            /* check if remove request was for a directory */
            if ((mask & S_IFDIR)) {
                LOGDBG("Attempting to rmdir a non-dir %s in UNIFYFS", upath);
                return ENOTDIR;
            }
        }

        /* delete the target */
        ret = invoke_client_unlink_rpc(posix_client, gfid);
        if (ret != UNIFYFS_SUCCESS) {
            LOGDBG("unlink rpc failed on %s in UNIFYFS", upath);
            return unifyfs_rc_errno(ret);
        }
    }

    /* success */
    return 0;
}

int UNIFYFS_WRAP(rmdir)(const char* path)
{
    /* determine whether we should intercept this path */
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* call shared logic function with S_IFDIR mask */
        int ret = _unifyfs_remove(upath, S_IFDIR);
        if (ret) {
            LOGDBG("rmdir() failed on %s in UNIFYFS", upath);
            errno = ret;
            return -1;
        }

        /* success */
        errno = 0;
        return 0;
    } else {
        MAP_OR_FAIL(rmdir);
        int ret = UNIFYFS_REAL(rmdir)(path);
        return ret;
    }
}

int UNIFYFS_WRAP(chdir)(const char* path)
{
    /* determine whether we should intercept this path */
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* TODO: check that path is not a file? */
        /* we're happy to change into any directory in unifyfs */
        char* upath_copy = strdup(upath);
        if (upath_copy == NULL) {
            errno = ENOMEM;
            return -1;
        }
        if (posix_client->cwd != NULL) {
            free(posix_client->cwd);
        }
        posix_client->cwd = upath_copy;
        errno = 0;
        return 0;
    } else {
        MAP_OR_FAIL(chdir);
        int ret = UNIFYFS_REAL(chdir)(path);

        /* if the change dir was successful,
         * update our current working directory */
        if (unifyfs_initialized && ret == 0) {
            if (posix_client->cwd != NULL) {
                free(posix_client->cwd);
            }

            /* if we did a real chdir, let's use a real getcwd
             * to get the current working directory */
            MAP_OR_FAIL(getcwd);
            char* cwd = UNIFYFS_REAL(getcwd)(NULL, 0);
            if (cwd != NULL) {
                posix_client->cwd = cwd;

                /* check posix_client->cwd is within max filename length */
                size_t len = strlen(cwd) + 1;
                if (len > UNIFYFS_MAX_FILENAME) {
                    LOGERR("Current working dir longer (%lu bytes) "
                           "than UNIFYFS_MAX_FILENAME=%d",
                           (unsigned long) len, UNIFYFS_MAX_FILENAME);
                }
            } else {
                /* ERROR */
                LOGERR("Failed to getcwd after chdir(%s) errno=%d %s",
                    path, errno, strerror(errno));
            }
        }

        return ret;
    }
}

/* common logic for getcwd and __getcwd_chk */
static char* _getcwd_impl(char* path, size_t size)
{
    /* man page if size=0 and path not NULL, return EINVAL */
    if (size == 0 && path != NULL) {
        errno = EINVAL;
        return NULL;
    }

    /* get length of current working dir */
    size_t len = strlen(posix_client->cwd) + 1;

    /* if user didn't provide a buffer,
     * we attempt to allocate and return one for them */
    if (path == NULL) {
        /* we'll allocate a buffer to return to the caller */
        char* buf = NULL;

        /* if path is NULL and size is positive, we must
         * allocate a buffer of length size and copy into it */
        if (size > 0) {
            /* check that size is big enough for the string */
            if (len <= size) {
                /* path will fit, allocate buffer and copy */
                buf = (char*) malloc(size);
                if (buf != NULL) {
                    strncpy(buf, posix_client->cwd, size);
                    errno = 0;
                } else {
                    errno = ENOMEM;
                }
                return buf;
            } else {
                /* user's buffer limit is too small */
                errno = ERANGE;
                return NULL;
            }
        }

        /* otherwise size == 0, so allocate a buffer
         * that is big enough */
        buf = (char*) malloc(len);
        if (buf != NULL) {
            strlcpy(buf, posix_client->cwd, len);
            errno = 0;
        } else {
            errno = ENOMEM;
        }
        return buf;
    }

    /* to get here, caller provided an actual buffer,
     * check that path fits in the caller's buffer */
    if (len <= size) {
        /* current working dir fits, copy and return */
        strncpy(path, posix_client->cwd, size);
        errno = 0;
        return path;
    } else {
        /* user's buffer is too small */
        errno = ERANGE;
        return NULL;
    }
}

char* UNIFYFS_WRAP(__getcwd_chk)(char* path, size_t size, size_t buflen)
{
    /* if we're initialized, we're tracking the current working dir */
    if (unifyfs_initialized) {
        /* check that we have a string,
         * return unusual error in case we don't */
        if (posix_client->cwd == NULL) {
            errno = EACCES;
            return NULL;
        }

        /* If posix_client->cwd is in unifyfs space, handle the cwd logic.
         * Otherwise, call the real getcwd, and if actual cwd does
         * not match what we expect, throw an error (the user somehow
         * changed dir without us noticing, so there is a bug here) */
        char upath[UNIFYFS_MAX_FILENAME];
        if (unifyfs_intercept_path(posix_client->cwd, upath)) {
#if 0
            /* TODO: what to do here? */
            if (size > buflen) {
                __chk_fail();
            }
#endif

            /* delegate the rest to our common getcwd function */
            return _getcwd_impl(path, size);
        } else {
            /* current working dir is in real file system,
             * fall through to real getcwd call */
            MAP_OR_FAIL(__getcwd_chk);
            char* ret = UNIFYFS_REAL(__getcwd_chk)(path, size, buflen);

            /* check that current working dir is what we think
             * it should be as a sanity check */
            if (ret != NULL && strcmp(posix_client->cwd, ret) != 0) {
                LOGERR("Expcted cwd=%s vs actual=%s",
                    posix_client->cwd, ret);
            }

            return ret;
        }
    } else {
        /* not initialized, so fall through to real __getcwd_chk */
        MAP_OR_FAIL(__getcwd_chk);
        char* ret = UNIFYFS_REAL(__getcwd_chk)(path, size, buflen);
        return ret;
    }
}

char* UNIFYFS_WRAP(getcwd)(char* path, size_t size)
{
    /* if we're initialized, we're tracking the current working dir */
    if (unifyfs_initialized) {

        /* check that we have a string,
         * return unusual error in case we don't */
        if (posix_client->cwd == NULL) {
            errno = EACCES;
            return NULL;
        }

        /* If posix_client->cwd is in unifyfs space, handle the cwd logic.
         * Otherwise, call the real getcwd, and if actual cwd does
         * not match what we expect, throw an error (the user somehow
         * changed dir without us noticing, so there is a bug here) */
        char upath[UNIFYFS_MAX_FILENAME];
        if (unifyfs_intercept_path(posix_client->cwd, upath)) {
            /* delegate the rest to our common getcwd function */
            return _getcwd_impl(path, size);
        } else {
            /* current working dir is in real file system,
             * fall through to real getcwd call */
            MAP_OR_FAIL(getcwd);
            char* ret = UNIFYFS_REAL(getcwd)(path, size);

            /* check that current working dir is what we think
             * it should be as a sanity check */
            if (ret != NULL && strcmp(posix_client->cwd, ret) != 0) {
                LOGERR("Expcted cwd=%s vs actual=%s",
                       posix_client->cwd, ret);
            }

            return ret;
        }
    } else {
        /* not initialized, so fall through to real getcwd */
        MAP_OR_FAIL(getcwd);
        char* ret = UNIFYFS_REAL(getcwd)(path, size);
        return ret;
    }
}

char* UNIFYFS_WRAP(getwd)(char* path)
{
    /* if we're initialized, we're tracking the current working dir */
    if (unifyfs_initialized) {
        /* check that we have a string,
         * return unusual error in case we don't */
        if (posix_client->cwd == NULL) {
            errno = EACCES;
            return NULL;
        }

        /* If posix_client->cwd is in unifyfs space, handle the cwd logic.
         * Otherwise, call the real getwd, and if actual cwd does
         * not match what we expect, throw an error (the user somehow
         * changed dir without us noticing, so there is a bug here) */
        char upath[UNIFYFS_MAX_FILENAME];
        if (unifyfs_intercept_path(posix_client->cwd, upath)) {
            /* check that we got a valid path */
            if (path == NULL) {
                errno = EINVAL;
                return NULL;
            }

            /* finally get length of current working dir and check
             * that it fits in the caller's buffer */
            size_t len = strlen(posix_client->cwd) + 1;
            if (len <= PATH_MAX) {
                strncpy(path, posix_client->cwd, PATH_MAX);
                errno = 0;
                return path;
            } else {
                /* user's buffer is too small */
                errno = ENAMETOOLONG;
                return NULL;
            }
        } else {
            /* current working dir is in real file system,
             * fall through to real getwd call */
            MAP_OR_FAIL(getwd);
            char* ret = UNIFYFS_REAL(getwd)(path);

            /* check that current working dir is what we think
             * it should be as a sanity check */
            if (ret != NULL && strcmp(posix_client->cwd, ret) != 0) {
                LOGERR("Expcted cwd=%s vs actual=%s",
                       posix_client->cwd, ret);
            }

            return ret;
        }
    } else {
        /* not initialized, so fall through to real getwd */
        MAP_OR_FAIL(getwd);
        char* ret = UNIFYFS_REAL(getwd)(path);
        return ret;
    }
}

char* UNIFYFS_WRAP(get_current_dir_name)(void)
{
    /* if we're initialized, we're tracking the current working dir */
    if (unifyfs_initialized) {
        /* check that we have a string, return unusual error
         * in case we don't */
        if (posix_client->cwd == NULL) {
            errno = EACCES;
            return NULL;
        }

        /* If posix_client->cwd is in unifyfs space, handle the cwd logic.
         * Otherwise, call real get_current_dir_name, and if actual cwd does
         * not match what we expect, throw an error (the user somehow
         * changed dir without us noticing, so there is a bug here) */
        char upath[UNIFYFS_MAX_FILENAME];
        if (unifyfs_intercept_path(posix_client->cwd, upath)) {
            /* supposed to allocate a copy of the current working dir
             * and return that to caller, to be freed by caller */
            char* ret = strdup(posix_client->cwd);
            if (ret == NULL) {
                errno = ENOMEM;
            } else {
                errno = 0;
            }
            return ret;
        } else {
            /* current working dir is in real file system,
             * fall through to real get_current_dir_name call */
            MAP_OR_FAIL(get_current_dir_name);
            char* ret = UNIFYFS_REAL(get_current_dir_name)();

            /* check that current working dir is what we think
             * it should be as a sanity check */
            if (ret != NULL && strcmp(posix_client->cwd, ret) != 0) {
                LOGERR("Expcted cwd=%s vs actual=%s",
                    posix_client->cwd, ret);
            }

            return ret;
        }
    } else {
        /* not initialized, so fall through to real get_current_dir_name */
        MAP_OR_FAIL(get_current_dir_name);
        char* ret = UNIFYFS_REAL(get_current_dir_name)();
        return ret;
    }
}

int UNIFYFS_WRAP(rename)(const char* oldpath, const char* newpath)
{
    /* TODO: allow oldpath / newpath to split across memfs and normal
     * linux fs, which means we'll need to do a read / write */

    /* check whether the old path is in our file system */
    char old_upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(oldpath, old_upath)) {
        /* for now, we can only rename within our file system */
        char new_upath[UNIFYFS_MAX_FILENAME];
        if (!unifyfs_intercept_path(newpath, new_upath)) {
            /* ERROR: can't yet rename across file systems */
            errno = EXDEV;
            return -1;
        }

        /* verify that we really have a file by the old name */
        int fid = unifyfs_fid_from_path(posix_client, old_upath);
        if (fid < 0) {
            /* ERROR: oldname does not exist */
            LOGDBG("Couldn't find entry for %s in UNIFYFS", old_upath);
            errno = ENOENT;
            return -1;
        }
        LOGDBG("orig file in position %d", fid);

        /* check that new name is within bounds */
        size_t newpathlen = strlen(new_upath) + 1;
        if (newpathlen > UNIFYFS_MAX_FILENAME) {
            errno = ENAMETOOLONG;
            return -1;
        }

        /* TODO: rename should replace existing file atomically */

        /* verify that we don't already have a file by the new name */
        int newfid = unifyfs_fid_from_path(posix_client, new_upath);
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
        pthread_mutex_lock(&(posix_client->sync));
        LOGDBG("Changing %s to %s",
               (char*)posix_client->unifyfs_filelist[fid].filename, new_upath);
        strcpy((void*)posix_client->unifyfs_filelist[fid].filename, new_upath);
        pthread_mutex_unlock(&(posix_client->sync));

        /* success */
        errno = 0;
        return 0;
    } else {
        /* for now, we can only rename within our file system */
        char upath[UNIFYFS_MAX_FILENAME];
        if (unifyfs_intercept_path(newpath, upath)) {
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* get file id for path name */
        int fid = unifyfs_fid_from_path(posix_client, upath);
        if (fid >= 0) {
            /* got the file locally, use fid_truncate the file */
            int rc = unifyfs_fid_truncate(posix_client, fid, length);
            if (rc != UNIFYFS_SUCCESS) {
                errno = unifyfs_rc_errno(rc);
                return -1;
            }
        } else {
            /* invoke truncate rpc */
            /* TODO: here, we don't correctly set EISDIR for directories.
             * we could fetch file attribute w/ metaget and check for such
             * invalid requests to avoid extra rpcs. */
            int gfid = unifyfs_generate_gfid(upath);
            int rc = invoke_client_truncate_rpc(posix_client, gfid, length);
            if (rc != UNIFYFS_SUCCESS) {
                LOGDBG("truncate rpc failed %s in UNIFYFS", upath);
                errno = unifyfs_rc_errno(rc);
                return -1;
            }
        }

        /* success */
        errno = 0;
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* call shared logic function with S_IFREG mask */
        int ret = _unifyfs_remove(upath, S_IFREG);
        if (ret) {
            LOGDBG("unlink() failed on %s in UNIFYFS", upath);
            errno = ret;
            return -1;
        }

        /* success */
        errno = 0;
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* call shared logic function with 0 mask */
        int ret = _unifyfs_remove(upath, 0);
        if (ret) {
            LOGDBG("remove() failed on %s in UNIFYFS", upath);
            errno = ret;
            return -1;
        }

        /* success */
        errno = 0;
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
    int ret = unifyfs_get_global_file_meta(posix_client, gfid, pfattr);
    if (ret != UNIFYFS_SUCCESS) {
        LOGDBG("get metadata rpc failed");
        return ret;
    }

    /* if file is laminated, we assume the file size in the meta
     * data is already accurate, if not, look up the current file
     * size with an rpc. we only track size for regular files. */
    if (S_ISREG(pfattr->mode) && !pfattr->is_laminated) {
        /* lookup current global file size */
        size_t filesize;
        ret = invoke_client_filesize_rpc(posix_client, gfid, &filesize);
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

    /* flush any pending writes if needed */
    int fid = unifyfs_fid_from_path(posix_client, path);
    if (fid != -1) {
        int sync_rc = unifyfs_fid_sync_extents(posix_client, fid);
        if (sync_rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_rc_errno(sync_rc);
            return -1;
        }
    }

    /* clear the user buffer */
    memset(buf, 0, sizeof(*buf));

    /* get global file id for given path */
    int gfid = unifyfs_generate_gfid(path);

    /* get stat information for file */
    unifyfs_file_attr_t fattr;
    memset(&fattr, 0, sizeof(fattr));
    int ret = unifyfs_get_meta_with_size(gfid, &fattr);
    if (ret != UNIFYFS_SUCCESS) {
        errno = unifyfs_rc_errno(ret);
        return -1;
    }

    /* update local file metadata (if applicable) */
    if (fid != -1) {
        unifyfs_fid_update_file_meta(posix_client, fid, &fattr);
    }

    /* copy attributes to stat struct */
    unifyfs_file_attr_to_stat(&fattr, buf);
    errno = 0;
    return 0;
}

int UNIFYFS_WRAP(stat)(const char* path, struct stat* buf)
{
    LOGDBG("stat was called for %s", path);
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        int ret = __stat(upath, buf);
        return ret;
    } else {
        MAP_OR_FAIL(stat);
        int ret = UNIFYFS_REAL(stat)(path, buf);
        return ret;
    }
}

int UNIFYFS_WRAP(fstat)(int fd, struct stat* buf)
{
    LOGDBG("fstat was called for fd: %d", fd);

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        int fid = unifyfs_get_fid_from_fd(fd);
        const char* path = unifyfs_path_from_fid(posix_client, fid);
        int ret = __stat(path, buf);
        return ret;
    } else {
        MAP_OR_FAIL(fstat);
        int ret = UNIFYFS_REAL(fstat)(fd, buf);
        return ret;
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

/*
 * NOTE 2: As of glibc-2.33, _STAT_VER is no longer defined in bits/stat.h.
 * __xstat is also no longer declared in stat.h, but it still exists in the
 * library, so HAVE___XSTAT will be true.  (The same goes for __lxstat &
 * __fxstat.)  In such a case, we have to define _STAT_VER ourselves.
 */
#ifndef _STAT_VER
  #define _STAT_VER 3
#endif

#ifdef HAVE___XSTAT
int UNIFYFS_WRAP(__xstat)(int vers, const char* path, struct stat* buf)
{
    LOGDBG("xstat was called for %s", path);

    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }
        int ret = __stat(upath, buf);
        return ret;
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

    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        if (vers != _STAT_VER) {
            errno = EINVAL;
            return -1;
        }
        int ret = __stat(upath, buf);
        return ret;
    } else {
        MAP_OR_FAIL(__lxstat);
        int ret = UNIFYFS_REAL(__lxstat)(vers, path, buf);
        return ret;
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
        const char* path = unifyfs_path_from_fid(posix_client, fid);
        int ret = __stat(path, buf);
        return ret;
    } else {
        MAP_OR_FAIL(__fxstat);
        int ret = UNIFYFS_REAL(__fxstat)(vers, fd, buf);
        return ret;
    }
}
#endif


#ifdef HAVE_SYS_STATFS_H

/* tmpfs seems like a safe choice for something like UnifyFS */
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

static int unifyfs_statfs(struct statfs* fsbuf)
{
    if (NULL != fsbuf) {
        memset(fsbuf, 0, sizeof(*fsbuf));

        /* set file system type */
        if (posix_client->use_unifyfs_magic) {
            /* return a magic value that is specific to UnifyFS */
            fsbuf->f_type = UNIFYFS_SUPER_MAGIC;
        } else {
            /* option to fallback to a well-known magic value for clients
             * that don't handle a UnifyFS magic value */
            fsbuf->f_type = TMPFS_MAGIC;
        }

        fsbuf->f_bsize = UNIFYFS_LOGIO_CHUNK_SIZE; /* Optimal block size */
        //fsbuf->f_blocks = ??;  /* Total data blocks in filesystem */
        //fsbuf->f_bfree = ??;   /* Free blocks in filesystem */
        //fsbuf->f_bavail = ??;  /* Free blocks available */
        fsbuf->f_files = posix_client->max_files;   /* Total file nodes */
        //fsbuf->f_ffree = ??;   /* Free file nodes in filesystem */
        fsbuf->f_namelen = UNIFYFS_MAX_FILENAME; /* Max filename length */
        return 0;
    } else {
        return EFAULT;
    }
}

#ifdef HAVE_STATFS
int UNIFYFS_WRAP(statfs)(const char* path, struct statfs* fsbuf)
{
    LOGDBG("statfs() was called for %s", path);

    int ret;
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        ret = unifyfs_statfs(fsbuf);
        if (ret) {
            errno = ret;
            ret = -1;
        } else {
            errno = 0;
        }
    } else {
        MAP_OR_FAIL(statfs);
        ret = UNIFYFS_REAL(statfs)(path, fsbuf);
    }
    return ret;
}
#endif

#ifdef HAVE_FSTATFS
int UNIFYFS_WRAP(fstatfs)(int fd, struct statfs* fsbuf)
{
    LOGDBG("fstatfs() was called for fd: %d", fd);

    /* check whether we should intercept this file descriptor */
    int ret;
    if (unifyfs_intercept_fd(&fd)) {
        ret = unifyfs_statfs(fsbuf);
        if (ret) {
            errno = ret;
            ret = -1;
        } else {
            errno = 0;
        }
    } else {
        MAP_OR_FAIL(fstatfs);
        ret = UNIFYFS_REAL(fstatfs)(fd, fsbuf);
    }
    return ret;
}
#endif

#endif /* HAVE_SYS_STATFS_H */

/* ---------------------------------------
 * POSIX wrappers: file descriptors
 * --------------------------------------- */

/*
 * Read 'count' bytes info 'buf' from file starting at offset 'pos'.
 *
 * Returns success or error code.
 */
int unifyfs_fd_read(int fd, off_t pos, void* buf, size_t count, size_t* nread)
{
    /* assume we'll fail, set bytes read to 0 as a clue */
    *nread = 0;

    /* get the file id for this file descriptor */
    int fid = unifyfs_get_fid_from_fd(fd);
    if (fid < 0) {
        return EBADF;
    }

    /* it's an error to read from a directory */
    if (unifyfs_fid_is_dir(posix_client, fid)) {
        /* TODO: note that read/pread can return this, but not fread */
        return EISDIR;
    }

    /* check that file descriptor is open for read */
    unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
    if (!filedesc->read) {
        return EBADF;
    }

    /* TODO: is it safe to assume that off_t is bigger than size_t? */
    /* check that we don't overflow the file length */
    if (unifyfs_would_overflow_offt(pos, (off_t) count)) {
        return EOVERFLOW;
    }

    /* if we don't read any bytes, return success */
    if (count == 0) {
        LOGDBG("zero bytes requested");
        return UNIFYFS_SUCCESS;
    }

    /* TODO: handle error if sync fails? */
    /* sync data for file before reading, if needed */
    unifyfs_fid_sync_extents(posix_client, fid);

    /* fill in read request */
    read_req_t req;
    req.gfid    = unifyfs_gfid_from_fid(posix_client, fid);
    req.offset  = (size_t) pos;
    req.length  = count;
    req.nread   = 0;
    req.errcode = 0;
    req.buf     = buf;
    req.aiocbp  = NULL;
    req.cover_begin_offset = (size_t)-1;
    req.cover_end_offset   = (size_t)-1;

    /* execute read operation */
    int ret = process_gfid_reads(posix_client, &req, 1);
    if (ret != UNIFYFS_SUCCESS) {
        /* failed to issue read operation */
        return ret;
    } else if ((req.errcode != UNIFYFS_SUCCESS) &&
               (req.errcode != ENODATA)) {
        /* read executed, but failed */
        return req.errcode;
    }

    /* success, get number of bytes read from read request field */
    *nread = req.nread;

    return UNIFYFS_SUCCESS;
}

/*
 * Write 'count' bytes from 'buf' into file starting at offset' pos'.
 * Allocates new bytes and updates file size as necessary.  It is assumed
 * that 'pos' is actually where you want to write, and so O_APPEND behavior
 * is ignored.  Fills any gaps with zeros
 */
int unifyfs_fd_write(int fd, off_t pos, const void* buf, size_t count,
                     size_t* nwritten)
{
    /* assume we'll fail, set bytes written to 0 as a clue */
    *nwritten = 0;

    /* get the file id for this file descriptor */
    int fid = unifyfs_get_fid_from_fd(fd);
    if (fid < 0) {
        return EBADF;
    }

    /* it's an error to write to a directory */
    if (unifyfs_fid_is_dir(posix_client, fid)) {
        return EINVAL;
    }

    /* check that file descriptor is open for write */
    unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
    if (!filedesc->write) {
        return EBADF;
    }

    /* TODO: is it safe to assume that off_t is bigger than size_t? */
    /* check that our write won't overflow the length */
    if (unifyfs_would_overflow_offt(pos, (off_t) count)) {
        /* TODO: want to return EFBIG here for streams */
        return EOVERFLOW;
    }

    /* finally write specified data to file */
    int write_rc = unifyfs_fid_write(posix_client, fid, pos,
                                     buf, count, nwritten);
    return write_rc;
}

static int posix_create(char* upath, mode_t mode)
{
    /* equivalent to open(path, O_WRONLY|O_CREAT|O_TRUNC, mode) */

    /* create the file */
    int fid;
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    off_t pos;
    int rc = unifyfs_fid_open(posix_client, upath, flags, mode, &fid, &pos);
    if (rc == EEXIST) {
        /* POSIX allows O_CREAT on existing file */
        flags = O_WRONLY | O_TRUNC;
        rc = unifyfs_fid_open(posix_client, upath, flags, mode, &fid, &pos);
    }
    if (rc != UNIFYFS_SUCCESS) {
        errno = unifyfs_rc_errno(rc);
        return -1;
    }

    /* allocate a free file descriptor value */
    int fd = unifyfs_stack_pop(posix_fd_stack);
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
    filedesc->use_count = 1;

    /* don't conflict with active system fds that range from 0 - (fd_limit) */
    int ret = fd + unifyfs_fd_limit;
    LOGDBG("using fds (internal=%d, exposed=%d) for fid %d file %s",
           fd, ret, fid, upath);
    errno = 0;
    return ret;
}

int UNIFYFS_WRAP(creat)(const char* path, mode_t mode)
{
    /* check whether we should intercept this path */
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* TODO: handle relative paths using current working directory */
        return posix_create(upath, mode);
    } else {
        MAP_OR_FAIL(creat);
        int ret = UNIFYFS_REAL(creat)(path, mode);
        return ret;
    }
}

int UNIFYFS_WRAP(creat64)(const char* path, mode_t mode)
{
    /* check whether we should intercept this path */
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* TODO: handle relative paths using current working directory */
        return posix_create(upath, mode);
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* TODO: handle relative paths using current working directory */

        /* open the file */
        int fid;
        off_t pos;
        int rc = unifyfs_fid_open(posix_client, upath, flags, mode,
                                  &fid, &pos);
        if (rc == EEXIST) {
            /* POSIX allows O_CREAT on existing file */
            if ((flags & O_CREAT) && !(flags & O_EXCL)) {
                flags -= O_CREAT;
                rc = unifyfs_fid_open(posix_client, upath, flags, mode,
                                      &fid, &pos);
            }
        }
        if (rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_rc_errno(rc);
            return -1;
        }

        /* allocate a free file descriptor value */
        int fd = unifyfs_stack_pop(posix_fd_stack);
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
        filedesc->append = (flags & O_APPEND);
        filedesc->use_count = 1;

        /* don't conflict with active system fds that range from 0 - (fd_limit) */
        ret = fd + unifyfs_fd_limit;
        LOGDBG("using fds (internal=%d, exposed=%d) for fid %d file %s",
               fd, ret, fid, upath);
        errno = 0;
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        LOGDBG("__open_2 was intercepted for path %s", upath);

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

int UNIFYFS_WRAP(dup)(int fd)
{
    int err = 0;
    int ret;

    LOGDBG("fd=%d", fd);

    /* check whether we should intercept this file descriptor */
    int oldfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        /* dup() is expected to return an unused file descriptor nearest to 0,
         * approximate that by calling socket(), which should be assigned the
         * same fd value */

        /* get file descriptor for this file */
        unifyfs_fd_t* fdesc = unifyfs_get_filedesc_from_fd(fd);
        if (fdesc == NULL) {
            errno = EBADF;
            return -1;
        }

        errno = 0;
        int newfd = socket(PF_LOCAL, SOCK_DGRAM, 0);
        err = errno;
        if (-1 != newfd) {
            int duped_fd = unifyfs_dup_fds[newfd];
            if (-1 == duped_fd) {
                /* slot is available, set it to our internal fd index */
                unifyfs_dup_fds[newfd] = fd;
                LOGDBG("assigned new fd=%d to old=%d", newfd, oldfd);
                fdesc->use_count++;

                err = 0;
                ret = newfd;
            } else {
                LOGWARN("dup_fds[newfd=%d] slot already in use (internal=%d)",
                        newfd, duped_fd);
                /* close socket */
                MAP_OR_FAIL(close);
                UNIFYFS_REAL(close)(newfd);
                err = EMFILE;
                ret = -1;
            }
        } else {
            LOGERR("failed to get an unused system fd - %s", strerror(err));
            ret = -1;
        }
    } else {
        MAP_OR_FAIL(dup);
        errno = 0;
        ret = UNIFYFS_REAL(dup)(fd);
        err = errno;
    }

    errno = err;
    return ret;
}

int UNIFYFS_WRAP(dup2)(int fd, int desired_fd)
{
    int err = 0;
    int ret;

    LOGDBG("fd=%d, desired_fd=%d", fd, desired_fd);

    if (fd == desired_fd) {
        errno = 0;
        return desired_fd;
    }

    /* check whether we should intercept this file descriptor */
    int oldfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        /* dup2() is expected to return the desired file descriptor, closing it
         * if it's currently open. */

        /* get file descriptor for this file */
        unifyfs_fd_t* fdesc = unifyfs_get_filedesc_from_fd(fd);
        if (fdesc == NULL) {
            errno = EBADF;
            return -1;
        }

        MAP_OR_FAIL(close);
        errno = 0;
        ret = UNIFYFS_REAL(close)(desired_fd);
        err = errno;
        if (err && (err != EBADF)) {
            /* dup2() returns -1 with errno=EIO if close(desired_fd) fails.
             * EBADF just means that desired_fd wasn't open, which is OK */
            err = EIO;
            ret = -1;
        } else {
            int newfd = desired_fd;
            int duped_fd = unifyfs_dup_fds[newfd];
            if (-1 == duped_fd) {
                /* slot is available, set it to our internal fd index */
                unifyfs_dup_fds[newfd] = fd;
                LOGDBG("assigned new fd=%d to old=%d", newfd, oldfd);
                fdesc->use_count++;

                err = 0;
                ret = newfd;
            } else {
                LOGWARN("dup_fds[newfd=%d] slot already in use (internal=%d)",
                        newfd, duped_fd);
                err = EMFILE;
                ret = -1;
            }
        }
    } else {
        MAP_OR_FAIL(dup2);
        errno = 0;
        ret = UNIFYFS_REAL(dup2)(fd, desired_fd);
        err = errno;
    }

    errno = err;
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
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(posix_client,
                                                             fid);
        if (meta == NULL) {
            /* bad file descriptor */
            errno = EBADF;
            return (off_t)(-1);
        }

        /* get file descriptor for fd */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);

        /* get current file position */
        off_t current_pos = filedesc->pos;
        off_t logical_eof;

        /* compute final file position */
        switch (whence) {
        case SEEK_SET:
            /* seek to offset */
            if (offset < 0) {
                /* negative offset is invalid */
                errno = EINVAL;
                return (off_t)(-1);
            }
            current_pos = offset;
            break;
        case SEEK_CUR:
            /* seek to current position + offset */
            if (current_pos + offset < 0) {
                /* offset is negative and will result in negative position */
                errno = EINVAL;
                return (off_t)(-1);
            }
            current_pos += offset;
            break;
        case SEEK_END:
            /* seek to EOF + offset */
            logical_eof = unifyfs_fid_logical_size(posix_client, fid);
            if (logical_eof + offset < 0) {
                /* offset is negative and will result in negative position */
                errno = EINVAL;
                return (off_t)(-1);
            }
            current_pos = logical_eof + offset;
            break;
        case SEEK_DATA:
            /* Using fallback approach: always return offset */
            logical_eof = unifyfs_fid_logical_size(posix_client, fid);
            if (offset < 0 || offset > logical_eof) {
                /* negative offset and offset beyond EOF are invalid */
                errno = ENXIO;
                return (off_t)(-1);
            }
            current_pos = offset;
            break;
        case SEEK_HOLE:
            /* Using fallback approach: always return offset for EOF */
            logical_eof = unifyfs_fid_logical_size(posix_client, fid);
            if (offset < 0 || offset > logical_eof) {
                /* negative offset and offset beyond EOF are invalid */
                errno = ENXIO;
                return (off_t)(-1);
            }
            current_pos = logical_eof;
            break;
        default:
            errno = EINVAL;
            return (off_t)(-1);
        }

        /* set and return final file position */
        filedesc->pos = current_pos;
        errno = 0;
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
             * delegate to lseek wrapper */
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
        size_t bytes;
        int read_rc = unifyfs_fd_read(fd, filedesc->pos, buf, count, &bytes);
        if (read_rc != UNIFYFS_SUCCESS) {
            /* read operation failed */
            errno = unifyfs_rc_errno(read_rc);
            return (ssize_t)(-1);
        }

        /* success, update file pointer position */
        filedesc->pos += (off_t)bytes;

        /* return number of bytes read */
        errno = 0;
        return (ssize_t)bytes;
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

    /* check whether we should intercept this file descriptor */
    if (unifyfs_intercept_fd(&fd)) {
        /* get pointer to file descriptor structure */
        unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
        if (filedesc == NULL) {
            /* ERROR: invalid file descriptor */
            errno = EBADF;
            return (ssize_t)(-1);
        }

        /* compute starting position to write within file,
         * assume at current position on file descriptor */
        off_t pos = filedesc->pos;
        if (filedesc->append) {
            /*
             * With O_APPEND we always write to the end, despite the current
             * file position.
             */
            int fid = unifyfs_get_fid_from_fd(fd);
            pos = unifyfs_fid_logical_size(posix_client, fid);
        }

        /* write data to file */
        size_t bytes;
        int write_rc = unifyfs_fd_write(fd, pos, buf, count, &bytes);
        if (write_rc != UNIFYFS_SUCCESS) {
            /* write failed */
            errno = unifyfs_rc_errno(write_rc);
            return (ssize_t)(-1);
        }

        /* update file position */
        filedesc->pos = pos + bytes;

        /* return number of bytes written */
        errno = 0;
        return (ssize_t)bytes;
    } else {
        MAP_OR_FAIL(write);
        ssize_t ret = UNIFYFS_REAL(write)(fd, buf, count);
        return ret;
    }
}

ssize_t UNIFYFS_WRAP(readv)(int fd, const struct iovec* iov, int iovcnt)
{
    ssize_t ret;

    /* check whether we should intercept this file descriptor */
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        ssize_t rret;
        int i;
        ret = 0;
        for (i = 0; i < iovcnt; i++) {
            rret = UNIFYFS_WRAP(read)(origfd, (void*)iov[i].iov_base,
                iov[i].iov_len);
            if (-1 == rret) {
                return -1;
            } else if (0 == rret) {
                return ret;
            } else {
                ret += rret;
            }
        }
        errno = 0;
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
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        ssize_t wret;
        int i;
        ret = 0;
        for (i = 0; i < iovcnt; i++) {
            wret = UNIFYFS_WRAP(write)(origfd, (const void*)iov[i].iov_base,
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
        errno = 0;
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

        /* LOGDBG("aiocb(fd=%d, op=%d, count=%zu, offset=%zu, buf=%p)",
         *      cbp->aio_fildes, cbp->aio_lio_opcode, cbp->aio_nbytes,
         *      cbp->aio_offset, cbp->aio_buf); */

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
                    /* TODO: handle error if sync fails? */
                    /* sync data for file before reading, if needed */
                    unifyfs_fid_sync_extents(posix_client, fid);

                    /* define read request for this file */
                    reqs[reqcnt].gfid    = unifyfs_gfid_from_fid(posix_client,
                                                                 fid);
                    reqs[reqcnt].offset  = (size_t)(cbp->aio_offset);
                    reqs[reqcnt].length  = cbp->aio_nbytes;
                    reqs[reqcnt].nread   = 0;
                    reqs[reqcnt].errcode = 0;
                    reqs[reqcnt].buf     = (char*)(cbp->aio_buf);
                    reqs[reqcnt].aiocbp  = cbp;
                    reqs[reqcnt].cover_begin_offset = (size_t)-1;
                    reqs[reqcnt].cover_end_offset   = (size_t)-1;
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
            LOGDBG("lio_vec[%d] - unexpected LIO op %d",
                   i, cbp->aio_lio_opcode);
            break;
        }
    }

    if (reqcnt) {
        rc = process_gfid_reads(posix_client, reqs, reqcnt);
        if (rc != UNIFYFS_SUCCESS) {
            /* error reading data */
            ret = rc;
        }

        /* update aiocb fields to record error status and return value */
        for (i = 0; i < reqcnt; i++) {
            for (ndx = 0; ndx < nitems; ndx++) {
                cbp = aiocb_list[ndx];
                if (cbp == reqs[i].aiocbp) {
                    AIOCB_ERROR_CODE(cbp) =
                        unifyfs_rc_errno((unifyfs_rc)(reqs[i].errcode));
                    if (0 == reqs[i].errcode) {
                        AIOCB_RETURN_VAL(cbp) = reqs[i].length;
                    }
                    break; // continue outer loop
                }
            }
        }
    }

    free(reqs);

    if (ret) {
        errno = unifyfs_rc_errno(ret);
        ret = -1;
    } else {
        errno = 0;
    }
    return ret;
}
#endif

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

        /* TODO: handle error if sync fails? */
        /* sync data for file before reading, if needed */
        unifyfs_fid_sync_extents(posix_client, fid);

        /* fill in read request */
        read_req_t req;
        req.gfid    = unifyfs_gfid_from_fid(posix_client, fid);
        req.offset  = offset;
        req.length  = count;
        req.nread   = 0;
        req.errcode = 0;
        req.buf     = buf;
        req.aiocbp  = NULL;
        req.cover_begin_offset = (size_t)-1;
        req.cover_end_offset   = (size_t)-1;

        /* execute read operation */
        ssize_t retcount;
        int ret = process_gfid_reads(posix_client, &req, 1);
        if (ret != UNIFYFS_SUCCESS) {
            /* error reading data */
            errno = unifyfs_rc_errno(ret);
            retcount = -1;
        } else if (req.errcode != UNIFYFS_SUCCESS) {
            /* error reading data */
            errno = unifyfs_rc_errno((unifyfs_rc)req.errcode);
            retcount = -1;
        } else {
            /* read succeeded, get number of bytes from nread field */
            retcount = (ssize_t) req.nread;
        }

        /* return number of bytes read */
        errno = 0;
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
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        return UNIFYFS_WRAP(pread)(origfd, buf, count, (off_t)offset);
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
        size_t bytes;
        LOGDBG("pwrite - fd=%d offset=%zu count=%zu",
               fd, (size_t)offset, count);
        int write_rc = unifyfs_fd_write(fd, offset, buf, count, &bytes);
        if (write_rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_rc_errno(write_rc);
            return (ssize_t)(-1);
        }

        /* return number of bytes written */
        errno = 0;
        return (ssize_t)bytes;
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
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        return UNIFYFS_WRAP(pwrite)(origfd, buf, count, (off_t)offset);
    } else {
        MAP_OR_FAIL(pwrite64);
        ssize_t ret = UNIFYFS_REAL(pwrite64)(fd, buf, count, offset);
        return ret;
    }
}

int UNIFYFS_WRAP(fchdir)(int fd)
{
    /* determine whether we should intercept this path */
    if (unifyfs_intercept_fd(&fd)) {
        /* lookup file id for file descriptor */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return -1;
        }

        /* lookup path for fd */
        const char* path = unifyfs_path_from_fid(posix_client, fid);

        /* TODO: test that path is not a file? */

        /* we're happy to change into any directory in unifyfs
         * should we check that we don't change into a file at least? */
        if (posix_client->cwd != NULL) {
            free(posix_client->cwd);
        }
        posix_client->cwd = strdup(path);
        errno = 0;
        return 0;
    } else {
        MAP_OR_FAIL(fchdir);
        int ret = UNIFYFS_REAL(fchdir)(fd);

        /* if the change dir was successful,
         * update our current working directory */
        if (unifyfs_initialized && ret == 0) {
            if (posix_client->cwd != NULL) {
                free(posix_client->cwd);
            }

            /* if we did a real chdir, let's use a real getcwd
             * to get the current working directory */
            MAP_OR_FAIL(getcwd);
            char* cwd = UNIFYFS_REAL(getcwd)(NULL, 0);
            if (cwd != NULL) {
                posix_client->cwd = cwd;

                /* some code may assume posix_client->cwd is a max size */
                size_t len = strlen(cwd) + 1;
                if (len > UNIFYFS_MAX_FILENAME) {
                    LOGERR("Current working dir longer (%lu bytes) "
                           "than UNIFYFS_MAX_FILENAME=%d",
                           (unsigned long) len, UNIFYFS_MAX_FILENAME);
                }
            } else {
                /* ERROR */
                LOGERR("Failed to getcwd after fchdir(%d) errno=%d %s",
                       fd, errno, strerror(errno));
            }
        }

        return ret;
    }
}

int UNIFYFS_WRAP(ftruncate)(int fd, off_t length)
{
    /* check whether we should intercept this file descriptor */
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

        /* truncate the file */
        int rc = unifyfs_fid_truncate(posix_client, fid, length);
        if (rc != UNIFYFS_SUCCESS) {
            errno = unifyfs_rc_errno(rc);
            return -1;
        }

        errno = 0;
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

        /* invoke fsync rpc to register index metadata with server */
        int rc = unifyfs_fid_sync_extents(posix_client, fid);
        if (rc != UNIFYFS_SUCCESS) {
            /* metadata sync failed, set errno and return error */
            errno = unifyfs_rc_errno(rc);
            return -1;
        } else if (posix_client->use_fsync_persist) {
            /* now sync file data to storage */
            rc = unifyfs_fid_sync_data(posix_client, fid);
            if (rc != UNIFYFS_SUCCESS) {
                /* data sync failed, set errno and return error */
                errno = unifyfs_rc_errno(rc);
                return -1;
            }
        }

        errno = 0;
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
        // KMM I removed the locking code because it was causing hangs
        errno = 0;
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
    int origfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        void* ret = UNIFYFS_WRAP(mmap)(addr, length, prot, flags, origfd,
            (off_t)offset);
        return ret;
    } else {
        MAP_OR_FAIL(mmap64);
        void* ret = UNIFYFS_REAL(mmap64)(addr, length, prot, flags, fd, offset);
        return ret;
    }
}

int UNIFYFS_WRAP(close)(int fd)
{
    /* check whether we should intercept this file descriptor */
    int oldfd = fd;
    if (unifyfs_intercept_fd(&fd)) {
        LOGDBG("closing (internal=%d, exposed=%d)", fd, oldfd);

        /* get file descriptor for this file */
        unifyfs_fd_t* fdesc = unifyfs_get_filedesc_from_fd(fd);
        if (fdesc == NULL) {
            errno = EBADF;
            return -1;
        }
        fdesc->use_count--;

        if ((oldfd < UNIFYFS_CLIENT_MAX_FILES) &&
            (fd == unifyfs_dup_fds[oldfd])) {
                LOGDBG("closing duped fd=%d", oldfd);
                /* if oldfd is a socket created in dup() wrapper, close it */
                int type_val;
                socklen_t type_len = sizeof(type_val);
                errno = 0;
                int rc = getsockopt(oldfd, SOL_SOCKET, SO_TYPE,
                                    (void*)&type_val, &type_len);
                if (rc == 0) {
                    MAP_OR_FAIL(close);
                    UNIFYFS_REAL(close)(oldfd);
                }
                unifyfs_dup_fds[oldfd] = -1;
        }

        /* TODO: what to do if underlying file has been deleted? */

        /* check that fd is actually in use */
        int fid = unifyfs_get_fid_from_fd(fd);
        if (fid < 0) {
            errno = EBADF;
            return -1;
        }

        /* if file was opened for writing, sync it */
        if (fdesc->write) {
            LOGDBG("syncing fid=%d", fid);
            int sync_rc = unifyfs_fid_sync_extents(posix_client, fid);
            if (sync_rc != UNIFYFS_SUCCESS) {
                errno = unifyfs_rc_errno(sync_rc);
                return -1;
            }
        }

        if (0 == fdesc->use_count) {
            /* close the file id */
            LOGDBG("closing fid=%d", fid);
            int close_rc = unifyfs_fid_close(posix_client, fid);
            if (close_rc != UNIFYFS_SUCCESS) {
                errno = unifyfs_rc_errno(close_rc);
                return -1;
            }

            /* reinitialize file descriptor to indicate that
             * it is no longer associated with a file,
             * not technically needed but may help catch bugs */
            unifyfs_fd_init(fd);

            /* add file descriptor back to free stack */
            unifyfs_stack_push(posix_fd_stack, fd);
        }

        errno = 0;
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
    const char* path = unifyfs_path_from_fid(posix_client, fid);

    /* lookup metadata for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(posix_client, fid);
    if (NULL == meta) {
        LOGDBG("no metadata info for %s", path);
        errno = ENOENT;
        return -1;
    }

    /* Once a file is laminated, you can't modify it in any way */
    if (meta->attrs.is_laminated) {
        LOGDBG("%s is already laminated", path);
        errno = EROFS;
        return -1;
    }

    /* found file, and it's not yet laminated,
     * get the global file id */
    int gfid = unifyfs_gfid_from_fid(posix_client, fid);

    /*
     * If the chmod clears all the existing write bits, then it's a laminate.
     *
     * meta->mode & 0222                  Was at least one write bit set before?
     * ((meta->mode & 0222) & mode) == 0  Will all the write bits be cleared?
     */
    if ((meta->attrs.mode & 0222) &&
        (((meta->attrs.mode & 0222) & mode) == 0)) {
        /* We're laminating. */
        ret = invoke_client_laminate_rpc(posix_client, gfid);
        if (ret) {
            LOGERR("laminate failed");
            errno = unifyfs_rc_errno(ret);
            return -1;
        }
    }

    /* Clear out our old permission bits, and set the new ones in */
    meta->attrs.mode = meta->attrs.mode & ~0777;
    meta->attrs.mode = meta->attrs.mode | mode;

    /* update the global meta data to reflect new permissions */
    unifyfs_file_attr_op_e op = UNIFYFS_FILE_ATTR_OP_CHMOD;
    ret = unifyfs_set_global_file_meta_from_fid(posix_client, fid, op);
    if (ret) {
        LOGERR("chmod: can't set global meta entry for %s (fid:%d)",
               path, fid);
        errno = unifyfs_rc_errno(ret);
        return -1;
    }

    /* read metadata back to pick up file size and laminated flag */
    unifyfs_file_attr_t attr = {0};
    ret = unifyfs_get_global_file_meta(posix_client, gfid, &attr);
    if (ret) {
        LOGERR("chmod: can't get global meta entry for %s (fid:%d)",
               path, fid);
        errno = unifyfs_rc_errno(ret);
        return -1;
    }

    LOGDBG("attributes from global metadata");
    debug_print_file_attr(&attr);

    /* update global size of file from global metadata */
    unifyfs_fid_update_file_meta(posix_client, fid, &attr);

    errno = 0;
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
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        /* check if path exists */
        int fid = unifyfs_fid_from_path(posix_client, upath);
        if (fid < 0) {
            LOGDBG("chmod: unifyfs_get_id_from path failed, returning -1, %s",
                   upath);
            errno = ENOENT;
            return -1;
        }

        LOGDBG("chmod: setting %s to %o", upath, mode);
        return __chmod(fid, mode);
    } else {
        MAP_OR_FAIL(chmod);
        int ret = UNIFYFS_REAL(chmod)(path, mode);
        return ret;
    }
}
