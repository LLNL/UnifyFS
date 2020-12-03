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

#ifndef UNIFYFS_INTERNAL_H
#define UNIFYFS_INTERNAL_H

#include "config.h"

#ifdef HAVE_OFF64_T
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE
#else
#define off64_t int64_t
#endif

/* -------------------------------
 * Common includes
 * -------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// system headers
#include <aio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <poll.h>
#include <search.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <dirent.h>

#include <pthread.h>
#include <sched.h>

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

// common headers
#include "unifyfs_configurator.h"
#include "unifyfs_const.h"
#include "unifyfs_keyval.h"
#include "unifyfs_log.h"
#include "unifyfs_logio.h"
#include "unifyfs_meta.h"
#include "unifyfs_shm.h"
#include "seg_tree.h"

// client headers
#include "unifyfs.h"
#include "unifyfs-stack.h"
#include "utlist.h"
#include "uthash.h"

/* -------------------------------
 * Defines and types
 * -------------------------------
 */

/* define a macro to capture function name, file name, and line number
 * along with user-defined string */
#define UNIFYFS_UNSUPPORTED(fmt, args...) \
      unifyfs_unsupported(__func__, __FILE__, __LINE__, fmt, ##args)

#ifdef UNIFYFS_GOTCHA
#include <gotcha/gotcha.h>

/* the name of our wrapper - we use __wrap_<iofunc> instead of <iofunc> */
#define UNIFYFS_WRAP(name) __wrap_ ## name

/* the name of the real function pointer */
#define UNIFYFS_REAL(name) __real_ ## name

/* declare anything that will be used externally */
#define UNIFYFS_DECL(name, ret, args)  \
    extern gotcha_wrappee_handle_t wrappee_handle_ ## name; \
    extern ret (*__real_ ## name) args; \
    ret __wrap_ ## name args

/* ask gotcha for the address of the real function */
#define MAP_OR_FAIL(name) \
do { \
    if (NULL == __real_ ## name) { \
        __real_ ## name = gotcha_get_wrappee(wrappee_handle_ ## name); \
        if (NULL == __real_ ## name) { \
            assert(!"missing Gotcha wrappee for " #name); \
        } \
    } \
} while (0)

int setup_gotcha_wrappers(void);

#elif UNIFYFS_PRELOAD

/* ===================================================================
 * Using LD_PRELOAD to intercept
 * ===================================================================
 * we need to use the same function names the application is calling,
 * and we then invoke the real library function after looking it up with
 * dlsym */

/* we need the dlsym function */
#include <dlfcn.h>

/* define a static variable called __real_open to record address of
 * real open call and initialize it to NULL */
#define UNIFYFS_DECL(name,ret,args) \
      static ret (*__real_ ## name)args = NULL;

/* our open wrapper assumes the name of open() */
#define UNIFYFS_WRAP(name) name

/* the address of the real open call is stored in __real_open variable */
#define UNIFYFS_REAL(name) __real_ ## name

/* if __real_open is still NULL, call dlsym to lookup address of real
 * function and record it */
#define MAP_OR_FAIL(func) \
        if (!(__real_ ## func)) \
        { \
            __real_ ## func = dlsym(RTLD_NEXT, #func); \
            if (!(__real_ ## func)) { \
               fprintf(stderr, "UNIFYFS failed to map symbol: %s\n", #func); \
               exit(1); \
           } \
        }
#else

/* ===================================================================
 * Using ld -wrap option to intercept
 * ===================================================================
 * the linker will convert application calls from open --> __wrap_open,
 * so we define all of our functions as the __wrap variant and then
 * to call the real library, we call __real_open */

/* we don't need a variable to record the address of the real function,
 * just declare the existence of __real_open so the compiler knows the
 * prototype of this function (linker will provide it), also need to
 * declare prototype for __wrap_open */
#define UNIFYFS_DECL(name,ret,args) \
      extern ret __real_ ## name args;  \
      ret __wrap_ ## name args;

/* we define our wrapper function as __wrap_open instead of open */
#define UNIFYFS_WRAP(name) __wrap_ ## name

/* the linker maps the open call to __real_open() */
#define UNIFYFS_REAL(name) __real_ ## name

/* no need to look up the address of the real function */
#define MAP_OR_FAIL(func)

#endif

/* structure to represent file descriptors */
typedef struct {
    int   fid;   /* local file id associated with fd */
    off_t pos;   /* current file pointer */
    int   read;  /* whether file is opened for read */
    int   write; /* whether file is opened for write */
    int   append; /* whether file is opened for append */
} unifyfs_fd_t;

enum unifyfs_stream_orientation {
    UNIFYFS_STREAM_ORIENTATION_NULL = 0,
    UNIFYFS_STREAM_ORIENTATION_BYTE,
    UNIFYFS_STREAM_ORIENTATION_WIDE,
};

/* structure to represent FILE* streams */
typedef struct {
    int    sid;      /* index within unifyfs_streams */
    int    err;      /* stream error indicator flag */
    int    eof;      /* stream end-of-file indicator flag */
    int    fd;       /* file descriptor associated with stream */
    int    append;   /* whether file is opened in append mode */
    int    orient;   /* stream orientation, UNIFYFS_STREAM_ORIENTATION_{NULL,BYTE,WIDE} */

    void*  buf;      /* pointer to buffer */
    int    buffree;  /* whether we need to free buffer */
    int    buftype;  /* _IOFBF fully buffered, _IOLBF line buffered, _IONBF unbuffered */
    size_t bufsize;  /* size of buffer in bytes */
    off_t  bufpos;   /* byte offset in file corresponding to start of buffer */
    size_t buflen;   /* number of bytes active in buffer */
    size_t bufdirty; /* whether data in buffer needs to be flushed */

    unsigned char* ubuf; /* ungetc buffer (we store bytes from end) */
    size_t ubufsize;     /* size of ungetc buffer in bytes */
    size_t ubuflen;      /* number of active bytes in buffer */

    unsigned char* _p; /* pointer to character in buffer */
    size_t         _r; /* number of bytes left at pointer */
} unifyfs_stream_t;

/* structure to represent DIR* streams */
typedef struct {
    int dirid; /* index within unifyfs_dirstreams */
    int fid;   /* local file id of directory for this stream */
    int fd;    /* file descriptor associated with stream */
    off_t pos; /* position within directory stream */
} unifyfs_dirstream_t;

enum flock_enum {
    UNLOCKED,
    EX_LOCKED,
    SH_LOCKED
};

enum {FILE_STORAGE_NULL = 0, FILE_STORAGE_LOGIO};


typedef struct {
    off_t global_size;            /* Global size of the file */
    pthread_spinlock_t fspinlock; /* file lock variable */
    enum flock_enum flock_status; /* file lock status */

    int storage;                  /* FILE_STORAGE type */

    int fid;                      /* local file index in filemetas array */
    int gfid;                     /* global file id for this file */
    int needs_sync;               /* have unsynced writes */

    int is_laminated;             /* Is this file laminated */
    uint32_t mode;                /* st_mode bits.  This has file
                                   * permission info and will tell you if this
                                   * is a regular file or directory. */
    struct seg_tree extents_sync; /* Segment tree containing our coalesced
                                   * writes between sync operations */
    struct seg_tree extents;      /* Segment tree of all local data extents */
} unifyfs_filemeta_t;

/* struct used to map a full path to its local file id,
 * an array of these is kept and a simple linear search
 * is used to find a match */
typedef struct {
    /* flag incidating whether slot is in use */
    int in_use;

    /* full path and name of file */
    const char filename[UNIFYFS_MAX_FILENAME];
} unifyfs_filename_t;

/*unifyfs structures*/

/* This structure defines a client read request for a file.
 * It is initialized by the client describing the global file id,
 * offset, and length to be read and provides a pointer to
 * the user buffer where the data should be placed.  The
 * server sets the errcode field to UNIFYFS_SUCCESS if the read
 * succeeds and otherwise records an error code pertaining to
 * why the read failed.  The server records the number of bytes
 * read in the nread field, which the client can use to detect
 * short read operations. */
typedef struct {
    int gfid;      /* global file id to be read */
    int errcode;   /* error code for read operation if any */
    size_t offset; /* logical offset in file to read from */
    size_t length; /* number of bytes to read */
    size_t nread;  /* number of bytes actually read */
    char* buf;     /* pointer to user buffer to place data */
    struct aiocb* aiocbp; /* the original request from application */
} read_req_t;

typedef struct {
    size_t* ptr_num_entries;
    unifyfs_index_t* index_entry;
} unifyfs_index_buf_t;

extern unifyfs_index_buf_t unifyfs_indices;
extern unsigned long unifyfs_max_index_entries;

/* shmem context for read-request replies data region */
extern shm_context* shm_recv_ctx;

/* log-based I/O context */
extern logio_context* logio_ctx;

extern int unifyfs_app_id;
extern int unifyfs_client_id;

/* -------------------------------
 * Global varaible declarations
 * ------------------------------- */

/*definition for unifyfs*/
#define UNIFYFS_CLI_TIME_OUT 5000

typedef enum {
    ACK_SUCCESS,
    ACK_FAIL,
} ack_status_t;

/* keep track of what we've initialized */
extern int unifyfs_initialized;

/* list of file name structures of fixed length,
 * used to map a full path to its local file id,
 * an array of these is kept and a simple linear search
 * is used to find a match */
extern unifyfs_filename_t* unifyfs_filelist;

/* mount directory */
extern char*  unifyfs_mount_prefix;
extern size_t unifyfs_mount_prefixlen;

/* tracks current working directory within unifyfs directory namespace */
extern char* unifyfs_cwd;

/* array of file descriptors */
extern unifyfs_fd_t unifyfs_fds[UNIFYFS_MAX_FILEDESCS];
extern rlim_t unifyfs_fd_limit;

/* array of file streams */
extern unifyfs_stream_t unifyfs_streams[UNIFYFS_MAX_FILEDESCS];

/* array of directory streams */
extern unifyfs_dirstream_t unifyfs_dirstreams[UNIFYFS_MAX_FILEDESCS];

/* stack of free file descriptor values,
 * each is an index into unifyfs_fds array */
extern void* unifyfs_fd_stack;

/* stack of free streams,
 * each is an index into unifyfs_streams array */
extern void* unifyfs_stream_stack;

/* stack of directory streams,
 * each is an index into unifyfs_dirstreams array */
extern void* unifyfs_dirstream_stack;

/* mutex to lock stack operations */
extern pthread_mutex_t unifyfs_stack_mutex;

extern int    unifyfs_max_files;  /* maximum number of files to store */
extern bool   unifyfs_local_extents;  /* enable tracking of local extents */

/* -------------------------------
 * Common functions
 * ------------------------------- */

/* single function to route all unsupported wrapper calls through */
int unifyfs_unsupported(const char* fn_name, const char* file, int line,
                        const char* fmt, ...);

/* returns 1 if two input parameters will overflow their type when
 * added together */
int unifyfs_would_overflow_offt(off_t a, off_t b);

/* returns 1 if two input parameters will overflow their type when
 * added together */
int unifyfs_would_overflow_long(long a, long b);

int unifyfs_stack_lock(void);

int unifyfs_stack_unlock(void);

/* sets flag if the path should be intercept as a unifyfs path,
 * and if so, writes normalized path in upath, which should
 * be a buffer of size UNIFYFS_MAX_FILENAME */
int unifyfs_intercept_path(const char* path, char* upath);

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
int unifyfs_intercept_fd(int* fd);

/* given a FILE*, returns 1 if we should intercept this file,
 * 0 otherwise */
int unifyfs_intercept_stream(FILE* stream);

/* given a DIR*, returns 1 if we should intercept this directory,
 * 0 otherwise */
int unifyfs_intercept_dirstream(DIR* dirp);

/* given a path, return the file id */
int unifyfs_get_fid_from_path(const char* path);

/* given a file descriptor, return the file id */
int unifyfs_get_fid_from_fd(int fd);

/* initialze file descriptor structure corresponding to fd value */
int unifyfs_fd_init(int fd);

/* initialze file stream structure corresponding to id value */
int unifyfs_stream_init(int sid);

/* initialze directory stream descriptor structure
 * corresponding to id value */
int unifyfs_dirstream_init(int dirid);

/* return address of file descriptor structure or NULL if fd is out
 * of range */
unifyfs_fd_t* unifyfs_get_filedesc_from_fd(int fd);

/* given a file id, return a pointer to the meta data,
 * otherwise return NULL */
unifyfs_filemeta_t* unifyfs_get_meta_from_fid(int fid);

/* Return 1 if fid is laminated, 0 if not */
int unifyfs_fid_is_laminated(int fid);

/* Return 1 if fd is laminated, 0 if not */
int unifyfs_fd_is_laminated(int fd);

/* Given a fid, return the path.  */
const char* unifyfs_path_from_fid(int fid);

/* Given a fid, return a gfid */
int unifyfs_gfid_from_fid(const int fid);

/* returns fid for corresponding gfid, if one is active,
 * returns -1 otherwise */
int unifyfs_fid_from_gfid(const int gfid);

/* checks to see if fid is a directory
 * returns 1 for yes
 * returns 0 for no */
int unifyfs_fid_is_dir(int fid);

/* checks to see if a directory is empty
 * assumes that check for is_dir has already been made
 * only checks for full path matches, does not check relative paths,
 * e.g. ../dirname will not work
 * returns 1 for yes it is empty
 * returns 0 for no */
int unifyfs_fid_is_dir_empty(const char* path);

/* Return current global size of given file id */
off_t unifyfs_fid_global_size(int fid);

/* if we have a local fid structure corresponding to the gfid
 * in question, we attempt the file lookup with the fid method
 * otherwise call back to the rpc */
off_t unifyfs_gfid_filesize(int gfid);

/*
 * Return current size of given file id.  If the file is laminated, return the
 * global size.  Otherwise, return the local size.
 */
off_t unifyfs_fid_logical_size(int fid);

/* Update local metadata for file from global metadata */
int unifyfs_fid_update_file_meta(int fid, unifyfs_file_attr_t* gfattr);

/* allocate a file id slot for a new file
 * return the fid or -1 on error */
int unifyfs_fid_alloc(void);

/* return the file id back to the free pool */
int unifyfs_fid_free(int fid);

/* add a new file and initialize metadata
 * returns the new fid, or negative value on error */
int unifyfs_fid_create_file(const char* path);

/* add a new directory and initialize metadata
 * returns the new fid, or a negative value on error */
int unifyfs_fid_create_directory(const char* path);

/* write count bytes from buf into file starting at offset pos */
int unifyfs_fid_write(
    int fid,         /* local file id to write to */
    off_t pos,       /* starting offset within file */
    const void* buf, /* buffer of data to be written */
    size_t count,    /* number of bytes to write */
    size_t* nwritten /* returns number of bytes written */
);

/* truncate file id to given length, frees resources if length is
 * less than size and allocates and zero-fills new bytes if length
 * is more than size */
int unifyfs_fid_truncate(int fid, off_t length);

/* sync data for file id to server if needed */
int unifyfs_fid_sync(int fid);

/* opens a new file id with specified path, access flags, and permissions,
 * fills outfid with file id and outpos with position for current file pointer,
 * returns UNIFYFS error code */
int unifyfs_fid_open(const char* path, int flags, mode_t mode, int* outfid,
                     off_t* outpos);

int unifyfs_fid_close(int fid);

/* delete a file id and return file its resources to free pools */
int unifyfs_fid_unlink(int fid);


/* functions used in UnifyFS */

/* issue a set of read requests */
int unifyfs_gfid_read_reqs(read_req_t* in_reqs, int in_count);

int unifyfs_set_global_file_meta_from_fid(int fid,
                                          unifyfs_file_attr_op_e op);

int unifyfs_set_global_file_meta(int gfid,
                                 unifyfs_file_attr_op_e op,
                                 unifyfs_file_attr_t* gfattr);

int unifyfs_get_global_file_meta(int gfid,
                                 unifyfs_file_attr_t* gfattr);

// These require types/structures defined above
#include "unifyfs-fixed.h"
#include "unifyfs-stdio.h"
#include "unifyfs-sysio.h"
#include "unifyfs-dirops.h"

#endif /* UNIFYFS_INTERNAL_H */
