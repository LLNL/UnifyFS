/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
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
#include "unifyfs-fixed.h"
#include "unifyfs_runstate.h"

#include <time.h>
#include <mpi.h>
#include <openssl/md5.h>

#ifdef UNIFYFS_GOTCHA
#include "gotcha/gotcha_types.h"
#include "gotcha/gotcha.h"
#include "gotcha_map_unifyfs_list.h"
#endif

#include "unifyfs_client_rpcs.h"
#include "unifyfs_server_rpcs.h"
#include "unifyfs_rpc_util.h"
#include "margo_client.h"
#include "seg_tree.h"

/* avoid duplicate mounts (for now) */
static int unifyfs_mounted = -1;

static int unifyfs_fpos_enabled   = 1;  /* whether we can use fgetpos/fsetpos */

unifyfs_cfg_t client_cfg;

unifyfs_index_buf_t unifyfs_indices;
static size_t unifyfs_index_buf_size;    /* size of metadata log */
static size_t unifyfs_fattr_buf_size;
unsigned long unifyfs_max_index_entries; /* max metadata log entries */
int unifyfs_spillmetablock;

/* tracks total number of unsync'd segments for all files */
unsigned long unifyfs_segment_count;

int global_rank_cnt;  /* count of world ranks */
int local_rank_cnt;
int local_rank_idx;

int local_del_cnt = 1;
int client_sockfd = -1;
struct pollfd cmd_fd;

/* shared memory buffer to transfer read requests
 * from client to server */
static char   shm_req_name[GEN_STR_LEN]  = {0};
static size_t shm_req_size = UNIFYFS_SHMEM_REQ_SIZE;
void* shm_req_buf;

/* shared memory buffer to transfer read replies
 * from server to client */
static char   shm_recv_name[GEN_STR_LEN] = {0};
static size_t shm_recv_size = UNIFYFS_SHMEM_RECV_SIZE;
void* shm_recv_buf;

int client_rank;
int app_id;
size_t unifyfs_key_slice_range;

/* whether chunks should be allocated to
 * store file contents in memory */
int unifyfs_use_memfs = 1;

/* whether chunks should be allocated to
 * store file contents on spill over device */
int unifyfs_use_spillover = 1;

static int unifyfs_use_single_shm = 0;
static int unifyfs_page_size      = 0;

static off_t unifyfs_max_offt;
static off_t unifyfs_min_offt;
static off_t unifyfs_max_long;
static off_t unifyfs_min_long;

/* TODO: moved these to fixed file */
int    unifyfs_max_files;  /* maximum number of files to store */
bool   unifyfs_flatten_writes; /* flatten our writes (true = enabled) */
bool   unifyfs_local_extents;  /* track data extents in client to read local */
size_t unifyfs_chunk_mem;  /* number of bytes in memory to be used for chunk storage */
int    unifyfs_chunk_bits; /* we set chunk size = 2^unifyfs_chunk_bits */
off_t  unifyfs_chunk_size; /* chunk size in bytes */
off_t  unifyfs_chunk_mask; /* mask applied to logical offset to determine physical offset within chunk */
long   unifyfs_max_chunks; /* maximum number of chunks that fit in memory */

/* number of bytes in spillover to be used for chunk storage */
static size_t unifyfs_spillover_size;

/* maximum number of chunks that fit in spillover storage */
long unifyfs_spillover_max_chunks;

extern pthread_mutex_t unifyfs_stack_mutex;

/* keep track of what we've initialized */
int unifyfs_initialized = 0;

/* shared memory for superblock */
static char   shm_super_name[GEN_STR_LEN] = {0};
static size_t shm_super_size;

/* global persistent memory block (metadata + data) */
void* shm_super_buf;
static void* free_fid_stack;
void* free_chunk_stack;
void* free_spillchunk_stack;
unifyfs_filename_t* unifyfs_filelist;
static unifyfs_filemeta_t* unifyfs_filemetas;

unifyfs_chunkmeta_t* unifyfs_chunkmetas;

char* unifyfs_chunks;
int unifyfs_spilloverblock = 0;
int unifyfs_spillmetablock = 0; /*used for log-structured i/o*/

/* array of file descriptors */
unifyfs_fd_t unifyfs_fds[UNIFYFS_MAX_FILEDESCS];
rlim_t unifyfs_fd_limit;

/* array of file streams */
unifyfs_stream_t unifyfs_streams[UNIFYFS_MAX_FILEDESCS];

/*
 * TODO: the number of open directories clearly won't exceed the number of file
 * descriptors. however, the current UNIFYFS_MAX_FILEDESCS value of 256 will
 * quickly run out. if this value is fixed to be reasonably larger, then we
 * would need a way to dynamically allocate the dirstreams instead of the
 * following fixed size array.
 */

/* array of DIR* streams to be used */
unifyfs_dirstream_t unifyfs_dirstreams[UNIFYFS_MAX_FILEDESCS];

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
static key_t  unifyfs_mount_shmget_key = 0;

/* mutex to lock stack operations */
pthread_mutex_t unifyfs_stack_mutex = PTHREAD_MUTEX_INITIALIZER;

/* path of external storage's mount point*/

char external_data_dir[UNIFYFS_MAX_FILENAME] = {0};
char external_meta_dir[UNIFYFS_MAX_FILENAME] = {0};

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

/* given an UNIFYFS error code, return corresponding errno code */
int unifyfs_err_map_to_errno(int rc)
{
    unifyfs_error_e ec = (unifyfs_error_e)rc;

    switch (ec) {
    case UNIFYFS_SUCCESS:
        return 0;
    case UNIFYFS_ERROR_BADF:
        return EBADF;
    case UNIFYFS_ERROR_EXIST:
        return EEXIST;
    case UNIFYFS_ERROR_FBIG:
        return EFBIG;
    case UNIFYFS_ERROR_INVAL:
        return EINVAL;
    case UNIFYFS_ERROR_ISDIR:
        return EISDIR;
    case UNIFYFS_ERROR_NAMETOOLONG:
        return ENAMETOOLONG;
    case UNIFYFS_ERROR_NFILE:
        return ENFILE;
    case UNIFYFS_ERROR_NOENT:
        return ENOENT;
    case UNIFYFS_ERROR_NOMEM:
        return ENOMEM;
    case UNIFYFS_ERROR_NOSPC:
        return ENOSPC;
    case UNIFYFS_ERROR_NOTDIR:
        return ENOTDIR;
    case UNIFYFS_ERROR_OVERFLOW:
        return EOVERFLOW;
    default:
        break;
    }
    return ec;
}

