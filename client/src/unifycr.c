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

#include "unifycr-internal.h"
#include "unifycr-fixed.h"
#include "unifycr_meta.h"
#include "unifycr_pmix.h"
#include "unifycr_runstate.h"
#include "unifycr_client_context.h"

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

static int unifycr_fpos_enabled   = 1;  /* whether we can use fgetpos/fsetpos */

/*
 * unifycr variable:
 * */

unifycr_cfg_t client_cfg;

//fs_type_t fs_type = UNIFYCRFS;
unifycr_index_buf_t unifycr_indices;
unifycr_fattr_buf_t unifycr_fattrs;

size_t unifycr_index_buf_size; /* metadata log size for log-structured io */
size_t unifycr_fattr_buf_size;
size_t unifycr_max_index_entries; /* max entries for log-structured write */
size_t unifycr_max_fattr_entries;
size_t glb_superblock_size;
int unifycr_spillmetablock;

int *local_rank_lst;
int local_rank_cnt;
int local_rank_idx;

int local_del_cnt = 0;
int client_sockfd;
struct pollfd cmd_fd;
size_t shm_req_size = UNIFYCR_SHMEM_REQ_SIZE;
size_t shm_recv_size = UNIFYCR_SHMEM_RECV_SIZE;
char *shm_recv_buf;
char *shm_req_buf;
char cmd_buf[CMD_BUF_SIZE] = {0};
char ack_msg[3] = {0};

/* QUESTION: what's the difference between the following two? */
int dbgrank;
int dbg_rank;

int app_id;
int glb_size;
int reqbuf_fd = -1;
int recvbuf_fd = -1;
int superblock_fd = -1;
size_t unifycr_key_slice_range;

int unifycr_use_logio = 0;
int unifycr_use_memfs = 1;
int unifycr_use_spillover = 1;

static int unifycr_use_single_shm = 0;
static int unifycr_page_size      = 0;

static off_t unifycr_max_offt;
static off_t unifycr_min_offt;
static off_t unifycr_max_long;
static off_t unifycr_min_long;

int    unifycr_max_files;  /* maximum number of files to store */
int    unifycr_chunk_bits; /* unifycr_chunk_size = 2 ^ unifycr_chunk_bits */
size_t unifycr_chunk_size; /* chunk size in bytes */
size_t unifycr_chunk_mem;  /* memory bytes to be used for chunk storage */
size_t unifycr_chunk_mask; /* chunk offset mask (logical to physical) */
int    unifycr_max_chunks; /* maximum number of chunks in memory */

size_t unifycr_spillover_size;    /* bytes in spillover for chunk storage */
int unifycr_spillover_max_chunks; /* maximum number of spillover chunks */

#ifdef HAVE_LIBNUMA
static char unifycr_numa_policy[10];
static int unifycr_numa_bank = -1;
#endif

extern pthread_mutex_t unifycr_stack_mutex;

/* keep track of what we've initialized */
int unifycr_initialized = 0;

/* keep track of debug level */
int unifycr_debug_level;

/* global persistent memory block (metadata + data) */
void *unifycr_superblock = NULL;
static void *free_fid_stack = NULL;
void *free_chunk_stack = NULL;
void *free_spillchunk_stack = NULL;
unifycr_filename_t *unifycr_filelist    = NULL;
static unifycr_filemeta_t *unifycr_filemetas   = NULL;
static unifycr_chunkmeta_t *unifycr_chunkmetas = NULL;

char *unifycr_chunks = NULL;
int unifycr_spilloverblock = 0;
int unifycr_spillmetablock = 0; /*used for log-structured i/o*/

/* array of file descriptors */
unifycr_fd_t unifycr_fds[UNIFYCR_MAX_FILEDESCS];
rlim_t unifycr_fd_limit;

/* array of file streams */
unifycr_stream_t unifycr_streams[UNIFYCR_MAX_FILEDESCS];

/* mount point information */
char  *unifycr_mount_prefix = NULL;
size_t unifycr_mount_prefixlen = 0;
static key_t  unifycr_mount_shmget_key = 0;

/* mutex to lock stack operations */
pthread_mutex_t unifycr_stack_mutex = PTHREAD_MUTEX_INITIALIZER;

/* path of external storage's mount point*/

char external_data_dir[UNIFYCR_MAX_FILENAME] = {0};
char external_meta_dir[UNIFYCR_MAX_FILENAME] = {0};

