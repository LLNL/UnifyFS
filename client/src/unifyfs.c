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

#include "unifyfs.h"
#include "unifyfs-internal.h"
#include "unifyfs-fixed.h"
#include "client_read.h"

// client-server rpc headers
#include "unifyfs_client_rpcs.h"
#include "unifyfs_rpc_util.h"
#include "margo_client.h"

#ifdef USE_SPATH
#include "spath.h"
#endif /* USE_SPATH */

/* avoid duplicate mounts (for now) */
int unifyfs_mounted = -1;

/* whether we can use fgetpos/fsetpos */
static int unifyfs_fpos_enabled = 1;

static unifyfs_cfg_t client_cfg;

unifyfs_index_buf_t unifyfs_indices;
static size_t unifyfs_index_buf_size;    /* size of metadata log */
unsigned long unifyfs_max_index_entries; /* max metadata log entries */

int global_rank_cnt; /* count of world ranks */
int client_rank;     /* client-provided rank (for debugging) */

int unifyfs_app_id;    /* application (aka mountpoint) id */
int unifyfs_client_id; /* client id within application */

static int unifyfs_use_single_shm = 0;
static int unifyfs_page_size      = 0;

/* Determine whether we automatically sync every write to server.
 * This slows write performance, but it can serve as a work
 * around for apps that do not have all necessary syncs. */
static bool unifyfs_write_sync;

static off_t unifyfs_max_offt;
static off_t unifyfs_min_offt;
static off_t unifyfs_max_long;
static off_t unifyfs_min_long;

/* TODO: moved these to fixed file */
int    unifyfs_max_files;  /* maximum number of files to store */
bool   unifyfs_local_extents;  /* track data extents in client to read local */

/* whether to return UNIFYFS (true) or TMPFS (false) magic value from statfs */
bool unifyfs_super_magic;

/* log-based I/O context */
logio_context* logio_ctx;

/* keep track of what we've initialized */
int unifyfs_initialized = 0;

/* superblock - persistent shared memory region (metadata + data) */
static shm_context* shm_super_ctx;

/* per-file metadata */
static void* free_fid_stack;
unifyfs_filename_t* unifyfs_filelist;
static unifyfs_filemeta_t* unifyfs_filemetas;

/* TODO: metadata spillover is not currently supported */
int unifyfs_spillmetablock = -1;

/* array of file descriptors */
unifyfs_fd_t unifyfs_fds[UNIFYFS_CLIENT_MAX_FILEDESCS];
rlim_t unifyfs_fd_limit;

/* array of file streams */
unifyfs_stream_t unifyfs_streams[UNIFYFS_CLIENT_MAX_FILEDESCS];

/*
 * TODO: the number of open directories clearly won't exceed the number of
 * file descriptors. however, the current MAX_FILEDESCS value of 256 will
 * quickly run out. if this value is fixed to be reasonably larger, then we
 * would need a way to dynamically allocate the dirstreams instead of the
 * following fixed size array.
 */

/* array of DIR* streams to be used */
unifyfs_dirstream_t unifyfs_dirstreams[UNIFYFS_CLIENT_MAX_FILEDESCS];

/* stack to track free file descriptor values,
 * each is an index into unifyfs_fds array */
void* unifyfs_fd_stack;

/* stack to track free file streams,
 * each is an index into unifyfs_streams array */
void* unifyfs_stream_stack;

/* stack to track free directory streams,
 * each is an index into unifyfs_dirstreams array */
void* unifyfs_dirstream_stack;

/* mount point information */
char*  unifyfs_mount_prefix;
size_t unifyfs_mount_prefixlen = 0;

/* to track current working directory */
char* unifyfs_cwd;

/* mutex to lock stack operations */
pthread_mutex_t unifyfs_stack_mutex = PTHREAD_MUTEX_INITIALIZER;

/* single function to route all unsupported wrapper calls through */
int unifyfs_vunsupported(
    const char* fn_name,
    const char* file,
    int line,
    const char* fmt,
    va_list args)
{
    /* print a message about where in the UNIFYFS code we are */
    printf("UNIFYFS UNSUPPORTED: %s() at %s:%d: ", fn_name, file, line);

    /* print string with more info about call, e.g., param values */
    va_list args2;
    va_copy(args2, args);
    vprintf(fmt, args2);
    va_end(args2);

    /* TODO: optionally abort */

    return UNIFYFS_SUCCESS;
}

/* single function to route all unsupported wrapper calls through */
int unifyfs_unsupported(
    const char* fn_name,
    const char* file,
    int line,
    const char* fmt,
    ...)
{
    /* print string with more info about call, e.g., param values */
    va_list args;
    va_start(args, fmt);
    int rc = unifyfs_vunsupported(fn_name, file, line, fmt, args);
    va_end(args);
    return rc;
}

/* returns 1 if two input parameters will overflow their type when
 * added together */
inline int unifyfs_would_overflow_offt(off_t a, off_t b)
{
    /* if both parameters are positive, they could overflow when
     * added together */
    if (a > 0 && b > 0) {
        /* if the distance between a and max is greater than or equal to
         * b, then we could add a and b and still not exceed max */
        if (unifyfs_max_offt - a >= b) {
            return 0;
        }
        return 1;
    }

    /* if both parameters are negative, they could underflow when
     * added together */
    if (a < 0 && b < 0) {
        /* if the distance between min and a is less than or equal to
         * b, then we could add a and b and still not exceed min */
        if (unifyfs_min_offt - a <= b) {
            return 0;
        }
        return 1;
    }

    /* if a and b are mixed signs or at least one of them is 0,
     * then adding them together will produce a result closer to 0
     * or at least no further away than either value already is */
    return 0;
}

/* returns 1 if two input parameters will overflow their type when
 * added together */
inline int unifyfs_would_overflow_long(long a, long b)
{
    /* if both parameters are positive, they could overflow when
     * added together */
    if (a > 0 && b > 0) {
        /* if the distance between a and max is greater than or equal to
         * b, then we could add a and b and still not exceed max */
        if (unifyfs_max_long - a >= b) {
            return 0;
        }
        return 1;
    }

    /* if both parameters are negative, they could underflow when
     * added together */
    if (a < 0 && b < 0) {
        /* if the distance between min and a is less than or equal to
         * b, then we could add a and b and still not exceed min */
        if (unifyfs_min_long - a <= b) {
            return 0;
        }
        return 1;
    }

    /* if a and b are mixed signs or at least one of them is 0,
     * then adding them together will produce a result closer to 0
     * or at least no further away than either value already is */
    return 0;
}

/* lock access to shared data structures in superblock */
inline int unifyfs_stack_lock(void)
{
    if (unifyfs_use_single_shm) {
        return pthread_mutex_lock(&unifyfs_stack_mutex);
    }
    return 0;
}

/* unlock access to shared data structures in superblock */
inline int unifyfs_stack_unlock(void)
{
    if (unifyfs_use_single_shm) {
        return pthread_mutex_unlock(&unifyfs_stack_mutex);
    }
    return 0;
}

static void unifyfs_normalize_path(const char* path, char* normalized)
{
    /* if we have a relative path, prepend the current working directory */
    if (path[0] != '/' && unifyfs_cwd != NULL) {
        /* got a relative path, add our cwd */
        snprintf(normalized, UNIFYFS_MAX_FILENAME, "%s/%s", unifyfs_cwd, path);
    } else {
        snprintf(normalized, UNIFYFS_MAX_FILENAME, "%s", path);
    }

#ifdef USE_SPATH
    /* normalize path to handle '.', '..',
     * and extra or trailing '/' characters */
    char* str = spath_strdup_reduce_str(normalized);
    snprintf(normalized, UNIFYFS_MAX_FILENAME, "%s", str);
    free(str);
#endif /* USE_SPATH */
}

/* Given a path, which may relative or absoluate,
 * return 1 if we should intercept the path, 0 otherwise.
 * If path is to be intercepted, returned a normalized version in upath. */