/* given an errno error code, return corresponding UnifyFS error code */
int unifyfs_errno_map_to_err(int rc)
{
    switch (rc) {
    case 0:
        return (int)UNIFYFS_SUCCESS;
    case EBADF:
        return (int)UNIFYFS_ERROR_BADF;
    case EEXIST:
        return (int)UNIFYFS_ERROR_EXIST;
    case EFBIG:
        return (int)UNIFYFS_ERROR_FBIG;
    case EINVAL:
        return (int)UNIFYFS_ERROR_INVAL;
    case EIO:
        return (int)UNIFYFS_ERROR_IO;
    case EISDIR:
        return (int)UNIFYFS_ERROR_ISDIR;
    case ENAMETOOLONG:
        return (int)UNIFYFS_ERROR_NAMETOOLONG;
    case ENFILE:
        return (int)UNIFYFS_ERROR_NFILE;
    case ENOENT:
        return (int)UNIFYFS_ERROR_NOENT;
    case ENOMEM:
        return (int)UNIFYFS_ERROR_NOMEM;
    case ENOSPC:
        return (int)UNIFYFS_ERROR_NOSPC;
    case ENOTDIR:
        return (int)UNIFYFS_ERROR_NOTDIR;
    case EOVERFLOW:
        return (int)UNIFYFS_ERROR_OVERFLOW;
    default:
        break;
    }
    return (int)UNIFYFS_FAILURE;
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

/* given an input mode, mask it with umask and return, can specify
 * an input mode==0 to specify all read/write bits */
mode_t unifyfs_getmode(mode_t perms)
{
    /* perms == 0 is shorthand for all read and write bits */
    if (perms == 0) {
        perms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    }

    /* get current user mask */
    mode_t mask = umask(0);
    umask(mask);

    /* mask off bits from desired permissions */
    mode_t ret = perms & ~mask & 0777;
    return ret;
}

/* lock access to shared data structures in superblock */
inline int unifyfs_stack_lock()
{
    if (unifyfs_use_single_shm) {
        return pthread_mutex_lock(&unifyfs_stack_mutex);
    }
    return 0;
}

/* unlock access to shared data structures in superblock */
inline int unifyfs_stack_unlock()
{
    if (unifyfs_use_single_shm) {
        return pthread_mutex_unlock(&unifyfs_stack_mutex);
    }
    return 0;
}

/* sets flag if the path is a special path */
inline int unifyfs_intercept_path(const char* path)
{
    /* don't intecept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* if the path starts with our mount point, intercept it */
    if (strncmp(path, unifyfs_mount_prefix, unifyfs_mount_prefixlen) == 0) {
        return 1;
    }
    return 0;
}

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
inline int unifyfs_intercept_fd(int* fd)
{
    int oldfd = *fd;

    /* don't intecept anything until we're initialized */
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

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
inline int unifyfs_intercept_stream(FILE* stream)
{
    /* don't intecept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * file stream array */
    unifyfs_stream_t* ptr   = (unifyfs_stream_t*) stream;
    unifyfs_stream_t* start = &(unifyfs_streams[0]);
    unifyfs_stream_t* end   = &(unifyfs_streams[UNIFYFS_MAX_FILEDESCS]);
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given an directory stream, return 1 if we should intercept this
 * fdirecotry, 0 otherwise */
inline int unifyfs_intercept_dirstream(DIR* dirp)
{
    /* don't intecept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * directory stream array */
    unifyfs_dirstream_t* ptr   = (unifyfs_dirstream_t*) dirp;
    unifyfs_dirstream_t* start = &(unifyfs_dirstreams[0]);
    unifyfs_dirstream_t* end   = &(unifyfs_dirstreams[UNIFYFS_MAX_FILEDESCS]);
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given a path, return the file id */
inline int unifyfs_get_fid_from_path(const char* path)
{
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
    if (fd < 0 || fd >= UNIFYFS_MAX_FILEDESCS) {
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
    if (fd >= 0 && fd < UNIFYFS_MAX_FILEDESCS) {
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

int unifyfs_fid_is_laminated(int fid)
{
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    return meta->is_laminated;
}

int unifyfs_fd_is_laminated(int fd)
{
    int fid = unifyfs_get_fid_from_fd(fd);
    return unifyfs_fid_is_laminated(fid);
}

/* ---------------------------------------
 * Operations on file storage
 * --------------------------------------- */

/* allocate and initialize data management resource for file */
static int unifyfs_fid_store_alloc(int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);

    /* indicate that we're using LOGIO to store data for this file */
    meta->storage = FILE_STORAGE_LOGIO;

    return UNIFYFS_SUCCESS;
}

/* free data management resource for file */
static int unifyfs_fid_store_free(int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);

    /* set storage type back to NULL */
    meta->storage = FILE_STORAGE_NULL;

    /* Free our write seg_tree */
    if (unifyfs_flatten_writes) {
        seg_tree_destroy(&meta->extents_sync);
    }

    /* Free our extent seg_tree */
    if (unifyfs_local_extents) {
        seg_tree_destroy(&meta->extents);
    }

    return UNIFYFS_SUCCESS;
}

/* ---------------------------------------
 * Operations on file ids
 * --------------------------------------- */

/* checks to see if fid is a directory
 * returns 1 for yes
 * returns 0 for no */
int unifyfs_fid_is_dir(int fid)
{
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (meta && meta->mode & S_IFDIR) {
        return 1;
    } else {
        /* if it doesn't exist, then it's not a directory? */
        return 0;
    }
}

/*
 * hash a path to gfid
 * @param path: file path
 * return: gfid
 */
int unifyfs_generate_gfid(const char* path)
{
    unsigned char digested[16] = { 0, };
    unsigned long len = strlen(path);
    int* ival = (int*) digested;

    MD5((const unsigned char*) path, len, digested);

    return abs(ival[0]);
}

int unifyfs_gfid_from_fid(const int fid)
{
    /* check that local file id is in range */
    if (fid < 0 || fid >= unifyfs_max_files) {
        return -1;
    }

    /* return global file id, cached in file meta struct */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    return meta->gfid;
}

/* scan list of files and return fid corresponding to target gfid,
 * returns -1 if not found */
int unifyfs_fid_from_gfid(int gfid)
{
    int i;
    for (i = 0; i < unifyfs_max_files; i++) {
        if (unifyfs_filelist[i].in_use &&
            unifyfs_filemetas[i].gfid == gfid) {
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
             * also check to make sure that it's not the directory entry itself */
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
    if (NULL != meta) {
        return meta->global_size;
    }
    return (off_t)-1;
}

/* Return the log size of the file */
off_t unifyfs_fid_local_size(int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (NULL != meta) {
        return meta->local_size;
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
        return unifyfs_fid_global_size(fid);
    } else {
        return unifyfs_fid_local_size(fid);
    }
}

/* Return the local (un-laminated) size of the file */
off_t unifyfs_fid_log_size(int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (NULL != meta) {
        return meta->log_size;
    }
    return (off_t)-1;
}

/* Update local metadata for file from global metadata */
int unifyfs_fid_update_file_meta(int fid, unifyfs_file_attr_t* gfattr)
{
    if (NULL == gfattr) {
        return UNIFYFS_FAILURE;
    }

    /* lookup local metadata for file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (NULL != meta) {
        /* update lamination state */
        meta->is_laminated = gfattr->is_laminated;
        if (meta->is_laminated) {
            /* update file size */
            meta->global_size = (off_t)gfattr->size;
            meta->local_size = meta->global_size;
            LOGDBG("laminated file size is %zu bytes",
                   (size_t)meta->global_size);
        }
        return UNIFYFS_SUCCESS;
    }
    /* else, bad fid */
    return UNIFYFS_FAILURE;
}

/*
 * Set the metadata values for a file (after optionally creating it).
 * The gfid for the file is in f_meta->gfid.
 *
 * gfid:   The global file id on which to set metadata.
 *
 * create: If set to 1, attempt to create the file first.  If the file
 *         already exists, then update its metadata with the values in
 *         gfattr.  If set to 0, and the file does not exist, then
 *         the server will return an error.
 *
 * gfattr: The metadata values to store.
 */
int unifyfs_set_global_file_meta(
    int gfid,   /* file id to set meta data for */
    int create, /* whether to set size/laminated fields (1) or not (0) */
    unifyfs_file_attr_t* gfattr) /* meta data to store for file */
{
    /* check that we have an input buffer */
    if (NULL == gfattr) {
        return UNIFYFS_FAILURE;
    }

    /* force the gfid field value to match the gfid we're
     * submitting this under */
    gfattr->gfid = gfid;

    /* submit file attributes to global key/value store */
    int ret = invoke_client_metaset_rpc(create, gfattr);
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
 * Set the metadata values for a file (after optionally creating it),
 * using metadata associated with a given local file id.
 *
 * fid:    The local file id on which to base global metadata values.
 *
 * create: If set to 1, attempt to create the file first.  If the file
 *         already exists, then update its metadata with the values in
 *         gfattr.  If set to 0, and the file does not exist, then
 *         the server will return an error.
 */
int unifyfs_set_global_file_meta_from_fid(int fid, int create)
{
    /* initialize an empty file attributes structure */
    unifyfs_file_attr_t fattr = {0};

    /* lookup local metadata for file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);

    /* copy our file name */
    const char* path = unifyfs_path_from_fid(fid);
    sprintf(fattr.filename, "%s", path);

    /* set global file id */
    fattr.gfid = meta->gfid;

    /* use current time for atime/mtime/ctime */
    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);
    fattr.atime = tp;
    fattr.mtime = tp;
    fattr.ctime = tp;

    /* copy file mode bits and lamination flag */
    fattr.mode = meta->mode;

    /* these fields are set by server, except when we're creating a
     * new file in which case, we should initialize them both to 0 */
    fattr.is_laminated = 0;
    fattr.size         = 0;

    /* capture current uid and gid */
    fattr.uid = getuid();
    fattr.gid = getgid();

    /* submit file attributes to global key/value store */
    int ret = unifyfs_set_global_file_meta(meta->gfid, create, &fattr);
    return ret;
}

/* allocate a file id slot for a new file
 * return the fid or -1 on error */
int unifyfs_fid_alloc()
{
    unifyfs_stack_lock();
    int fid = unifyfs_stack_pop(free_fid_stack);
    unifyfs_stack_unlock();
    LOGDBG("unifyfs_stack_pop() gave %d", fid);
    if (fid < 0) {
        /* need to create a new file, but we can't */
        LOGERR("unifyfs_stack_pop() failed (%d)", fid);
        return -1;
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
    int rc;

    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return (int) UNIFYFS_ERROR_NAMETOOLONG;
    }

    /* allocate an id for this file */
    int fid = unifyfs_fid_alloc();
    if (fid < 0)  {
        /* was there an error? if so, return it */
        errno = ENOSPC;
        return fid;
    }

    /* mark this slot as in use */
    unifyfs_filelist[fid].in_use = 1;

    /* copy file name into slot */
    strcpy((void*)&unifyfs_filelist[fid].filename, path);
    LOGDBG("Filename %s got unifyfs fd %d",
           unifyfs_filelist[fid].filename, fid);

    /* initialize meta data */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    meta->global_size  = 0;
    meta->local_size   = 0;
    meta->log_size     = 0;
    meta->flock_status = UNLOCKED;
    meta->storage      = FILE_STORAGE_NULL;
    meta->gfid         = unifyfs_generate_gfid(path);
    meta->needs_sync   = 0;
    meta->chunks       = 0;
    meta->is_laminated = 0;
    meta->mode         = UNIFYFS_STAT_DEFAULT_FILE_MODE;

    if (unifyfs_flatten_writes) {
        /* Initialize our segment tree that will record our writes */
        rc = seg_tree_init(&meta->extents_sync);
        if (rc != 0) {
            errno = rc;
            fid = -1;
        }
    }

    /* Initialize our segment tree to track extents for all writes
     * by this process, can be used to read back local data */
    if (unifyfs_local_extents) {
        rc = seg_tree_init(&meta->extents);
        if (rc != 0) {
            errno = rc;
            fid = -1;
        }
    }

    /* PTHREAD_PROCESS_SHARED allows Process-Shared Synchronization */
    pthread_spin_init(&meta->fspinlock, PTHREAD_PROCESS_SHARED);

    return fid;
}

int unifyfs_fid_create_directory(const char* path)
{
    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return (int) UNIFYFS_ERROR_NAMETOOLONG;
    }

    /* get local and global file ids */
    int fid  = unifyfs_get_fid_from_path(path);
    int gfid = unifyfs_generate_gfid(path);

    /* test whether we have info for file in our local file list */
    int found_local = (fid >= 0);

    /* test whether we have metadata for file in global key/value store */
    int found_global = 0;
    unifyfs_file_attr_t gfattr = { 0, };
    if (unifyfs_get_global_file_meta(gfid, &gfattr) == UNIFYFS_SUCCESS) {
        found_global = 1;
    }

    /* can't create if it already exists */
    if (found_global) {
        return (int) UNIFYFS_ERROR_EXIST;
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
         * we currently return EIO, and this needs to be addressed according to
         * a consistency model this fs intance assumes.
         */
        return (int) UNIFYFS_ERROR_IO;
    }

    /* now, we need to create a new directory. */
    fid = unifyfs_fid_create_file(path);
    if (fid < 0) {
        /* FIXME: ENOSPC or EIO? */
        return (int) UNIFYFS_ERROR_IO;
    }

    /* Set as directory */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    meta->mode = (meta->mode & ~S_IFREG) | S_IFDIR;

    /* insert global meta data for directory */
    int ret = unifyfs_set_global_file_meta_from_fid(fid, 1);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("Failed to populate the global meta entry for %s (fid:%d)",
               path, fid);
        return (int) UNIFYFS_ERROR_IO;
    }

    return UNIFYFS_SUCCESS;
}

/* write count bytes from buf into file starting at offset pos,
 * all bytes are assumed to be allocated to file, so file should
 * be extended before calling this routine.
 *
 * Returns a UNIFYFS_ERROR on error, 0 on success.
 */
int unifyfs_fid_write(int fid, off_t pos, const void* buf, size_t count)
{
    int rc;

    /* short-circuit a 0-byte write */
    if (count == 0) {
        return UNIFYFS_SUCCESS;
    }

    /* get meta for this file id */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);

    /* determine storage type to write file data */
    if (meta->storage == FILE_STORAGE_LOGIO) {
        /* file stored in fixed-size chunks */
        rc = unifyfs_fid_store_fixed_write(fid, meta, pos, buf, count);
    } else {
        /* unknown storage type */
        rc = (int)UNIFYFS_ERROR_IO;
    }

    return rc;
}

/* given a file id, write zero bytes to region of specified offset
 * and length, assumes space is already reserved */
int unifyfs_fid_write_zero(int fid, off_t pos, off_t count)
{
    int rc = UNIFYFS_SUCCESS;

    /* allocate an aligned chunk of memory */
    size_t buf_size = 1024 * 1024;
    void* buf = (void*) malloc(buf_size);
    if (buf == NULL) {
        return (int)UNIFYFS_ERROR_IO;
    }

    /* set values in this buffer to zero */
    memset(buf, 0, buf_size);

    /* write zeros to file */
    off_t written = 0;
    off_t curpos = pos;
    while (written < count) {
        /* compute number of bytes to write on this iteration */
        size_t num = buf_size;
        off_t remaining = count - written;
        if (remaining < (off_t) buf_size) {
            num = (size_t) remaining;
        }

        /* write data to file */
        int write_rc = unifyfs_fid_write(fid, curpos, buf, num);
        if (write_rc != UNIFYFS_SUCCESS) {
            rc = (int)UNIFYFS_ERROR_IO;
            break;
        }

        /* update the number of bytes written */
        curpos  += (off_t) num;
        written += (off_t) num;
    }

    /* free the buffer */
    free(buf);

    return rc;
}

/* increase size of file if length is greater than current size,
 * and allocate additional chunks as needed to reserve space for
 * length bytes */
int unifyfs_fid_extend(int fid, off_t length)
{
    int rc;

    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);

    /* determine file storage type */
    if (meta->storage == FILE_STORAGE_LOGIO) {
        /* file stored in fixed-size chunks */
        rc = unifyfs_fid_store_fixed_extend(fid, meta, length);
    } else {
        /* unknown storage type */
        rc = (int)UNIFYFS_ERROR_IO;
    }

    return rc;
}

/* if length is less than reserved space, give back space down to length */
int unifyfs_fid_shrink(int fid, off_t length)
{
    /* TODO: implement this function */

    return UNIFYFS_ERROR_IO;
}

/* truncate file id to given length, frees resources if length is
 * less than size and allocates and zero-fills new bytes if length
 * is more than size */
int unifyfs_fid_truncate(int fid, off_t length)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (meta->is_laminated) {
        /* Can't truncate a laminated file */
        return (int)UNIFYFS_ERROR_INVAL;
    }

    /* determine file storage type */
    if (meta->storage == FILE_STORAGE_LOGIO) {
        /* invoke truncate rpc */
        int gfid = unifyfs_gfid_from_fid(fid);
        int rc = invoke_client_truncate_rpc(gfid, length);
        if (rc != UNIFYFS_SUCCESS) {
            return rc;
        }

        /* truncate succeeded, update global and local size to
         * reflect truncated size, note log size is not affected */
        meta->global_size = length;
        meta->local_size  = length;
    } else {
        /* unknown storage type */
        return (int)UNIFYFS_ERROR_IO;
    }

    return UNIFYFS_SUCCESS;
}

/* opens a new file id with specified path, access flags, and permissions,
 * fills outfid with file id and outpos with position for current file pointer,
 * returns UNIFYFS error code
 */
int unifyfs_fid_open(const char* path, int flags, mode_t mode, int* outfid,
                     off_t* outpos)
{
    int ret;

    /* set the pointer to the start of the file */
    off_t pos = 0;

    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return (int) UNIFYFS_ERROR_NAMETOOLONG;
    }

    /* check whether this file already exists */
    /*
     * TODO: The test of file existence involves both local and global checks.
     * However, the testing below does not seem to cover all cases. For
     * instance, a globally unlinked file might be still cached locally because
     * the broadcast for cache invalidation has not been implemented, yet.
     */

    /* get local and global file ids */
    int fid  = unifyfs_get_fid_from_path(path);
    int gfid = unifyfs_generate_gfid(path);

    LOGDBG("unifyfs_get_fid_from_path() gave %d (gfid = %d)", fid, gfid);

    /* test whether we have info for file in our local file list */
    int found_local = (fid >= 0);

    /* test whether we have metadata for file in global key/value store */
    int found_global = 0;
    unifyfs_file_attr_t gfattr = { 0, };
    if (unifyfs_get_global_file_meta(gfid, &gfattr) == UNIFYFS_SUCCESS) {
        found_global = 1;
    }

    /*
     * Catch any case where we could potentially want to write to a laminated
     * file.
     */
    if (gfattr.is_laminated &&
        ((flags & (O_CREAT | O_TRUNC | O_APPEND | O_WRONLY)) ||
         ((mode & 0222) && (flags != O_RDONLY)))) {
            LOGDBG("Can't open laminated file %s with a writable flag.", path);
            return EROFS;
    }

    /* possibly, the file still exists in our local cache but globally
     * unlinked. Invalidate the entry
     *
     * FIXME: unifyfs_fid_unlink() always returns success.
     */
    if (found_local && !found_global) {
        LOGDBG("file found locally, but seems to be deleted globally. "
               "invalidating the local cache.");
        unifyfs_fid_unlink(fid);
        return (int) UNIFYFS_ERROR_NOENT;
    }

    /* for all other three cases below, we need to open the file and allocate a
     * file descriptor for the client.
     */
    if (!found_local && found_global) {
        /* file has possibly been created by another process.  We need to
         * create a local meta cache and also initialize the local storage
         * space.
         */

        /* initialize local metadata for this file */
        fid = unifyfs_fid_create_file(path);
        if (fid < 0) {
            /* FIXME: UNIFYFS_ERROR_NFILE or UNIFYFS_ERROR_IO ? */
            LOGERR("failed to create a new file %s", path);
            return (int) UNIFYFS_ERROR_IO;
        }

        /* initialize local storage for this file */
        ret = unifyfs_fid_store_alloc(fid);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("failed to allocate storage space for file %s (fid=%d)",
                   path, fid);
            return (int) UNIFYFS_ERROR_IO;
        }

        /* initialize global size of file from global metadata */
        unifyfs_fid_update_file_meta(fid, &gfattr);
    } else if (found_local && found_global) {
        /* file exists and is valid.  */
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            return (int)UNIFYFS_ERROR_EXIST;
        }

        if ((flags & O_DIRECTORY) && !unifyfs_fid_is_dir(fid)) {
            return (int)UNIFYFS_ERROR_NOTDIR;
        }

        if (!(flags & O_DIRECTORY) && unifyfs_fid_is_dir(fid)) {
            return (int)UNIFYFS_ERROR_NOTDIR;
        }

        /* update local metadata from global metadata */
        unifyfs_fid_update_file_meta(fid, &gfattr);

        if ((flags & O_TRUNC) && (flags & (O_RDWR | O_WRONLY))) {
            unifyfs_fid_truncate(fid, 0);
        }

        if (flags & O_APPEND) {
            /* We only support O_APPEND on non-laminated (local) files, so
             * this will use local_size here. */
            pos = unifyfs_fid_logical_size(fid);
        }
    } else {
        /* !found_local && !found_global
         * If we reach here, we need to create a brand new file.
         */
        if (!(flags & O_CREAT)) {
            LOGERR("%s does not exist (O_CREAT not given).", path);
            return (int) UNIFYFS_ERROR_NOENT;
        }

        LOGDBG("Creating a new entry for %s.", path);
        LOGDBG("shm_super_buf = %p; free_fid_stack = %p; "
               "free_chunk_stack = %p; unifyfs_filelist = %p; "
               "chunks = %p", shm_super_buf, free_fid_stack,
               free_chunk_stack, unifyfs_filelist, unifyfs_chunks);

        /* allocate a file id slot for this new file */
        fid = unifyfs_fid_create_file(path);
        if (fid < 0) {
            LOGERR("Failed to create new file %s", path);
            return (int) UNIFYFS_ERROR_NFILE;
        }

        /* initialize the storage for the file */
        int store_rc = unifyfs_fid_store_alloc(fid);
        if (store_rc != UNIFYFS_SUCCESS) {
            LOGERR("Failed to create storage for file %s", path);
            return (int) UNIFYFS_ERROR_IO;
        }

        /* insert file attribute for file in key-value store */
        ret = unifyfs_set_global_file_meta_from_fid(fid, 1);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("Failed to populate the global meta entry for %s (fid:%d)",
                   path, fid);
            return (int) UNIFYFS_ERROR_IO;
        }
    }

    /* TODO: allocate a free file descriptor and associate it with fid set
     * in_use flag and file pointer */

    /* return local file id and starting file position */
    *outfid = fid;
    *outpos = pos;

    LOGDBG("UNIFYFS_open generated fd %d for file %s", fid, path);

    return UNIFYFS_SUCCESS;
}

