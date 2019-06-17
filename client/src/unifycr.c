/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
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

#include "unifycr-internal.h"
#include "unifycr-fixed.h"
#include "unifycr_runstate.h"

#include <time.h>
#include <mpi.h>
#include <openssl/md5.h>

#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif

#ifdef UNIFYCR_GOTCHA
#include "gotcha/gotcha_types.h"
#include "gotcha/gotcha.h"
#include "gotcha_map_unifycr_list.h"
#endif

#include "unifycr_client_rpcs.h"
#include "unifycr_server_rpcs.h"
#include "unifycr_rpc_util.h"
#include "margo_client.h"

/* avoid duplicate mounts (for now) */
static int unifycr_mounted = -1;

static int unifycr_fpos_enabled   = 1;  /* whether we can use fgetpos/fsetpos */

/*
 * unifycr variable:
 * */

unifycr_cfg_t client_cfg;

unifycr_index_buf_t unifycr_indices;
unifycr_fattr_buf_t unifycr_fattrs;
static size_t unifycr_index_buf_size;    /* size of metadata log */
static size_t unifycr_fattr_buf_size;
unsigned long unifycr_max_index_entries; /* max metadata log entries */
unsigned int unifycr_max_fattr_entries;
int unifycr_spillmetablock;

int* local_rank_lst;
int local_rank_cnt;
int local_rank_idx;

int local_del_cnt = 1;
int client_sockfd = -1;
struct pollfd cmd_fd;

/* shared memory buffer to transfer read requests
 * from client to server */
static char   shm_req_name[GEN_STR_LEN]  = {0};
static size_t shm_req_size = UNIFYCR_SHMEM_REQ_SIZE;
void* shm_req_buf;

/* shared memory buffer to transfer read replies
 * from server to client */
static char   shm_recv_name[GEN_STR_LEN] = {0};
static size_t shm_recv_size = UNIFYCR_SHMEM_RECV_SIZE;
void* shm_recv_buf;

char cmd_buf[CMD_BUF_SIZE] = {0};

int app_id;
size_t unifycr_key_slice_range;

/* whether chunks should be allocated to
 * store file contents in memory */
int unifycr_use_memfs = 1;

/* whether chunks should be allocated to
 * store file contents on spill over device */
int unifycr_use_spillover = 1;

static int unifycr_use_single_shm = 0;
static int unifycr_page_size      = 0;

static off_t unifycr_max_offt;
static off_t unifycr_min_offt;
static off_t unifycr_max_long;
static off_t unifycr_min_long;

/* TODO: moved these to fixed file */
int    unifycr_max_files;  /* maximum number of files to store */
size_t unifycr_chunk_mem;  /* number of bytes in memory to be used for chunk storage */
int    unifycr_chunk_bits; /* we set chunk size = 2^unifycr_chunk_bits */
off_t  unifycr_chunk_size; /* chunk size in bytes */
off_t  unifycr_chunk_mask; /* mask applied to logical offset to determine physical offset within chunk */
long   unifycr_max_chunks; /* maximum number of chunks that fit in memory */

/* number of bytes in spillover to be used for chunk storage */
static size_t unifycr_spillover_size;

/* maximum number of chunks that fit in spillover storage */
long unifycr_spillover_max_chunks;

#ifdef HAVE_LIBNUMA
static char unifycr_numa_policy[10];
static int unifycr_numa_bank = -1;
#endif

extern pthread_mutex_t unifycr_stack_mutex;

/* keep track of what we've initialized */
int unifycr_initialized = 0;

/* shared memory for superblock */
static char   shm_super_name[GEN_STR_LEN] = {0};
static size_t shm_super_size;

/* global persistent memory block (metadata + data) */
void* shm_super_buf;
static void* free_fid_stack;
void* free_chunk_stack;
void* free_spillchunk_stack;
unifycr_filename_t* unifycr_filelist;
static unifycr_filemeta_t* unifycr_filemetas;

unifycr_chunkmeta_t* unifycr_chunkmetas;

char* unifycr_chunks;
int unifycr_spilloverblock = 0;
int unifycr_spillmetablock = 0; /*used for log-structured i/o*/

/* array of file descriptors */
unifycr_fd_t unifycr_fds[UNIFYCR_MAX_FILEDESCS];
rlim_t unifycr_fd_limit;

/* array of file streams */
unifycr_stream_t unifycr_streams[UNIFYCR_MAX_FILEDESCS];

/*
 * TODO: the number of open directories clearly won't exceed the number of file
 * descriptors. however, the current UNIFYCR_MAX_FILEDESCS value of 256 will
 * quickly run out. if this value is fixed to be reasonably larger, then we
 * would need a way to dynamically allocate the dirstreams instead of the
 * following fixed size array.
 */

/* array of DIR* streams to be used */
unifycr_dirstream_t unifycr_dirstreams[UNIFYCR_MAX_FILEDESCS];

/* stack to track free file descriptor values,
 * each is an index into unifycr_fds array */
void* unifycr_fd_stack;

/* stack to track free file streams,
 * each is an index into unifycr_streams array */
void* unifycr_stream_stack;

/* stack to track free directory streams,
 * each is an index into unifycr_dirstreams array */
void* unifycr_dirstream_stack;

/* mount point information */
char*  unifycr_mount_prefix;
size_t unifycr_mount_prefixlen = 0;
static key_t  unifycr_mount_shmget_key = 0;

/* mutex to lock stack operations */
pthread_mutex_t unifycr_stack_mutex = PTHREAD_MUTEX_INITIALIZER;

/* path of external storage's mount point*/

char external_data_dir[UNIFYCR_MAX_FILENAME] = {0};
char external_meta_dir[UNIFYCR_MAX_FILENAME] = {0};

/* single function to route all unsupported wrapper calls through */
int unifycr_vunsupported(
    const char* fn_name,
    const char* file,
    int line,
    const char* fmt,
    va_list args)
{
    /* print a message about where in the UNIFYCR code we are */
    printf("UNIFYCR UNSUPPORTED: %s() at %s:%d: ", fn_name, file, line);

    /* print string with more info about call, e.g., param values */
    va_list args2;
    va_copy(args2, args);
    vprintf(fmt, args2);
    va_end(args2);

    /* TODO: optionally abort */

    return UNIFYCR_SUCCESS;
}

/* single function to route all unsupported wrapper calls through */
int unifycr_unsupported(
    const char* fn_name,
    const char* file,
    int line,
    const char* fmt,
    ...)
{
    /* print string with more info about call, e.g., param values */
    va_list args;
    va_start(args, fmt);
    int rc = unifycr_vunsupported(fn_name, file, line, fmt, args);
    va_end(args);
    return rc;
}

/* given an UNIFYCR error code, return corresponding errno code */
int unifycr_err_map_to_errno(int rc)
{
    unifycr_error_e ec = (unifycr_error_e)rc;

    switch (ec) {
    case UNIFYCR_SUCCESS:
        return 0;
    case UNIFYCR_ERROR_BADF:
        return EBADF;
    case UNIFYCR_ERROR_EXIST:
        return EEXIST;
    case UNIFYCR_ERROR_FBIG:
        return EFBIG;
    case UNIFYCR_ERROR_INVAL:
        return EINVAL;
    case UNIFYCR_ERROR_ISDIR:
        return EISDIR;
    case UNIFYCR_ERROR_NAMETOOLONG:
        return ENAMETOOLONG;
    case UNIFYCR_ERROR_NFILE:
        return ENFILE;
    case UNIFYCR_ERROR_NOENT:
        return ENOENT;
    case UNIFYCR_ERROR_NOMEM:
        return ENOMEM;
    case UNIFYCR_ERROR_NOSPC:
        return ENOSPC;
    case UNIFYCR_ERROR_NOTDIR:
        return ENOTDIR;
    case UNIFYCR_ERROR_OVERFLOW:
        return EOVERFLOW;
    default:
        break;
    }
    return EIO;
}

/* given an errno error code, return corresponding UnifyCR error code */
int unifycr_errno_map_to_err(int rc)
{
    switch (rc) {
    case 0:
        return (int)UNIFYCR_SUCCESS;
    case EBADF:
        return (int)UNIFYCR_ERROR_BADF;
    case EEXIST:
        return (int)UNIFYCR_ERROR_EXIST;
    case EFBIG:
        return (int)UNIFYCR_ERROR_FBIG;
    case EINVAL:
        return (int)UNIFYCR_ERROR_INVAL;
    case EIO:
        return (int)UNIFYCR_ERROR_IO;
    case EISDIR:
        return (int)UNIFYCR_ERROR_ISDIR;
    case ENAMETOOLONG:
        return (int)UNIFYCR_ERROR_NAMETOOLONG;
    case ENFILE:
        return (int)UNIFYCR_ERROR_NFILE;
    case ENOENT:
        return (int)UNIFYCR_ERROR_NOENT;
    case ENOMEM:
        return (int)UNIFYCR_ERROR_NOMEM;
    case ENOSPC:
        return (int)UNIFYCR_ERROR_NOSPC;
    case ENOTDIR:
        return (int)UNIFYCR_ERROR_NOTDIR;
    case EOVERFLOW:
        return (int)UNIFYCR_ERROR_OVERFLOW;
    default:
        break;
    }
    return (int)UNIFYCR_FAILURE;
}

/* returns 1 if two input parameters will overflow their type when
 * added together */
