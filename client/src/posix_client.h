/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef UNIFYFS_POSIX_CLIENT_H
#define UNIFYFS_POSIX_CLIENT_H

#include "unifyfs.h"
#include "unifyfs-internal.h"
#include "unifyfs_api_internal.h"

/* ----------------------------------------
 * Structure and enumeration declarations
 * ---------------------------------------- */

/* structure to represent file descriptors */
typedef struct {
    off_t pos;       /* current file pointer */
    int   read;      /* whether file is opened for read */
    int   write;     /* whether file is opened for write */
    int   append;    /* whether file is opened for append */
    int   fid;       /* local file id associated with fd */
    int   use_count; /* how many exposed fds refer to this */
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
    int    orient;   /* stream orientation {NULL, BYTE, WIDE} */

    void*  buf;      /* pointer to buffer */
    int    buffree;  /* whether we need to free buffer */
    int    buftype;  /* fully buffered, line buffered, or unbuffered */
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


/* client transfer (stage-in/out) support */

#define UNIFYFS_TRANSFER_BUF_SIZE (8 * MIB)

enum {
    UNIFYFS_TRANSFER_DIRECTION_OUT = 0,
    UNIFYFS_TRANSFER_DIRECTION_IN = 1
};


/* -------------------------------
 * Global variable declarations
 * ------------------------------- */

extern unifyfs_client* posix_client;

extern int global_rank_cnt; /* count of world ranks */
extern int client_rank;     /* client-provided rank (for debugging) */

/* keep track of whether we have initialized & mounted */
extern char*  unifyfs_mount_prefix;
extern size_t unifyfs_mount_prefixlen;
extern int unifyfs_initialized;
extern int unifyfs_mount_id; /* gfid of mountpoint */

/* whether we can use fgetpos/fsetpos */
extern int unifyfs_fpos_enabled;

/* arrays of file descriptors */
extern int unifyfs_dup_fds[UNIFYFS_CLIENT_MAX_FILES];
extern unifyfs_fd_t unifyfs_fds[UNIFYFS_CLIENT_MAX_FILES];
extern rlim_t unifyfs_fd_limit;

/* array of file streams */
extern unifyfs_stream_t unifyfs_streams[UNIFYFS_CLIENT_MAX_FILES];

/* array of directory streams */
extern unifyfs_dirstream_t unifyfs_dirstreams[UNIFYFS_CLIENT_MAX_FILES];

/* stack of free file descriptor values,
 * each is an index into unifyfs_fds array */
extern void* posix_fd_stack;

/* stack of free streams,
 * each is an index into unifyfs_streams array */
extern void* posix_stream_stack;

/* stack of directory streams,
 * each is an index into unifyfs_dirstreams array */
extern void* posix_dirstream_stack;

/* mutex to lock stack operations */
extern pthread_mutex_t posix_stack_mutex;

/* -------------------------------
 * Initialization and finalizatio
 * ------------------------------- */

/* Initialize UnifyFS as a POSIX client */
int posix_client_init(void);

/* Finalize POSIX client */
int posix_client_fini(void);


/* -------------------------------
 * Common functions
 * ------------------------------- */

int posix_stack_lock(void);

int posix_stack_unlock(void);

/* returns 1 if two input parameters will overflow their type when
 * added together */
int unifyfs_would_overflow_offt(off_t a, off_t b);

/* returns 1 if two input parameters will overflow their type when
 * added together */
int unifyfs_would_overflow_long(long a, long b);

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

/* given a file descriptor, return the file id */
int unifyfs_get_fid_from_fd(int fd);

/* initialze file descriptor structure corresponding to fd value */
int unifyfs_fd_init(int fd);

/* initialize unifyfs_dup_fds[] entry corresponding to fd value */
int unifyfs_dup_fd_init(int fd);

/* initialze file stream structure corresponding to id value */
int unifyfs_stream_init(int sid);

/* initialze directory stream descriptor structure
 * corresponding to id value */
int unifyfs_dirstream_init(int dirid);

/* return address of file descriptor structure or NULL if fd is out
 * of range */
unifyfs_fd_t* unifyfs_get_filedesc_from_fd(int fd);

/* Return 1 if fd is laminated, 0 if not */
int unifyfs_fd_is_laminated(int fd);

/* transfer src file to dst using a single client */
int transfer_file_serial(const char* src,
                         const char* dst,
                         struct stat* sb_src,
                         int direction);

/* transfer src file to dst using all clients */
int transfer_file_parallel(const char* src,
                           const char* dst,
                           struct stat* sb_src,
                           int direction);



#endif /* UNIFYFS_POSIX_CLIENT_H */