int unifyfs_fid_close(int fid)
{
    /* TODO: clear any held locks */

    /* nothing to do here, just a place holder */
    return UNIFYFS_SUCCESS;
}

/* delete a file id and return file its resources to free pools */
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
    rc = unifyfs_fid_store_free(fid);
    if (rc != UNIFYFS_SUCCESS) {
        /* released strorage for file, but failed to release
         * structures tracking storage, again bail out to keep
         * its file id active */
        return rc;
    }

    /* at this point, we have released all storage for the file,
     * and data structures that track its storage, so we can
     * release the file id itself */

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

/* ---------------------------------------
 * Operations to mount file system
 * --------------------------------------- */

/* The super block is a region of shared memory that is used to
 * persist file system data.  It contains both room for data
 * structures used to track file names, meta data, the list of
 * storage blocks used for each file, and optional blocks.
 * It also contains a fixed-size region for keeping log
 * index entries for each file.
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
 *    file id, records list of storage blocks used to
 *    store data for the file
 *
 *  - array of unifyfs_chunkmeta structs, indexed by local
 *    file id and then by chunk id for recording metadata
 *    of each chunk allocated to a file, including host
 *    storage and id of that chunk within its storage
 *
 *  - stack to track free list of memory chunks
 *
 *  - stack to track free list of spillover chunks
 *
 *  - array of storage chunks of length unifyfs_max_chunks,
 *    if storing data in memory
 *
 *  - count of number of active index entries
 *  - array of index metadata to track physical offset
 *    of logical file data, of length unifyfs_max_index_entries,
 *    entries added during write operations
 */

