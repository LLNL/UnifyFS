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
#include <config.h>

#include "unifycr-sysio.h"

/* given a file id corresponding to a directory,
 * allocate and initialize a directory stream */
static inline unifycr_dirstream_t *unifycr_dirstream_alloc(int fid)
{
    /* allocate a file descriptor for this stream */
    int fd = unifycr_stack_pop(unifycr_fd_stack);
    if (fd < 0) {
        /* exhausted our file descriptors */
        errno = EMFILE;
        return NULL;
    }

    /* allocate a directory stream id */
    int dirid = unifycr_stack_pop(unifycr_dirstream_stack);
    if (dirid < 0) {
        /* exhausted our directory streams,
         * return our file descriptor and set errno */
        unifycr_stack_push(unifycr_fd_stack, fd);
        errno = EMFILE;
        return NULL;
    }

    /* get file descriptor for this fd */
    unifycr_fd_t *filedesc = unifycr_get_filedesc_from_fd(fd);
    filedesc->fid   = fid;
    filedesc->pos   = 0;
    filedesc->read  = 0;
    filedesc->write = 0;

    /* get pointer to file stream structure */
    unifycr_dirstream_t *dirp = &(unifycr_dirstreams[dirid]);

    /* initialize fields in structure */
    memset((void *) dirp, 0, sizeof(*dirp));

    /* record index into unifycr_dirstreams */
    dirp->dirid = dirid;

    /* record local file id corresponding to directory */
    dirp->fid = fid;

    /* record file descriptor we're using for this stream */
    dirp->fd = fd;

    /* position directory pointer to first item */
    dirp->pos = 0;

    return dirp;
}

/* release resources allocated in unifycr_dirstream_alloc */
static inline int unifycr_dirstream_free(unifycr_dirstream_t *dirp)
{
    /* reinit file descriptor to indicate that it's no longer in use,
     * not really necessary, but should help find bugs */
    unifycr_fd_init(dirp->fd);

    /* return file descriptor to the free stack */
    unifycr_stack_push(unifycr_fd_stack, dirp->fd);

    /* reinit dir stream to indicate that it's no longer in use,
     * not really necessary, but should help find bugs */
    unifycr_dirstream_init(dirp->dirid);

    /* return our index to directory stream stack */
    unifycr_stack_push(unifycr_dirstream_stack, dirp->dirid);

    return UNIFYCR_SUCCESS;
}

DIR *UNIFYCR_WRAP(opendir)(const char *name)
{
    /* call real opendir and return early if this is
     * not one of our paths */
    if (!unifycr_intercept_path(name)) {
        MAP_OR_FAIL(opendir);
        return UNIFYCR_REAL(opendir)(name);
    }

    /*
     * check the valid existence of the given entry in the global metadb.
     * if valid, populate the local file meta cache accordingly.
     */

    int fid  = unifycr_get_fid_from_path(name);
    int gfid = unifycr_generate_gfid(name);

    unifycr_file_attr_t gfattr = { 0, };
    int ret = unifycr_get_global_file_meta(fid, gfid, &gfattr);
    if (ret != UNIFYCR_SUCCESS) {
        errno = ENOENT;
        return NULL;
    }

    struct stat *sb = &gfattr.file_attr;
    if (!S_ISDIR(sb->st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }

    unifycr_filemeta_t *meta = NULL;
    if (fid >= 0) {
        meta = unifycr_get_meta_from_fid(fid);

        /*
         * FIXME: We found an inconsistent status between local cache and
         * global metadb. is it safe to invalidate the local entry and
         * re-populate with the global data?
         */
        if (!meta->is_dir) {
            errno = EIO;
            return NULL;
        }

        /*
         * FIXME: also, is it safe to oeverride this local data?
         */
        meta->size = sb->st_size;
        meta->chunks = sb->st_blocks;
        meta->log_size = 0; /* no need of local storage for dir operations */
    } else {
        fid = unifycr_fid_create_file(name);
        if (fid < 0) {
            errno = EIO;
            return NULL;
        }

        meta = unifycr_get_meta_from_fid(fid);
        meta->is_dir   = 1;
        meta->size     = sb->st_size;
        meta->chunks   = sb->st_blocks;
        meta->log_size = 0;
    }

    unifycr_dirstream_t *dirp = unifycr_dirstream_alloc(fid);

    return (DIR *) dirp;
}

DIR *UNIFYCR_WRAP(fdopendir)(int fd)
{
    if (unifycr_intercept_fd(&fd)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
            __FILE__, __LINE__);
        errno = ENOSYS;
        return NULL;
    } else {
        MAP_OR_FAIL(fdopendir);
        DIR *ret = UNIFYCR_REAL(fdopendir)(fd);
        return ret;
    }
}