inline int unifyfs_intercept_path(const char* path, char* upath)
{
    /* don't intercept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* if we have a relative path, prepend the current working directory */
    char target[UNIFYFS_MAX_FILENAME];
    unifyfs_normalize_path(path, target);

    /* if the path starts with our mount point, intercept it */
    int intercept = 0;
    if (strncmp(target, unifyfs_mount_prefix, unifyfs_mount_prefixlen) == 0) {
        /* characters in target up through mount point match,
         * assume we match */
        intercept = 1;

        /* if we have another character, it must be '/' */
        if (strlen(target) > unifyfs_mount_prefixlen &&
            target[unifyfs_mount_prefixlen] != '/') {
            intercept = 0;
        }
    }

    /* copy normalized path into upath */
    if (intercept) {
        strncpy(upath, target, UNIFYFS_MAX_FILENAME);
    }

    return intercept;
}

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
inline int unifyfs_intercept_fd(int* fd)
{
    int oldfd = *fd;

    /* don't intercept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    if (oldfd < unifyfs_fd_limit) {
        /* this fd is a real system fd, so leave it as is */
        return 0;
    } else if (oldfd < 0) {
        /* this is an invalid fd, so we should not intercept it */
        return 0;
    } else {
        /* this is an fd we generated and returned to the user,
         * so intercept the call and shift the fd */
        int newfd = oldfd - unifyfs_fd_limit;
        *fd = newfd;
        LOGDBG("Changing fd from exposed %d to internal %d", oldfd, newfd);
        return 1;
    }
}

/* given a file stream, return 1 if we should intercept this file,
 * 0 otherwise */