inline int unifycr_would_overflow_offt(off_t a, off_t b)
{
    /* if both parameters are positive, they could overflow when
     * added together */
    if (a > 0 && b > 0) {
        /* if the distance between a and max is greater than or equal to
         * b, then we could add a and b and still not exceed max */
        if (unifycr_max_offt - a >= b) {
            return 0;
        }
        return 1;
    }

    /* if both parameters are negative, they could underflow when
     * added together */
    if (a < 0 && b < 0) {
        /* if the distance between min and a is less than or equal to
         * b, then we could add a and b and still not exceed min */
        if (unifycr_min_offt - a <= b) {
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
inline int unifycr_would_overflow_long(long a, long b)
{
    /* if both parameters are positive, they could overflow when
     * added together */
    if (a > 0 && b > 0) {
        /* if the distance between a and max is greater than or equal to
         * b, then we could add a and b and still not exceed max */
        if (unifycr_max_long - a >= b) {
            return 0;
        }
        return 1;
    }

    /* if both parameters are negative, they could underflow when
     * added together */
    if (a < 0 && b < 0) {
        /* if the distance between min and a is less than or equal to
         * b, then we could add a and b and still not exceed min */
        if (unifycr_min_long - a <= b) {
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
mode_t unifycr_getmode(mode_t perms)
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
inline int unifycr_stack_lock()
{
    if (unifycr_use_single_shm) {
        return pthread_mutex_lock(&unifycr_stack_mutex);
    }
    return 0;
}

/* unlock access to shared data structures in superblock */
inline int unifycr_stack_unlock()
{
    if (unifycr_use_single_shm) {
        return pthread_mutex_unlock(&unifycr_stack_mutex);
    }
    return 0;
}

/* sets flag if the path is a special path */
inline int unifycr_intercept_path(const char* path)
{
    /* don't intecept anything until we're initialized */
    if (!unifycr_initialized) {
        return 0;
    }

    /* if the path starts with our mount point, intercept it */
    if (strncmp(path, unifycr_mount_prefix, unifycr_mount_prefixlen) == 0) {
        return 1;
    }
    return 0;
}

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
inline int unifycr_intercept_fd(int* fd)
{
    int oldfd = *fd;

    /* don't intecept anything until we're initialized */
    if (!unifycr_initialized) {
        return 0;
    }

    if (oldfd < unifycr_fd_limit) {
        /* this fd is a real system fd, so leave it as is */
        return 0;
    } else if (oldfd < 0) {
        /* this is an invalid fd, so we should not intercept it */
        return 0;
    } else {
        /* this is an fd we generated and returned to the user,
         * so intercept the call and shift the fd */
        int newfd = oldfd - unifycr_fd_limit;
        *fd = newfd;
        LOGDBG("Changing fd from exposed %d to internal %d", oldfd, newfd);
        return 1;
    }
}

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
inline int unifycr_intercept_stream(FILE* stream)
{
    /* don't intecept anything until we're initialized */
    if (!unifycr_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * file stream array */
    unifycr_stream_t* ptr   = (unifycr_stream_t*) stream;
    unifycr_stream_t* start = &(unifycr_streams[0]);
    unifycr_stream_t* end   = &(unifycr_streams[UNIFYCR_MAX_FILEDESCS]);
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given an directory stream, return 1 if we should intercept this
 * fdirecotry, 0 otherwise */
inline int unifycr_intercept_dirstream(DIR* dirp)
{
    /* don't intecept anything until we're initialized */
    if (!unifycr_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * directory stream array */
    unifycr_dirstream_t* ptr   = (unifycr_dirstream_t*) dirp;
    unifycr_dirstream_t* start = &(unifycr_dirstreams[0]);
    unifycr_dirstream_t* end   = &(unifycr_dirstreams[UNIFYCR_MAX_FILEDESCS]);
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given a path, return the file id */
inline int unifycr_get_fid_from_path(const char* path)
{
    int i = 0;
    while (i < unifycr_max_files) {
        if (unifycr_filelist[i].in_use &&
            strcmp((void*)&unifycr_filelist[i].filename, path) == 0) {
            LOGDBG("File found: unifycr_filelist[%d].filename = %s",
                   i, (char*)&unifycr_filelist[i].filename);
            return i;
        }
        i++;
    }

    /* couldn't find specified path */
    return -1;
}

/* initialize file descriptor structure for given fd value */
int unifycr_fd_init(int fd)
{
    /* get pointer to file descriptor struct for this fd value */
    unifycr_fd_t* filedesc = &(unifycr_fds[fd]);

    /* set fid to -1 to indicate fd is not active,
     * set file position to max value,
     * disable read and write flags */
    filedesc->fid   = -1;
    filedesc->pos   = (off_t) -1;
    filedesc->read  = 0;
    filedesc->write = 0;

    return UNIFYCR_SUCCESS;
}

/* initialize file streams structure for given sid value */
int unifycr_stream_init(int sid)
{
    /* get pointer to file stream struct for this id value */
    unifycr_stream_t* s = &(unifycr_streams[sid]);

    /* record our id so when given a pointer to the stream
     * struct we can easily recover our id value */
    s->sid = sid;

    /* set fd to -1 to indicate stream is not active */
    s->fd = -1;

    return UNIFYCR_SUCCESS;
}

/* initialize directory streams structure for given dirid value */
int unifycr_dirstream_init(int dirid)
{
    /* get pointer to directory stream struct for this id value */
    unifycr_dirstream_t* dirp = &(unifycr_dirstreams[dirid]);

    /* initialize fields in structure */
    memset((void*) dirp, 0, sizeof(*dirp));

    /* record our id so when given a pointer to the stream
     * struct we can easily recover our id value */
    dirp->dirid = dirid;

    /* set fid to -1 to indicate stream is not active */
    dirp->fid = -1;

    return UNIFYCR_SUCCESS;
}

/* given a file descriptor, return the file id */
inline int unifycr_get_fid_from_fd(int fd)
{
    /* check that file descriptor is within range */
    if (fd < 0 || fd >= UNIFYCR_MAX_FILEDESCS) {
        return -1;
    }

    /* get local file id that file descriptor is assocated with,
     * will be -1 if not active */
    int fid = unifycr_fds[fd].fid;
    return fid;
}

/* return address of file descriptor structure or NULL if fd is out
 * of range */
inline unifycr_fd_t* unifycr_get_filedesc_from_fd(int fd)
{
    if (fd >= 0 && fd < UNIFYCR_MAX_FILEDESCS) {
        unifycr_fd_t* filedesc = &(unifycr_fds[fd]);
        return filedesc;
    }
    return NULL;
}

/* given a file id, return a pointer to the meta data,
 * otherwise return NULL */
unifycr_filemeta_t* unifycr_get_meta_from_fid(int fid)
{
    /* check that the file id is within range of our array */
    if (fid >= 0 && fid < unifycr_max_files) {
        /* get a pointer to the file meta data structure */
        unifycr_filemeta_t* meta = &unifycr_filemetas[fid];
        return meta;
    }
    return NULL;
}

/* ---------------------------------------
 * Operations on file storage
 * --------------------------------------- */

/* allocate and initialize data management resource for file */
static int unifycr_fid_store_alloc(int fid)
{
    /* get meta data for this file */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);

    /* indicate that we're using LOGIO to store data for this file */
    meta->storage = FILE_STORAGE_LOGIO;

    return UNIFYCR_SUCCESS;
}

/* free data management resource for file */
static int unifycr_fid_store_free(int fid)
{
    /* get meta data for this file */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);

    /* set storage type back to NULL */
    meta->storage = FILE_STORAGE_NULL;

    return UNIFYCR_SUCCESS;
}

/* ---------------------------------------
 * Operations on file ids
 * --------------------------------------- */

/* checks to see if fid is a directory
 * returns 1 for yes
 * returns 0 for no */
int unifycr_fid_is_dir(int fid)
{
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
    if (meta) {
        /* found a file with that id, return value of directory flag */
        int rc = meta->is_dir;
        return rc;
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
int unifycr_generate_gfid(const char* path)
{
    unsigned char digested[16] = { 0, };
    unsigned long len = strlen(path);
    int* ival = (int*) digested;

    MD5((const unsigned char*) path, len, digested);

    return abs(ival[0]);
}

static int unifycr_gfid_from_fid(const int fid)
{
    /* check that local file id is in range */
    if (fid < 0 || fid >= unifycr_max_files) {
        return -EINVAL;
    }

    /* lookup file name structure for this local file id */
    unifycr_filename_t* fname = &unifycr_filelist[fid];

    /* generate global file id from path if file is valid */
    if (fname->in_use) {
        int gfid = unifycr_generate_gfid(fname->filename);
        return gfid;
    }

    return -EINVAL;
}

/* checks to see if a directory is empty
 * assumes that check for is_dir has already been made
 * only checks for full path matches, does not check relative paths,
 * e.g. ../dirname will not work
 * returns 1 for yes it is empty
 * returns 0 for no */
int unifycr_fid_is_dir_empty(const char* path)
{
    int i = 0;
    while (i < unifycr_max_files) {
        /* only check this element if it's active */
        if (unifycr_filelist[i].in_use) {
            /* if the file starts with the path, it is inside of that directory
             * also check to make sure that it's not the directory entry itself */
            char* strptr = strstr(path, unifycr_filelist[i].filename);
            if (strptr == unifycr_filelist[i].filename &&
                strcmp(path, unifycr_filelist[i].filename) != 0) {
                /* found a child item in path */
                LOGDBG("File found: unifycr_filelist[%d].filename = %s",
                       i, (char*)&unifycr_filelist[i].filename);
                return 0;
            }
        }

        /* go on to next file */
        i++;
    }

    /* couldn't find any files with this prefix, dir must be empty */
    return 1;
}

/* return current size of given file id */
off_t unifycr_fid_size(int fid)
{
    /* get meta data for this file */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
    return meta->size;
}

static inline
void unifycr_init_file_attr(unifycr_file_attr_t* fattr, int isdir)
{
    if (fattr) {
        struct timespec tp = { 0, };

        clock_gettime(CLOCK_REALTIME, &tp);
        fattr->atime = tp;
        fattr->mtime = tp;
        fattr->ctime = tp;

        fattr->mode = isdir ? UNIFYCR_STAT_DEFAULT_DIR_MODE
                            : UNIFYCR_STAT_DEFAULT_FILE_MODE;

        fattr->uid = getuid();
        fattr->gid = getgid();
    }
}

/*
 * insert file attribute to attributed shared memory buffer,
 * keep entries ordered by file id
 */
static int ins_file_meta(unifycr_fattr_buf_t* ptr_f_meta_log,
                         unifycr_file_attr_t* ins_fattr)
{
    /* get pointer to start of stat structures in shared memory buffer */
    unifycr_file_attr_t* meta_entry = ptr_f_meta_log->meta_entry;

    /* get number of active entries currently in the buffer */
    int meta_cnt = *(ptr_f_meta_log->ptr_num_entries);

    /* TODO: Improve the search time */
    /* search backwards until we find an entry whose
     * file id is less than the current file id */
    int i;
    for (i = meta_cnt - 1; i >= 0; i--) {
        if (meta_entry[i].fid <= ins_fattr->fid) {
            /* sort in acsending order */
            break;
        }
    }

    /* compute position to store stat info for this file */
    int ins_pos = i + 1;

    /* we need to move some entries up a slot to make room
     * for this one */
    for (i = meta_cnt - 1; i >= ins_pos; i--) {
        meta_entry[i + 1] = meta_entry[i];
    }

    /* insert stat data for this file into buffer */
    meta_entry[ins_pos] = *ins_fattr;

    /* increment our count of active entries */
    (*ptr_f_meta_log->ptr_num_entries)++;

    return 0;
}

int unifycr_set_global_file_meta(const char* path, int fid, int gfid,
                                 int isdir)
{
    int ret = 0;
    unifycr_file_attr_t new_fmeta = { 0, };

    memset((void*) &new_fmeta, 0, sizeof(new_fmeta));

    sprintf(new_fmeta.filename, "%s", path);

    new_fmeta.fid = fid;
    new_fmeta.gfid = gfid;

    unifycr_init_file_attr(&new_fmeta, isdir);

    ret = invoke_client_metaset_rpc(&new_fmeta);
    if (ret < 0) {
        return ret;
    }

    ins_file_meta(&unifycr_fattrs, &new_fmeta);

    return 0;
}

int unifycr_get_global_file_meta(int fid, int gfid, unifycr_file_attr_t* gfattr)
{
    if (!gfattr) {
        return -EINVAL;
    }

    unifycr_file_attr_t fmeta;
    int ret = invoke_client_metaget_rpc(gfid, &fmeta);
    if (ret == UNIFYCR_SUCCESS) {
        *gfattr = fmeta;
         gfattr->fid = fid;
    }

    return ret;
}

/* fill in limited amount of stat information for global file id */
int unifycr_gfid_stat(int gfid, struct stat* buf)
{
    /* check that we have an output buffer to write to */
    if (!buf) {
        return UNIFYCR_ERROR_INVAL;
    }

    /* zero out user's stat buffer */
    memset(buf, 0, sizeof(struct stat));

    /* lookup stat data for global file id */
    unifycr_file_attr_t fattr;
    int ret = invoke_client_metaget_rpc(gfid, &fattr);
    if (ret != UNIFYCR_SUCCESS) {
        return ret;
    }

    /* execute rpc to get file size */
    size_t filesize;
    ret = invoke_client_filesize_rpc(app_id, local_rank_idx, gfid, &filesize);
    if (ret != UNIFYCR_SUCCESS) {
        return ret;
    }

    /* copy stat structure */
    unifycr_file_attr_to_stat(&fattr, buf);

    /* set the file size */
    buf->st_size = filesize;

    return UNIFYCR_SUCCESS;
}

/* fill in limited amount of stat information */
int unifycr_fid_stat(int fid, struct stat* buf)
{
    /* check that fid is defined */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
    if (meta == NULL) {
        return UNIFYCR_ERROR_IO;
    }

    /* get global file id corresponding to local file id */
    int gfid = unifycr_gfid_from_fid(fid);

    /* lookup stat info for global file id */
    int ret = unifycr_gfid_stat(gfid, buf);
    if (ret != UNIFYCR_SUCCESS) {
        return UNIFYCR_ERROR_IO;
    }

    return UNIFYCR_SUCCESS;
}

/* allocate a file id slot for a new file
 * return the fid or -1 on error */
int unifycr_fid_alloc()
{
    unifycr_stack_lock();
    int fid = unifycr_stack_pop(free_fid_stack);
    unifycr_stack_unlock();
    LOGDBG("unifycr_stack_pop() gave %d", fid);
    if (fid < 0) {
        /* need to create a new file, but we can't */
        LOGERR("unifycr_stack_pop() failed (%d)", fid);
        return -1;
    }
    return fid;
}

/* return the file id back to the free pool */
int unifycr_fid_free(int fid)
{
    unifycr_stack_lock();
    unifycr_stack_push(free_fid_stack, fid);
    unifycr_stack_unlock();
    return UNIFYCR_SUCCESS;
}

/* add a new file and initialize metadata
 * returns the new fid, or negative value on error */
int unifycr_fid_create_file(const char* path)
{
    int fid = unifycr_fid_alloc();
    if (fid < 0)  {
        /* was there an error? if so, return it */
        errno = ENOSPC;
        return fid;
    }

    /* mark this slot as in use and copy the filename */
    unifycr_filelist[fid].in_use = 1;

    /* TODO: check path length to see if it is < 128 bytes
     * and return appropriate error if it is greater
     */

    /* copy file name into slot */
    strcpy((void*)&unifycr_filelist[fid].filename, path);
    LOGDBG("Filename %s got unifycr fd %d",
           unifycr_filelist[fid].filename, fid);

    /* initialize meta data */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
    meta->size    = 0;
    meta->chunks  = 0;
    meta->is_dir  = 0;
    meta->log_size = 0;
    meta->storage = FILE_STORAGE_NULL;
    meta->needs_sync = 0;
    meta->flock_status = UNLOCKED;

    /* PTHREAD_PROCESS_SHARED allows Process-Shared Synchronization*/
    pthread_spin_init(&meta->fspinlock, PTHREAD_PROCESS_SHARED);

    return fid;
}

int unifycr_fid_create_directory(const char* path)
{
    int ret = 0;
    int fid = 0;
    int gfid = 0;
    int found_global = 0;
    int found_local = 0;
    size_t pathlen = strlen(path) + 1;
    struct stat sb = { 0, };
    unifycr_file_attr_t gfattr = { 0, };
    unifycr_filemeta_t* meta = NULL;

    if (pathlen > UNIFYCR_MAX_FILENAME) {
        return (int) UNIFYCR_ERROR_NAMETOOLONG;
    }

    fid = unifycr_get_fid_from_path(path);
    gfid = unifycr_generate_gfid(path);

    found_global =
        (unifycr_get_global_file_meta(fid, gfid, &gfattr) == UNIFYCR_SUCCESS);
    found_local = (fid >= 0);

    if (found_local && found_global) {
        return (int) UNIFYCR_ERROR_EXIST;
    }

    if (found_local && !found_global) {
        /* FIXME: so, we have detected the cache inconsistency here.
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
        return (int) UNIFYCR_ERROR_IO;
    }

    if (!found_local && found_global) {
        /* populate the local cache, then return EEXIST */

        return (int) UNIFYCR_ERROR_EXIST;
    }

    /* now, we need to create a new directory. */
    fid = unifycr_fid_create_file(path);
    if (fid < 0) {
        return (int) UNIFYCR_ERROR_IO;    /* FIXME: ENOSPC or EIO? */
    }

    meta = unifycr_get_meta_from_fid(fid);
    meta->is_dir = 1;

    ret = unifycr_set_global_file_meta(path, fid, gfid, 1);
    if (ret) {
        LOGERR("Failed to populate the global meta entry for %s (fid:%d)",
               path, fid);
        return (int) UNIFYCR_ERROR_IO;
    }

    return UNIFYCR_SUCCESS;
}

/* read count bytes from file starting from pos and store into buf,
 * all bytes are assumed to exist, so checks on file size should be
 * done before calling this routine */
int unifycr_fid_read(int fid, off_t pos, void* buf, size_t count)
{
    int rc;

    /* short-circuit a 0-byte read */
    if (count == 0) {
        return UNIFYCR_SUCCESS;
    }

    /* get meta for this file id */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);

    /* determine storage type to read file data */
    if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
        /* file stored in fixed-size chunks */
        rc = unifycr_fid_store_fixed_read(fid, meta, pos, buf, count);
    } else {
        /* unknown storage type */
        rc = (int)UNIFYCR_ERROR_IO;
    }

    return rc;
}

/* write count bytes from buf into file starting at offset pos,
 * all bytes are assumed to be allocated to file, so file should
 * be extended before calling this routine */
int unifycr_fid_write(int fid, off_t pos, const void* buf, size_t count)
{
    int rc;

    /* short-circuit a 0-byte write */
    if (count == 0) {
        return UNIFYCR_SUCCESS;
    }

    /* get meta for this file id */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);

    /* determine storage type to write file data */
    if (meta->storage == FILE_STORAGE_FIXED_CHUNK ||
        meta->storage == FILE_STORAGE_LOGIO) {
        /* file stored in fixed-size chunks */
        rc = unifycr_fid_store_fixed_write(fid, meta, pos, buf, count);
    } else {
        /* unknown storage type */
        rc = (int)UNIFYCR_ERROR_IO;
    }

    return rc;
}

/* given a file id, write zero bytes to region of specified offset
 * and length, assumes space is already reserved */
int unifycr_fid_write_zero(int fid, off_t pos, off_t count)
{
    int rc = UNIFYCR_SUCCESS;

    /* allocate an aligned chunk of memory */
    size_t buf_size = 1024 * 1024;
    void* buf = (void*) malloc(buf_size);
    if (buf == NULL) {
        return (int)UNIFYCR_ERROR_IO;
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
        int write_rc = unifycr_fid_write(fid, curpos, buf, num);
        if (write_rc != UNIFYCR_SUCCESS) {
            rc = (int)UNIFYCR_ERROR_IO;
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
int unifycr_fid_extend(int fid, off_t length)
{
    int rc;

    /* get meta data for this file */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);

    /* determine file storage type */
    if (meta->storage == FILE_STORAGE_FIXED_CHUNK ||
        meta->storage == FILE_STORAGE_LOGIO) {
        /* file stored in fixed-size chunks */
        rc = unifycr_fid_store_fixed_extend(fid, meta, length);
    } else {
        /* unknown storage type */
        rc = (int)UNIFYCR_ERROR_IO;
    }

    /* TODO: move this statement elsewhere */
    /* increase file size up to length */
    if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
        if (length > meta->size) {
            meta->size = length;
        }
    }

    return rc;
}

/* if length is less than reserved space, give back space down to length */
int unifycr_fid_shrink(int fid, off_t length)
{
    int rc;

    /* get meta data for this file */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);

    /* determine file storage type */
    if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
        /* file stored in fixed-size chunks */
        rc = unifycr_fid_store_fixed_shrink(fid, meta, length);
    } else {
        /* unknown storage type */
        rc = (int)UNIFYCR_ERROR_IO;
    }

    return rc;
}

/* truncate file id to given length, frees resources if length is
 * less than size and allocates and zero-fills new bytes if length
 * is more than size */
int unifycr_fid_truncate(int fid, off_t length)
{
    /* get meta data for this file */
    unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);

    /* get current size of file */
    off_t size = meta->size;

    /* drop data if length is less than current size,
     * allocate new space and zero fill it if bigger */
    if (length < size) {
        /* determine the number of chunks to leave after truncating */
        int shrink_rc = unifycr_fid_shrink(fid, length);
        if (shrink_rc != UNIFYCR_SUCCESS) {
            return shrink_rc;
        }
    } else if (length > size) {
        /* file size has been extended, allocate space */
        int extend_rc = unifycr_fid_extend(fid, length);
        if (extend_rc != UNIFYCR_SUCCESS) {
            return (int)UNIFYCR_ERROR_NOSPC;
        }

        /* write zero values to new bytes */
        off_t gap_size = length - size;
        int zero_rc = unifycr_fid_write_zero(fid, size, gap_size);
        if (zero_rc != UNIFYCR_SUCCESS) {
            return (int)UNIFYCR_ERROR_IO;
        }
    }

    /* set the new size */
    meta->size = length;

    return UNIFYCR_SUCCESS;
}

/*
 * hash a path to gfid
 * @param path: file path
 * return: error code, gfid
 * */
static int unifycr_get_global_fid(const char* path, int* gfid)
{
    MD5_CTX ctx;

    unsigned char md[16];
    memset(md, 0, 16);

    MD5_Init(&ctx);
    MD5_Update(&ctx, path, strlen(path));
    MD5_Final(md, &ctx);

    *gfid = *((int*)md);
    return UNIFYCR_SUCCESS;
}

/* opens a new file id with specified path, access flags, and permissions,
 * fills outfid with file id and outpos with position for current file pointer,
 * returns UNIFYCR error code
 */
int unifycr_fid_open(const char* path, int flags, mode_t mode, int* outfid,
                     off_t* outpos)
{
    /* check that path is short enough */
    int ret = 0;
    size_t pathlen = strlen(path) + 1;
    int fid = 0;
    int gfid = -1;
    int found_global = 0;
    int found_local = 0;
    off_t pos = 0;      /* set the pointer to the start of the file */
    unifycr_file_attr_t gfattr = { 0, };

    if (pathlen > UNIFYCR_MAX_FILENAME) {
        return (int) UNIFYCR_ERROR_NAMETOOLONG;
    }

    /* check whether this file already exists */
    /*
     * TODO: The test of file existence involves both local and global checks.
     * However, the testing below does not seem to cover all cases. For
     * instance, a globally unlinked file might be still cached locally because
     * the broadcast for cache invalidation has not been implemented, yet.
     */

    gfid = unifycr_generate_gfid(path);
    fid = unifycr_get_fid_from_path(path);

    LOGDBG("unifycr_get_fid_from_path() gave %d (gfid = %d)", fid, gfid);

    found_global =
        (unifycr_get_global_file_meta(fid, gfid, &gfattr) == UNIFYCR_SUCCESS);
    found_local = (fid >= 0);

    /* possibly, the file still exists in our local cache but globally
     * unlinked. Invalidate the entry
     *
     * FIXME: unifycr_fid_unlink() always returns success.
     */
    if (found_local && !found_global) {
        LOGDBG("file found locally, but seems to be deleted globally. "
               "invalidating the local cache.");

        unifycr_fid_unlink(fid);

        return (int) UNIFYCR_ERROR_NOENT;
    }

    /* for all other three cases below, we need to open the file and allocate a
     * file descriptor for the client.
     */

    if (!found_local && found_global) {
        /* file has possibly been created by another process.  We need to
         * create a local meta cache and also initialize the local storage
         * space.
         */
        unifycr_filemeta_t* meta = NULL;

        fid = unifycr_fid_create_file(path);
        if (fid < 0) {
            LOGERR("failed to create a new file %s", path);

            /* FIXME: UNIFYCR_ERROR_NFILE or UNIFYCR_ERROR_IO ? */
            return (int) UNIFYCR_ERROR_IO;
        }

        ret = unifycr_fid_store_alloc(fid);
        if (ret != UNIFYCR_SUCCESS) {
            LOGERR("failed to allocate storage space for file %s (fid=%d)",
                   path, fid);
            return (int) UNIFYCR_ERROR_IO;
        }

        meta = unifycr_get_meta_from_fid(fid);

        meta->size = gfattr.size;
        gfattr.fid = fid;
        gfattr.gfid = gfid;

        ins_file_meta(&unifycr_fattrs, &gfattr);
    } else if (found_local && found_global) {
        /* file exists and is valid.  */
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            return (int)UNIFYCR_ERROR_EXIST;
        }

        if ((flags & O_DIRECTORY) && !unifycr_fid_is_dir(fid)) {
            return (int)UNIFYCR_ERROR_NOTDIR;
        }

        if (!(flags & O_DIRECTORY) && unifycr_fid_is_dir(fid)) {
            return (int)UNIFYCR_ERROR_NOTDIR;
        }

        if ((flags & O_TRUNC) && (flags & (O_RDWR | O_WRONLY))) {
            unifycr_fid_truncate(fid, 0);
        }

        if (flags & O_APPEND) {
            unifycr_filemeta_t* meta = unifycr_get_meta_from_fid(fid);
            pos = meta->size;
        }
    } else {
        /* !found_local && !found_global
         * If we reach here, we need to create a brand new file.
         */
        struct stat sb = { 0, };

        if (!(flags & O_CREAT)) {
            LOGERR("%s does not exist (O_CREAT not given).", path);
            return (int) UNIFYCR_ERROR_NOENT;
        }

        LOGDBG("Creating a new entry for %s.", path);
        LOGDBG("shm_super_buf = %p; free_fid_stack = %p; "
               "free_chunk_stack = %p; unifycr_filelist = %p; "
               "chunks = %p", shm_super_buf, free_fid_stack,
               free_chunk_stack, unifycr_filelist, unifycr_chunks);

        /* allocate a file id slot for this new file */
        fid = unifycr_fid_create_file(path);
        if (fid < 0) {
            LOGERR("Failed to create new file %s", path);
            return (int) UNIFYCR_ERROR_NFILE;
        }

        /* initialize the storage for the file */
        int store_rc = unifycr_fid_store_alloc(fid);
        if (store_rc != UNIFYCR_SUCCESS) {
            LOGERR("Failed to create storage for file %s", path);
            return (int) UNIFYCR_ERROR_IO;
        }

        /*create a file and send its attribute to key-value store*/
        ret = unifycr_set_global_file_meta(path, fid, gfid, 0);
        if (ret) {
            LOGERR("Failed to populate the global meta entry for %s (fid:%d)",
                   path, fid);
            return (int) UNIFYCR_ERROR_IO;
        }
    }

    /* TODO: allocate a free file descriptor and associate it with fid set
     * in_use flag and file pointer
     */
    *outfid = fid;
    *outpos = pos;

    LOGDBG("UNIFYCR_open generated fd %d for file %s", fid, path);

    return UNIFYCR_SUCCESS;
}

int unifycr_fid_close(int fid)
{
    /* TODO: clear any held locks */

    /* nothing to do here, just a place holder */
    return UNIFYCR_SUCCESS;
}

/* delete a file id and return file its resources to free pools */
int unifycr_fid_unlink(int fid)
{
    /* return data to free pools */
    int rc = unifycr_fid_truncate(fid, 0);
    if (rc != UNIFYCR_SUCCESS) {
        /* failed to release storage for the file,
         * so bail out to keep its file id active */
        return rc;
    }

    /* finalize the storage we're using for this file */
    rc = unifycr_fid_store_free(fid);
    if (rc != UNIFYCR_SUCCESS) {
        /* released strorage for file, but failed to release
         * structures tracking storage, again bail out to keep
         * its file id active */
        return rc;
    }

    /* at this point, we have released all storage for the file,
     * and data structures that track its storage, so we can
     * release the file id itself */

    /* set this file id as not in use */
    unifycr_filelist[fid].in_use = 0;

    /* add this id back to the free stack */
    rc = unifycr_fid_free(fid);
    if (rc != UNIFYCR_SUCCESS) {
        /* storage for the file was released, but we hit
         * an error while freeing the file id */
        return rc;
    }

    return UNIFYCR_SUCCESS;
}

/* ---------------------------------------
 * Operations to mount file system
 * --------------------------------------- */

/* The super block is a region of shared memory that is used to
 * persist file system data.  It contains both room for data
 * structures used to track file names, meta data, the list of
 * storage blocks used for each file, and optional blocks.
 * It also contains a fixed-size region for keeping log
 * index entries and stat info for each file.
 *
 *  - stack of free local file ids of length max_files,
 *    the local file id is used to index into other data
 *    structures
 *
 *  - array of unifycr_filename structs, indexed by local
 *    file id, provides a field indicating whether file
 *    slot is in use and if so, the current file name
 *
 *  - array of unifycr_filemeta structs, indexed by local
 *    file id, records list of storage blocks used to
 *    store data for the file
 *
 *  - array of unifycr_chunkmeta structs, indexed by local
 *    file id and then by chunk id for recording metadata
 *    of each chunk allocated to a file, including host
 *    storage and id of that chunk within its storage
 *
 *  - stack to track free list of memory chunks
 *
 *  - stack to track free list of spillover chunks
 *
 *  - array of storage chunks of length unifycr_max_chunks,
 *    if storing data in memory
 *
 *  - count of number of active index entries
 *  - array of index metadata to track physical offset
 *    of logical file data, of length unifycr_max_index_entries,
 *    entries added during write operations
 *
 *  - count of number of active file metadata entries
 *  - array of file metadata to track stat info for each
 *    file, of length unifycr_max_fattr_entries, filed
 *    in by client and read by server to record file meta
 *    data
 */

/* compute memory size of superblock in bytes,
 * critical to keep this consistent with
 * unifycr_init_pointers */
static size_t unifycr_superblock_size(void)
{
    size_t sb_size = 0;

    /* header: uint32_t to hold magic number to indicate
     * that superblock is initialized */
    sb_size += sizeof(uint32_t);

    /* free file id stack */
    sb_size += unifycr_stack_bytes(unifycr_max_files);

    /* file name struct array */
    sb_size += unifycr_max_files * sizeof(unifycr_filename_t);

    /* file metadata struct array */
    sb_size += unifycr_max_files * sizeof(unifycr_filemeta_t);

    if (unifycr_use_memfs) {
        /* memory chunk metadata struct array for each file,
         * enables a file to use all space in memory */
        sb_size += unifycr_max_files * unifycr_max_chunks *
                   sizeof(unifycr_chunkmeta_t);
    }
    if (unifycr_use_spillover) {
        /* spillover chunk metadata struct array for each file,
         * enables a file to use all space in spillover file */
        sb_size += unifycr_max_files * unifycr_spillover_max_chunks *
                   sizeof(unifycr_chunkmeta_t);
    }

    /* free chunk stack */
    if (unifycr_use_memfs) {
        sb_size += unifycr_stack_bytes(unifycr_max_chunks);
    }
    if (unifycr_use_spillover) {
        sb_size += unifycr_stack_bytes(unifycr_spillover_max_chunks);
    }

    /* space for memory chunks */
    if (unifycr_use_memfs) {
        sb_size += unifycr_page_size;
        sb_size += unifycr_max_chunks * unifycr_chunk_size;
    }

    /* index region size */
    sb_size += unifycr_page_size;
    sb_size += unifycr_max_index_entries * sizeof(unifycr_index_t);

    /* attribute region size */
    sb_size += unifycr_page_size;
    sb_size += unifycr_max_fattr_entries * sizeof(unifycr_file_attr_t);

    /* return number of bytes */
    return sb_size;
}

static inline
char* next_page_align(char* ptr)
{
    intptr_t orig = (intptr_t) ptr;
    intptr_t aligned = orig;
    intptr_t offset = orig % unifycr_page_size;
    if (offset) {
        aligned += (unifycr_page_size - offset);
    }
    LOGDBG("orig=0x%p, next-page-aligned=0x%p", ptr, (char*)aligned);
    return (char*) aligned;
}

/* initialize our global pointers into the given superblock */
static void* unifycr_init_pointers(void* superblock)
{
    char* ptr = (char*)superblock;

    /* jump over header (right now just a uint32_t to record
     * magic value of 0xdeadbeef if initialized */
    ptr += sizeof(uint32_t);

    /* stack to manage free file ids */
    free_fid_stack = ptr;
    ptr += unifycr_stack_bytes(unifycr_max_files);

    /* record list of file names */
    unifycr_filelist = (unifycr_filename_t*)ptr;
    ptr += unifycr_max_files * sizeof(unifycr_filename_t);

    /* array of file meta data structures */
    unifycr_filemetas = (unifycr_filemeta_t*)ptr;
    ptr += unifycr_max_files * sizeof(unifycr_filemeta_t);

    /* array of chunk meta data strucutres for each file */
    unifycr_chunkmetas = (unifycr_chunkmeta_t*)ptr;
    if (unifycr_use_memfs) {
        ptr += unifycr_max_files * unifycr_max_chunks *
               sizeof(unifycr_chunkmeta_t);
    }
    if (unifycr_use_spillover) {
        ptr += unifycr_max_files * unifycr_spillover_max_chunks *
               sizeof(unifycr_chunkmeta_t);
    }

    /* stack to manage free memory data chunks */
    if (unifycr_use_memfs) {
        free_chunk_stack = ptr;
        ptr += unifycr_stack_bytes(unifycr_max_chunks);
    }
    if (unifycr_use_spillover) {
        free_spillchunk_stack = ptr;
        ptr += unifycr_stack_bytes(unifycr_spillover_max_chunks);
    }

    /* Only set this up if we're using memfs */
    if (unifycr_use_memfs) {
        /* pointer to start of memory data chunks */
        ptr = next_page_align(ptr);
        unifycr_chunks = ptr;
        ptr += unifycr_max_chunks * unifycr_chunk_size;
    } else {
        unifycr_chunks = NULL;
    }

    /* record pointer to number of index entries */
    unifycr_indices.ptr_num_entries = (size_t*)ptr;

    /* pointer to array of index entries */
    ptr += unifycr_page_size;
    unifycr_indices.index_entry = (unifycr_index_t*)ptr;
    ptr += unifycr_max_index_entries * sizeof(unifycr_index_t);

    /* pointer to number of file metadata entries */
    unifycr_fattrs.ptr_num_entries = (size_t*)ptr;

    /* pointer to array of file metadata entries */
    ptr += unifycr_page_size;
    unifycr_fattrs.meta_entry = (unifycr_file_attr_t*)ptr;
    ptr += unifycr_max_fattr_entries * sizeof(unifycr_file_attr_t);

    /* compute size of memory we're using and check that
     * it matches what we allocated */
    size_t ptr_size = (size_t)(ptr - (char*)superblock);
    if (ptr_size > shm_super_size) {
        LOGERR("Data structures in superblock extend beyond its size");
    }

    return ptr;
}

/* initialize data structures for first use */
static int unifycr_init_structures()
{
    /* compute total number of storage chunks available */
    int numchunks = 0;
    if (unifycr_use_memfs) {
        numchunks += unifycr_max_chunks;
    }
    if (unifycr_use_spillover) {
        numchunks += unifycr_spillover_max_chunks;
    }

    int i;
    for (i = 0; i < unifycr_max_files; i++) {
        /* indicate that file id is not in use by setting flag to 0 */
        unifycr_filelist[i].in_use = 0;

        /* set pointer to array of chunkmeta data structures */
        unifycr_filemeta_t* filemeta = &unifycr_filemetas[i];

        /* compute offset to start of chunk meta list for this file */
        filemeta->chunkmeta_idx = numchunks * i;
    }

    /* initialize stack of free file ids */
    unifycr_stack_init(free_fid_stack, unifycr_max_files);

    /* initialize list of free memory chunks */
    if (unifycr_use_memfs) {
        unifycr_stack_init(free_chunk_stack, unifycr_max_chunks);
    }

    /* initialize list of free spillover chunks */
    if (unifycr_use_spillover) {
        unifycr_stack_init(free_spillchunk_stack, unifycr_spillover_max_chunks);
    }

    /* initialize count of key/value entries */
    *(unifycr_indices.ptr_num_entries) = 0;

    /* initialize count of file stat structures */
    *(unifycr_fattrs.ptr_num_entries) = 0;

    LOGDBG("Meta-stacks initialized!");

    return UNIFYCR_SUCCESS;
}

static int unifycr_get_spillblock(size_t size, const char* path)
{
    //MAP_OR_FAIL(open);
    mode_t perms = unifycr_getmode(0);
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
static void* unifycr_superblock_shmget(size_t size, key_t key)
{
    /* define name for superblock shared memory region */
    snprintf(shm_super_name, sizeof(shm_super_name), "%d-super-%d",
             app_id, key);
    LOGDBG("Key for superblock = %x", key);

    /* open shared memory file */
    void* addr = unifycr_shm_alloc(shm_super_name, size);
    if (addr == NULL) {
        LOGERR("Failed to create superblock");
        return NULL;
    }

    /* init our global variables to point to spots in superblock */
    unifycr_init_pointers(addr);

    /* initialize structures in superblock if it's newly allocated,
     * we depend on shm_open setting all bytes to 0 to know that
     * it is not initialized */
    int32_t initialized = *(int32_t*)addr;
    if (initialized == 0) {
        /* not yet initialized, so initialize values within superblock */
        unifycr_init_structures();

        /* superblock structure has been initialized,
         * so set flag to indicate that fact */
        *(int32_t*)addr = 0xDEADBEEF;
    }

    /* return starting memory address of super block */
    return addr;
}

static int unifycr_init(int rank)
{
    int rc;
    int i;
    bool b;
    long l;
    unsigned long long bits;
    char* cfgval;

    if (!unifycr_initialized) {
        /* unifycr debug level default is zero */
        unifycr_log_level = 0;
        cfgval = client_cfg.log_verbosity;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifycr_log_level = (int)l;
            }
        }

#ifdef UNIFYCR_GOTCHA
        /* insert our I/O wrappers using gotcha */
        enum gotcha_error_t result;
        result = gotcha_wrap(wrap_unifycr_list, GOTCHA_NFUNCS, "unifycr");
        if (result != GOTCHA_SUCCESS) {
            LOGERR("gotcha_wrap returned %d", (int) result);
        }

        /* check for an errors when registering functions with gotcha */
        for (i = 0; i < GOTCHA_NFUNCS; i++) {
            if (*(void**)(wrap_unifycr_list[i].function_address_pointer) == 0) {
                LOGERR("This function name failed to be wrapped: %s",
                       wrap_unifycr_list[i].name);
            }
        }
#endif

        /* as a hack to support fgetpos/fsetpos, we store the value of
         * a void* in an fpos_t so check that there's room and at least
         * print a message if this won't work */
        if (sizeof(fpos_t) < sizeof(void*)) {
            LOGERR("fgetpos/fsetpos will not work correctly");
            unifycr_fpos_enabled = 0;
        }

        /* look up page size for buffer alignment */
        unifycr_page_size = getpagesize();

        /* compute min and max off_t values */
        bits = sizeof(off_t) * 8;
        unifycr_max_offt = (off_t)((1ULL << (bits - 1ULL)) - 1ULL);
        unifycr_min_offt = (off_t)(-(1ULL << (bits - 1ULL)));

        /* compute min and max long values */
        unifycr_max_long = LONG_MAX;
        unifycr_min_long = LONG_MIN;

        /* will we use spillover to store the files? */
        unifycr_use_spillover = 1;
        cfgval = client_cfg.spillover_enabled;
        if (cfgval != NULL) {
            rc = configurator_bool_val(cfgval, &b);
            if ((rc == 0) && !b) {
                unifycr_use_spillover = 0;
            }
        }
        LOGDBG("are we using spillover? %d", unifycr_use_spillover);

        /* determine maximum number of bytes of spillover for chunk storage */
        unifycr_spillover_size = UNIFYCR_SPILLOVER_SIZE;
        cfgval = client_cfg.spillover_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifycr_spillover_size = (size_t)l;
            }
        }

        /* determine max number of files to store in file system */
        unifycr_max_files = UNIFYCR_MAX_FILES;
        cfgval = client_cfg.client_max_files;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifycr_max_files = (int)l;
            }
        }

        /* determine number of bits for chunk size */
        unifycr_chunk_bits = UNIFYCR_CHUNK_BITS;
        cfgval = client_cfg.shmem_chunk_bits;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifycr_chunk_bits = (int)l;
            }
        }

        /* determine maximum number of bytes of memory for chunk storage */
        unifycr_chunk_mem = UNIFYCR_CHUNK_MEM;
        cfgval = client_cfg.shmem_chunk_mem;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifycr_chunk_mem = (size_t)l;
            }
        }

        /* set chunk size, set chunk offset mask, and set total number
         * of chunks */
        unifycr_chunk_size = 1 << unifycr_chunk_bits;
        unifycr_chunk_mask = unifycr_chunk_size - 1;
        unifycr_max_chunks = unifycr_chunk_mem >> unifycr_chunk_bits;

        /* set number of chunks in spillover device */
        unifycr_spillover_max_chunks = unifycr_spillover_size >> unifycr_chunk_bits;

        /* define size of buffer used to cache key/value pairs for
         * data offsets before passing them to the server */
        unifycr_index_buf_size = UNIFYCR_INDEX_BUF_SIZE;
        cfgval = client_cfg.logfs_index_buf_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifycr_index_buf_size = (size_t)l;
            }
        }
        unifycr_max_index_entries =
            unifycr_index_buf_size / sizeof(unifycr_index_t);

        /* define size of buffer used to cache stat structures
         * for files we create before passing this info
         * to the server */
        unifycr_fattr_buf_size = UNIFYCR_FATTR_BUF_SIZE;
        cfgval = client_cfg.logfs_attr_buf_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0) {
                unifycr_fattr_buf_size = (size_t)l;
            }
        }
        unifycr_max_fattr_entries =
            unifycr_fattr_buf_size / sizeof(unifycr_file_attr_t);

        /* if we're using NUMA, process some configuration settings */