/* single function to route all unsupported wrapper calls through */
int unifycr_vunsupported(
    const char *fn_name,
    const char *file,
    int line,
    const char *fmt,
    va_list args)
{
    /* print a message about where in the UNIFYCR code we are */
    printf("UNSUPPORTED: %s() at %s:%d: ", fn_name, file, line);

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
    const char *fn_name,
    const char *file,
    int line,
    const char *fmt,
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
     * or at least no further away than either value already is*/
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
     * or at least no further away than either value already is*/
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

inline int unifycr_stack_lock()
{
    if (unifycr_use_single_shm) {
        return pthread_mutex_lock(&unifycr_stack_mutex);
    }
    return 0;
}

inline int unifycr_stack_unlock()
{
    if (unifycr_use_single_shm) {
        return pthread_mutex_unlock(&unifycr_stack_mutex);
    }
    return 0;
}

/* sets flag if the path is a special path */
inline int unifycr_intercept_path(const char *path)
{
    /* don't intecept anything until we're initialized */
    if (! unifycr_initialized) {
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
inline int unifycr_intercept_fd(int *fd)
{
    int oldfd = *fd;

    /* don't intecept anything until we're initialized */
    if (! unifycr_initialized) {
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
        //DEBUG("Changing fd from exposed %d to internal %d\n", oldfd, newfd);
        return 1;
    }
}

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
inline int unifycr_intercept_stream(FILE *stream)
{
    /* don't intecept anything until we're initialized */
    if (! unifycr_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * file stream array */
    unifycr_stream_t *ptr   = (unifycr_stream_t *) stream;
    unifycr_stream_t *start = &(unifycr_streams[0]);
    unifycr_stream_t *end   = &(unifycr_streams[UNIFYCR_MAX_FILEDESCS]);
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given a path, return the file id */
inline int unifycr_get_fid_from_path(const char *path)
{
    int i = 0;
    while (i < unifycr_max_files) {
        if (unifycr_filelist[i].in_use &&
            strcmp((void *)&unifycr_filelist[i].filename, path) == 0) {
            DEBUG("File found: unifycr_filelist[%d].filename = %s\n",
                  i, (char *)&unifycr_filelist[i].filename);
            return i;
        }
        i++;
    }

    /* couldn't find specified path */
    return -1;
}

/* given a file descriptor, return the file id */
inline int unifycr_get_fid_from_fd(int fd)
{
    /* check that file descriptor is within range */
    if (fd < 0 || fd >= UNIFYCR_MAX_FILEDESCS) {
        return -1;
    }

    /* right now, the file descriptor is equal to the file id */
    return fd;
}

/* return address of file descriptor structure or NULL if fd is out
 * of range */
inline unifycr_fd_t *unifycr_get_filedesc_from_fd(int fd)
{
    if (fd >= 0 && fd < UNIFYCR_MAX_FILEDESCS) {
        unifycr_fd_t *filedesc = &(unifycr_fds[fd]);
        return filedesc;
    }
    return NULL;
}

/* given a file id, return a pointer to the meta data,
 * otherwise return NULL */
unifycr_filemeta_t *unifycr_get_meta_from_fid(int fid)
{
    /* check that the file id is within range of our array */
    if (fid >= 0 && fid < unifycr_max_files) {
        /* get a pointer to the file meta data structure */
        unifycr_filemeta_t *meta = &unifycr_filemetas[fid];
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
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);

    meta->storage = FILE_STORAGE_LOGIO;

    return UNIFYCR_SUCCESS;
}

/* free data management resource for file */
static int unifycr_fid_store_free(int fid)
{
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
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);
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
int unifycr_generate_gfid(const char *path)
{
    unsigned char digested[16] = { 0, };
    unsigned long len = strlen(path);
    int *ival = (int *) digested;

    MD5((const unsigned char *) path, len, digested);

    return abs(ival[0]);
}

static int unifycr_gfid_from_fid(const int fid)
{
    unifycr_filename_t *fname = NULL;

    if (fid < 0 || fid >= unifycr_max_files)
        return -EINVAL;

    fname = &unifycr_filelist[fid];

    if (fname->in_use)
        return unifycr_generate_gfid(fname->filename);
    else
        return -EINVAL;
}

/* checks to see if a directory is empty
 * assumes that check for is_dir has already been made
 * only checks for full path matches, does not check relative paths,
 * e.g. ../dirname will not work
 * returns 1 for yes it is empty
 * returns 0 for no */
int unifycr_fid_is_dir_empty(const char *path)
{
    int i = 0;
    while (i < unifycr_max_files) {
        if (unifycr_filelist[i].in_use) {
            /* if the file starts with the path, it is inside of that directory
             * also check to make sure that it's not the directory entry itself */
            char *strptr = strstr(path, unifycr_filelist[i].filename);
            if (strptr == unifycr_filelist[i].filename
                && strcmp(path, unifycr_filelist[i].filename)) {
                DEBUG("File found: unifycr_filelist[%d].filename = %s\n",
                      i, (char *)&unifycr_filelist[i].filename);
                return 0;
            }
        }
        ++i;
    }

    /* couldn't find any files with this prefix, dir must be empty */
    return 1;
}

/* return current size of given file id */
off_t unifycr_fid_size(int fid)
{
    /* get meta data for this file */
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);
    return meta->size;
}

/*
 * insert file attribute to shared memory buffer
 */
static int ins_file_meta(unifycr_fattr_buf_t *ptr_f_meta_log,
                         unifycr_file_attr_t *ins_fattr)
{
    size_t i, ins_pos;
    size_t meta_cnt = *(ptr_f_meta_log->ptr_num_entries);
    unifycr_file_attr_t *meta_entry = ptr_f_meta_log->meta_entry;

    /* TODO: Improve the search time */
    for (i = meta_cnt - 1; i >= 0; i--) {
        if (meta_entry[i].fid <= ins_fattr->fid) {
            /* sort in ascending order */
            break;
        }
    }
    ins_pos = i + 1;

    (*ptr_f_meta_log->ptr_num_entries)++;

    if (ins_pos == meta_cnt) {
        /* easy case: insert at end */
        meta_entry[meta_cnt] = *ins_fattr;
    } else {
        /* shift entries after insert position */
        for (i = meta_cnt - 1; i >= ins_pos; i--) {
            meta_entry[i + 1] = meta_entry[i];
        }
        meta_entry[ins_pos] = *ins_fattr;
    }

    return 0;
}


/*
 * send global file metadata to the delegator,
 * which puts it to the key-value store
 * @param gfid: global file id
 * @return: error code
 */
static int set_global_file_meta(unifycr_file_attr_t *file_meta)
{
    int rc = 0;
    ssize_t nbytes = 0;
    int cmd = COMM_META_SET;
    int *response = NULL;

    if (!file_meta)
        return -EINVAL;

    memset(cmd_buf, 0, sizeof(cmd_buf));
    memcpy(cmd_buf, &cmd, sizeof(int));
    memcpy(cmd_buf + sizeof(int), file_meta, sizeof(*file_meta));

    nbytes = __real_write(client_sockfd, cmd_buf, sizeof(cmd_buf));
    if (nbytes <= 0)
        return -errno;

    nbytes = 0;
    cmd_fd.events = POLLIN | POLLPRI;
    cmd_fd.revents = 0;

    /*
     * hsim: potentially giving -1 (unlimited) timeout may hang the program.
     */
    rc = poll(&cmd_fd, 1, -1);
    if (rc <= 0) /* timeout or error, both are fail cases for us */
        return -EIO;

    /* FIXME: how do we have to report back this case? */
    if (cmd_fd.revents != POLLIN)
        return -EIO;

    nbytes = __real_read(client_sockfd, cmd_buf, sizeof(cmd_buf));
    if (nbytes == 0)
        return -errno;  /*remote connection is closed*/

    response = (int *) cmd_buf;

    if (response[0] != COMM_META_SET || response[1] != ACK_SUCCESS)
        return -EIO;

    return UNIFYCR_SUCCESS;
}

int unifycr_set_global_file_meta(const char *path, int fid, int gfid,
                                 struct stat *sb)
{
    int ret = 0;
    unifycr_file_attr_t new_fmeta = { 0, };

    memset((void *) &new_fmeta, 0, sizeof(new_fmeta));

    sprintf(new_fmeta.filename, "%s", path);

    new_fmeta.fid = fid;
    new_fmeta.gfid = gfid;
    new_fmeta.file_attr = *sb;

    ret = set_global_file_meta(&new_fmeta);
    if (ret < 0)
        return ret;

    ins_file_meta(&unifycr_fattrs, &new_fmeta);

    return 0;
}

int unifycr_get_global_file_meta(int gfid, unifycr_file_attr_t *gfattr)
{
    int rc = 0;
    ssize_t nbytes = 0;
    int cmd = COMM_META_GET;
    int *response = NULL;
    unifycr_file_attr_t *fmeta = NULL;

    if (!gfattr)
        return -EINVAL;

    memset(cmd_buf, 0, sizeof(cmd_buf));
    memcpy(cmd_buf, &cmd, sizeof(int));
    memcpy(cmd_buf + sizeof(int), &gfid, sizeof(int));

    nbytes = __real_write(client_sockfd, cmd_buf, sizeof(cmd_buf));
    if (nbytes <= 0)
        return -errno;

    nbytes = 0;
    cmd_fd.events = POLLIN | POLLPRI;
    cmd_fd.revents = 0;

    rc = poll(&cmd_fd, 1, -1);
    if (rc <= 0) /* timeout or error, both are fail cases for us */
        return -EIO;

    /* FIXME: how do we have to report back this case? */
    if (cmd_fd.revents != POLLIN)
        return -EIO;

    nbytes = __real_read(client_sockfd, cmd_buf, sizeof(cmd_buf));
    if (nbytes == 0)
        return -errno;  /*remote connection is closed*/

    response = (int *) cmd_buf;

    if (response[0] != COMM_META_GET || response[1] != ACK_SUCCESS)
        return -EIO;

    fmeta = (unifycr_file_attr_t *) &response[2];
    *gfattr = *fmeta;

    return UNIFYCR_SUCCESS;
}


/* fill in limited amount of stat information */
int unifycr_fid_stat(int fid, struct stat *buf)
{
    int ret = 0;
    int gfid = -1;
    unifycr_filemeta_t *meta = NULL;
    unifycr_file_attr_t gfattr = { 0, };

    if (!buf)
        return -EINVAL;

    meta = unifycr_get_meta_from_fid(fid);
    if (meta == NULL)
        return -UNIFYCR_ERROR_IO;

    gfid = unifycr_gfid_from_fid(fid);

    ret = unifycr_get_global_file_meta(gfid, &gfattr);
    if (ret != UNIFYCR_SUCCESS)
        return -UNIFYCR_ERROR_IO;

    *buf = gfattr.file_attr;

    /* set the file size */
    /* TODO: size needs to be set correctly in the global entry, when file is
     * synced/closed. The local metadata has wrong size for files written by
     * multiple clients. */
    buf->st_size = meta->size;

    return 0;
}

/* allocate a file id slot for a new file
 * return the fid or -1 on error */
int unifycr_fid_alloc()
{
    unifycr_stack_lock();
    int fid = unifycr_stack_pop(free_fid_stack);
    unifycr_stack_unlock();
    DEBUG("unifycr_stack_pop() gave %d\n", fid);
    if (fid < 0) {
        /* need to create a new file, but we can't */
        DEBUG("unifycr_stack_pop() failed (%d)\n", fid);
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
int unifycr_fid_create_file(const char *path)
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
    strcpy((void *)&unifycr_filelist[fid].filename, path);
    DEBUG("Filename %s got unifycr fd %d\n",
          unifycr_filelist[fid].filename, fid);

    /* initialize meta data */
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);
    meta->size    = 0;
    meta->chunks  = 0;
    meta->is_dir  = 0;
    meta->log_size = 0;
    meta->storage = FILE_STORAGE_NULL;
    meta->flock_status = UNLOCKED;
    /* PTHREAD_PROCESS_SHARED allows Process-Shared Synchronization*/
    pthread_spin_init(&meta->fspinlock, PTHREAD_PROCESS_SHARED);

    return fid;
}

/*
 * TODO: we need to generate proper mode for each entry.
 */
static const mode_t unifycr_default_file_mode = S_IFREG | 0644;
static const mode_t unifycr_default_dir_mode = S_IFDIR | 0755;

static inline void unifycr_init_file_attr(struct stat *sb, int gfid)
{
    if (sb) {
        memset((void *) sb, 0, sizeof(*sb));

        sb->st_ino = gfid;
        sb->st_size = 0;
        sb->st_blocks = 1;
        sb->st_blksize = 4096;
        sb->st_atime = sb->st_mtime = sb->st_ctime = time(NULL);
        sb->st_mode = unifycr_default_file_mode;
    }
}

static inline void unifycr_init_dir_attr(struct stat *sb, int gfid)
{
    if (sb) {
        memset((void *) sb, 0, sizeof(*sb));

        sb->st_ino = gfid;
        sb->st_size = 0;
        sb->st_blocks = 1;
        sb->st_blksize = 4096;
        sb->st_atime = sb->st_mtime = sb->st_ctime = time(NULL);
        sb->st_mode = unifycr_default_dir_mode;
    }
}

int unifycr_fid_create_directory(const char *path)
{
    int ret = 0;
    int fid = 0;
    int gfid = 0;
    int found_global = 0;
    int found_local = 0;
    size_t pathlen = strlen(path) + 1;
    struct stat sb = { 0, };
    unifycr_file_attr_t gfattr = { 0, };
    unifycr_filemeta_t *meta = NULL;

    if (pathlen > UNIFYCR_MAX_FILENAME)
        return (int) UNIFYCR_ERROR_NAMETOOLONG;

    fid = unifycr_get_fid_from_path(path);
    gfid = unifycr_generate_gfid(path);

    found_global =
        (unifycr_get_global_file_meta(gfid, &gfattr) == UNIFYCR_SUCCESS);
    found_local = (fid >= 0);

    if (found_local && found_global)
        return (int) UNIFYCR_ERROR_EXIST;

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
         * a consistency model this fs instance assumes.
         */
        return (int) UNIFYCR_ERROR_IO;
    }

    if (!found_local && found_global) {
        /* populate the local cache, then return EEXIST */

        return (int) UNIFYCR_ERROR_EXIST;
    }

    /* now, we need to create a new directory. */
    fid = unifycr_fid_create_file(path);
    if (fid < 0)
        return (int) UNIFYCR_ERROR_IO; /* FIXME: ENOSPC or EIO? */

    meta = unifycr_get_meta_from_fid(fid);
    meta->is_dir = 1;

    unifycr_init_dir_attr(&sb, gfid);

    ret = unifycr_set_global_file_meta(path, fid, gfid, &sb);
    if (ret) {
        DEBUG("Failed to populate the global meta entry for %s (fid:%d)\n",
              path, fid);
        return (int) UNIFYCR_ERROR_IO;
    }

    return UNIFYCR_SUCCESS;
}

/* read count bytes from file starting from pos and store into buf,
 * all bytes are assumed to exist, so checks on file size should be
 * done before calling this routine */
int unifycr_fid_read(int fid, off_t pos, void *buf, size_t count)
{
    int rc;

    /* short-circuit a 0-byte read */
    if (count == 0) {
        return UNIFYCR_SUCCESS;
    }

    /* get meta for this file id */
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);

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
int unifycr_fid_write(int fid, off_t pos, const void *buf, size_t count)
{
    int rc;

    /* short-circuit a 0-byte write */
    if (count == 0) {
        return UNIFYCR_SUCCESS;
    }

    /* get meta for this file id */
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);

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

    /* allocate an aligned chunk of zeroed memory */
    size_t buf_size = 1048576;
    void *buf = calloc(buf_size, 1);
    if (buf == NULL) {
        return (int)UNIFYCR_ERROR_IO;
    }

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
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);

    /* determine file storage type */
    if (meta->storage == FILE_STORAGE_FIXED_CHUNK ||
        meta->storage == FILE_STORAGE_LOGIO) {
        /* file stored in fixed-size chunks */
        rc = unifycr_fid_store_fixed_extend(fid, meta, length);
    } else {
        /* unknown storage type */
        rc = (int)UNIFYCR_ERROR_IO;
    }

    return rc;
}

/* if length is less than reserved space, give back space down to length */
int unifycr_fid_shrink(int fid, off_t length)
{
    int rc;

    /* get meta data for this file */
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);

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
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);

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