/* compute memory size of superblock in bytes,
 * critical to keep this consistent with
 * unifyfs_init_pointers */
static size_t unifyfs_superblock_size(void)
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

    if (unifyfs_use_memfs) {
        /* memory chunk metadata struct array for each file,
         * enables a file to use all space in memory */
        sb_size += unifyfs_max_files * unifyfs_max_chunks *
                   sizeof(unifyfs_chunkmeta_t);
    }
    if (unifyfs_use_spillover) {
        /* spillover chunk metadata struct array for each file,
         * enables a file to use all space in spillover file */
        sb_size += unifyfs_max_files * unifyfs_spillover_max_chunks *
                   sizeof(unifyfs_chunkmeta_t);
    }

    /* free chunk stack */
    if (unifyfs_use_memfs) {
        sb_size += unifyfs_stack_bytes(unifyfs_max_chunks);
    }
    if (unifyfs_use_spillover) {
        sb_size += unifyfs_stack_bytes(unifyfs_spillover_max_chunks);
    }

    /* space for memory chunks */
    if (unifyfs_use_memfs) {
        sb_size += unifyfs_page_size;
        sb_size += unifyfs_max_chunks * unifyfs_chunk_size;
    }

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
static void* unifyfs_init_pointers(void* superblock)
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

    /* array of chunk meta data strucutres for each file */
    unifyfs_chunkmetas = (unifyfs_chunkmeta_t*)ptr;
    if (unifyfs_use_memfs) {
        ptr += unifyfs_max_files * unifyfs_max_chunks *
               sizeof(unifyfs_chunkmeta_t);
    }
    if (unifyfs_use_spillover) {
        ptr += unifyfs_max_files * unifyfs_spillover_max_chunks *
               sizeof(unifyfs_chunkmeta_t);
    }

    /* stack to manage free memory data chunks */
    if (unifyfs_use_memfs) {
        free_chunk_stack = ptr;
        ptr += unifyfs_stack_bytes(unifyfs_max_chunks);
    }
    if (unifyfs_use_spillover) {
        free_spillchunk_stack = ptr;
        ptr += unifyfs_stack_bytes(unifyfs_spillover_max_chunks);
    }

    /* Only set this up if we're using memfs */
    if (unifyfs_use_memfs) {
        /* pointer to start of memory data chunks */
        ptr = next_page_align(ptr);
        unifyfs_chunks = ptr;
        ptr += unifyfs_max_chunks * unifyfs_chunk_size;
    } else {
        unifyfs_chunks = NULL;
    }

    /* record pointer to number of index entries */
    unifyfs_indices.ptr_num_entries = (size_t*)ptr;

    /* pointer to array of index entries */
    ptr += unifyfs_page_size;
    unifyfs_indices.index_entry = (unifyfs_index_t*)ptr;
    ptr += unifyfs_max_index_entries * sizeof(unifyfs_index_t);

    /* compute size of memory we're using and check that
     * it matches what we allocated */
    size_t ptr_size = (size_t)(ptr - (char*)superblock);
    if (ptr_size > shm_super_size) {
        LOGERR("Data structures in superblock extend beyond its size");
    }

    return ptr;
}

/* initialize data structures for first use */
static int unifyfs_init_structures()
{
    /* compute total number of storage chunks available */
    int numchunks = 0;
    if (unifyfs_use_memfs) {
        numchunks += unifyfs_max_chunks;
    }
    if (unifyfs_use_spillover) {
        numchunks += unifyfs_spillover_max_chunks;
    }

    int i;
    for (i = 0; i < unifyfs_max_files; i++) {
        /* indicate that file id is not in use by setting flag to 0 */
        unifyfs_filelist[i].in_use = 0;

        /* set pointer to array of chunkmeta data structures */
        unifyfs_filemeta_t* filemeta = &unifyfs_filemetas[i];

        /* compute offset to start of chunk meta list for this file */
        filemeta->chunkmeta_idx = numchunks * i;
    }

    /* initialize stack of free file ids */
    unifyfs_stack_init(free_fid_stack, unifyfs_max_files);

    /* initialize list of free memory chunks */
    if (unifyfs_use_memfs) {
        unifyfs_stack_init(free_chunk_stack, unifyfs_max_chunks);
    }

    /* initialize list of free spillover chunks */
    if (unifyfs_use_spillover) {
        unifyfs_stack_init(free_spillchunk_stack, unifyfs_spillover_max_chunks);
    }

    /* initialize count of key/value entries */
    *(unifyfs_indices.ptr_num_entries) = 0;

    LOGDBG("Meta-stacks initialized!");

    return UNIFYFS_SUCCESS;
}