inline int unifyfs_intercept_stream(FILE* stream)
{
    /* don't intercept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * file stream array */
    unifyfs_stream_t* ptr   = (unifyfs_stream_t*) stream;
    unifyfs_stream_t* start = unifyfs_streams;
    unifyfs_stream_t* end   = start + UNIFYFS_CLIENT_MAX_FILEDESCS;
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given an directory stream, return 1 if we should intercept this
 * direcotry, 0 otherwise */
inline int unifyfs_intercept_dirstream(DIR* dirp)
{
    /* don't intercept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * directory stream array */

    unifyfs_dirstream_t* ptr   = (unifyfs_dirstream_t*) dirp;
    unifyfs_dirstream_t* start = unifyfs_dirstreams;
    unifyfs_dirstream_t* end   = start + UNIFYFS_CLIENT_MAX_FILEDESCS;
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given a path, return the file id */
inline int unifyfs_get_fid_from_path(const char* path)
{
    /* scan through active entries in filelist array looking
     * for a match of path */
    int i = 0;
    while (i < unifyfs_max_files) {
        if (unifyfs_filelist[i].in_use &&
            strcmp((void*)&unifyfs_filelist[i].filename, path) == 0) {
            LOGDBG("File found: unifyfs_filelist[%d].filename = %s",
                   i, (char*)&unifyfs_filelist[i].filename);
            return i;
        }
        i++;
    }

    /* couldn't find specified path */
    return -1;
}

/* initialize file descriptor structure for given fd value */
int unifyfs_fd_init(int fd)
{
    /* get pointer to file descriptor struct for this fd value */
    unifyfs_fd_t* filedesc = &(unifyfs_fds[fd]);

    /* set fid to -1 to indicate fd is not active,
     * set file position to max value,
     * disable read and write flags */
    filedesc->fid   = -1;
    filedesc->pos   = (off_t) -1;
    filedesc->read  = 0;
    filedesc->write = 0;

    return UNIFYFS_SUCCESS;
}

/* initialize file streams structure for given sid value */
int unifyfs_stream_init(int sid)
{
    /* get pointer to file stream struct for this id value */
    unifyfs_stream_t* s = &(unifyfs_streams[sid]);

    /* record our id so when given a pointer to the stream
     * struct we can easily recover our id value */
    s->sid = sid;

    /* set fd to -1 to indicate stream is not active */
    s->fd = -1;

    return UNIFYFS_SUCCESS;
}

/* initialize directory streams structure for given dirid value */
int unifyfs_dirstream_init(int dirid)
{
    /* get pointer to directory stream struct for this id value */
    unifyfs_dirstream_t* dirp = &(unifyfs_dirstreams[dirid]);

    /* initialize fields in structure */
    memset((void*) dirp, 0, sizeof(*dirp));

    /* record our id so when given a pointer to the stream
     * struct we can easily recover our id value */
    dirp->dirid = dirid;

    /* set fid to -1 to indicate stream is not active */
    dirp->fid = -1;

    return UNIFYFS_SUCCESS;
}

/* given a file descriptor, return the file id */
inline int unifyfs_get_fid_from_fd(int fd)
{
    /* check that file descriptor is within range */
    if (fd < 0 || fd >= UNIFYFS_CLIENT_MAX_FILEDESCS) {
        return -1;
    }

    /* get local file id that file descriptor is assocated with,
     * will be -1 if not active */
    int fid = unifyfs_fds[fd].fid;
    return fid;
}

/* return address of file descriptor structure or NULL if fd is out
 * of range */
inline unifyfs_fd_t* unifyfs_get_filedesc_from_fd(int fd)
{
    if (fd >= 0 && fd < UNIFYFS_CLIENT_MAX_FILEDESCS) {
        unifyfs_fd_t* filedesc = &(unifyfs_fds[fd]);
        return filedesc;
    }
    return NULL;
}

/* given a file id, return a pointer to the meta data,
 * otherwise return NULL */
unifyfs_filemeta_t* unifyfs_get_meta_from_fid(int fid)
{
    /* check that the file id is within range of our array */
    if (fid >= 0 && fid < unifyfs_max_files) {
        /* get a pointer to the file meta data structure */
        unifyfs_filemeta_t* meta = &unifyfs_filemetas[fid];
        return meta;
    }
    return NULL;
}

/* given a file id, return 1 if file is laminated, 0 otherwise */
int unifyfs_fid_is_laminated(int fid)
{
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if ((meta != NULL) && (meta->fid == fid)) {
        return meta->attrs.is_laminated;
    }
    return 0;
}

/* given a file descriptor, return 1 if file is laminated,
 * and 0 otherwise */
int unifyfs_fd_is_laminated(int fd)
{
    int fid = unifyfs_get_fid_from_fd(fd);
    int laminated = unifyfs_fid_is_laminated(fid);
    return laminated;
}

/* ---------------------------------------
 * Operations on file storage
 * --------------------------------------- */

/* allocate and initialize data management resource for file */
static int fid_store_alloc(int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if ((meta != NULL) && (meta->fid == fid)) {
        /* Initialize our segment tree that will record our writes */
        int rc = seg_tree_init(&meta->extents_sync);
        if (rc != 0) {
            return UNIFYFS_FAILURE;
        }

        /* Initialize our segment tree to track extents for all writes
         * by this process, can be used to read back local data */
        if (unifyfs_local_extents) {
            rc = seg_tree_init(&meta->extents);
            if (rc != 0) {
                /* free off extents_sync tree we initialized */
                seg_tree_destroy(&meta->extents_sync);
                return UNIFYFS_FAILURE;
            }
        }

        /* indicate that we're using LOGIO to store data for this file */
        meta->storage = FILE_STORAGE_LOGIO;

        return UNIFYFS_SUCCESS;
    } else {
        LOGERR("failed to get filemeta for fid=%d", fid);
    }

    return UNIFYFS_FAILURE;
}

/* free data management resource for file */
static int fid_storage_free(int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if ((meta != NULL) && (meta->fid == fid)) {
        if (meta->storage == FILE_STORAGE_LOGIO) {
            /* Free our write seg_tree */
            seg_tree_destroy(&meta->extents_sync);

            /* Free our extent seg_tree */
            if (unifyfs_local_extents) {
                seg_tree_destroy(&meta->extents);
            }
        }

        /* set storage type back to NULL */
        meta->storage = FILE_STORAGE_NULL;

        return UNIFYFS_SUCCESS;
    }

    return UNIFYFS_FAILURE;
}

/* =======================================
 * Operations on file ids
 * ======================================= */

/* checks to see if fid is a directory
 * returns 1 for yes
 * returns 0 for no */
int unifyfs_fid_is_dir(int fid)
{
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if ((meta != NULL) && (meta->attrs.mode & S_IFDIR)) {
        return 1;
    }
    return 0;
}

int unifyfs_gfid_from_fid(const int fid)
{
    /* check that local file id is in range */
    if (fid < 0 || fid >= unifyfs_max_files) {
        return -1;
    }

    /* return global file id, cached in file meta struct */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (meta != NULL) {
        return meta->attrs.gfid;
    } else {
        return -1;
    }
}

/* scan list of files and return fid corresponding to target gfid,
 * returns -1 if not found */
int unifyfs_fid_from_gfid(int gfid)
{
    int i;
    for (i = 0; i < unifyfs_max_files; i++) {
        if (unifyfs_filelist[i].in_use &&
            unifyfs_filemetas[i].attrs.gfid == gfid) {
            /* found a file id that's in use and it matches
             * the target fid, this is the one */
            return i;
        }
    }
    return -1;
}

/* Given a fid, return the path.  */
const char* unifyfs_path_from_fid(int fid)
{
    unifyfs_filename_t* fname = &unifyfs_filelist[fid];
    if (fname->in_use) {
        return fname->filename;
    }
    return NULL;
}

/* checks to see if a directory is empty
 * assumes that check for is_dir has already been made
 * only checks for full path matches, does not check relative paths,
 * e.g. ../dirname will not work
 * returns 1 for yes it is empty
 * returns 0 for no */
int unifyfs_fid_is_dir_empty(const char* path)
{
    int i = 0;
    while (i < unifyfs_max_files) {
        /* only check this element if it's active */
        if (unifyfs_filelist[i].in_use) {
            /* if the file starts with the path, it is inside of that directory
             * also check that it's not the directory entry itself */
            char* strptr = strstr(path, unifyfs_filelist[i].filename);
            if (strptr == unifyfs_filelist[i].filename &&
                strcmp(path, unifyfs_filelist[i].filename) != 0) {
                /* found a child item in path */
                LOGDBG("File found: unifyfs_filelist[%d].filename = %s",
                       i, (char*)&unifyfs_filelist[i].filename);
                return 0;
            }
        }

        /* go on to next file */
        i++;
    }

    /* couldn't find any files with this prefix, dir must be empty */
    return 1;
}

/* Return the global (laminated) size of the file */
off_t unifyfs_fid_global_size(int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (meta != NULL) {
        return meta->attrs.size;
    }
    return (off_t)-1;
}

/*
 * Return the size of the file.  If the file is laminated, return the
 * laminated size.  If the file is not laminated, return the local
 * size.
 */
off_t unifyfs_fid_logical_size(int fid)
{
    /* get meta data for this file */
    if (unifyfs_fid_is_laminated(fid)) {
        off_t size = unifyfs_fid_global_size(fid);
        return size;
    } else {
        /* invoke an rpc to ask the server what the file size is */

        /* sync any writes to disk before requesting file size */
        unifyfs_fid_sync(fid);

        /* get file size for this file */
        size_t filesize;
        int gfid = unifyfs_gfid_from_fid(fid);
        int ret = invoke_client_filesize_rpc(gfid, &filesize);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to get file size */
            return (off_t)-1;
        }
        return (off_t)filesize;
    }
}

/* if we have a local fid structure corresponding to the gfid
 * in question, we attempt the file lookup with the fid method
 * otherwise call back to the rpc */
off_t unifyfs_gfid_filesize(int gfid)
{
    off_t filesize = (off_t)-1;

    /* see if we have a fid for this gfid */
    int fid = unifyfs_fid_from_gfid(gfid);
    if (fid >= 0) {
        /* got a fid, look up file size through that
         * method, since it may avoid a server rpc call */
        filesize = unifyfs_fid_logical_size(fid);
    } else {
        /* no fid for this gfid,
         * look it up with server rpc */
        size_t size;
        int ret = invoke_client_filesize_rpc(gfid, &size);
        if (ret == UNIFYFS_SUCCESS) {
            /* got the file size successfully */
            filesize = size;
        }
    }

    return filesize;
}

/* Update local metadata for file from global metadata */
int unifyfs_fid_update_file_meta(int fid, unifyfs_file_attr_t* gfattr)
{
    if (NULL == gfattr) {
        return EINVAL;
    }

    /* lookup local metadata for file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (meta != NULL) {
        meta->attrs = *gfattr;
        return UNIFYFS_SUCCESS;
    }

    /* else, bad fid */
    return EINVAL;
}

/*
 * Set the metadata values for a file (after optionally creating it).
 * The gfid for the file is in f_meta->gfid.
 *
 * gfid:   The global file id on which to set metadata.
 *
 * op:     If set to FILE_ATTR_OP_CREATE, attempt to create the file first.
 *         If the file already exists, then update its metadata with the values
 *         from fid filemeta.  If not creating and the file does not exist,
 *         then the server will return an error.
 *
 * gfattr: The metadata values to store.
 */
int unifyfs_set_global_file_meta(
    int gfid,
    unifyfs_file_attr_op_e attr_op,
    unifyfs_file_attr_t* gfattr)
{
    /* check that we have an input buffer */
    if (NULL == gfattr) {
        return UNIFYFS_FAILURE;
    }

    /* force the gfid field value to match the gfid we're
     * submitting this under */
    gfattr->gfid = gfid;

    /* send file attributes to server */
    int ret = invoke_client_metaset_rpc(attr_op, gfattr);
    return ret;
}

int unifyfs_get_global_file_meta(int gfid, unifyfs_file_attr_t* gfattr)
{
    /* check that we have an output buffer to write to */
    if (NULL == gfattr) {
        return UNIFYFS_FAILURE;
    }

    /* attempt to lookup file attributes in key/value store */
    unifyfs_file_attr_t fmeta;
    int ret = invoke_client_metaget_rpc(gfid, &fmeta);
    if (ret == UNIFYFS_SUCCESS) {
        /* found it, copy attributes to output struct */
        *gfattr = fmeta;
    }
    return ret;
}

/*
 * Set the global metadata values for a file using local file
 * attributes associated with the given local file id.
 *
 * fid:    The local file id on which to base global metadata values.
 *
 * op:     If set to FILE_ATTR_OP_CREATE, attempt to create the file first.
 *         If the file already exists, then update its metadata with the values
 *         from fid filemeta.  If not creating and the file does not exist,
 *         then the server will return an error.
 */
int unifyfs_set_global_file_meta_from_fid(int fid, unifyfs_file_attr_op_e op)
{
    /* initialize an empty file attributes structure */
    unifyfs_file_attr_t fattr;
    unifyfs_file_attr_set_invalid(&fattr);

    /* lookup local metadata for file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    assert(meta != NULL);

    /* set global file id */
    fattr.gfid = meta->attrs.gfid;

    LOGDBG("setting global file metadata for fid:%d gfid:%d path:%s",
           fid, fattr.gfid, meta->attrs.filename);

    unifyfs_file_attr_update(op, &fattr, &(meta->attrs));

    LOGDBG("using following attributes");
    debug_print_file_attr(&fattr);

    /* submit file attributes to global key/value store */
    int ret = unifyfs_set_global_file_meta(fattr.gfid, op, &fattr);
    return ret;
}

/* allocate a file id slot for a new file
 * return the fid or -1 on error */
int unifyfs_fid_alloc(void)
{
    unifyfs_stack_lock();
    int fid = unifyfs_stack_pop(free_fid_stack);
    unifyfs_stack_unlock();
    LOGDBG("unifyfs_stack_pop() gave %d", fid);
    if (fid < 0) {
        /* need to create a new file, but we can't */
        LOGERR("unifyfs_stack_pop() failed (%d)", fid);
        return -EMFILE;
    }
    return fid;
}

/* return the file id back to the free pool */
int unifyfs_fid_free(int fid)
{
    unifyfs_stack_lock();
    unifyfs_stack_push(free_fid_stack, fid);
    unifyfs_stack_unlock();
    return UNIFYFS_SUCCESS;
}

/* add a new file and initialize metadata
 * returns the new fid, or negative value on error */
int unifyfs_fid_create_file(const char* path)
{
    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return -ENAMETOOLONG;
    }

    /* allocate an id for this file */
    int fid = unifyfs_fid_alloc();
    if (fid < 0)  {
        return fid;
    }

    /* mark this slot as in use */
    unifyfs_filelist[fid].in_use = 1;

    /* copy file name into slot */
    strlcpy((void*)&unifyfs_filelist[fid].filename, path, UNIFYFS_MAX_FILENAME);
    LOGDBG("Filename %s got unifyfs fid %d",
           unifyfs_filelist[fid].filename, fid);

    /* get metadata for this file id */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    assert(meta != NULL);

    /* initialize file attributes */
    unifyfs_file_attr_set_invalid(&(meta->attrs));
    meta->attrs.gfid = unifyfs_generate_gfid(path);
    meta->attrs.size = 0;
    meta->attrs.mode = UNIFYFS_STAT_DEFAULT_FILE_MODE;
    meta->attrs.is_laminated = 0;
    meta->attrs.filename = (char*)&(unifyfs_filelist[fid].filename);

    /* use client user/group */
    meta->attrs.uid = getuid();
    meta->attrs.gid = getgid();

    /* use current time for atime/mtime/ctime */
    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);
    meta->attrs.atime = tp;
    meta->attrs.mtime = tp;
    meta->attrs.ctime = tp;

    /* set UnifyFS client metadata */
    meta->fid          = fid;
    meta->storage      = FILE_STORAGE_NULL;
    meta->needs_sync   = 0;

    /* PTHREAD_PROCESS_SHARED allows Process-Shared Synchronization */
    meta->flock_status = UNLOCKED;
    pthread_spin_init(&meta->fspinlock, PTHREAD_PROCESS_SHARED);

    return fid;
}

