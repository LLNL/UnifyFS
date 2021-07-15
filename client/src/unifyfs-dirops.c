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

#include "unifyfs-sysio.h"
#include "posix_client.h"
#include "unifyfs_fid.h"

/* given a file id corresponding to a directory,
 * allocate and initialize a directory stream */
static unifyfs_dirstream_t* unifyfs_dirstream_alloc(int fid)
{
    /* allocate a file descriptor for this stream */
    int fd = unifyfs_stack_pop(posix_fd_stack);
    if (fd < 0) {
        /* exhausted our file descriptors */
        errno = EMFILE;
        return NULL;
    }

    /* allocate a directory stream id */
    int dirid = unifyfs_stack_pop(posix_dirstream_stack);
    if (dirid < 0) {
        /* exhausted our directory streams,
         * return our file descriptor and set errno */
        unifyfs_stack_push(posix_fd_stack, fd);
        errno = EMFILE;
        return NULL;
    }

    /* get file descriptor for this fd */
    unifyfs_fd_t* filedesc = unifyfs_get_filedesc_from_fd(fd);
    filedesc->fid   = fid;
    filedesc->pos   = 0;
    filedesc->read  = 0;
    filedesc->write = 0;

    /* get pointer to file stream structure */
    unifyfs_dirstream_t* dirp = &(unifyfs_dirstreams[dirid]);

    /* initialize fields in structure */
    memset((void*) dirp, 0, sizeof(*dirp));

    /* record index into unifyfs_dirstreams */
    dirp->dirid = dirid;

    /* record local file id corresponding to directory */
    dirp->fid = fid;

    /* record file descriptor we're using for this stream */
    dirp->fd = fd;

    /* position directory pointer to first item */
    dirp->pos = 0;

    return dirp;
}

/* release resources allocated in unifyfs_dirstream_alloc */
static int unifyfs_dirstream_free(unifyfs_dirstream_t* dirp)
{
    /* reinit file descriptor to indicate that it's no longer in use,
     * not really necessary, but should help find bugs */
    unifyfs_fd_init(dirp->fd);

    /* return file descriptor to the free stack */
    unifyfs_stack_push(posix_fd_stack, dirp->fd);

    /* reinit dir stream to indicate that it's no longer in use,
     * not really necessary, but should help find bugs */
    unifyfs_dirstream_init(dirp->dirid);

    /* return our index to directory stream stack */
    unifyfs_stack_push(posix_dirstream_stack, dirp->dirid);

    return UNIFYFS_SUCCESS;
}

DIR* UNIFYFS_WRAP(opendir)(const char* name)
{
    /* call real opendir and return early if this is
     * not one of our paths */
    char upath[UNIFYFS_MAX_FILENAME];
    if (!unifyfs_intercept_path(name, upath)) {
        MAP_OR_FAIL(opendir);
        return UNIFYFS_REAL(opendir)(name);
    }

    /*
     * check the valid existence of the given entry in the global metadb.
     * if valid, populate the local file meta cache accordingly.
     */

    int fid  = unifyfs_fid_from_path(posix_client, upath);
    int gfid = unifyfs_generate_gfid(upath);

    unifyfs_file_attr_t gfattr = { 0, };
    int ret = unifyfs_get_global_file_meta(posix_client, gfid, &gfattr);
    if (ret != UNIFYFS_SUCCESS) {
        errno = ENOENT;
        return NULL;
    }

    struct stat sb = { 0, };

    unifyfs_file_attr_to_stat(&gfattr, &sb);

    if (!S_ISDIR(sb.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }

    unifyfs_filemeta_t* meta = NULL;
    if (fid >= 0) {
        meta = unifyfs_get_meta_from_fid(posix_client, fid);
        assert(meta != NULL);

        /*
         * FIXME: We found an inconsistent status between local cache and
         * global metadb. is it safe to invalidate the local entry and
         * re-populate with the global data?
         */
        if (!unifyfs_fid_is_dir(posix_client, fid)) {
            errno = ENOTDIR;
            return NULL;
        }
    } else {
        fid = unifyfs_fid_create_file(posix_client, upath, 0);
        if (fid < 0) {
            errno = unifyfs_rc_errno(-fid);
            return NULL;
        }

        meta = unifyfs_get_meta_from_fid(posix_client, fid);
        assert(meta != NULL);

        /* set as directory */
        meta->attrs.mode = (meta->attrs.mode & ~S_IFREG) | S_IFDIR;
    }

    meta->attrs.size = sb.st_size;

    unifyfs_dirstream_t* dirp = unifyfs_dirstream_alloc(fid);

    return (DIR*) dirp;
}

DIR* UNIFYFS_WRAP(fdopendir)(int fd)
{
    if (unifyfs_intercept_fd(&fd)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
                __FILE__, __LINE__);
        errno = ENOSYS;
        return NULL;
    } else {
        MAP_OR_FAIL(fdopendir);
        DIR* ret = UNIFYFS_REAL(fdopendir)(fd);
        return ret;
    }
}