static int unifyfs_get_spillblock(size_t size, const char* path)
{
    //MAP_OR_FAIL(open);
    mode_t perms = unifyfs_getmode(0);
    int spillblock_fd = __real_open(path, O_RDWR | O_CREAT | O_EXCL, perms);
    if (spillblock_fd < 0) {
        if (errno == EEXIST) {
            /* spillover block exists; attach and return */
            spillblock_fd = __real_open(path, O_RDWR);
        } else {
            LOGERR("open() failed: errno=%d (%s)", errno, strerror(errno));
            return -1;
        }
    } else {
        /* new spillover block created */
        /* TODO: align to SSD block size*/

        /*temp*/
        off_t rc = __real_lseek(spillblock_fd, size, SEEK_SET);
        if (rc < 0) {
            LOGERR("lseek() failed: errno=%d (%s)", errno, strerror(errno));
        }
    }

    return spillblock_fd;
}

/* create superblock of specified size and name, or attach to existing
 * block if available */
static void* unifyfs_superblock_shmget(size_t size, key_t key)
{
    /* define name for superblock shared memory region */
    snprintf(shm_super_name, sizeof(shm_super_name), "%d-super-%d",
             app_id, key);
    LOGDBG("Key for superblock = %x", key);

    /* open shared memory file */
    void* addr = unifyfs_shm_alloc(shm_super_name, size);
    if (addr == NULL) {
        LOGERR("Failed to create superblock");
        return NULL;
    }

    /* init our global variables to point to spots in superblock */
    unifyfs_init_pointers(addr);

    /* initialize structures in superblock if it's newly allocated,
     * we depend on shm_open setting all bytes to 0 to know that
     * it is not initialized */
    int32_t initialized = *(int32_t*)addr;
    if (initialized == 0) {
        /* not yet initialized, so initialize values within superblock */
        unifyfs_init_structures();

        /* superblock structure has been initialized,
         * so set flag to indicate that fact */
        *(int32_t*)addr = 0xDEADBEEF;
    }

    /* return starting memory address of super block */
    return addr;
}

static int unifyfs_init(int rank)
{
    int rc;
    int i;
    bool b;
    long l;
    unsigned long long bits;
    char* cfgval;

    if (!unifyfs_initialized) {
        /* unifyfs default log level is LOG_ERR */
        cfgval = client_cfg.log_verbosity;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_set_log_level((unifyfs_log_level_t)l);
            }
        }

#ifdef UNIFYFS_GOTCHA
        /* insert our I/O wrappers using gotcha */
        enum gotcha_error_t result;
        result = gotcha_wrap(wrap_unifyfs_list, GOTCHA_NFUNCS, "unifyfs");
        if (result != GOTCHA_SUCCESS) {
            LOGERR("gotcha_wrap returned %d", (int) result);
        }

        /* check for an errors when registering functions with gotcha */
        for (i = 0; i < GOTCHA_NFUNCS; i++) {
            if (*(void**)(wrap_unifyfs_list[i].function_address_pointer) == 0) {
                LOGERR("This function name failed to be wrapped: %s",
                       wrap_unifyfs_list[i].name);

            }
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
        unifyfs_page_size = getpagesize();

        /* compute min and max off_t values */
        bits = sizeof(off_t) * 8;
        unifyfs_max_offt = (off_t)((1ULL << (bits - 1ULL)) - 1ULL);
        unifyfs_min_offt = (off_t)(-(1ULL << (bits - 1ULL)));

        /* compute min and max long values */
        unifyfs_max_long = LONG_MAX;
        unifyfs_min_long = LONG_MIN;

        /* will we use spillover to store the files? */
        unifyfs_use_spillover = 1;
        cfgval = client_cfg.spillover_enabled;
        if (cfgval != NULL) {
            rc = configurator_bool_val(cfgval, &b);
            if ((rc == 0) && !b) {
                unifyfs_use_spillover = 0;
            }
        }
        LOGDBG("are we using spillover? %d", unifyfs_use_spillover);

        /* determine maximum number of bytes of spillover for chunk storage */
        unifyfs_spillover_size = UNIFYFS_SPILLOVER_SIZE;
        cfgval = client_cfg.spillover_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_spillover_size = (size_t)l;
            }
        }

        /* determine max number of files to store in file system */
        unifyfs_max_files = UNIFYFS_MAX_FILES;
        cfgval = client_cfg.client_max_files;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_max_files = (int)l;
            }
        }

        /* Determine if we should flatten writes or not */
        unifyfs_flatten_writes = 1;
        cfgval = client_cfg.client_flatten_writes;
        if (cfgval != NULL) {
            rc = configurator_bool_val(cfgval, &b);
            if (rc == 0) {
                unifyfs_flatten_writes = (bool)b;
            }
        }

        /* Determine if we should track all write extents and use them
         * to service read requests if all data is local */
        unifyfs_local_extents = 0;
        cfgval = client_cfg.client_local_extents;
        if (cfgval != NULL) {
            rc = configurator_bool_val(cfgval, &b);
            if (rc == 0) {
                unifyfs_local_extents = (bool)b;
            }
        }

        /* determine number of bits for chunk size */
        unifyfs_chunk_bits = UNIFYFS_CHUNK_BITS;
        cfgval = client_cfg.shmem_chunk_bits;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_chunk_bits = (int)l;
            }
        }

        /* determine maximum number of bytes of memory for chunk storage */
        unifyfs_chunk_mem = UNIFYFS_CHUNK_MEM;
        cfgval = client_cfg.shmem_chunk_mem;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifyfs_chunk_mem = (size_t)l;
            }
        }

        /* set chunk size, set chunk offset mask, and set total number
         * of chunks */
        unifyfs_chunk_size = 1 << unifyfs_chunk_bits;
        unifyfs_chunk_mask = unifyfs_chunk_size - 1;
        unifyfs_max_chunks = unifyfs_chunk_mem >> unifyfs_chunk_bits;

        /* set number of chunks in spillover device */
        unifyfs_spillover_max_chunks = unifyfs_spillover_size >> unifyfs_chunk_bits;

        /* define size of buffer used to cache key/value pairs for
         * data offsets before passing them to the server */
        unifyfs_index_buf_size = UNIFYFS_INDEX_BUF_SIZE;
        cfgval = client_cfg.logfs_index_buf_size;
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
        int num_fds = UNIFYFS_MAX_FILEDESCS;
        for (i = 0; i < num_fds; i++) {
            unifyfs_fd_init(i);
        }

        /* initialize file stream structures */
        int num_streams = UNIFYFS_MAX_FILEDESCS;
        for (i = 0; i < num_streams; i++) {
            unifyfs_stream_init(i);
        }

        /* initialize directory stream structures */
        int num_dirstreams = UNIFYFS_MAX_FILEDESCS;
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
        shm_super_size = unifyfs_superblock_size();

        /* get a superblock of shared memory and initialize our
         * global variables for this block */
        shm_super_buf = unifyfs_superblock_shmget(
                            shm_super_size, unifyfs_mount_shmget_key);
        if (shm_super_buf == NULL) {
            LOGERR("unifyfs_superblock_shmget() failed");
            return UNIFYFS_FAILURE;
        }

        /* initialize spillover store */
        if (unifyfs_use_spillover) {
            /* get directory in which to create spill over files */
            cfgval = client_cfg.spillover_data_dir;
            if (cfgval != NULL) {
                strncpy(external_data_dir, cfgval, sizeof(external_data_dir));
            } else {
                LOGERR("UNIFYFS_SPILLOVER_DATA_DIR not set, must be an existing"
                       " writable path (e.g., /mnt/ssd):");
                return UNIFYFS_FAILURE;
            }

            /* define path to the spill over file for data chunks */
            char spillfile_prefix[UNIFYFS_MAX_FILENAME];
            snprintf(spillfile_prefix, sizeof(spillfile_prefix),
                     "%s/spill_%d_%d.log",
                     external_data_dir, app_id, local_rank_idx);

            /* create the spill over file */
            unifyfs_spilloverblock =
                unifyfs_get_spillblock(unifyfs_spillover_size,
                                       spillfile_prefix);
            if (unifyfs_spilloverblock < 0) {
                LOGERR("unifyfs_get_spillblock() failed!");
                return UNIFYFS_FAILURE;
            }

            /* get directory in which to create spill over files
             * for key/value pairs */
            cfgval = client_cfg.spillover_meta_dir;
            if (cfgval != NULL) {
                strncpy(external_meta_dir, cfgval, sizeof(external_meta_dir));
            } else {
                LOGERR("UNIFYFS_SPILLOVER_META_DIR not set, must be an existing"
                       " writable path (e.g., /mnt/ssd):");
                return UNIFYFS_FAILURE;
            }

            /* define path to the spill over file for key/value pairs */
            snprintf(spillfile_prefix, sizeof(spillfile_prefix),
                     "%s/spill_index_%d_%d.log",
                     external_meta_dir, app_id, local_rank_idx);

            /* create the spill over file for key value data */
            unifyfs_spillmetablock =
                unifyfs_get_spillblock(unifyfs_index_buf_size,
                                       spillfile_prefix);
            if (unifyfs_spillmetablock < 0) {
                LOGERR("unifyfs_get_spillmetablock failed!");
                return UNIFYFS_FAILURE;
            }
        }

        /* remember that we've now initialized the library */
        unifyfs_initialized = 1;
    }

    return UNIFYFS_SUCCESS;
}