int unifyfs_fid_create_directory(const char* path)
{
    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return (int) ENAMETOOLONG;
    }

    /* get local and global file ids */
    int fid  = unifyfs_get_fid_from_path(path);
    int gfid = unifyfs_generate_gfid(path);

    /* test whether we have info for file in our local file list */
    int found_local = (fid >= 0);

    /* test whether we have metadata for file in global key/value store */
    unifyfs_file_attr_t gfattr = { 0, };
    if (unifyfs_get_global_file_meta(gfid, &gfattr) == UNIFYFS_SUCCESS) {
        /* can't create if it already exists */
        return EEXIST;
    }

    if (found_local) {
        /* exists locally, but not globally
         *
         * FIXME: so, we have detected the cache inconsistency here.
         * we cannot simply unlink or remove the entry because then we also
         * need to check whether any subdirectories or files exist.
         *
         * this can happen when
         * - a process created a directory. this process (A) has opened it at
         *   least once.
         * - then, the directory has been deleted by another process (B). it
         *   deletes the global entry without checking any local used entries
         *   in other processes.
         *
         * we currently return EEXIS, and this needs to be addressed according
         * to a consistency model this fs intance assumes.
         */
        return EEXIST;
    }

    /* now, we need to create a new directory. we reuse the file creation
     * method and then update the mode to indicate it's a directory */
    fid = unifyfs_fid_create_file(path);
    if (fid < 0) {
        return -fid;
    }
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    assert(meta != NULL);
    meta->attrs.mode = (meta->attrs.mode & ~S_IFREG) | S_IFDIR;

    /* insert global meta data for directory */
    unifyfs_file_attr_op_e op = UNIFYFS_FILE_ATTR_OP_CREATE;
    int ret = unifyfs_set_global_file_meta_from_fid(fid, op);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("Failed to populate the global meta entry for %s (fid:%d)",
               path, fid);
        return ret;
    }

    return UNIFYFS_SUCCESS;
}

/* delete a file id, free its local storage resources and return
 * the file id to free stack */
int unifyfs_fid_delete(int fid)
{
    /* finalize the storage we're using for this file */
    int rc = fid_storage_free(fid);
    if (rc != UNIFYFS_SUCCESS) {
        /* failed to release structures tracking storage,
         * bail out to keep its file id active */
        return rc;
    }

    /* set this file id as not in use */
    unifyfs_filelist[fid].in_use = 0;

    /* add this id back to the free stack */
    rc = unifyfs_fid_free(fid);
    if (rc != UNIFYFS_SUCCESS) {
        /* storage for the file was released, but we hit
         * an error while freeing the file id */
        return rc;
    }

    return UNIFYFS_SUCCESS;
}

/* Write count bytes from buf into file starting at offset pos.
 *
 * Returns UNIFYFS_SUCCESS, or an error code
 */
int unifyfs_fid_write(
    int fid,          /* local file id to write to */
    off_t pos,        /* starting position in file */
    const void* buf,  /* buffer to be written */
    size_t count,     /* number of bytes to write */
    size_t* nwritten) /* returns number of bytes written */
{
    int rc;

    /* assume we won't write anything */
    *nwritten = 0;

    /* short-circuit a 0-byte write */
    if (count == 0) {
        return UNIFYFS_SUCCESS;
    }

    /* get meta for this file id */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    assert(meta != NULL);

    if (meta->attrs.is_laminated) {
        /* attempt to write to laminated file, return read-only filesystem */
        return EROFS;
    }

    /* determine storage type to write file data */
    if (meta->storage == FILE_STORAGE_LOGIO) {
        /* file stored in logged i/o */
        rc = unifyfs_fid_logio_write(fid, meta, pos, buf, count, nwritten);
        if (rc == UNIFYFS_SUCCESS) {
            /* write succeeded, remember that we have new data
             * that needs to be synced with the server */
            meta->needs_sync = 1;

            /* optionally sync after every write */
            if (unifyfs_write_sync) {
                int ret = unifyfs_sync_extents(fid);
                if (ret != UNIFYFS_SUCCESS) {
                    LOGERR("client sync after write failed");
                    rc = ret;
                }
            }
        }
    } else {
        /* unknown storage type */
        LOGERR("unknown storage type for fid=%d", fid);
        rc = EIO;
    }

    return rc;
}

/* truncate file id to given length, frees resources if length is
 * less than size and allocates and zero-fills new bytes if length
 * is more than size */
int unifyfs_fid_truncate(int fid, off_t length)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    assert(meta != NULL);

    /* truncate is not valid for directories */
    if (S_ISDIR(meta->attrs.mode)) {
        return EISDIR;
    }

    if (meta->attrs.is_laminated) {
        /* Can't truncate a laminated file */
        return EINVAL;
    }

    if (meta->storage != FILE_STORAGE_LOGIO) {
        /* unknown storage type */
        return EIO;
    }

    /* remove/update writes past truncation size for this file id */
    int rc = truncate_write_meta(meta, length);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* truncate is a sync point */
    rc = unifyfs_fid_sync(fid);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* update global size in filemeta to reflect truncated size.
     * note that log size is not affected */
    meta->attrs.size = length;

    /* invoke truncate rpc */
    int gfid = unifyfs_gfid_from_fid(fid);
    rc = invoke_client_truncate_rpc(gfid, length);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    return UNIFYFS_SUCCESS;
}

/* sync data for file id to server if needed */
int unifyfs_fid_sync(int fid)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* get metadata for the file id */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    assert(meta != NULL);

    /* sync data with server */
    if (meta->needs_sync) {
        ret = unifyfs_sync_extents(fid);
    }

    return ret;
}