#ifdef HAVE_LIBNUMA
        char* env = getenv("UNIFYCR_NUMA_POLICY");
        if (env) {
            sprintf(unifycr_numa_policy, env);
            LOGDBG("NUMA policy used: %s", unifycr_numa_policy);
        } else {
            sprintf(unifycr_numa_policy, "default");
        }

        env = getenv("UNIFYCR_USE_NUMA_BANK");
        if (env) {
            int val = atoi(env);
            if (val >= 0) {
                unifycr_numa_bank = val;
            } else {
                LOGERR("Incorrect NUMA bank specified in UNIFYCR_USE_NUMA_BANK."
                       " Proceeding with default allocation policy.");
            }
        }
#endif

        /* record the max fd for the system */
        /* RLIMIT_NOFILE specifies a value one greater than the maximum
         * file descriptor number that can be opened by this process */
        struct rlimit r_limit;

        if (getrlimit(RLIMIT_NOFILE, &r_limit) < 0) {
            LOGERR("getrlimit failed: errno=%d (%s)", errno, strerror(errno));
            return UNIFYCR_FAILURE;
        }
        unifycr_fd_limit = r_limit.rlim_cur;
        LOGDBG("FD limit for system = %ld", unifycr_fd_limit);

        /* initialize file descriptor structures */
        int num_fds = UNIFYCR_MAX_FILEDESCS;
        for (i = 0; i < num_fds; i++) {
            unifycr_fd_init(i);
        }

        /* initialize file stream structures */
        int num_streams = UNIFYCR_MAX_FILEDESCS;
        for (i = 0; i < num_streams; i++) {
            unifycr_stream_init(i);
        }

        /* initialize directory stream structures */
        int num_dirstreams = UNIFYCR_MAX_FILEDESCS;
        for (i = 0; i < num_dirstreams; i++) {
            unifycr_dirstream_init(i);
        }

        /* initialize stack of free fd values */
        size_t free_fd_size = unifycr_stack_bytes(num_fds);
        unifycr_fd_stack = malloc(free_fd_size);
        unifycr_stack_init(unifycr_fd_stack, num_fds);

        /* initialize stack of free stream values */
        size_t free_stream_size = unifycr_stack_bytes(num_streams);
        unifycr_stream_stack = malloc(free_stream_size);
        unifycr_stack_init(unifycr_stream_stack, num_streams);

        /* initialize stack of free directory stream values */
        size_t free_dirstream_size = unifycr_stack_bytes(num_dirstreams);
        unifycr_dirstream_stack = malloc(free_dirstream_size);
        unifycr_stack_init(unifycr_dirstream_stack, num_dirstreams);

        /* determine the size of the superblock */
        shm_super_size = unifycr_superblock_size();

        /* get a superblock of shared memory and initialize our
         * global variables for this block */
        shm_super_buf = unifycr_superblock_shmget(
                            shm_super_size, unifycr_mount_shmget_key);
        if (shm_super_buf == NULL) {
            LOGERR("unifycr_superblock_shmget() failed");
            return UNIFYCR_FAILURE;
        }

        /* initialize spillover store */
        if (unifycr_use_spillover) {
            /* get directory in which to create spill over files */
            cfgval = client_cfg.spillover_data_dir;
            if (cfgval != NULL) {
                strncpy(external_data_dir, cfgval, sizeof(external_data_dir));
            } else {
                LOGERR("UNIFYCR_SPILLOVER_DATA_DIR not set, must be an existing"
                       " writable path (e.g., /mnt/ssd):");
                return UNIFYCR_FAILURE;
            }

            /* define path to the spill over file for data chunks */
            char spillfile_prefix[UNIFYCR_MAX_FILENAME];
            snprintf(spillfile_prefix, sizeof(spillfile_prefix),
                     "%s/spill_%d_%d.log",
                     external_data_dir, app_id, local_rank_idx);

            /* create the spill over file */
            unifycr_spilloverblock =
                unifycr_get_spillblock(unifycr_spillover_size,
                                       spillfile_prefix);
            if (unifycr_spilloverblock < 0) {
                LOGERR("unifycr_get_spillblock() failed!");
                return UNIFYCR_FAILURE;
            }

            /* get directory in which to create spill over files
             * for key/value pairs */
            cfgval = client_cfg.spillover_meta_dir;
            if (cfgval != NULL) {
                strncpy(external_meta_dir, cfgval, sizeof(external_meta_dir));
            } else {
                LOGERR("UNIFYCR_SPILLOVER_META_DIR not set, must be an existing"
                       " writable path (e.g., /mnt/ssd):");
                return UNIFYCR_FAILURE;
            }

            /* define path to the spill over file for key/value pairs */
            snprintf(spillfile_prefix, sizeof(spillfile_prefix),
                     "%s/spill_index_%d_%d.log",
                     external_meta_dir, app_id, local_rank_idx);

            /* create the spill over file for key value data */
            unifycr_spillmetablock =
                unifycr_get_spillblock(unifycr_index_buf_size,
                                       spillfile_prefix);
            if (unifycr_spillmetablock < 0) {
                LOGERR("unifycr_get_spillmetablock failed!");
                return UNIFYCR_FAILURE;
            }
        }

        /* remember that we've now initialized the library */
        unifycr_initialized = 1;
    }

    return UNIFYCR_SUCCESS;
}