/* opens a new file id with specified path, access flags, and permissions,
 * fills outfid with file id and outpos with position for current file pointer,
 * returns UNIFYCR error code
 */
int unifycr_fid_open(const char *path, int flags, mode_t mode, int *outfid,
                     off_t *outpos)
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

    if (pathlen > UNIFYCR_MAX_FILENAME)
        return (int) UNIFYCR_ERROR_NAMETOOLONG;

    /* check whether this file already exists */
    /*
     * TODO: The test of file existence involves both local and global checks.
     * However, the testing below does not seem to cover all cases. For
     * instance, a globally unlinked file might be still cached locally because
     * the broadcast for cache invalidation has not been implemented, yet.
     */

    gfid = unifycr_generate_gfid(path);
    fid = unifycr_get_fid_from_path(path);

    DEBUG("unifycr_get_fid_from_path() gave %d (gfid = %d)\n", fid, gfid);

    found_global =
        (unifycr_get_global_file_meta(gfid, &gfattr) == UNIFYCR_SUCCESS);
    found_local = (fid >= 0);

    /* possibly, the file still exists in our local cache but globally
     * unlinked. Invalidate the entry
     *
     * FIXME: unifycr_fid_unlink() always returns success.
     */
    if (found_local && !found_global) {
        DEBUG("file found locally, but seems to be deleted globally. "
              "invalidating the local cache..\n");

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
        unifycr_filemeta_t *meta = NULL;

        fid = unifycr_fid_create_file(path);
        if (fid < 0) {
            DEBUG("failed to create a new file %s\n", path);

            /* FIXME: UNIFYCR_ERROR_NFILE or UNIFYCR_ERROR_IO ? */
            return (int) UNIFYCR_ERROR_IO;
        }

        ret = unifycr_fid_store_alloc(fid);
        if (ret != UNIFYCR_SUCCESS) {
            DEBUG("failed to allocate storage space for file %s (fid=%d)\n",
                  path, fid);
            return (int) UNIFYCR_ERROR_IO;
        }

        meta = unifycr_get_meta_from_fid(fid);

        meta->size = gfattr.file_attr.st_size;
        gfattr.fid = fid;
        gfattr.gfid = gfid;

        ins_file_meta(&unifycr_fattrs, &gfattr);
    } else if (found_local && found_global) {
        /* file exists and is valid.  */
        if ((flags & O_CREAT) && (flags & O_EXCL))
            return (int)UNIFYCR_ERROR_EXIST;

        if ((flags & O_DIRECTORY) && !unifycr_fid_is_dir(fid))
            return (int)UNIFYCR_ERROR_NOTDIR;

        if (!(flags & O_DIRECTORY) && unifycr_fid_is_dir(fid))
            return (int)UNIFYCR_ERROR_NOTDIR;

        if ((flags & O_TRUNC) && (flags & (O_RDWR | O_WRONLY)))
            unifycr_fid_truncate(fid, 0);

        if (flags & O_APPEND) {
            unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);
            pos = meta->size;
        }
    } else {
        /* !found_local && !found_global
         * If we reach here, we need to create a brand new file.
         */
        struct stat sb = { 0, };

        if (!(flags & O_CREAT)) {
            DEBUG("%s does not exist (O_CREAT not given).\n", path);
            return (int) UNIFYCR_ERROR_NOENT;
        }

        DEBUG("Creating a new entry for %s.\n", path);
        DEBUG("unifycr_superblock = %p; free_fid_stack = %p;"
                "free_chunk_stack = %p; unifycr_filelist = %p;"
                "chunks = %p\n", unifycr_superblock, free_fid_stack,
                free_chunk_stack, unifycr_filelist, unifycr_chunks);

        /* allocate a file id slot for this new file */
        fid = unifycr_fid_create_file(path);
        if (fid < 0) {
            DEBUG("Failed to create new file %s\n", path);
            return (int) UNIFYCR_ERROR_NFILE;
        }

        /* initialize the storage for the file */
        int store_rc = unifycr_fid_store_alloc(fid);
        if (store_rc != UNIFYCR_SUCCESS) {
            DEBUG("Failed to create storage for file %s\n", path);
            return (int) UNIFYCR_ERROR_IO;
        }

        /* create a file and send its attributes to key-value store */
        unifycr_init_file_attr(&sb, gfid);

        ret = unifycr_set_global_file_meta(path, fid, gfid, &sb);
        if (ret) {
            DEBUG("Failed to populate the global meta entry for %s (fid:%d)\n",
                  path, fid);
            return (int) UNIFYCR_ERROR_IO;
        }
    }

    /* TODO: allocate a free file descriptor and associate it with fid set
     * in_use flag and file pointer
     */
    *outfid = fid;
    *outpos = pos;

    DEBUG("UNIFYCR_open generated fd %d for file %s\n", fid, path);

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
    unifycr_fid_truncate(fid, 0);

    /* finalize the storage we're using for this file */
    unifycr_fid_store_free(fid);

    /* set this file id as not in use */
    unifycr_filelist[fid].in_use = 0;

    /* add this id back to the free stack */
    unifycr_fid_free(fid);

    return UNIFYCR_SUCCESS;
}