/* opens a new file id with specified path, access flags, and permissions,
 * fills outfid with file id and outpos with position for current file pointer,
 * returns UNIFYFS error code
 */
int unifyfs_fid_open(
    const char* path, /* path of file to be opened */
    int flags,        /* flags bits as from open(2) */
    mode_t mode,      /* mode bits as from open(2) */
    int* outfid,      /* allocated local file id if open is successful */
    off_t* outpos)    /* initial file position if open is successful */
{
    int ret;

    /* set the pointer to the start of the file */
    off_t pos = 0;

    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return ENAMETOOLONG;
    }

    /*
     * TODO: The test of file existence involves both local and global checks.
     * However, the testing below does not seem to cover all cases. For
     * instance, a globally unlinked file might be still cached locally because
     * the broadcast for cache invalidation has not been implemented, yet.
     */

    /* look for local and global file ids */
    int fid  = unifyfs_get_fid_from_path(path);
    int gfid = unifyfs_generate_gfid(path);
    LOGDBG("unifyfs_get_fid_from_path() gave %d (gfid = %d)", fid, gfid);

    /* test whether we have info for file in our local file list */
    int found_local = (fid >= 0);

    /* determine whether any write flags are specified */
    int open_for_write = flags & (O_RDWR | O_WRONLY);

    /* struct to hold global metadata for file */
    unifyfs_file_attr_t gfattr = { 0, };

    /* if O_CREAT,
     *   if not local, allocate fid and storage
     *   create from local fid meta
     *   attempt to create global inode
     *   if EEXIST and O_EXCL, error and release fid/storage
     *   lookup global meta
     *   check that local and global info are consistent
     *   if O_TRUNC and not laminated, truncate
     * else
     *   lookup global meta
     *     if not found, error
     *   check that local and global info are consistent
     * if O_APPEND, set pos to file size
     */

    /* flag indicating whether file should be truncated */
    int need_truncate = 0;

    /* determine whether we are creating a new file
     * or opening an existing one */
    if (flags & O_CREAT) {
        /* user wants to create a new file,
         * allocate a local file id structure if needed */
        if (!found_local) {
            /* initialize local metadata for this file */
            fid = unifyfs_fid_create_file(path);
            if (fid < 0) {
                LOGERR("failed to create a new file %s", path);
                return -fid;
            }

            /* initialize local storage for this file */
            ret = fid_store_alloc(fid);
            if (ret != UNIFYFS_SUCCESS) {
                LOGERR("failed to allocate storage space for file %s (fid=%d)",
                    path, fid);
                unifyfs_fid_delete(fid);
                return ret;
            }

            /* TODO: set meta->mode bits to mode variable */
        }

        /* insert file attribute for file in key-value store */
        unifyfs_file_attr_op_e op = UNIFYFS_FILE_ATTR_OP_CREATE;
        ret = unifyfs_set_global_file_meta_from_fid(fid, op);
        if (ret == EEXIST && !(flags & O_EXCL)) {
            /* File didn't exist before, but now it does.
             * Another process beat us to the punch in creating it.
             * Read its metadata to update our cache. */
            ret = unifyfs_get_global_file_meta(gfid, &gfattr);
            if (ret == UNIFYFS_SUCCESS) {
                if (found_local) {
                    /* TODO: check that global metadata is consistent with
                     * our existing local entry */
                }

                /* Successful in fetching metadata for existing file.
                 * Update our local cache using that metadata. */
                unifyfs_fid_update_file_meta(fid, &gfattr);
            } else {
                /* Failed to get metadata for a file that should exist.
                 * Perhaps it was since deleted.  We could try to create
                 * it again and loop through these steps, but for now
                 * consider this situation to be an error. */
                LOGERR("Failed to get metadata on existing file %s (fid:%d)",
                    path, fid);
            }

            /* check for truncate if the file exists already */
            if ((flags & O_TRUNC) && open_for_write && !gfattr.is_laminated) {
                need_truncate = 1;
            }
        }
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("Failed to populate the global meta entry for %s (fid:%d)",
                path, fid);
            if (!found_local) {
                /* free fid we just allocated above,
                 * but don't do that by calling fid_unlink */
                unifyfs_fid_delete(fid);
            }
            return ret;
        }
    } else {
        /* trying to open without creating, file must already exist,
         * lookup global metadata for file */
        ret = unifyfs_get_global_file_meta(gfid, &gfattr);
        if (ret != UNIFYFS_SUCCESS) {
            /* bail out if we failed to find global file */
            if (found_local && ret == ENOENT) {
                /* Have a local entry, but there is no global entry.
                 * Perhaps global file was unlinked?
                 * Invalidate our local entry. */
                LOGDBG("file found locally, but seems to be deleted globally. "
                       "invalidating the local cache.");
                unifyfs_fid_delete(fid);
            }

            return ret;
        }

        /* succeeded in global lookup for file,
         * allocate a local file id structure if needed */
        if (!found_local) {
            /* initialize local metadata for this file */
            fid = unifyfs_fid_create_file(path);
            if (fid < 0) {
                LOGERR("failed to create a new file %s", path);
                return -fid;
            }

            /* initialize local storage for this file */
            ret = fid_store_alloc(fid);
            if (ret != UNIFYFS_SUCCESS) {
                LOGERR("failed to allocate storage space for file %s (fid=%d)",
                       path, fid);
                /* free fid we just allocated above,
                 * but don't do that by calling fid_unlink */
                unifyfs_fid_delete(fid);
                return ret;
            }
        } else {
            /* TODO: already have a local entry for this path and found
             * a global entry, check that they are consistent */
        }

        /* Successful in fetching metadata for existing file.
         * Update our local cache using that metadata. */
        unifyfs_fid_update_file_meta(fid, &gfattr);

        /* check if we need to truncate the existing file */
        if ((flags & O_TRUNC) && open_for_write && !gfattr.is_laminated) {
            need_truncate = 1;
        }
    }

    /* if given O_DIRECTORY, the named file must be a directory */
    if ((flags & O_DIRECTORY) && !unifyfs_fid_is_dir(fid)) {
        if (!found_local) {
            /* free fid we just allocated above,
             * but don't do that by calling fid_unlink */
            unifyfs_fid_delete(fid);
        }
        return ENOTDIR;
    }

    /* TODO: does O_DIRECTORY really have to be given to open a directory? */
    if (!(flags & O_DIRECTORY) && unifyfs_fid_is_dir(fid)) {
        if (!found_local) {
            /* free fid we just allocated above,
             * but don't do that by calling fid_unlink */
            unifyfs_fid_delete(fid);
        }
        return EISDIR;
    }

    /*
     * Catch any case where we could potentially want to write to a laminated
     * file.
     */
    if (gfattr.is_laminated &&
        ((flags & (O_CREAT | O_TRUNC | O_APPEND | O_WRONLY)) ||
         ((mode & 0222) && (flags != O_RDONLY)))) {
            LOGDBG("Can't open laminated file %s with a writable flag.", path);
            /* TODO: free fid we just allocated above,
             * but don't do that by calling fid_unlink */
            if (!found_local) {
                /* free fid we just allocated above,
                 * but don't do that by calling fid_unlink */
                unifyfs_fid_delete(fid);
            }
            return EROFS;
    }

    /* truncate the file, if we have to */
    if (need_truncate) {
        ret = unifyfs_fid_truncate(fid, 0);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("Failed to truncate the file %s", path);
            return ret;
        }
    }

    /* do we normally update position to EOF with O_APPEND? */
    if ((flags & O_APPEND) && open_for_write) {
        /* We only support O_APPEND on non-laminated files */
        pos = unifyfs_fid_logical_size(fid);
    }

    /* return local file id and starting file position */
    *outfid = fid;
    *outpos = pos;

    return UNIFYFS_SUCCESS;
}

int unifyfs_fid_close(int fid)
{
    /* TODO: clear any held locks */

    /* nothing to do here, just a place holder */
    return UNIFYFS_SUCCESS;
}