/* ---------------------------------------
 * APIs exposed to external libraries
 * --------------------------------------- */

/* Fill mount rpc input struct with client-side context info */
void fill_client_mount_info(unifycr_mount_in_t* in)
{
    size_t meta_offset = (char*)unifycr_indices.ptr_num_entries -
                         (char*)shm_super_buf;
    size_t meta_size   = unifycr_max_index_entries
                         * sizeof(unifycr_index_t);

    size_t fmeta_offset = (char*)unifycr_fattrs.ptr_num_entries -
                          (char*)shm_super_buf;
    size_t fmeta_size   = unifycr_max_fattr_entries
                          * sizeof(unifycr_file_attr_t);

    size_t data_offset = (char*)unifycr_chunks - (char*)shm_super_buf;
    size_t data_size   = (size_t)unifycr_max_chunks * unifycr_chunk_size;

    in->app_id             = app_id;
    in->local_rank_idx     = local_rank_idx;
    in->dbg_rank           = glb_rank;
    in->num_procs_per_node = local_rank_cnt;
    in->req_buf_sz         = shm_req_size;
    in->recv_buf_sz        = shm_recv_size;
    in->superblock_sz      = shm_super_size;
    in->meta_offset        = meta_offset;
    in->meta_size          = meta_size;
    in->fmeta_offset       = fmeta_offset;
    in->fmeta_size         = fmeta_size;
    in->data_offset        = data_offset;
    in->data_size          = data_size;
    in->external_spill_dir = strdup(external_data_dir);
}