/* ---------------------------------------
 * Operations to mount file system
 * --------------------------------------- */

/* initialize our global pointers into the given superblock */
static void unifycr_init_pointers(void *superblock)
{
    char *ptr = (char *)superblock;

    /* jump over header (right now just a uint32_t to record
     * magic value of 0xdeadbeef if initialized */
    ptr += sizeof(uint32_t);

    /* stack to manage free file ids */
    free_fid_stack = ptr;
    ptr += unifycr_stack_bytes(unifycr_max_files);

    /* record list of file names */
    unifycr_filelist = (unifycr_filename_t *)ptr;
    ptr += (size_t)unifycr_max_files * sizeof(unifycr_filename_t);

    /* array of file meta data structures */
    unifycr_filemetas = (unifycr_filemeta_t *)ptr;
    ptr += (size_t)unifycr_max_files * sizeof(unifycr_filemeta_t);

    /* array of chunk meta data strucutres for each file */
    unifycr_chunkmetas = (unifycr_chunkmeta_t *)ptr;
    ptr += (size_t)unifycr_max_files * (size_t)unifycr_max_chunks
           * sizeof(unifycr_chunkmeta_t);

    if (unifycr_use_spillover)
        ptr += (size_t)unifycr_max_files * (size_t)unifycr_spillover_max_chunks
               * sizeof(unifycr_chunkmeta_t);

    /* stack to manage free memory data chunks */
    free_chunk_stack = ptr;
    ptr += unifycr_stack_bytes(unifycr_max_chunks);

    if (unifycr_use_spillover) {
        /* stack to manage free spill-over data chunks */
        free_spillchunk_stack = ptr;
        ptr += unifycr_stack_bytes(unifycr_spillover_max_chunks);
    }

    /* Only set this up if we're using memfs */
    if (unifycr_use_memfs) {
        /* round ptr up to start of next page */
        unsigned long long ull_ptr  = (unsigned long long)ptr;
        unsigned long long ull_page = (unsigned long long)unifycr_page_size;
        unsigned long long num_pages = ull_ptr / ull_page;
        if (ull_ptr > num_pages * ull_page)
            ptr = (char *)((num_pages + 1) * ull_page);

        /* pointer to start of memory data chunks */
        unifycr_chunks = ptr;
        ptr += (size_t)unifycr_max_chunks * unifycr_chunk_size;
    } else {
        unifycr_chunks = NULL;
    }

    /* pointer to the log-structured metadata structures*/
    unifycr_indices.ptr_num_entries = (size_t *)ptr;
    ptr += unifycr_page_size;
    unifycr_indices.index_entry = (unifycr_index_t *)ptr;
    ptr += unifycr_max_index_entries * sizeof(unifycr_index_t);

    /* data structures to record the global metadata*/
    unifycr_fattrs.ptr_num_entries = (size_t *)ptr;
    ptr += unifycr_page_size;
    unifycr_fattrs.meta_entry = (unifycr_file_attr_t *)ptr;
}

/* initialize data structures for first use */
static int unifycr_init_structures()
{
    int i;
    for (i = 0; i < unifycr_max_files; i++) {
        /* indicate that file id is not in use by setting flag to 0 */
        unifycr_filelist[i].in_use = 0;

        /* set pointer to array of chunkmeta data structures */
        unifycr_filemeta_t *filemeta = &unifycr_filemetas[i];

        unifycr_chunkmeta_t *chunkmetas;
        if (!unifycr_use_spillover) {
            chunkmetas = &(unifycr_chunkmetas[unifycr_max_chunks * i]);
        } else
            chunkmetas = &(unifycr_chunkmetas[(unifycr_max_chunks +
                                               unifycr_spillover_max_chunks) * i]);
        filemeta->chunk_meta = chunkmetas;
    }

    unifycr_stack_init(free_fid_stack, unifycr_max_files);

    unifycr_stack_init(free_chunk_stack, unifycr_max_chunks);

    if (unifycr_use_spillover) {
        unifycr_stack_init(free_spillchunk_stack, unifycr_spillover_max_chunks);
    }

    *(unifycr_indices.ptr_num_entries) = 0;
    *(unifycr_fattrs.ptr_num_entries) = 0;

    DEBUG("Meta-stacks initialized!\n");

    return UNIFYCR_SUCCESS;
}

static int unifycr_get_spillblock(size_t size, const char *path)
{
    int spillblock_fd;
    mode_t perms = unifycr_getmode(0);

    //MAP_OR_FAIL(open);
    spillblock_fd = __real_open(path, O_RDWR | O_CREAT | O_EXCL, perms);
    if (spillblock_fd < 0) {

        if (errno == EEXIST) {
            /* spillover block exists; attach and return */
            spillblock_fd = __real_open(path, O_RDWR);
        } else {
            perror("open() in unifycr_get_spillblock() failed");
            return -1;
        }
    } else {
        /* new spillover block created */
        /* TODO: align to SSD block size*/

        /*temp*/
        off_t rc = __real_lseek(spillblock_fd, size, SEEK_SET);
        if (rc < 0) {
            perror("lseek failed");
        }
    }

    return spillblock_fd;
}

/* create superblock of specified size and name, or attach to existing
 * block if available */
static void *unifycr_superblock_shmget(size_t size, key_t key)
{
    void *shmblock = NULL;
    int ret = -1;
    char shm_name[GEN_STR_LEN] = {0};

    DEBUG("Key for superblock = %x\n", key);

    /* Use mmap to allocated share memory for UnifyCR*/
    snprintf(shm_name, sizeof(shm_name),
             "%d-super-%d", app_id, key);
    superblock_fd = shm_open(shm_name, MMAP_OPEN_FLAG, MMAP_OPEN_MODE);
    if (superblock_fd == -1)
        return NULL;

#ifdef HAVE_POSIX_FALLOCATE
    ret = posix_fallocate(superblock_fd, 0, size);
    if (ret) {
        errno = ret;
        return NULL;
    }
#else
    ret = ftruncate(superblock_fd, size);
    if (ret == -1)
        return NULL;
#endif

    shmblock = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED,
                    superblock_fd, 0);
    if (shmblock == NULL)
        return NULL;

    /* init our global variables to point to spots in superblock */
    if (shmblock != NULL) {
        unifycr_init_pointers(shmblock);
        unifycr_init_structures();
    }

    return shmblock;
}