int UNIFYCR_WRAP(closedir)(DIR *dirp)
{
    if (unifycr_intercept_dirstream(dirp)) {
        unifycr_dirstream_t *d = (unifycr_dirstream_t *) dirp;
        unifycr_dirstream_free(d);
        return 0;
    } else {
        MAP_OR_FAIL(closedir);
        int ret = UNIFYCR_REAL(closedir)(dirp);
        return ret;
    }
}

struct dirent *UNIFYCR_WRAP(readdir)(DIR *dirp)
{
    if (unifycr_intercept_dirstream(dirp)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
            __FILE__, __LINE__);
        errno = ENOSYS;
        return NULL;
    } else {
        MAP_OR_FAIL(readdir);
        struct dirent *d = UNIFYCR_REAL(readdir)(dirp);
        return d;
    }
}

void UNIFYCR_WRAP(rewinddir)(DIR *dirp)
{
    if (unifycr_intercept_dirstream(dirp)) {
        unifycr_dirstream_t *_dirp = (unifycr_dirstream_t *) dirp;

        /* TODO: update the pos in the file descriptor (fd) via lseek */

        _dirp->pos = 0;
    } else {
        MAP_OR_FAIL(rewinddir);
        UNIFYCR_REAL(rewinddir)(dirp);
        return;
    }
}

int UNIFYCR_WRAP(dirfd)(DIR *dirp)
{
    if (unifycr_intercept_dirstream(dirp)) {
        unifycr_dirstream_t *d = (unifycr_dirstream_t *) dirp;
        int fd = d->fd;
        return fd;
    } else {
        MAP_OR_FAIL(dirfd);
        int fd = UNIFYCR_REAL(dirfd)(dirp);
        return fd;
    }
}

long UNIFYCR_WRAP(telldir)(DIR *dirp)
{
    if (unifycr_intercept_dirstream(dirp)) {
        unifycr_dirstream_t *d = (unifycr_dirstream_t *) dirp;
        return d->pos;
    } else {
        MAP_OR_FAIL(telldir);
        long ret = UNIFYCR_REAL(telldir)(dirp);
        return ret;
    }
}

int UNIFYCR_WRAP(scandir)(const char *path, struct dirent **namelist,
                          int (*filter)(const struct dirent *),
                          int (*compar)(const struct dirent **,
                                        const struct dirent **))
{
    if (unifycr_intercept_path(path)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
            __FILE__, __LINE__);
        errno = ENOSYS;
        return -1;
    } else {
        MAP_OR_FAIL(scandir);
        long ret = UNIFYCR_REAL(scandir)(path, namelist, filter, compar);
        return ret;
    }
}

void UNIFYCR_WRAP(seekdir)(DIR *dirp, long loc)
{
    if (unifycr_intercept_dirstream(dirp)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
            __FILE__, __LINE__);
        errno = ENOSYS;
    } else {
        MAP_OR_FAIL(seekdir);
        UNIFYCR_REAL(seekdir)(dirp, loc);
        return;
    }
}