/* unlink file and then delete its associated state */
int unifyfs_fid_unlink(int fid)
{
    int rc;

    /* invoke unlink rpc */
    int gfid = unifyfs_gfid_from_fid(fid);
    rc = invoke_client_unlink_rpc(gfid);
    if (rc != UNIFYFS_SUCCESS) {
        /* TODO: if item does not exist globally, but just locally,
         * we still want to delete item locally */
        return rc;
    }

    /* finalize the storage we're using for this file */
    rc = unifyfs_fid_delete(fid);
    if (rc != UNIFYFS_SUCCESS) {
        /* released storage for file, but failed to release
         * structures tracking storage, again bail out to keep
         * its file id active */
        return rc;
    }

    return UNIFYFS_SUCCESS;
}

/* =======================================
 * Operations to mount/unmount file system
 * ======================================= */

/* -------------
 * static APIs
 * ------------- */

/* The super block is a region of shared memory that is used to
 * persist file system meta data.  It also contains a fixed-size
 * region for keeping log index entries for each file.
 *
 *  - stack of free local file ids of length max_files,
 *    the local file id is used to index into other data
 *    structures
 *
 *  - array of unifyfs_filename structs, indexed by local
 *    file id, provides a field indicating whether file
 *    slot is in use and if so, the current file name
 *
 *  - array of unifyfs_filemeta structs, indexed by local
 *    file id
 *
 *  - count of number of active index entries
 *  - array of index metadata to track physical offset
 *    of logical file data, of length unifyfs_max_index_entries,
 *    entries added during write operations
 */

/* compute memory size of superblock in bytes,
 * critical to keep this consistent with
 * init_superblock_pointers */
static size_t get_superblock_size(void)
{
    size_t sb_size = 0;

    /* header: uint32_t to hold magic number to indicate
     * that superblock is initialized */
    sb_size += sizeof(uint32_t);

    /* free file id stack */
    sb_size += unifyfs_stack_bytes(unifyfs_max_files);

    /* file name struct array */
    sb_size += unifyfs_max_files * sizeof(unifyfs_filename_t);

    /* file metadata struct array */
    sb_size += unifyfs_max_files * sizeof(unifyfs_filemeta_t);

    /* index region size */
    sb_size += unifyfs_page_size;
    sb_size += unifyfs_max_index_entries * sizeof(unifyfs_index_t);

    /* return number of bytes */
    return sb_size;
}

static inline
char* next_page_align(char* ptr)
{
    intptr_t orig = (intptr_t) ptr;
    intptr_t aligned = orig;
    intptr_t offset = orig % unifyfs_page_size;
    if (offset) {
        aligned += (unifyfs_page_size - offset);
    }
    LOGDBG("orig=0x%p, next-page-aligned=0x%p", ptr, (char*)aligned);
    return (char*) aligned;
}

/* initialize our global pointers into the given superblock */
static void init_superblock_pointers(void* superblock)
{
    char* ptr = (char*)superblock;

    /* jump over header (right now just a uint32_t to record
     * magic value of 0xdeadbeef if initialized */
    ptr += sizeof(uint32_t);

    /* stack to manage free file ids */
    free_fid_stack = ptr;
    ptr += unifyfs_stack_bytes(unifyfs_max_files);

    /* record list of file names */
    unifyfs_filelist = (unifyfs_filename_t*)ptr;
    ptr += unifyfs_max_files * sizeof(unifyfs_filename_t);

    /* array of file meta data structures */
    unifyfs_filemetas = (unifyfs_filemeta_t*)ptr;
    ptr += unifyfs_max_files * sizeof(unifyfs_filemeta_t);

    /* record pointer to number of index entries */
    unifyfs_indices.ptr_num_entries = (size_t*)ptr;

    /* pointer to array of index entries */
    ptr += unifyfs_page_size;
    unifyfs_indices.index_entry = (unifyfs_index_t*)ptr;
    ptr += unifyfs_max_index_entries * sizeof(unifyfs_index_t);

    /* compute size of memory we're using and check that
     * it matches what we allocated */
    size_t ptr_size = (size_t)(ptr - (char*)superblock);
    if (ptr_size > shm_super_ctx->size) {
        LOGERR("Data structures in superblock extend beyond its size");
    }
}

/* initialize data structures for first use */
static int init_superblock_structures(void)
{
    int i;
    for (i = 0; i < unifyfs_max_files; i++) {
        /* indicate that file id is not in use by setting flag to 0 */
        unifyfs_filelist[i].in_use = 0;
    }

    /* initialize stack of free file ids */
    unifyfs_stack_init(free_fid_stack, unifyfs_max_files);

    /* initialize count of key/value entries */
    *(unifyfs_indices.ptr_num_entries) = 0;

    LOGDBG("Meta-stacks initialized!");

    return UNIFYFS_SUCCESS;
}

/* create superblock of specified size and name, or attach to existing
 * block if available */
static int init_superblock_shm(size_t super_sz)
{
    char shm_name[SHMEM_NAME_LEN] = {0};

    /* attach shmem region for client's superblock */
    sprintf(shm_name, SHMEM_SUPER_FMTSTR, unifyfs_app_id, unifyfs_client_id);
    shm_context* shm_ctx = unifyfs_shm_alloc(shm_name, super_sz);
    if (NULL == shm_ctx) {
        LOGERR("Failed to attach to shmem superblock region %s", shm_name);
        return UNIFYFS_ERROR_SHMEM;
    }
    shm_super_ctx = shm_ctx;

    /* init our global variables to point to spots in superblock */
    void* addr = shm_ctx->addr;
    init_superblock_pointers(addr);

    /* initialize structures in superblock if it's newly allocated,
     * we depend on shm_open setting all bytes to 0 to know that
     * it is not initialized */
    uint32_t initialized = *(uint32_t*)addr;
    if (initialized == 0) {
        /* not yet initialized, so initialize values within superblock */
        init_superblock_structures();

        /* superblock structure has been initialized,
         * so set flag to indicate that fact */
        *(uint32_t*)addr = (uint32_t)0xDEADBEEF;
    } else {
        /* In this case, we have reattached to an existing superblock from
         * an earlier run.  We need to reset the segtree pointers to
         * newly allocated segtrees, because they point to structures
         * allocated in the last run whose memory addresses are no longer
         * valid. */

        /* TODO: what to do if a process calls unifyfs_init multiple times
         * in a run? */

        /* Clear any index entries from the cache.  We do this to ensure
         * the newly allocated seg trees are consistent with the extents
         * in the index.  It would be nice to call unifyfs_sync_extents to flush
         * any entries to the server, but we can't do that since that will
         * try to rewrite the index using the trees, which point to invalid
         * memory at this point. */
        /* initialize count of key/value entries */
        *(unifyfs_indices.ptr_num_entries) = 0;

        int i;
        for (i = 0; i < unifyfs_max_files; i++) {
            /* if the file entry is active, reset its segment trees */
            if (unifyfs_filelist[i].in_use) {
                /* got a live file, get pointer to its metadata */
                unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(i);
                assert(meta != NULL);

                /* Reset our segment tree that will record our writes */
                seg_tree_init(&meta->extents_sync);

                /* Reset our segment tree to track extents for all writes
                 * by this process, can be used to read back local data */
                if (unifyfs_local_extents) {
                    seg_tree_init(&meta->extents);
                }
            }
        }
    }

    /* return starting memory address of super block */
    return UNIFYFS_SUCCESS;
}