#if 0 // NO LONGER USED
/* converts string like 10mb to unsigned long long integer value of 10*1024*1024 */
static int unifycr_abtoull(char *str, unsigned long long *val)
{
    /* check that we have a string */
    if (str == NULL) {
        DEBUG("scr_abtoull: Can't convert NULL string to bytes @ %s:%d",
              __FILE__, __LINE__);
        return UNIFYCR_FAILURE;
    }

    /* check that we have a value to write to */
    if (val == NULL) {
        DEBUG("scr_abtoull: NULL address to store value @ %s:%d",
              __FILE__, __LINE__);
        return UNIFYCR_FAILURE;
    }

    /* pull the floating point portion of our byte string off */
    errno = 0;
    char *next = NULL;
    double num = strtod(str, &next);
    if (errno != 0) {
        DEBUG("scr_abtoull: Invalid double: %s @ %s:%d",
              str, __FILE__, __LINE__);
        return UNIFYCR_FAILURE;
    }

    /* now extract any units, e.g. KB MB GB, etc */
    unsigned long long units = 1;
    if (*next != '\0') {
        switch (*next) {
        case 'k':
        case 'K':
            units = 1024;
            break;
        case 'm':
        case 'M':
            units = 1024 * 1024;
            break;
        case 'g':
        case 'G':
            units = 1024 * 1024 * 1024;
            break;
        default:
            DEBUG("scr_abtoull: Unexpected byte string %s @ %s:%d",
                  str, __FILE__, __LINE__);
            return UNIFYCR_FAILURE;
        }

        next++;

        /* handle optional b or B character, e.g. in 10KB */
        if (*next == 'b' || *next == 'B') {
            next++;
        }

        /* check that we've hit the end of the string */
        if (*next != 0) {
            DEBUG("scr_abtoull: Unexpected byte string: %s @ %s:%d",
                  str, __FILE__, __LINE__);
            return UNIFYCR_FAILURE;
        }
    }

    /* check that we got a positive value */
    if (num < 0) {
        DEBUG("scr_abtoull: Byte string must be positive: %s @ %s:%d",
              str, __FILE__, __LINE__);
        return UNIFYCR_FAILURE;
    }

    /* multiply by our units and set out return value */
    *val = (unsigned long long)(num * (double) units);

    return UNIFYCR_SUCCESS;
}
#endif

static int unifycr_init(int rank)
{
    int rc;
    bool b;
    long l;
    unsigned long long bits;
    char *cfgval;

    if (! unifycr_initialized) {

        /* unifycr debug level default is zero */
        unifycr_debug_level = 0;
        cfgval = client_cfg.log_verbosity;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0)
                unifycr_debug_level = (int)l;
        }

#ifdef UNIFYCR_GOTCHA
        enum gotcha_error_t result;

        result = gotcha_wrap(wrap_unifycr_list, GOTCHA_NFUNCS, "unifycr");
        if (result != GOTCHA_SUCCESS) {
            DEBUG("gotcha_wrap returned %d\n", (int) result);
        }

        int i;
        for (i = 0; i < GOTCHA_NFUNCS; i++) {
            if (*(void **)(wrap_unifycr_list[i].function_address_pointer) == 0) {
                DEBUG("This function name failed to be wrapped: %s\n",
                       wrap_unifycr_list[i].name);
            }
        }
#endif

        /* as a hack to support fgetpos/fsetpos, we store the value of
         * a void* in an fpos_t so check that there's room and at least
         * print a message if this won't work */
        if (sizeof(fpos_t) < sizeof(void *)) {
            fprintf(stderr, "ERROR: fgetpos/fsetpos will not work correctly.\n");
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
            if ((rc == 0) && !b)
                unifycr_use_spillover = 0;
        }
        DEBUG("are we using spillover? %d\n", unifycr_use_spillover);

        /* determine maximum number of bytes of spillover for chunk storage */
        unifycr_spillover_size = UNIFYCR_SPILLOVER_SIZE;
        cfgval = client_cfg.spillover_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0)
                unifycr_spillover_size = (size_t)l;
        }

        /* determine max number of files to store in file system */
        unifycr_max_files = UNIFYCR_MAX_FILES;
        cfgval = client_cfg.client_max_files;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0)
                unifycr_max_files = (int)l;
        }

        /* determine number of bits for chunk size */
        unifycr_chunk_bits = UNIFYCR_CHUNK_BITS;
        cfgval = client_cfg.shmem_chunk_bits;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0)
                unifycr_chunk_bits = (int)l;
        }

        /* determine maximum number of bytes of memory for chunk storage */
        unifycr_chunk_mem = UNIFYCR_CHUNK_MEM;
        cfgval = client_cfg.shmem_chunk_mem;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0)
                unifycr_chunk_mem = (size_t)l;
        }

        /* set chunk size, set chunk offset mask, and set total number
         * of chunks */
        unifycr_chunk_size = 1 << unifycr_chunk_bits;
        unifycr_chunk_mask = unifycr_chunk_size - 1;
        unifycr_max_chunks = unifycr_chunk_mem >> unifycr_chunk_bits;

        /* set number of chunks in spillover device */
        unifycr_spillover_max_chunks = unifycr_spillover_size >> unifycr_chunk_bits;

        unifycr_index_buf_size = UNIFYCR_INDEX_BUF_SIZE;
        cfgval = client_cfg.logfs_index_buf_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0)
                unifycr_index_buf_size = (size_t)l;
        }
        unifycr_max_index_entries =
            unifycr_index_buf_size / sizeof(unifycr_index_t);

        unifycr_fattr_buf_size = UNIFYCR_FATTR_BUF_SIZE;
        cfgval = client_cfg.logfs_attr_buf_size;
        if (cfgval != NULL) {
            rc = configurator_int_val(cfgval, &l);
            if (rc == 0)
                unifycr_fattr_buf_size = (size_t)l;
        }
        unifycr_max_fattr_entries =
            unifycr_fattr_buf_size / sizeof(unifycr_file_attr_t);

#ifdef HAVE_LIBNUMA
        char *env = getenv("UNIFYCR_NUMA_POLICY");
        if (env) {
            sprintf(unifycr_numa_policy, env);
            DEBUG("NUMA policy used: %s\n", unifycr_numa_policy);
        } else {
            sprintf(unifycr_numa_policy, "default");
        }

        env = getenv("UNIFYCR_USE_NUMA_BANK");
        if (env) {
            int val = atoi(env);
            if (val >= 0) {
                unifycr_numa_bank = val;
            } else {
                fprintf(stderr, "Incorrect NUMA bank specified in UNIFYCR_USE_NUMA_BANK."
                        "Proceeding with default allocation policy!\n");
            }
        }