/* ---------------------------------------
 * APIs exposed to external libraries
 * --------------------------------------- */

/* Fill mount rpc input struct with client-side context info */
void fill_client_mount_info(unifyfs_mount_in_t* in)
{
    size_t meta_offset = (char*)unifyfs_indices.ptr_num_entries -
                         (char*)shm_super_buf;
    size_t meta_size   = unifyfs_max_index_entries
                         * sizeof(unifyfs_index_t);

    size_t data_offset = (char*)unifyfs_chunks - (char*)shm_super_buf;
    size_t data_size   = (size_t)unifyfs_max_chunks * unifyfs_chunk_size;

    in->app_id             = app_id;
    in->local_rank_idx     = local_rank_idx;
    in->dbg_rank           = client_rank;
    in->num_procs_per_node = local_rank_cnt;
    in->req_buf_sz         = shm_req_size;
    in->recv_buf_sz        = shm_recv_size;
    in->superblock_sz      = shm_super_size;
    in->meta_offset        = meta_offset;
    in->meta_size          = meta_size;
    in->data_offset        = data_offset;
    in->data_size          = data_size;
    in->external_spill_dir = strdup(external_data_dir);
}

/**
 * Initialize the shared recv memory buffer to receive data from the delegators
 */
static int unifyfs_init_recv_shm(int local_rank_idx, int app_id)
{
    /* get size of shared memory region from configuration */
    char* cfgval = client_cfg.shmem_recv_size;
    if (cfgval != NULL) {
        long l;
        int rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            shm_recv_size = l;
        }
    }

    /* define file name to shared memory file */
    snprintf(shm_recv_name, sizeof(shm_recv_name),
             "%d-recv-%d", app_id, local_rank_idx);

    /* allocate memory for shared memory receive buffer */
    shm_recv_buf = unifyfs_shm_alloc(shm_recv_name, shm_recv_size);
    if (shm_recv_buf == NULL) {
        LOGERR("Failed to create buffer for read replies");
        return UNIFYFS_FAILURE;
    }

    return UNIFYFS_SUCCESS;
}

/**
 * Initialize the shared request memory, which
 * is used to buffer the list of read requests
 * to be transferred to the delegator on the
 * server side.
 * @param local_rank_idx: local process id
 * @param app_id: which application this
 *  process is from
 * @return success/error code
 */
static int unifyfs_init_req_shm(int local_rank_idx, int app_id)
{
    /* get size of shared memory region from configuration */
    char* cfgval = client_cfg.shmem_req_size;
    if (cfgval != NULL) {
        long l;
        int rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            shm_req_size = l;
        }
    }

    /* define name of shared memory region for request buffer */
    snprintf(shm_req_name, sizeof(shm_req_name),
             "%d-req-%d", app_id, local_rank_idx);

    /* allocate memory for shared memory receive buffer */
    shm_req_buf = unifyfs_shm_alloc(shm_req_name, shm_req_size);
    if (shm_req_buf == NULL) {
        LOGERR("Failed to create buffer for read requests");
        return UNIFYFS_FAILURE;
    }

    return UNIFYFS_SUCCESS;
}


#if defined(UNIFYFS_USE_DOMAIN_SOCKET)
/**
 * initialize the client-side socket
 * used to communicate with the server-side
 * delegators. Each client is serviced by
 * one delegator.
 * @param proc_id: local process id
 * @param l_num_procs_per_node: number
 * of ranks on each compute node
 * @param l_num_del_per_node: number of server-side
 * delegators on the same node
 * @return success/error code
 */
static int unifyfs_init_socket(int proc_id, int l_num_procs_per_node,
                               int l_num_del_per_node)
{
    int rc = -1;
    int nprocs_per_del;
    int len;
    int result;
    int flag;
    struct sockaddr_un serv_addr;
    char tmp_path[UNIFYFS_MAX_FILENAME] = {0};
    char* pmi_path = NULL;

    client_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_sockfd < 0) {
        LOGERR("socket create failed");
        return -1;
    }

    /* calculate delegator assignment */
    nprocs_per_del = l_num_procs_per_node / l_num_del_per_node;
    if ((l_num_procs_per_node % l_num_del_per_node) != 0) {
        nprocs_per_del++;
    }
    snprintf(tmp_path, sizeof(tmp_path), "%s.%d.%d",
             SOCKET_PATH, getuid(), (proc_id / nprocs_per_del));

    // lookup domain socket path in key-val store
    if (unifyfs_keyval_lookup_local(key_unifyfsd_socket, &pmi_path) == 0) {
        memset(tmp_path, 0, sizeof(tmp_path));
        snprintf(tmp_path, sizeof(tmp_path), "%s", pmi_path);
        free(pmi_path);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strcpy(serv_addr.sun_path, tmp_path);
    len = sizeof(serv_addr);
    result = connect(client_sockfd, (struct sockaddr*)&serv_addr, len);

    /* exit with error if connection is not successful */
    if (result == -1) {
        rc = -1;
        LOGERR("socket connect failed");
        return rc;
    }

    flag = fcntl(client_sockfd, F_GETFL);
    fcntl(client_sockfd, F_SETFL, flag | O_NONBLOCK);

    cmd_fd.fd = client_sockfd;
    cmd_fd.events = POLLIN | POLLHUP;
    cmd_fd.revents = 0;

    return 0;
}
#endif // UNIFYFS_USE_DOMAIN_SOCKET

static int compare_int(const void* a, const void* b)
{
    const int* ptr_a = a;
    const int* ptr_b = b;

    if (*ptr_a - *ptr_b > 0) {
        return 1;
    }

    if (*ptr_a - *ptr_b < 0) {
        return -1;
    }

    return 0;
}

static int compare_name_rank_pair(const void* a, const void* b)
{
    const name_rank_pair_t* pair_a = a;
    const name_rank_pair_t* pair_b = b;

    if (strcmp(pair_a->hostname, pair_b->hostname) > 0) {
        return 1;
    }

    if (strcmp(pair_a->hostname, pair_b->hostname) < 0) {
        return -1;
    }

    return 0;
}

/**
 * calculate the number of ranks per node
 *
 * sets global variables local_rank_cnt & local_rank_idx
 *
 * @param numTasks: number of tasks in the application
 * @return success/error code
 */