int unifyfs_init(unifyfs_cfg_t* clnt_cfg)
{
    int rc;
    int i;
    bool b;
    long l;
    unsigned long long bits;
    char* cfgval;

    if (!unifyfs_initialized) {

#ifdef UNIFYFS_GOTCHA
        rc = setup_gotcha_wrappers();
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to setup gotcha wrappers");
            return rc;
        }
#endif

        /* as a hack to support fgetpos/fsetpos, we store the value of
         * a void* in an fpos_t so check that there's room and at least
         * print a message if this won't work */
        if (sizeof(fpos_t) < sizeof(void*)) {
            LOGERR("fgetpos/fsetpos will not work correctly");
            unifyfs_fpos_enabled = 0;
        }

        /* look up page size for buffer alignment */
        unifyfs_page_size = get_page_size();

        /* compute min and max off_t values */
        bits = sizeof(off_t) * 8;
        unifyfs_max_offt = (off_t)((1ULL << (bits - 1ULL)) - 1ULL);
        unifyfs_min_offt = (off_t)(-(1ULL << (bits - 1ULL)));

        /* compute min and max long values */
        unifyfs_max_long = LONG_MAX;
        unifyfs_min_long = LONG_MIN;

        /* set our current working directory if user gave us one */
        cfgval = clnt_cfg->client_cwd;
        if (cfgval != NULL) {
            unifyfs_cwd = strdup(cfgval);

            /* check that cwd falls somewhere under the mount point */
            int cwd_within_mount = 0;
            if (strncmp(unifyfs_cwd, unifyfs_mount_prefix,
                unifyfs_mount_prefixlen) == 0) {
                /* characters in target up through mount point match,
                 * assume we match */
                cwd_within_mount = 1;

                /* if we have another character, it must be '/' */
                if (strlen(unifyfs_cwd) > unifyfs_mount_prefixlen &&
                    unifyfs_cwd[unifyfs_mount_prefixlen] != '/') {
                    cwd_within_mount = 0;
                }
            }
            if (!cwd_within_mount) {
                /* path given in CWD is outside of the UnifyFS mount point */
                LOGERR("UNIFYFS_CLIENT_CWD '%s' must be within the mount '%s'",
                    unifyfs_cwd, unifyfs_mount_prefix);

                /* ignore setting and set back to NULL */
                free(unifyfs_cwd);
                unifyfs_cwd = NULL;
            }
        } else {
            /* user did not specify a CWD, so initialize with the actual
             * current working dir */
            char* cwd = getcwd(NULL, 0);
            if (cwd != NULL) {
                unifyfs_cwd = cwd;
            } else {
                LOGERR("Failed getcwd (%s)", strerror(errno));
            }
        }

        /* determine max number of files to store in file system */
        unifyfs_max_files = UNIFYFS_CLIENT_MAX_FILES;
        cfgval = clnt_cfg->client_max_files;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_max_files = (int)l;
            }
        }

        /* Determine if we should track all write extents and use them
         * to service read requests if all data is local */
        unifyfs_local_extents = 0;
        cfgval = clnt_cfg->client_local_extents;
        if (cfgval != NULL) {
            rc = configurator_bool_val(cfgval, &b);
            if (rc == 0) {
                unifyfs_local_extents = (bool)b;
            }
        }

        /* Determine whether we automatically sync every write to server.
         * This slows write performance, but it can serve as a work
         * around for apps that do not have all necessary syncs. */
        unifyfs_write_sync = false;
        cfgval = clnt_cfg->client_write_sync;
        if (cfgval != NULL) {
            rc = configurator_bool_val(cfgval, &b);
            if (rc == 0) {
                unifyfs_write_sync = (bool)b;
            }
        }

        /* Determine SUPER MAGIC value to return from statfs.
         * Use UNIFYFS_SUPER_MAGIC if true, TMPFS_SUPER_MAGIC otherwise. */
        unifyfs_super_magic = true;
        cfgval = client_cfg.client_super_magic;
        if (cfgval != NULL) {
            rc = configurator_bool_val(cfgval, &b);
            if (rc == 0) {
                unifyfs_super_magic = (bool)b;
            }
        }

        /* define size of buffer used to cache key/value pairs for
         * data offsets before passing them to the server */
        unifyfs_index_buf_size = UNIFYFS_CLIENT_WRITE_INDEX_SIZE;
        cfgval = clnt_cfg->client_write_index_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_index_buf_size = (size_t)l;
            }
        }
        unifyfs_max_index_entries =
            unifyfs_index_buf_size / sizeof(unifyfs_index_t);

        /* record the max fd for the system */
        /* RLIMIT_NOFILE specifies a value one greater than the maximum
         * file descriptor number that can be opened by this process */
        struct rlimit r_limit;

        if (getrlimit(RLIMIT_NOFILE, &r_limit) < 0) {
            LOGERR("getrlimit failed: errno=%d (%s)", errno, strerror(errno));
            return UNIFYFS_FAILURE;
        }
        unifyfs_fd_limit = r_limit.rlim_cur;
        LOGDBG("FD limit for system = %ld", unifyfs_fd_limit);

        /* initialize file descriptor structures */
        int num_fds = UNIFYFS_CLIENT_MAX_FILEDESCS;
        for (i = 0; i < num_fds; i++) {
            unifyfs_fd_init(i);
        }

        /* initialize file stream structures */
        int num_streams = UNIFYFS_CLIENT_MAX_FILEDESCS;
        for (i = 0; i < num_streams; i++) {
            unifyfs_stream_init(i);
        }

        /* initialize directory stream structures */
        int num_dirstreams = UNIFYFS_CLIENT_MAX_FILEDESCS;
        for (i = 0; i < num_dirstreams; i++) {
            unifyfs_dirstream_init(i);
        }

        /* initialize stack of free fd values */
        size_t free_fd_size = unifyfs_stack_bytes(num_fds);
        unifyfs_fd_stack = malloc(free_fd_size);
        unifyfs_stack_init(unifyfs_fd_stack, num_fds);

        /* initialize stack of free stream values */
        size_t free_stream_size = unifyfs_stack_bytes(num_streams);
        unifyfs_stream_stack = malloc(free_stream_size);
        unifyfs_stack_init(unifyfs_stream_stack, num_streams);

        /* initialize stack of free directory stream values */
        size_t free_dirstream_size = unifyfs_stack_bytes(num_dirstreams);
        unifyfs_dirstream_stack = malloc(free_dirstream_size);
        unifyfs_stack_init(unifyfs_dirstream_stack, num_dirstreams);

        /* determine the size of the superblock */
        size_t shm_super_size = get_superblock_size();

        /* get a superblock of shared memory and initialize our
         * global variables for this block */
        rc = init_superblock_shm(shm_super_size);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to initialize superblock shmem");
            return rc;
        }

        /* initialize active_mreads arraylist */
        active_mreads = arraylist_create(UNIFYFS_CLIENT_MAX_READ_COUNT);
        if (NULL == active_mreads) {
            LOGERR("failed to create arraylist for active reads");
            return UNIFYFS_FAILURE;
        }

        /* initialize log-based I/O context */
        rc = unifyfs_logio_init_client(unifyfs_app_id, unifyfs_client_id,
                                       clnt_cfg, &logio_ctx);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("failed to initialize log-based I/O (rc = %s)",
                   unifyfs_rc_enum_str(rc));
            return rc;
        }

        /* remember that we've now initialized the library */
        unifyfs_initialized = 1;
    }

    return UNIFYFS_SUCCESS;
}

/* free resources allocated during unifyfs_init().
 * generally, we do this in reverse order with respect to
 * how things were initialized */
int unifyfs_fini(void)
{
    int rc = UNIFYFS_SUCCESS;

    if (!unifyfs_initialized) {
        /* not initialized yet, so we shouldn't call finalize */
        return UNIFYFS_FAILURE;
    }

    /* close spillover files */
    if (NULL != logio_ctx) {
        unifyfs_logio_close(logio_ctx, 0);
        logio_ctx = NULL;
    }
    if (unifyfs_spillmetablock != -1) {
        close(unifyfs_spillmetablock);
        unifyfs_spillmetablock = -1;
    }

    /* detach from superblock shmem, but don't unlink the file so that
     * a later client can reattach. */
    unifyfs_shm_free(&shm_super_ctx);

    /* free directory stream stack */
    if (unifyfs_dirstream_stack != NULL) {
        free(unifyfs_dirstream_stack);
        unifyfs_dirstream_stack = NULL;
    }

    /* free file stream stack */
    if (unifyfs_stream_stack != NULL) {
        free(unifyfs_stream_stack);
        unifyfs_stream_stack = NULL;
    }

    /* free file descriptor stack */
    if (unifyfs_fd_stack != NULL) {
        free(unifyfs_fd_stack);
        unifyfs_fd_stack = NULL;
    }

    /* no longer initialized, so update the flag */
    unifyfs_initialized = 0;

    return rc;
}