#endif

        /* record the max fd for the system */
        /* RLIMIT_NOFILE specifies a value one greater than the maximum
         * file descriptor number that can be opened by this process */
        struct rlimit r_limit;

        if (getrlimit(RLIMIT_NOFILE, &r_limit) < 0) {
            perror("getrlimit failed");
            return UNIFYCR_FAILURE;
        }
        unifycr_fd_limit = r_limit.rlim_cur;
        DEBUG("FD limit for system = %ld\n", unifycr_fd_limit);

        /* determine the size of the superblock */
        /* generous allocation for chunk map (one file can take entire space)*/
        size_t sb_size = 0;

        /* header: uint32_t to hold number of initialization */
        sb_size += sizeof(uint32_t);

        /* free file id stack */
        sb_size += unifycr_stack_bytes(unifycr_max_files);

        /* file name struct array */
        sb_size += unifycr_max_files * sizeof(unifycr_filename_t);

        /* file metadata struct array */
        sb_size += unifycr_max_files * sizeof(unifycr_filemeta_t);

        /* chunk metadata struct array for each file */
        sb_size += unifycr_max_files * unifycr_max_chunks *
                   sizeof(unifycr_chunkmeta_t);
        if (unifycr_use_spillover) {
            /* spillover chunk metadata struct array for each file */
            sb_size += unifycr_max_files * unifycr_spillover_max_chunks *
                       sizeof(unifycr_chunkmeta_t);
        }

        /* free chunk stack */
        sb_size += unifycr_stack_bytes(unifycr_max_chunks);

        if (unifycr_use_spillover) {
            /* free spillover chunk stack */
            sb_size += unifycr_stack_bytes(unifycr_spillover_max_chunks);
        }

        if (unifycr_use_memfs) {
            /* memory chunks */
            sb_size += unifycr_page_size +
                       (unifycr_max_chunks * unifycr_chunk_size);
        }

        /* index region size */
        sb_size += unifycr_max_index_entries * sizeof(unifycr_file_attr_t) +
                   unifycr_page_size;

        /* attribute region size */
        sb_size += unifycr_max_fattr_entries * sizeof(unifycr_file_attr_t) +
                   unifycr_page_size;

        glb_superblock_size = sb_size;

        /* get a superblock of persistent memory and initialize our
         * global variables for this block */
        unifycr_superblock =
            unifycr_superblock_shmget(sb_size, unifycr_mount_shmget_key);
        if (unifycr_superblock == NULL) {
            DEBUG("unifycr_superblock_shmget() failed\n");
            return UNIFYCR_FAILURE;
        }

        /* initialize spillover store */
        if (unifycr_use_spillover) {
            char spillfile_prefix[UNIFYCR_MAX_FILENAME];

            cfgval = client_cfg.spillover_data_dir;
            if (cfgval != NULL)
                strncpy(external_data_dir, cfgval, sizeof(external_data_dir));
            else {
                DEBUG("UNIFYCR_SPILLOVER_DATA_DIR not set, must be an existing "
                      "writable path (e.g., /mnt/ssd):\n");
                return UNIFYCR_FAILURE;
            }
            snprintf(spillfile_prefix, sizeof(spillfile_prefix),
                     "%s/spill_%d_%d.log",
                     external_data_dir, app_id, local_rank_idx);
            unifycr_spilloverblock =
                unifycr_get_spillblock(unifycr_spillover_size,
                                       spillfile_prefix);
            if (unifycr_spilloverblock < 0) {
                DEBUG("unifycr_get_spillblock() failed!\n");
                return UNIFYCR_FAILURE;
            }

            cfgval = client_cfg.spillover_meta_dir;
            if (cfgval != NULL)
                strncpy(external_meta_dir, cfgval, sizeof(external_meta_dir));
            else {
                DEBUG("UNIFYCR_SPILLOVER_META_DIR not set, must be an existing "
                      "writable path (e.g., /mnt/ssd):\n");
                return UNIFYCR_FAILURE;
            }
            snprintf(spillfile_prefix, sizeof(spillfile_prefix),
                     "%s/spill_index_%d_%d.log",
                     external_meta_dir, app_id, local_rank_idx);

            unifycr_spillmetablock =
                unifycr_get_spillblock(unifycr_index_buf_size,
                                       spillfile_prefix);
            if (unifycr_spillmetablock < 0) {
                DEBUG("unifycr_get_spillmetablock failed!\n");
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


/**
* mount a file system at a given prefix
* subtype: 0-> log-based file system;
* 1->striping based file system, not implemented yet.
* @param prefix: directory prefix
* @param size: the number of ranks
* @param l_app_id: application ID
* @return success/error code
*/
#if 0
int unifycr_mount(const char prefix[], int rank, size_t size,
                  int l_app_id, int subtype)
{
    switch (subtype) {
    case UNIFYCRFS:
        fs_type = UNIFYCRFS;
        break;
    case UNIFYCR_LOG:
        fs_type = UNIFYCR_LOG;
        break;
    case UNIFYCR_STRIPE:
        fs_type = UNIFYCR_STRIPE;
        break;
    default:
        fs_type = UNIFYCR_LOG;
        break;
    }
#endif
int unifycr_mount(const char prefix[], int rank, size_t size,
                  int l_app_id)
{
    int rc;

    dbg_rank = rank;
    app_id = l_app_id;

    // initialize configuration
    rc = unifycr_config_init(&client_cfg, 0, NULL);
    if (rc) {
        DEBUG("rank:%d, failed to initialize configuration.", dbg_rank);
        return -1;
    }

    // update configuration from runstate file
    rc = unifycr_read_runstate(&client_cfg, NULL);
    if (rc) {
        DEBUG("rank:%d, failed to update configuration from runstate.",
              dbg_rank);
        return -1;
    }

    return unifycrfs_mount(prefix, size, rank);
}

/**
 * unmount the mounted file system, triggered
 * by the root process of an application
 * ToDo: add the support for more operations
 * beyond terminating the servers. E.g.
 * data flush for persistence.
 * @return success/error code
 */
int unifycr_unmount(void)
{
    int cmd = COMM_UNMOUNT;
    int bytes_read = 0;
    int rc;
    int *response = NULL;

    memset(cmd_buf, 0, sizeof(cmd_buf));
    memcpy(cmd_buf, &cmd, sizeof(int));

    rc = __real_write(cmd_fd.fd, cmd_buf, sizeof(cmd_buf));
    if (rc <= 0)
        return UNIFYCR_FAILURE;

    cmd_fd.events = POLLIN | POLLPRI;
    cmd_fd.revents = 0;

    rc = poll(&cmd_fd, 1, -1);
    if (rc < 0)
        return UNIFYCR_FAILURE;

    if (cmd_fd.revents != 0 && cmd_fd.revents == POLLIN) {
        bytes_read = __real_read(cmd_fd.fd, cmd_buf, sizeof(cmd_buf));

        response = (int *) cmd_buf;

        if (bytes_read <= 0 ||
            response[0] != COMM_UNMOUNT || response[1] != ACK_SUCCESS)
            return UNIFYCR_FAILURE;
    }

    return UNIFYCR_SUCCESS;
}

/**
 * Transfer the client-side context information to the corresponding
 * delegator on the server side.
 */

/*
 * This function sends the client configuration with the delegator on the
 * server side. The corresponding server side function is sync_with_client
 * in unifycr_cmd_handler.c
 */
static int unifycr_sync_to_del(void)
{
    // the client side context information
    unifycr_client_context_t client_ctx;
    size_t cmd_len;
    int cmd = COMM_MOUNT;

    client_ctx.app_id = app_id;
    client_ctx.local_rank_index = local_rank_idx;
    client_ctx.dbg_rank = dbg_rank;
    client_ctx.num_procs_per_node = local_rank_cnt;
    client_ctx.req_buf_sz = shm_req_size;
    client_ctx.recv_buf_sz = shm_recv_size;
    client_ctx.superblock_sz = glb_superblock_size;
    client_ctx.meta_offset = (char *)unifycr_indices.ptr_num_entries -
                             (char *)unifycr_superblock;
    client_ctx.meta_size = unifycr_max_index_entries * sizeof(unifycr_index_t);
    client_ctx.fmeta_offset = (char *)unifycr_fattrs.ptr_num_entries -
                              (char *)unifycr_superblock;
    client_ctx.fmeta_size = unifycr_max_fattr_entries *
                            sizeof(unifycr_file_attr_t);
    client_ctx.data_offset = (char *)unifycr_chunks -
                             (char *)unifycr_superblock;
    client_ctx.data_size = unifycr_max_chunks * unifycr_chunk_size;
    strcpy(client_ctx.external_spill_dir, external_data_dir);

    /*
     * Copy the client-side information to the command
     * buffer, then send to the delegator. The delegator
     * will attach to the client-side shared memory and open
     * the spill log file based on this information.
     */

    /*
     * TODO: might want to allocate this (probably from a memory pool)
     */
    memset(cmd_buf, 0, sizeof(cmd_buf));
    *(int *)cmd_buf = cmd;
    cmd_len = sizeof(cmd);

    // pack the client context into the command buffer
    unifycr_pack_client_context(&client_ctx, cmd_buf + cmd_len);
    cmd_len += sizeof(client_ctx);

    int res = __real_write(client_sockfd, cmd_buf, cmd_len);
    if (res != 0) {
        int bytes_read = 0;
        int rc = -1;
        int *response = NULL;

        cmd_fd.events = POLLIN | POLLPRI;
        cmd_fd.revents = 0;

        rc = poll(&cmd_fd, 1, -1);
        if (rc == 0) {
            /* encounter timeout*/
            return -1;
        } else {
            if (rc > 0) {
                if (cmd_fd.revents != 0) {
                    if (cmd_fd.revents == POLLIN) {
                        bytes_read = __real_read(client_sockfd, cmd_buf,
                                                 sizeof(cmd_buf));
                        if (bytes_read == 0) {
                            /*remote connection is closed*/
                            return -1;
                        } else {
                            response = (int *) cmd_buf;

                            if (response[0] != COMM_MOUNT ||
                                response[1] != ACK_SUCCESS)
                                return rc;

                            unifycr_key_slice_range =
                                *(size_t *)(cmd_buf + 2 * sizeof(int));
                        }
                    } else {
                        /*encounter connection error*/
                        return -1;
                    }
                } else {
                    /*file descriptor is negative*/
                    return -1;
                }
            } else {
                /* encounter error*/
                return -1;
            }
        }
    } else {
        /*write error*/
        return -1;
    }

    return 0;
}

/**
 * Initialize the shared recv memory buffer to receive data from the delegators
 */
static int unifycr_init_recv_shm(int local_rank_idx, int app_id)
{
    char *cfgval;
    char shm_name[GEN_STR_LEN] = {0};
    int rc = -1;
    long l;

    cfgval = client_cfg.shmem_recv_size;
    if (cfgval != NULL) {
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0)
            shm_recv_size = l;
    }

    snprintf(shm_name, sizeof(shm_name),
             "%d-recv-%d", app_id, local_rank_idx);
    recvbuf_fd = shm_open(shm_name, MMAP_OPEN_FLAG, MMAP_OPEN_MODE);
    rc = recvbuf_fd;
    if (rc == -1)
        return UNIFYCR_FAILURE;

#ifdef HAVE_POSIX_FALLOCATE
    rc = posix_fallocate(recvbuf_fd, 0, shm_recv_size);
    if (rc) {
        errno = rc;
        return UNIFYCR_FAILURE;
    }
#else
    rc = ftruncate(recvbuf_fd, shm_recv_size);
    if (rc == -1)
        return UNIFYCR_FAILURE;
#endif

    shm_recv_buf = mmap(NULL, shm_recv_size, PROT_WRITE | PROT_READ,
                        MAP_SHARED, recvbuf_fd, 0);
    if (shm_recv_buf == NULL)
        return UNIFYCR_FAILURE;

    *((int *)shm_recv_buf) = app_id + 3; // TODO: + 3 ?? Why ??
    return 0;
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
    char *cfgval;
    char shm_name[GEN_STR_LEN] = {0};
    int rc = -1;
    long l;

    /* initialize request buffer size*/
    cfgval = client_cfg.shmem_req_size;
    if (cfgval != NULL) {
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0)
            shm_req_size = l;
    }

    snprintf(shm_name, sizeof(shm_name),
             "%d-req-%d", app_id, local_rank_idx);
    reqbuf_fd = shm_open(shm_name, MMAP_OPEN_FLAG, MMAP_OPEN_MODE);
    rc = reqbuf_fd;
    if (rc == -1)
        return UNIFYCR_FAILURE;

#ifdef HAVE_POSIX_FALLOCATE
    rc = posix_fallocate(reqbuf_fd, 0, shm_req_size);
    if (rc) {
        errno = rc;
        return UNIFYCR_FAILURE;
    }
#else
    rc = ftruncate(reqbuf_fd, shm_req_size);
    if (rc == -1)
        return UNIFYCR_FAILURE;
#endif


    shm_req_buf = mmap(NULL, shm_req_size, PROT_WRITE | PROT_READ,
                       MAP_SHARED, reqbuf_fd, 0);
    if (shm_req_buf == NULL)
        return UNIFYCR_FAILURE;

    return 0;
}

/**
 * get the number of delegators on the
 * same node from the first delegator
 * on the server side
 */
static int get_del_cnt(void)
{
    int cmd = COMM_SYNC_DEL;
    int res;
    int *response = NULL;

    memset(cmd_buf, 0, sizeof(cmd_buf));
    memcpy(cmd_buf, &cmd, sizeof(int));
    res = __real_write(client_sockfd, cmd_buf, sizeof(cmd_buf));

    if (res != 0) {
        int bytes_read = 0;
        int rc = -1;

        cmd_fd.events = POLLIN | POLLPRI;
        cmd_fd.revents = 0;

        rc = poll(&cmd_fd, 1, -1);
        if (rc == 0) {
            /* encounter timeout*/
            return -1;
        } else {
            if (rc > 0) {
                if (cmd_fd.revents != 0) {
                    if (cmd_fd.revents == POLLIN) {
                        bytes_read = __real_read(client_sockfd, cmd_buf,
                                                 sizeof(cmd_buf));

                        response = (int *) cmd_buf;

                        if (bytes_read == 0)
                            return -1;

                        if (response[0] != COMM_SYNC_DEL ||
                            response[1] != ACK_SUCCESS)
                            return rc;
                    } else {
                        /*encounter connection error*/
                        return -1;
                    }
                } else {
                    /*file descriptor is negative*/
                    return -1;
                }
            } else {
                /* encounter error*/
                return -1;
            }
        }
    } else {
        /*write error*/
        return -1;
    }

    return *(int *)(cmd_buf + 2 * sizeof(int));

}

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
    char tmp_path[UNIFYCR_MAX_FILENAME];
#ifdef HAVE_PMIX_H
    char *pmix_path = NULL;
#endif

    memset(tmp_path, 0, sizeof(tmp_path));

#ifdef HAVE_PMIX_H
    // lookup domain socket path in PMIx
    if (unifycr_pmix_lookup(pmix_key_unifycrd_socket, 0, &pmix_path) == 0) {
        snprintf(tmp_path, sizeof(tmp_path), "%s", pmix_path);
        free(pmix_path);
    }
#else
    /* calculate delegator assignment */
    nprocs_per_del = l_num_procs_per_node / l_num_del_per_node;
    if ((l_num_procs_per_node % l_num_del_per_node) != 0)
        nprocs_per_del++;
    snprintf(tmp_path, sizeof(tmp_path), "%s.%d.%d",
             SOCKET_PATH, getuid(), proc_id / nprocs_per_del);
#endif

    client_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_sockfd < 0)
        return -1;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strcpy(serv_addr.sun_path, tmp_path);
    len = sizeof(serv_addr);
    result = connect(client_sockfd, (struct sockaddr *)&serv_addr, len);

    /* exit with error if connection is not successful */
    if (result == -1) {
        rc = -1;
        return rc;
    }

    flag = fcntl(client_sockfd, F_GETFL);
    fcntl(client_sockfd, F_SETFL, flag | O_NONBLOCK);
    cmd_fd.fd = client_sockfd;
    cmd_fd.events = POLLIN | POLLHUP;
    cmd_fd.revents = 0;

    return 0;
}