static int CountTasksPerNode(int rank, int numTasks)
{
    char hostname[UNIFYFS_MAX_HOSTNAME];
    char localhost[UNIFYFS_MAX_HOSTNAME];
    int resultsLen = UNIFYFS_MAX_HOSTNAME;
    MPI_Status status;
    int i, j, rc;
    int* local_rank_lst;

    if (numTasks <= 0) {
        LOGERR("invalid number of tasks");
        return -1;
    }

    rc = MPI_Get_processor_name(localhost, &resultsLen);
    if (rc != 0) {
        LOGERR("failed to get the processor's name");
    }

    if (rank == 0) {
        /* a container of (rank, host) mappings*/
        name_rank_pair_t* host_set =
            (name_rank_pair_t*)calloc(numTasks,
                                      sizeof(name_rank_pair_t));

        strcpy(host_set[0].hostname, localhost);
        host_set[0].rank = 0;

        /*
         * MPI_Recv all hostnames, and compare to local hostname
         */
        for (i = 1; i < numTasks; i++) {
            rc = MPI_Recv(hostname, UNIFYFS_MAX_HOSTNAME,
                          MPI_CHAR, MPI_ANY_SOURCE,
                          MPI_ANY_TAG, MPI_COMM_WORLD,
                          &status);
            if (rc != 0) {
                LOGERR("cannot receive hostnames");
                return -1;
            }
            strcpy(host_set[i].hostname, hostname);
            host_set[i].rank = status.MPI_SOURCE;
        }

        /* sort by hostname */
        qsort(host_set, numTasks, sizeof(name_rank_pair_t),
              compare_name_rank_pair);

        /*
         * rank_cnt: records the number of processes on each node
         * rank_set: the list of ranks for each node
         */
        int** rank_set = (int**)calloc(numTasks, sizeof(int*));
        int* rank_cnt = (int*)calloc(numTasks, sizeof(int));
        int cursor = 0;
        int set_counter = 0;

        for (i = 1; i < numTasks; i++) {
            if (strcmp(host_set[i].hostname,
                       host_set[i - 1].hostname) != 0) {
                // found a different host, so switch to a new set
                rank_set[set_counter] =
                    (int*)calloc((i - cursor), sizeof(int));
                rank_cnt[set_counter] = i - cursor;
                int hiter, riter = 0;
                for (hiter = cursor; hiter < i; hiter++, riter++) {
                    rank_set[set_counter][riter] = host_set[hiter].rank;
                }

                set_counter++;
                cursor = i;
            }
        }

        /* fill rank_cnt and rank_set entry for the last node */
        rank_set[set_counter] = (int*)calloc((i - cursor), sizeof(int));
        rank_cnt[set_counter] = numTasks - cursor;
        j = 0;
        for (i = cursor; i < numTasks; i++, j++) {
            rank_set[set_counter][j] = host_set[i].rank;
        }
        set_counter++;

        /* broadcast the rank_cnt and rank_set information to each rank */
        int root_set_no = -1;
        for (i = 0; i < set_counter; i++) {
            /* send each rank set to all of its ranks */
            for (j = 0; j < rank_cnt[i]; j++) {
                if (rank_set[i][j] != 0) {
                    rc = MPI_Send(&rank_cnt[i], 1, MPI_INT, rank_set[i][j],
                                    0, MPI_COMM_WORLD);
                    if (rc != 0) {
                        LOGERR("cannot send local rank cnt");
                        return -1;
                    }
                    rc = MPI_Send(rank_set[i], rank_cnt[i], MPI_INT,
                                  rank_set[i][j], 0, MPI_COMM_WORLD);
                    if (rc != 0) {
                        LOGERR("cannot send local rank list");
                        return -1;
                    }
                } else {
                    root_set_no = i;
                    local_rank_cnt = rank_cnt[i];
                    local_rank_lst = (int*)calloc(rank_cnt[i], sizeof(int));
                    memcpy(local_rank_lst, rank_set[i],
                           (local_rank_cnt * sizeof(int)));
                }
            }
        }

        for (i = 0; i < set_counter; i++) {
            free(rank_set[i]);
        }
        free(rank_cnt);
        free(host_set);
        free(rank_set);
    } else {
        /* non-root process - MPI_Send hostname to root node */
        rc = MPI_Send(localhost, UNIFYFS_MAX_HOSTNAME, MPI_CHAR,
                      0, 0, MPI_COMM_WORLD);
        if (rc != 0) {
            LOGERR("cannot send host name");
            return -1;
        }
        /* receive the local rank set count */
        rc = MPI_Recv(&local_rank_cnt, 1, MPI_INT,
                      0, 0, MPI_COMM_WORLD, &status);
        if (rc != 0) {
            LOGERR("cannot receive local rank cnt");
            return -1;
        }
        /* receive the the local rank set */
        local_rank_lst = (int*)calloc(local_rank_cnt, sizeof(int));
        rc = MPI_Recv(local_rank_lst, local_rank_cnt, MPI_INT,
                      0, 0, MPI_COMM_WORLD, &status);
        if (rc != 0) {
            free(local_rank_lst);
            LOGERR("cannot receive local rank list");
            return -1;
        }
    }

    /* sort local ranks by rank */
    qsort(local_rank_lst, local_rank_cnt, sizeof(int), compare_int);
    for (i = 0; i < local_rank_cnt; i++) {
        if (local_rank_lst[i] == rank) {
            local_rank_idx = i;
            break;
        }
    }
    free(local_rank_lst);
    return 0;
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
int unifyfs_mount(const char prefix[], int rank, size_t size,
                  int l_app_id)
{
    int rc;
    int kv_rank, kv_nranks;
    bool b;
    char* cfgval;

    if (-1 != unifyfs_mounted) {
        if (l_app_id != unifyfs_mounted) {
            LOGERR("multiple mount support not yet implemented");
            return UNIFYFS_FAILURE;
        } else {
            LOGDBG("already mounted");
            return UNIFYFS_SUCCESS;
        }
    }

    /* record our rank for debugging messages,
     * record the value we should use for an app_id */
    app_id = l_app_id;
    client_rank = rank;
    global_rank_cnt = (int)size;

    /* print log messages to stderr */
    unifyfs_log_open(NULL);

    /************************
     * read configuration values
     ************************/

    // initialize configuration
    rc = unifyfs_config_init(&client_cfg, 0, NULL);
    if (rc) {
        LOGERR("failed to initialize configuration.");
        return UNIFYFS_FAILURE;
    }
    client_cfg.ptype = UNIFYFS_CLIENT;

    // update configuration from runstate file
    rc = unifyfs_read_runstate(&client_cfg, NULL);
    if (rc) {
        LOGERR("failed to update configuration from runstate.");
        return UNIFYFS_FAILURE;
    }

    // initialize k-v store access
    kv_rank = client_rank;
    kv_nranks = size;
    rc = unifyfs_keyval_init(&client_cfg, &kv_rank, &kv_nranks);
    if (rc) {
        LOGERR("failed to update configuration from runstate.");
        return UNIFYFS_FAILURE;
    }
    if ((client_rank != kv_rank) || (size != kv_nranks)) {
        LOGDBG("mismatch on mount vs kvstore rank/size");
    }

    /************************
     * record our mount point, and initialize structures to
     * store data
     ************************/

    /* record a copy of the prefix string defining the mount point
     * we should intercept */
    unifyfs_mount_prefix = strdup(prefix);
    unifyfs_mount_prefixlen = strlen(unifyfs_mount_prefix);

    /*
     * unifyfs_mount_shmget_key marks the start of
     * the superblock shared memory of each rank
     * each process has three types of shared memory:
     * request memory, recv memory and superblock
     * memory. We set unifyfs_mount_shmget_key in
     * this way to avoid different ranks conflicting
     * on the same name in shm_open.
     */
    cfgval = client_cfg.shmem_single;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if ((rc == 0) && b) {
            unifyfs_use_single_shm = 1;
        }
    }

    /* compute our local rank on the node,
     * the following call initializes local_rank_{cnt,ndx} */
    rc = CountTasksPerNode(rank, size);
    if (rc < 0) {
        LOGERR("cannot get the local rank list.");
        return -1;
    }

    /* use our local rank on the node in shared memory and file
     * names to avoid conflicting with other procs on our node */
    unifyfs_mount_shmget_key = local_rank_idx;

    /* initialize our library, creates superblock and spillover files */
    int ret = unifyfs_init(rank);
    if (ret != UNIFYFS_SUCCESS) {
        return ret;
    }

    /* open rpc connection to server */
    ret = unifyfs_client_rpc_init();
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("Failed to initialize client RPC");
        return ret;
    }

    /* create shared memory region for read requests */
    rc = unifyfs_init_req_shm(local_rank_idx, app_id);
    if (rc < 0) {
        LOGERR("failed to init shared request memory");
        return UNIFYFS_FAILURE;
    }

    /* create shared memory region for holding data for read replies */
    rc = unifyfs_init_recv_shm(local_rank_idx, app_id);
    if (rc < 0) {
        LOGERR("failed to init shared receive memory");
        unifyfs_shm_unlink(shm_req_name);
        return UNIFYFS_FAILURE;
    }

    /* Call client mount rpc function
     * to register our shared memory and files with server */
    LOGDBG("calling mount");
    ret = invoke_client_mount_rpc();
    if (ret != UNIFYFS_SUCCESS) {
        /* If we fail to connect to the server, bail with an error */
        LOGERR("Failed to mount to server");

        /* TODO: need more clean up here, but this at least deletes
         * some files we would otherwise leave behind */

        /* Delete file for shared memory regions for
         * read requests and read replies */
        unifyfs_shm_unlink(shm_req_name);
        unifyfs_shm_unlink(shm_recv_name);

        return ret;
    }

    /* Once we return from mount, we know the server has attached to our
     * shared memory regions for read requests and read replies, so we
     * can safely remove these files.  The memory regions will stay active
     * until both client and server unmap them.  We keep the superblock file
     * around so that a future client can reattach to it. */
    unifyfs_shm_unlink(shm_req_name);
    unifyfs_shm_unlink(shm_recv_name);

#if defined(UNIFYFS_USE_DOMAIN_SOCKET)
    /* open a socket to the server */
    rc = unifyfs_init_socket(local_rank_idx, local_rank_cnt,
                             local_del_cnt);
    if (rc < 0) {
        LOGERR("failed to initialize socket, rc == %d", rc);
        return UNIFYFS_FAILURE;
    }
#endif

    /* add mount point as a new directory in the file list */
    if (unifyfs_get_fid_from_path(prefix) < 0) {
        /* no entry exists for mount point, so create one */
        int fid = unifyfs_fid_create_directory(prefix);
        if (fid < 0) {
            /* if there was an error, return it */
            LOGERR("failed to create directory entry for mount point: `%s'",
                   prefix);
            return UNIFYFS_FAILURE;
        }
    }

    /* record client state as mounted for specific app_id */
    unifyfs_mounted = app_id;

    return UNIFYFS_SUCCESS;
}

/* free resources allocated during unifyfs_init,
 * generally we do this in reverse order that
 * things were initailized in */