/**
 * Initialize the shared recv memory buffer to receive data from the delegators
 */
static int unifycr_init_recv_shm(int local_rank_idx, int app_id)
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
    shm_recv_buf = unifycr_shm_alloc(shm_recv_name, shm_recv_size);
    if (shm_recv_buf == NULL) {
        LOGERR("Failed to create buffer for read replies");
        return UNIFYCR_FAILURE;
    }

    /* what is this for? */
    *((int*)shm_recv_buf) = app_id + 3;

    return UNIFYCR_SUCCESS;
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
static int unifycr_init_req_shm(int local_rank_idx, int app_id)
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
    shm_req_buf = unifycr_shm_alloc(shm_req_name, shm_req_size);
    if (shm_req_buf == NULL) {
        LOGERR("Failed to create buffer for read requests");
        return UNIFYCR_FAILURE;
    }

    return UNIFYCR_SUCCESS;
}


#ifdef USE_DOMAIN_SOCKET
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
static int unifycr_init_socket(int proc_id, int l_num_procs_per_node,
                               int l_num_del_per_node)
{
    int rc = -1;
    int nprocs_per_del;
    int len;
    int result;
    int flag;
    struct sockaddr_un serv_addr;
    char tmp_path[UNIFYCR_MAX_FILENAME] = {0};
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
    if (unifycr_keyval_lookup_local(key_unifycrd_socket, &pmi_path) == 0) {
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
#endif // USE_DOMAIN_SOCKET

int compare_fattr(const void* a, const void* b)
{
    const unifycr_file_attr_t* ptr_a = a;
    const unifycr_file_attr_t* ptr_b = b;

    if (ptr_a->fid > ptr_b->fid) {
        return 1;
    }

    if (ptr_a->fid < ptr_b->fid) {
        return -1;
    }

    return 0;
}

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
 * find the local index of a given rank among all ranks
 * collocated on the same node
 * @param local_rank_lst: a list of local ranks
 * @param local_rank_cnt: number of local ranks
 * @return index of rank in local_rank_lst
 */
static int find_rank_idx(int rank, int* local_rank_lst, int local_rank_cnt)
{
    int i;

    for (i = 0; i < local_rank_cnt; i++) {
        if (local_rank_lst[i] == rank) {
            return i;
        }
    }

    return -1;
}

/**
 * calculate the number of ranks per node,
 *
 * @param numTasks: number of tasks in the application
 * @return success/error code
 * @return local_rank_lst: a list of local ranks
 * @return local_rank_cnt: number of local ranks
 */
static int CountTasksPerNode(int rank, int numTasks)
{
    char hostname[HOST_NAME_MAX];
    char localhost[HOST_NAME_MAX];
    int resultsLen = 30;
    MPI_Status status;
    int rc;

    rc = MPI_Get_processor_name(localhost, &resultsLen);
    if (rc != 0) {
        LOGERR("failed to get the processor's name");
    }

    if (numTasks > 0) {
        if (rank == 0) {
            int i;
            /* a container of (rank, host) mappings*/
            name_rank_pair_t* host_set =
                (name_rank_pair_t*)malloc(numTasks
                                          * sizeof(name_rank_pair_t));
            /*
             * MPI_receive all hostnames, and compare to local hostname
             * TODO: handle the case when the length of hostname is larger
             * than 30
             */
            for (i = 1; i < numTasks; i++) {
                rc = MPI_Recv(hostname, HOST_NAME_MAX,
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
            strcpy(host_set[0].hostname, localhost);
            host_set[0].rank = 0;

            /*sort according to the hostname*/
            qsort(host_set, numTasks, sizeof(name_rank_pair_t),
                  compare_name_rank_pair);

            /*
             * rank_cnt: records the number of processes on each node
             * rank_set: the list of ranks for each node
             */
            int** rank_set = (int**)malloc(numTasks * sizeof(int*));
            int* rank_cnt = (int*)malloc(numTasks * sizeof(int));
            int cursor = 0, set_counter = 0;

            for (i = 1; i < numTasks; i++) {
                if (strcmp(host_set[i].hostname,
                           host_set[i - 1].hostname) == 0) {
                    /*do nothing*/
                } else {
                    // find a different rank, so switch to a new set
                    int j, k = 0;

                    rank_set[set_counter] =
                        (int*)malloc((i - cursor) * sizeof(int));
                    rank_cnt[set_counter] = i - cursor;
                    for (j = cursor; j <= i - 1; j++) {

                        rank_set[set_counter][k] =  host_set[j].rank;
                        k++;
                    }

                    set_counter++;
                    cursor = i;
                }

            }

            /* fill rank_cnt and rank_set entry for the last node */
            int j = 0;

            rank_set[set_counter] = malloc((i - cursor) * sizeof(int));
            rank_cnt[set_counter] = numTasks - cursor;
            for (i = cursor; i <= numTasks - 1; i++) {
                rank_set[set_counter][j] = host_set[i].rank;
                j++;
            }
            set_counter++;

            /* broadcast the rank_cnt and rank_set information to each rank */
            int root_set_no = -1;

            for (i = 0; i < set_counter; i++) {
                for (j = 0; j < rank_cnt[i]; j++) {
                    if (rank_set[i][j] != 0) {
                        rc = MPI_Send(&rank_cnt[i], 1, MPI_INT, rank_set[i][j],
                                      0, MPI_COMM_WORLD);
                        if (rc != 0) {
                            LOGERR("cannot send local rank cnt");
                            return -1;
                        }

                        /*send the local rank set to the corresponding rank*/
                        rc = MPI_Send(rank_set[i], rank_cnt[i], MPI_INT,
                                      rank_set[i][j], 0, MPI_COMM_WORLD);
                        if (rc != 0) {
                            LOGERR("cannot send local rank list");
                            return -1;
                        }
                    } else {
                        root_set_no = i;
                    }
                }
            }


            /* root process set its own local rank set and rank_cnt*/
            if (root_set_no >= 0) {
                local_rank_lst = malloc(rank_cnt[root_set_no] * sizeof(int));
                for (i = 0; i < rank_cnt[root_set_no]; i++) {
                    local_rank_lst[i] = rank_set[root_set_no][i];
                }

                local_rank_cnt = rank_cnt[root_set_no];
            }

            for (i = 0; i < set_counter; i++) {
                free(rank_set[i]);
            }

            free(rank_cnt);
            free(host_set);
            free(rank_set);
        } else {
            /*
             * non-root process performs MPI_send to send hostname to root node
             */
            rc = MPI_Send(localhost, HOST_NAME_MAX, MPI_CHAR,
                          0, 0, MPI_COMM_WORLD);
            if (rc != 0) {
                LOGERR("cannot send host name");
                return -1;
            }
            /*receive the local rank count */
            rc = MPI_Recv(&local_rank_cnt, 1, MPI_INT,
                          0, 0, MPI_COMM_WORLD, &status);
            if (rc != 0) {
                LOGERR("cannot receive local rank cnt");
                return -1;
            }

            /* receive the the local rank list */
            local_rank_lst = (int*)malloc(local_rank_cnt * sizeof(int));
            rc = MPI_Recv(local_rank_lst, local_rank_cnt, MPI_INT,
                          0, 0, MPI_COMM_WORLD, &status);
            if (rc != 0) {
                free(local_rank_lst);
                LOGERR("cannot receive local rank list");
                return -1;
            }

        }

        qsort(local_rank_lst, local_rank_cnt, sizeof(int),
              compare_int);

        // scatter ranks out
    } else {
        LOGERR("number of tasks is smaller than 0");
        return -1;
    }

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
int unifycr_mount(const char prefix[], int rank, size_t size,
                  int l_app_id)
{
    int rc;
    int kv_rank, kv_nranks;
    bool b;
    char* cfgval;

    if (-1 != unifycr_mounted) {
        if (l_app_id != unifycr_mounted) {
            LOGERR("multiple mount support not yet implemented");
            return UNIFYCR_FAILURE;
        } else {
            LOGDBG("already mounted");
            return UNIFYCR_SUCCESS;
        }
    }

    /* record our rank for debugging messages,
     * record the value we should use for an app_id */
    app_id = l_app_id;
    glb_rank = rank;

    /* print log messages to stderr */
    unifycr_log_open(NULL);

    /************************
     * read configuration values
     ************************/

    // initialize configuration
    rc = unifycr_config_init(&client_cfg, 0, NULL);
    if (rc) {
        LOGERR("failed to initialize configuration.");
        return UNIFYCR_FAILURE;
    }
    client_cfg.ptype = UNIFYCR_CLIENT;

    // update configuration from runstate file
    rc = unifycr_read_runstate(&client_cfg, NULL);
    if (rc) {
        LOGERR("failed to update configuration from runstate.");
        return UNIFYCR_FAILURE;
    }

    // initialize k-v store access
    rc = unifycr_keyval_init(&client_cfg, &kv_rank, &kv_nranks);
    if (rc) {
        LOGERR("failed to update configuration from runstate.");
        return UNIFYCR_FAILURE;
    }
    if ((glb_rank != kv_rank) || (size != kv_nranks)) {
        LOGDBG("mismatch on mount vs kvstore rank/size");
    }

    /************************
     * record our mount point, and initialize structures to
     * store data
     ************************/

    /* record a copy of the prefix string defining the mount point
     * we should intercept */
    unifycr_mount_prefix = strdup(prefix);
    unifycr_mount_prefixlen = strlen(unifycr_mount_prefix);

    /*
     * unifycr_mount_shmget_key marks the start of
     * the superblock shared memory of each rank
     * each process has three types of shared memory:
     * request memory, recv memory and superblock
     * memory. We set unifycr_mount_shmget_key in
     * this way to avoid different ranks conflicting
     * on the same name in shm_open.
     */
    cfgval = client_cfg.shmem_single;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if ((rc == 0) && b) {
            unifycr_use_single_shm = 1;
        }
    }

    /* compute our local rank on the node,
     * the following call initializes local_rank_{lst,cnt} */
    rc = CountTasksPerNode(rank, size);
    if (rc < 0) {
        LOGERR("cannot get the local rank list.");
        return -1;
    }
    local_rank_idx = find_rank_idx(rank,
                                   local_rank_lst, local_rank_cnt);

    /* use our local rank on the node in shared memory and file
     * names to avoid conflicting with other procs on our node */
    unifycr_mount_shmget_key = local_rank_idx;

    /* initialize our library, creates superblock and spillover files */
    int ret = unifycr_init(rank);
    if (ret != UNIFYCR_SUCCESS) {
        return ret;
    }

    /* open rpc connection to server */
    ret = unifycr_client_rpc_init(local_rank_idx, app_id);
    if (ret != UNIFYCR_SUCCESS) {
        LOGERR("Failed to initialize client RPC");
        return ret;
    }

    /* call client mount rpc function here
     * to register our shared memory and files with server */
    LOGDBG("calling mount");
    invoke_client_mount_rpc();

#ifdef USE_DOMAIN_SOCKET
    /* open a socket to the server */
    rc = unifycr_init_socket(local_rank_idx, local_rank_cnt,
                             local_del_cnt);
    if (rc < 0) {
        LOGERR("failed to initialize socket, rc == %d", rc);
        return UNIFYCR_FAILURE;
    }
#endif

    /* create shared memory region for read requests */
    rc = unifycr_init_req_shm(local_rank_idx, app_id);
    if (rc < 0) {
        LOGERR("failed to init shared request memory");
        return UNIFYCR_FAILURE;
    }

    /* create shared memory region for holding data for read replies */
    rc = unifycr_init_recv_shm(local_rank_idx, app_id);
    if (rc < 0) {
        LOGERR("failed to init shared receive memory");
        return UNIFYCR_FAILURE;
    }

    /* add mount point as a new directory in the file list */
    if (unifycr_get_fid_from_path(prefix) < 0) {
        /* no entry exists for mount point, so create one */
        int fid = unifycr_fid_create_directory(prefix);
        if (fid < 0) {
            /* if there was an error, return it */
            LOGERR("failed to create directory entry for mount point: `%s'",
                   prefix);
            return UNIFYCR_FAILURE;
        }
    }

    /* record client state as mounted for specific app_id */
    unifycr_mounted = app_id;

    return UNIFYCR_SUCCESS;
}

/* free resources allocated during unifycr_init,
 * generally we do this in reverse order that
 * things were initailized in */
static int unifycr_finalize(void)
{
    int rc = UNIFYCR_SUCCESS;

    if (!unifycr_initialized) {
        /* not initialized yet, so we shouldn't call finalize */
        return UNIFYCR_FAILURE;
    }

    /* close spillover files */
    if (unifycr_spilloverblock != 0) {
        close(unifycr_spilloverblock);
        unifycr_spilloverblock = 0;
    }

    if (unifycr_spillmetablock != 0) {
        close(unifycr_spillmetablock);
        unifycr_spillmetablock = 0;
    }

    /* detach from superblock */
    unifycr_shm_free(shm_super_name, shm_super_size, &shm_super_buf);

    /* free directory stream stack */
    if (unifycr_dirstream_stack != NULL) {
        free(unifycr_dirstream_stack);
        unifycr_dirstream_stack = NULL;
    }

    /* free file stream stack */
    if (unifycr_stream_stack != NULL) {
        free(unifycr_stream_stack);
        unifycr_stream_stack = NULL;
    }

    /* free file descriptor stack */
    if (unifycr_fd_stack != NULL) {
        free(unifycr_fd_stack);
        unifycr_fd_stack = NULL;
    }

    /* no longer initialized, so update the flag */
    unifycr_initialized = 0;

    return rc;
}

/**
 * unmount the mounted file system
 * TODO: Add support for unmounting more than
 * one filesystem.
 * @return success/error code
 */
int unifycr_unmount(void)
{
    int rc;
    int ret = UNIFYCR_SUCCESS;

    if (-1 == unifycr_mounted) {
        return UNIFYCR_SUCCESS;
    }

    /************************
     * tear down connection to server
     ************************/

    /* detach from shared memory regions */
    unifycr_shm_free(shm_req_name,  shm_req_size,  &shm_req_buf);
    unifycr_shm_free(shm_recv_name, shm_recv_size, &shm_recv_buf);

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
        ret = UNIFYCR_FAILURE;
    }

    /* free resources allocated in client_rpc_init */
    unifycr_client_rpc_finalize();

    /************************
     * free our mount point, and detach from structures
     * storing data
     ************************/

    /* free resources allocated in unifycr_init */
    unifycr_finalize();

    /* free memory tracking our mount prefix string */
    if (unifycr_mount_prefix != NULL) {
        free(unifycr_mount_prefix);
        unifycr_mount_prefix = NULL;
        unifycr_mount_prefixlen = 0;
    }

    /************************
     * free configuration values
     ************************/

    /* clean up configuration */
    rc = unifycr_config_fini(&client_cfg);
    if (rc) {
        LOGERR("unifycr_config_fini() failed");
        ret = UNIFYCR_FAILURE;
    }

    /* shut down our logging */
    unifycr_log_close();

    unifycr_mounted = -1;

    return ret;
}

#define UNIFYCR_TX_BUFSIZE (64*(1<<10))

enum {
    UNIFYCR_TX_STAGE_OUT = 0,
    UNIFYCR_TX_STAGE_IN,
};

static
int unifycr_do_transfer_file(const char* src, const char* dst, int direction)
{
    int ret = 0;
    int fd_src = 0;
    int fd_dst = 0;
    ssize_t n_read = 0;
    ssize_t n_written = 0;
    ssize_t n_left = 0;
    char buf[UNIFYCR_TX_BUFSIZE] = { 0, };

    /*
     * for now, we do not use the @direction hint.
     */

    if (!buf) {
        return ENOMEM;
    }

    fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        return errno;
    }

    fd_dst = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_dst < 0) {
        ret = errno;
        goto out_close_src;
    }

    while (1) {
        n_read = read(fd_src, buf, UNIFYCR_TX_BUFSIZE);

        if (n_read == 0) {  /* EOF */
            break;
        } else if (n_read < 0) {   /* error */
            ret = errno;
            goto out_close_dst;
        }

        n_left = n_read;

        do {
            n_written = write(fd_dst, buf, n_left);

            if (n_written < 0) {
                ret = errno;
                goto out_close_dst;
            } else if (n_written == 0 && errno && errno != EAGAIN) {
                ret = errno;
                goto out_close_dst;
            }

            n_left -= n_written;
        } while (n_left);
    }

    fsync(fd_dst);

out_close_dst:
    close(fd_dst);
out_close_src:
    close(fd_src);

    return ret;
}

int unifycr_transfer_file(const char* src, const char* dst)
{
    int ret = 0;
    int direction = 0;
    struct stat sb_src = { 0, };
    struct stat sb_dst = { 0, };
    int unify_src = 0;
    int unify_dst = 0;
    char dst_path[PATH_MAX] = { 0, };
    char* pos = dst_path;
    char* src_path = strdup(src);

    if (!src_path) {
        return -ENOMEM;
    }

    if (unifycr_intercept_path(src)) {
        direction = UNIFYCR_TX_STAGE_OUT;
        unify_src = 1;
    }

    ret = UNIFYCR_WRAP(stat)(src, &sb_src);
    if (ret < 0) {
        return -errno;
    }

    pos += sprintf(pos, "%s", dst);

    if (unifycr_intercept_path(dst)) {
        direction = UNIFYCR_TX_STAGE_IN;
        unify_dst = 1;
    }

    ret = UNIFYCR_WRAP(stat)(dst, &sb_dst);
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

    return unifycr_do_transfer_file(src_path, dst_path, direction);
}