int compare_fattr(const void *a, const void *b)
{
    const unifycr_file_attr_t *ptr_a = a;
    const unifycr_file_attr_t *ptr_b = b;

    if (ptr_a->fid == ptr_b->fid)
        return 0;
    if (ptr_a->fid < ptr_b->fid)
        return -1;
    else
        return 1;
}

static int compare_int(const void *a, const void *b)
{
    const int ia = *(const int *)a;
    const int ib = *(const int *)b;

    if (ia == ib)
        return 0;
    else if (ia < ib)
        return -1;
    else
        return 1;
}

static int compare_name_rank_pair(const void *a, const void *b)
{
    const name_rank_pair_t *pair_a = a;
    const name_rank_pair_t *pair_b = b;

    return strcmp(pair_a->hostname, pair_b->hostname);
}

/**
 * find the local index of a given rank among all ranks
 * collocated on the same node
 * @param local_rank_lst: a list of local ranks
 * @param local_rank_cnt: number of local ranks
 * @return index of rank in local_rank_lst
 */
static int find_rank_idx(int rank, int *local_rank_lst, int local_rank_cnt)
{
    int i;

    for (i = 0; i < local_rank_cnt; i++) {
        if (local_rank_lst[i] == rank)
            return i;
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
    if (rc != 0)
        DEBUG("failed to get the processor's name");

    if (numTasks > 0) {
        if (rank == 0) {
            int i;
            /* a container of (rank, host) mappings*/
            name_rank_pair_t *host_set =
                (name_rank_pair_t *)malloc(numTasks
                                           * sizeof(name_rank_pair_t));
            /*
             * MPI_receive all hostnames, and compare to local hostname
             */
            for (i = 1; i < numTasks; i++) {
                rc = MPI_Recv(hostname, HOST_NAME_MAX,
                              MPI_CHAR, MPI_ANY_SOURCE,
                              MPI_ANY_TAG, MPI_COMM_WORLD,
                              &status);

                if (rc != 0) {
                    DEBUG("cannot receive hostnames");
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
            int **rank_set = (int **)malloc(numTasks * sizeof(int *));
            int *rank_cnt = (int *)malloc(numTasks * sizeof(int));
            int cursor = 0, set_counter = 0;

            for (i = 1; i < numTasks; i++) {
                if (strcmp(host_set[i].hostname,
                           host_set[i - 1].hostname) == 0) {
                    /*do nothing*/
                } else {
                    // find a different rank, so switch to a new set
                    int j, k = 0;

                    rank_set[set_counter] =
                        (int *)malloc((i - cursor) * sizeof(int));
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
                            DEBUG("cannot send local rank cnt");
                            return -1;
                        }

                        /*send the local rank set to the corresponding rank*/
                        rc = MPI_Send(rank_set[i], rank_cnt[i], MPI_INT,
                                      rank_set[i][j], 0, MPI_COMM_WORLD);
                        if (rc != 0) {
                            DEBUG("cannot send local rank list");
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
                for (i = 0; i < rank_cnt[root_set_no]; i++)
                    local_rank_lst[i] = rank_set[root_set_no][i];

                local_rank_cnt = rank_cnt[root_set_no];
            }

            for (i = 0; i < set_counter; i++)
                free(rank_set[i]);

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
                DEBUG("cannot send host name");
                return -1;
            }
            /*receive the local rank count */
            rc = MPI_Recv(&local_rank_cnt, 1, MPI_INT,
                          0, 0, MPI_COMM_WORLD, &status);
            if (rc != 0) {
                DEBUG("cannot receive local rank cnt");
                return -1;
            }

            /* receive the the local rank list */
            local_rank_lst = (int *)malloc(local_rank_cnt * sizeof(int));
            rc = MPI_Recv(local_rank_lst, local_rank_cnt, MPI_INT,
                          0, 0, MPI_COMM_WORLD, &status);
            if (rc != 0) {
                free(local_rank_lst);
                DEBUG("cannot receive local rank list");
                return -1;
            }

        }

        qsort(local_rank_lst, local_rank_cnt, sizeof(int),
              compare_int);
    } else {
        DEBUG("number of tasks is smaller than 0");
        return -1;
    }

    return 0;
}


/* mount memfs at some prefix location */
int unifycrfs_mount(const char prefix[], size_t size, int rank)
{
    int rc;
    bool b;
    char *cfgval;

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
        if ((rc == 0) && b)
            unifycr_use_single_shm = 1;
    }
#if 0 // OLD WAY
    if (unifycr_use_single_shm)
        unifycr_mount_shmget_key = UNIFYCR_SUPERBLOCK_KEY + rank;
    else
        unifycr_mount_shmget_key = IPC_PRIVATE;
#endif

    // note: the following call initializes local_rank_{lst,cnt}
    rc = CountTasksPerNode(rank, size);
    if (rc < 0) {
        DEBUG("rank:%d, cannot get the local rank list.", dbg_rank);
        return -1;
    }
    local_rank_idx = find_rank_idx(rank,
                                   local_rank_lst, local_rank_cnt);
    unifycr_mount_shmget_key = local_rank_idx;

    /* initialize our library */
    rc = unifycr_init(rank);
    if (rc != UNIFYCR_SUCCESS)
        return rc;

    /* get the number of collocated delegators*/
    if (local_rank_idx == 0) {
        rc = unifycr_init_socket(0, 1, 1);
        if (rc < 0) {
            DEBUG("rank:%d, failed to init socket.", dbg_rank);
            return UNIFYCR_FAILURE;
        }

        local_del_cnt = get_del_cnt();
        if (local_del_cnt > 0) {
            int i;
            for (i = 0; i < local_rank_cnt; i++) {
                if (local_rank_lst[i] != rank) {
                    rc = MPI_Send(&local_del_cnt, 1, MPI_INT,
                                  local_rank_lst[i], 0,
                                  MPI_COMM_WORLD);
                    if (rc != MPI_SUCCESS) {
                        DEBUG("rank:%d, MPI_Send failed", dbg_rank);
                        return UNIFYCR_FAILURE;
                    }

                }
            }
        } else {
            DEBUG("rank:%d, fail to get the delegator count.", dbg_rank);
            return UNIFYCR_FAILURE;
        }

    } else {
        MPI_Status status;
        rc = MPI_Recv(&local_del_cnt, 1, MPI_INT, local_rank_lst[0],
                      0, MPI_COMM_WORLD, &status);
        if (rc != MPI_SUCCESS) {
            DEBUG("rank:%d, MPI_Recv failed.", dbg_rank);
            return UNIFYCR_FAILURE;
        }
        if (local_del_cnt < 0 || rc < 0) {
            DEBUG("rank:%d, failed to init socket.", dbg_rank);
            return UNIFYCR_FAILURE;
        } else  {
            rc = unifycr_init_socket(local_rank_idx,
                                     local_rank_cnt, local_del_cnt);
            if (rc < 0) {
                DEBUG("rank:%d, failed to init socket.", dbg_rank);
                return UNIFYCR_FAILURE;
            }
        }
    }

    /*connect to server-side delegators*/

    rc = unifycr_init_req_shm(local_rank_idx, app_id);
    if (rc < 0) {
        DEBUG("rank:%d, fail to init shared request memory.", dbg_rank);
        return UNIFYCR_FAILURE;
    }

    rc = unifycr_init_recv_shm(local_rank_idx, app_id);
    if (rc < 0) {
        DEBUG("rank:%d, fail to init shared receive memory.", dbg_rank);
        return UNIFYCR_FAILURE;
    }

    rc = unifycr_sync_to_del();
    if (rc < 0) {
        DEBUG("rank:%d, fail to convey information to the delegator.",
                dbg_rank);
        return UNIFYCR_FAILURE;
    }

    /* add mount point as a new directory in the file list */
    if (unifycr_get_fid_from_path(prefix) >= 0) {
        /* we can't mount this location, because it already exists */
        errno = EEXIST;
        return -1;
    } else {
        /* claim an entry in our file list */
        int fid = unifycr_fid_create_directory(prefix);

        if (fid < 0) {
            /* if there was an error, return it */
            return fid;
        }
    }

    return 0;
}

/*
 * get information about the chunk data region for external async libraries
 * to register during their init
 */
size_t unifycr_get_data_region(void **ptr)
{
    *ptr = unifycr_chunks;
    return unifycr_chunk_mem;
}

/* get a list of chunks for a given file (useful for RDMA, etc.) */
chunk_list_t *unifycr_get_chunk_list(char *path)
{
    return NULL;
}

/*
 * debug function to print list of chunks constituting a file and to test
 * above function
 */
void unifycr_print_chunk_list(char *path)
{
}