static int unifyfs_finalize(void)
{
    int rc = UNIFYFS_SUCCESS;

    if (!unifyfs_initialized) {
        /* not initialized yet, so we shouldn't call finalize */
        return UNIFYFS_FAILURE;
    }

    /* close spillover files */
    if (unifyfs_spilloverblock != 0) {
        close(unifyfs_spilloverblock);
        unifyfs_spilloverblock = 0;
    }

    if (unifyfs_spillmetablock != 0) {
        close(unifyfs_spillmetablock);
        unifyfs_spillmetablock = 0;
    }

    /* detach from superblock */
    unifyfs_shm_free(shm_super_name, shm_super_size, &shm_super_buf);

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

/**
 * unmount the mounted file system
 * TODO: Add support for unmounting more than
 * one filesystem.
 * @return success/error code
 */
int unifyfs_unmount(void)
{
    int rc;
    int ret = UNIFYFS_SUCCESS;

    if (-1 == unifyfs_mounted) {
        return UNIFYFS_SUCCESS;
    }

    /************************
     * tear down connection to server
     ************************/

    /* detach from shared memory regions */
    unifyfs_shm_free(shm_req_name,  shm_req_size,  &shm_req_buf);
    unifyfs_shm_free(shm_recv_name, shm_recv_size, &shm_recv_buf);

    /* close socket to server */
    if (client_sockfd >= 0) {
        errno = 0;
        rc = close(client_sockfd);
        if (rc != 0) {
            LOGERR("Failed to close() socket to server errno=%d (%s)",
                   errno, strerror(errno));
        }
    }

    /* invoke unmount rpc to tell server we're disconnecting */
    LOGDBG("calling unmount");
    rc = invoke_client_unmount_rpc();
    if (rc) {
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
    unifyfs_finalize();

    /* free memory tracking our mount prefix string */
    if (unifyfs_mount_prefix != NULL) {
        free(unifyfs_mount_prefix);
        unifyfs_mount_prefix = NULL;
        unifyfs_mount_prefixlen = 0;
    }

    /************************
     * free configuration values
     ************************/

    /* clean up configuration */
    rc = unifyfs_config_fini(&client_cfg);
    if (rc) {
        LOGERR("unifyfs_config_fini() failed");
        ret = UNIFYFS_FAILURE;
    }

    /* shut down our logging */
    unifyfs_log_close();

    unifyfs_mounted = -1;

    return ret;
}

#define UNIFYFS_TX_BUFSIZE (64*(1<<10))

enum {
    UNIFYFS_TX_STAGE_OUT = 0,
    UNIFYFS_TX_STAGE_IN = 1,
    UNIFYFS_TX_SERIAL = 0,
    UNIFYFS_TX_PARALLEL = 1,
};

static
ssize_t do_transfer_data(int fd_src, int fd_dst, off_t offset, size_t count)
{
    ssize_t ret = 0;
    off_t pos = 0;
    ssize_t n_written = 0;
    ssize_t n_left = 0;
    ssize_t n_processed = 0;
    size_t len = UNIFYFS_TX_BUFSIZE;
    char buf[UNIFYFS_TX_BUFSIZE] = { 0, };

    pos = lseek(fd_src, offset, SEEK_SET);
    if (pos == (off_t) -1) {
        LOGERR("lseek failed (%d: %s)\n", errno, strerror(errno));
        ret = -1;
        goto out;
    }

    pos = lseek(fd_dst, offset, SEEK_SET);
    if (pos == (off_t) -1) {
        LOGERR("lseek failed (%d: %s)\n", errno, strerror(errno));
        ret = -1;
        goto out;
    }

    while (count > n_processed) {
        if (len > count) {
            len = count;
        }

        n_left = read(fd_src, buf, len);

        if (n_left == 0) {  /* EOF */
            break;
        } else if (n_left < 0) {   /* error */
            ret = errno;
            goto out;
        }

        do {
            n_written = write(fd_dst, buf, n_left);

            if (n_written < 0) {
                ret = errno;
                goto out;
            } else if (n_written == 0 && errno && errno != EAGAIN) {
                ret = errno;
                goto out;
            }

            n_left -= n_written;
            n_processed += n_written;
        } while (n_left);
    }

out:
    return ret;
}

static int do_transfer_file_serial(const char* src, const char* dst,
                                   struct stat* sb_src, int dir)
{
    int ret = 0;
    int fd_src = 0;
    int fd_dst = 0;
    char buf[UNIFYFS_TX_BUFSIZE] = { 0, };

    /*
     * for now, we do not use the @dir hint.
     */

    fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        return errno;
    }

    fd_dst = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_dst < 0) {
        ret = errno;
        goto out_close_src;
    }

    ret = do_transfer_data(fd_src, fd_dst, 0, sb_src->st_size);
    if (ret < 0) {
        LOGERR("do_transfer_data failed!");
    } else {
        fsync(fd_dst);
    }

    close(fd_dst);
out_close_src:
    close(fd_src);

    return ret;
}

static int do_transfer_file_parallel(const char* src, const char* dst,
                                     struct stat* sb_src, int dir)
{
    int ret = 0;
    int fd_src = 0;
    int fd_dst = 0;
    uint64_t total_chunks = 0;
    uint64_t chunk_start = 0;
    uint64_t remainder = 0;
    uint64_t n_chunks = 0;
    uint64_t offset = 0;
    uint64_t len = 0;
    uint64_t size = sb_src->st_size;

    fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        return errno;
    }

    fd_dst = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_dst < 0) {
        ret = errno;
        goto out_close_src;
    }

    /*
     * if the file is smaller than (rankcount*buffersize), just do with the
     * serial mode.
     *
     * FIXME: is this assumtion fair even for the large rank count?
     */
    if ((UNIFYFS_TX_BUFSIZE * global_rank_cnt) > size) {
        if (client_rank == 0) {
            ret = do_transfer_file_serial(src, dst, sb_src, dir);
            if (ret) {
                LOGERR("do_transfer_file_parallel failed");
            }

            return ret;
        }
    }

    total_chunks = size / UNIFYFS_TX_BUFSIZE;
    if (size % UNIFYFS_TX_BUFSIZE) {
        total_chunks++;
    }

    n_chunks = total_chunks / global_rank_cnt;
    remainder = total_chunks % global_rank_cnt;

    chunk_start = n_chunks * client_rank;
    if (client_rank < remainder) {
        chunk_start += client_rank;
        n_chunks += 1;
    } else {
        chunk_start += remainder;
    }

    offset = chunk_start * UNIFYFS_TX_BUFSIZE;

    if (client_rank == (global_rank_cnt - 1)) {
        len = (n_chunks - 1) * UNIFYFS_TX_BUFSIZE;
        len += size % UNIFYFS_TX_BUFSIZE;
    } else {
        len = n_chunks * UNIFYFS_TX_BUFSIZE;
    }

    LOGDBG("parallel transfer (%d/%d): offset=%lu, length=%lu",
           client_rank, global_rank_cnt,
           (unsigned long) offset, (unsigned long) len);

    ret = do_transfer_data(fd_src, fd_dst, offset, len);

    close(fd_dst);
out_close_src:
    close(fd_src);

    return ret;
}

int unifyfs_transfer_file(const char* src, const char* dst, int parallel)
{
    int ret = 0;
    int dir = 0;
    struct stat sb_src = { 0, };
    mode_t source_file_mode_write_removed;
    struct stat sb_dst = { 0, };
    int unify_src = 0;
    int unify_dst = 0;
    char dst_path[PATH_MAX] = { 0, };
    char* pos = dst_path;
    char* src_path = strdup(src);

    int local_return_val;

    if (!src_path) {
        return -ENOMEM;
    }

    if (unifyfs_intercept_path(src)) {
        dir = UNIFYFS_TX_STAGE_OUT;
        unify_src = 1;
    }

    ret = UNIFYFS_WRAP(stat)(src, &sb_src);
    if (ret < 0) {
        return -errno;
    }

    pos += sprintf(pos, "%s", dst);

    if (unifyfs_intercept_path(dst)) {
        dir = UNIFYFS_TX_STAGE_IN;
        unify_dst = 1;
    }

    ret = UNIFYFS_WRAP(stat)(dst, &sb_dst);
    if (ret == 0 && !S_ISREG(sb_dst.st_mode)) {
        if (S_ISDIR(sb_dst.st_mode)) {
            sprintf(pos, "/%s", basename((char*) src_path));
        } else {
            return -EEXIST;
        }
    }

    if (unify_src + unify_dst != 1) {
        return -EINVAL;
    }

    if (parallel) {
        local_return_val =
	    do_transfer_file_parallel(src_path, dst_path, &sb_src, dir);
    } else {
        local_return_val =
	    do_transfer_file_serial(src_path, dst_path, &sb_src, dir);
    }

    // We know here that one (but not both) of the constituent files
    // is in the unify FS.  We just have to decide if the *destination* file is.
    // If it is, then now that we've transferred it, we'll set it to be readable
    // so that it will be laminated and will be readable by other processes.
    if (unify_dst) {
      // pull the source file's mode bits, remove all the write bits but leave
      // the rest intact and store that new mode.  Now that the file has been
      // copied into the unify file system, chmod the file to the new
      // permission.  When unify senses all the write bits are removed it will
      // laminate the file.
        source_file_mode_write_removed =
	    (sb_src.st_mode) & ~(0222);
        chmod(dst_path, source_file_mode_write_removed);
    }
    return local_return_val;
}

