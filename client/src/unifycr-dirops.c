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

/*
 * TODO: the number of open directories clearly won't exceed the number of file
 * descriptors. however, the current UNIFYCR_MAX_FILEDESCS value of 256 will
 * quickly run out. if this value is fixed to be reasonably larger, then we
 * would need a way to dynamically allocate the dirstreams instead of the
 * following fixed size array.
 */
static unifycr_dirstream_t _dirstreams[UNIFYCR_MAX_FILEDESCS];

/*
 * TODO: currently, we just take the same indexed slot to our fid.
 */
#if 0
static pthread_spinlock_t _dirstream_lock;
static unifycr_dirstream_t *_dirstream_freelist;

static unifycr_dirstream_t *unifycr_dirstream_alloc(void)
{
}

static void unifycr_dirstream_free(unifycr_dirstream_t *dirp)
{
}
#endif

static inline unifycr_dirstream_t *unifycr_dirstream_alloc(int fid)
{
    unifycr_dirstream_t *dirp = &_dirstreams[fid];

    memset((void *) dirp, 0, sizeof(*dirp));

    return dirp;
}

static inline void unifycr_dirstream_free(unifycr_dirstream_t *dirp)
{
    return;
}

DIR *UNIFYCR_WRAP(opendir)(const char *name)
{
    int ret = 0;
    int gfid = -1;
    int fid = -1;
    struct stat *sb = NULL;
    unifycr_dirstream_t *_dirp = NULL;
    unifycr_file_attr_t gfattr = { 0, };
    unifycr_filemeta_t *meta = NULL;

    if (!unifycr_intercept_path(name)) {
        MAP_OR_FAIL(opendir);
        return UNIFYCR_REAL(opendir)(name);
    }

    /*
     * check the valid existence of the given entry in the global metadb.
     * if valid, populate the local file meta cache accordingly.
     */

    fid = unifycr_get_fid_from_path(name);
    gfid = unifycr_generate_gfid(name);
    ret = unifycr_get_global_file_meta(fid, gfid, &gfattr);
    if (ret != UNIFYCR_SUCCESS) {
        errno = ENOENT;
        return NULL;
    }

    sb = &gfattr.file_attr;
    if (!S_ISDIR(sb->st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }

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
        meta->real_size = sb->st_size;
    } else {
        fid = unifycr_fid_create_file(name);
        if (fid < 0) {
            errno = EIO;
            return NULL;
        }

        meta = unifycr_get_meta_from_fid(fid);
        meta->is_dir = 1;
        meta->size = sb->st_size;
        meta->chunks = sb->st_blocks;
        meta->real_size = sb->st_size;
    }

    _dirp = unifycr_dirstream_alloc(fid);

    return (DIR *) _dirp;
}

DIR *UNIFYCR_WRAP(fdopendir)(int fd)
{
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = ENOSYS;

    return NULL;
}

int UNIFYCR_WRAP(closedir)(DIR *dirp)
{
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = ENOSYS;

    return -1;
}

struct dirent *UNIFYCR_WRAP(readdir)(DIR *dirp)
{
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = ENOSYS;

    return NULL;
}

void UNIFYCR_WRAP(rewinddir)(DIR *dirp)
{
    unifycr_dirstream_t *_dirp = (unifycr_dirstream_t *) dirp;

    /* TODO: update the pos in the file descriptor (fd) via lseek */

    _dirp->filepos = 0;
}

int UNIFYCR_WRAP(dirfd)(DIR *dirp)
{
    unifycr_dirstream_t *_dirp = (unifycr_dirstream_t *) dirp;

    return _dirp->fid + unifycr_fd_limit;
}

long UNIFYCR_WRAP(telldir)(DIR *dirp)
{
    unifycr_dirstream_t *_dirp = (unifycr_dirstream_t *) dirp;

    return _dirp->filepos;
}

int UNIFYCR_WRAP(scandir)(const char *dirp, struct dirent **namelist,
                          int (*filter)(const struct dirent *),
                          int (*compar)(const struct dirent **,
                                        const struct dirent **))
{
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = ENOSYS;

    return -1;
}

void UNIFYCR_WRAP(seekdir)(DIR *dirp, long loc)
{
    fprintf(stderr, "Function not yet supported @ %s:%d\n", __FILE__, __LINE__);
    errno = ENOSYS;
}