int UNIFYFS_WRAP(closedir)(DIR* dirp)
{
    if (unifyfs_intercept_dirstream(dirp)) {
        unifyfs_dirstream_t* d = (unifyfs_dirstream_t*) dirp;
        unifyfs_dirstream_free(d);
        return 0;
    } else {
        MAP_OR_FAIL(closedir);
        int ret = UNIFYFS_REAL(closedir)(dirp);
        return ret;
    }
}

struct dirent* UNIFYFS_WRAP(readdir)(DIR* dirp)
{
    if (unifyfs_intercept_dirstream(dirp)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
                __FILE__, __LINE__);
        errno = ENOSYS;
        return NULL;
    } else {
        MAP_OR_FAIL(readdir);
        struct dirent* d = UNIFYFS_REAL(readdir)(dirp);
        return d;
    }
}

void UNIFYFS_WRAP(rewinddir)(DIR* dirp)
{
    if (unifyfs_intercept_dirstream(dirp)) {
        unifyfs_dirstream_t* _dirp = (unifyfs_dirstream_t*) dirp;

        /* TODO: update the pos in the file descriptor (fd) via lseek */

        _dirp->pos = 0;
    } else {
        MAP_OR_FAIL(rewinddir);
        UNIFYFS_REAL(rewinddir)(dirp);
        return;
    }
}

int UNIFYFS_WRAP(dirfd)(DIR* dirp)
{
    if (unifyfs_intercept_dirstream(dirp)) {
        unifyfs_dirstream_t* d = (unifyfs_dirstream_t*) dirp;
        int fd = d->fd;
        return fd;
    } else {
        MAP_OR_FAIL(dirfd);
        int fd = UNIFYFS_REAL(dirfd)(dirp);
        return fd;
    }
}

long UNIFYFS_WRAP(telldir)(DIR* dirp)
{
    if (unifyfs_intercept_dirstream(dirp)) {
        unifyfs_dirstream_t* d = (unifyfs_dirstream_t*) dirp;
        return d->pos;
    } else {
        MAP_OR_FAIL(telldir);
        long ret = UNIFYFS_REAL(telldir)(dirp);
        return ret;
    }
}

int UNIFYFS_WRAP(scandir)(const char* path, struct dirent** namelist,
                          int (*filter)(const struct dirent*),
                          int (*compar)(const struct dirent**,
                                        const struct dirent**))
{
    char upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(path, upath)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
                __FILE__, __LINE__);
        errno = ENOSYS;
        return -1;
    } else {
        MAP_OR_FAIL(scandir);
        long ret = UNIFYFS_REAL(scandir)(path, namelist, filter, compar);
        return ret;
    }
}

void UNIFYFS_WRAP(seekdir)(DIR* dirp, long loc)
{
    if (unifyfs_intercept_dirstream(dirp)) {
        fprintf(stderr, "Function not yet supported @ %s:%d\n",
                __FILE__, __LINE__);
        errno = ENOSYS;
    } else {
        MAP_OR_FAIL(seekdir);
        UNIFYFS_REAL(seekdir)(dirp, loc);
        return;
    }
}