/* ---------------
 * external APIs
 * --------------- */

/* Fill mount rpc input struct with client-side context info */
void fill_client_mount_info(unifyfs_cfg_t* clnt_cfg,
                            unifyfs_mount_in_t* in)
{
    in->dbg_rank = client_rank;
    in->mount_prefix = strdup(clnt_cfg->unifyfs_mountpoint);
}

/* Fill attach rpc input struct with client-side context info */
void fill_client_attach_info(unifyfs_cfg_t* clnt_cfg,
                             unifyfs_attach_in_t* in)
{
    size_t meta_offset = (char*)unifyfs_indices.ptr_num_entries -
                         (char*)shm_super_ctx->addr;
    size_t meta_size   = unifyfs_max_index_entries
                         * sizeof(unifyfs_index_t);

    in->app_id            = unifyfs_app_id;
    in->client_id         = unifyfs_client_id;
    in->shmem_super_size  = shm_super_ctx->size;
    in->meta_offset       = meta_offset;
    in->meta_size         = meta_size;

    if (NULL != logio_ctx->shmem) {
        in->logio_mem_size = logio_ctx->shmem->size;
    } else {
        in->logio_mem_size = 0;
    }

    in->logio_spill_size = logio_ctx->spill_sz;
    if (logio_ctx->spill_sz) {
        in->logio_spill_dir = strdup(clnt_cfg->logio_spill_dir);
    } else {
        in->logio_spill_dir = NULL;
    }
}

/**
 * mount a file system at a given prefix
 * subtype: 0-> log-based file system;
 * 1->striping based file system, not implemented yet.
 * @param prefix: directory prefix
 * @param size: the number of ranks
 * @param l_app_id: application ID
 * @return success/error code
 */
int unifyfs_mount(
    const char prefix[],
    int rank,
    size_t size,
    int l_app_id)
{
    int rc;
    int kv_rank, kv_nranks;

    if (-1 != unifyfs_mounted) {
        if (l_app_id != unifyfs_mounted) {
            LOGERR("multiple mount support not yet implemented");
            return UNIFYFS_FAILURE;
        } else {
            LOGDBG("already mounted");
            return UNIFYFS_SUCCESS;
        }
    }

    // record our rank for debugging messages
    client_rank = rank;
    global_rank_cnt = (int)size;

    // print log messages to stderr
    unifyfs_log_open(NULL);

    // initialize configuration
    rc = unifyfs_config_init(&client_cfg, 0, NULL, 0, NULL);
    if (rc) {
        LOGERR("failed to initialize configuration.");
        return UNIFYFS_FAILURE;
    }
    client_cfg.ptype = UNIFYFS_CLIENT;

    // set log level from config
    char* cfgval = client_cfg.log_verbosity;
    if (cfgval != NULL) {
        long l;
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            unifyfs_set_log_level((unifyfs_log_level_t)l);
        }
    }

    // record mountpoint prefix string
    unifyfs_mount_prefix = strdup(prefix);
    unifyfs_mount_prefixlen = strlen(unifyfs_mount_prefix);
    client_cfg.unifyfs_mountpoint = unifyfs_mount_prefix;

    // generate app_id from mountpoint prefix
    unifyfs_app_id = unifyfs_generate_gfid(unifyfs_mount_prefix);
    if (l_app_id != 0) {
        LOGDBG("ignoring passed app_id=%d, using mountpoint app_id=%d",
               l_app_id, unifyfs_app_id);
    }

    // initialize k-v store access
    kv_rank = client_rank;
    kv_nranks = size;
    rc = unifyfs_keyval_init(&client_cfg, &kv_rank, &kv_nranks);
    if (rc) {
        LOGERR("failed to initialize kvstore");
        return UNIFYFS_FAILURE;
    }
    if ((client_rank != kv_rank) || (size != kv_nranks)) {
        LOGDBG("mismatch on mount vs kvstore rank/size");
    }

    /* open rpc connection to server */
    rc = unifyfs_client_rpc_init();
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to initialize client RPC");
        return rc;
    }

    /* Call client mount rpc function to get client id */
    LOGDBG("calling mount rpc");
    rc = invoke_client_mount_rpc(&client_cfg);
    if (rc != UNIFYFS_SUCCESS) {
        /* If we fail to connect to the server, bail with an error */
        LOGERR("failed to mount to server");
        return rc;
    }

    /* initialize our library using assigned client id, creates shared memory
     * regions (e.g., superblock and data recv) and inits log-based I/O */
    rc = unifyfs_init(&client_cfg);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* Call client attach rpc function to register our newly created shared
     * memory and files with server */
    LOGDBG("calling attach rpc");
    rc = invoke_client_attach_rpc(&client_cfg);
    if (rc != UNIFYFS_SUCCESS) {
        /* If we fail, bail with an error */
        LOGERR("failed to attach to server");
        unifyfs_fini();
        return rc;
    }

    /* add mount point as a new directory in the file list */
    if (unifyfs_get_fid_from_path(prefix) < 0) {
        /* no entry exists for mount point, so create one */
        int fid = unifyfs_fid_create_directory(prefix);
        if (fid < 0) {
            /* if there was an error, return it */
            LOGERR("failed to create directory entry for mount point: `%s'",
                   prefix);
            unifyfs_fini();
            return UNIFYFS_FAILURE;
        }
    }

    /* record client state as mounted for specific app_id */
    unifyfs_mounted = unifyfs_app_id;

    return UNIFYFS_SUCCESS;
}

/**
 * unmount the mounted file system
 * TODO: Add support for unmounting more than
 * one filesystem.
 * @return success/error code
 */
int unifyfs_unmount(void)
{
    int ret = UNIFYFS_SUCCESS;

    if (-1 == unifyfs_mounted) {
        return UNIFYFS_SUCCESS;
    }

    /* sync any outstanding writes */
    LOGDBG("syncing data");
    int rc = unifyfs_sync_extents(-1);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("client sync failed");
        ret = UNIFYFS_FAILURE;
    }

    /************************
     * tear down connection to server
     ************************/

    /* invoke unmount rpc to tell server we're disconnecting */
    LOGDBG("calling unmount");
    rc = invoke_client_unmount_rpc();
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("client unmount rpc failed");
        ret = UNIFYFS_FAILURE;
    }

    /* free resources allocated in client_rpc_init */
    unifyfs_client_rpc_finalize();

    /************************
     * free our mount point, and detach from structures
     * storing data
     ************************/

    /* free resources allocated in unifyfs_init */
    unifyfs_fini();

    /* free memory tracking our mount prefix string */
    if (unifyfs_mount_prefix != NULL) {
        free(unifyfs_mount_prefix);
        unifyfs_mount_prefix = NULL;
        unifyfs_mount_prefixlen = 0;
        client_cfg.unifyfs_mountpoint = NULL;
    }

    /************************
     * free configuration values
     ************************/

    /* free global holding current working directory */
    if (unifyfs_cwd != NULL) {
        free(unifyfs_cwd);
    }

    /* clean up configuration */
    rc = unifyfs_config_fini(&client_cfg);
    if (rc != 0) {
        LOGERR("unifyfs_config_fini() failed");
        ret = UNIFYFS_FAILURE;
    }

    /* shut down our logging */
    unifyfs_log_close();

    unifyfs_mounted = -1;

    return ret;
}
