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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
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

#ifndef CRUISE_INTERNAL_H
#define CRUISE_INTERNAL_H

/* this is overkill to include all of these here, but just to get things working... */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <search.h>
#include <assert.h>
#include <libgen.h>
#include <limits.h>

#include "utlist.h"
#include "uthash.h"

/* -------------------------------
 * Defines and types
 * ------------------------------- */

/* TODO: move common includes to another file */
#include "cruise-defs.h"

#ifdef CRUISE_DEBUG
    #define debug(fmt, args... )  printf("%s: "fmt, __func__, ##args)
#else
    #define debug(fmt, args... )
#endif

/* define a macro to capture function name, file name, and line number
 * along with user-defined string */
#define CRUISE_UNSUPPORTED(fmt, args...) \
      cruise_unsupported(__func__, __FILE__, __LINE__, fmt, ##args)

#ifdef CRUISE_PRELOAD

    /* ===================================================================
     * Using LD_PRELOAD to intercept
     * ===================================================================
     * we need to use the same function names the application is calling,
     * and we then invoke the real library function after looking it up with
     * dlsym */

    /* we need the dlsym function */
    #define __USE_GNU
    #include <dlfcn.h>
    #include <stdlib.h>

    /* define a static variable called __real_open to record address of
     * real open call and initialize it to NULL */
    #define CRUISE_DECL(name,ret,args) \
      static ret (*__real_ ## name)args = NULL;

    /* our open wrapper assumes the name of open() */
    #define CRUISE_WRAP(name) name

    /* the address of the real open call is stored in __real_open variable */
    #define CRUISE_REAL(name) __real_ ## name

    /* if __real_open is still NULL, call dlsym to lookup address of real
     * function and record it */
    #define MAP_OR_FAIL(func) \
        if (!(__real_ ## func)) \
        { \
            __real_ ## func = dlsym(RTLD_NEXT, #func); \
            if(!(__real_ ## func)) { \
               fprintf(stderr, "CRUISE failed to map symbol: %s\n", #func); \
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
     * prototype of this function (linker will provide it) */
    #define CRUISE_DECL(name,ret,args) \
      extern ret __real_ ## name args;

    /* we define our wrapper function as __wrap_open instead of open */
    #define CRUISE_WRAP(name) __wrap_ ## name

    /* the linker maps the open call to __real_open() */
    #define CRUISE_REAL(name) __real_ ## name

    /* no need to look up the address of the real function */
    #define MAP_OR_FAIL(func)

#endif

#define CRUISE_SUCCESS     0
#define CRUISE_FAILURE    -1
#define CRUISE_ERR_NOSPC  -2
#define CRUISE_ERR_IO     -3
#define CRUISE_ERR_NAMETOOLONG -4
#define CRUISE_ERR_NOENT  -5
#define CRUISE_ERR_EXIST  -6
#define CRUISE_ERR_NOTDIR -7
#define CRUISE_ERR_NFILE  -8
#define CRUISE_ERR_INVAL  -9
#define CRUISE_ERR_OVERFLOW -10
#define CRUISE_ERR_FBIG   -11
#define CRUISE_ERR_BADF   -12
#define CRUISE_ERR_ISDIR  -13
#define CRUISE_ERR_NOMEM  -14

#ifndef HAVE_OFF64_T
typedef int64_t off64_t;
#endif

/* structure to represent file descriptors */
typedef struct {
    off_t pos;   /* current file pointer */
    int   read;  /* whether file is opened for read */
    int   write; /* whether file is opened for write */
} cruise_fd_t;

enum cruise_stream_orientation {
    CRUISE_STREAM_ORIENTATION_NULL = 0,
    CRUISE_STREAM_ORIENTATION_BYTE,
    CRUISE_STREAM_ORIENTATION_WIDE,
};

/* structure to represent FILE* streams */
typedef struct {
    int    err;      /* stream error indicator flag */
    int    eof;      /* stream end-of-file indicator flag */
    int    fd;       /* file descriptor associated with stream */
    int    append;   /* whether file is opened in append mode */
    int    orient;   /* stream orientation, CRUISE_STREAM_ORIENTATION_{NULL,BYTE,WIDE} */

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
} cruise_stream_t;

enum flock_enum {
    UNLOCKED,
    EX_LOCKED,
    SH_LOCKED
};

/* TODO: make this an enum */
#define FILE_STORAGE_NULL        0
#define FILE_STORAGE_FIXED_CHUNK 1
#define FILE_STORAGE_LOGIO		 2

#define EXTERNAL_DATA_DIR "/l/ssd"
#define EXTERNAL_META_DIR "/l/ssd"

/* TODO: make this an enum */
#define CHUNK_LOCATION_NULL      0
#define CHUNK_LOCATION_MEMFS     1
#define CHUNK_LOCATION_SPILLOVER 2

typedef struct {
    int location; /* CHUNK_LOCATION specifies how chunk is stored */
    off_t id;     /* physical id of chunk in its respective storage */
} cruise_chunkmeta_t;

typedef struct {
    off_t size;                     /* current file size */
		off_t real_size;								/* real size of the file for logio*/ 
    int is_dir;                     /* is this file a directory */
    pthread_spinlock_t fspinlock;   /* file lock variable */
    enum flock_enum flock_status;   /* file lock status */

    int storage;                    /* FILE_STORAGE specifies file data management */

    off_t chunks;                   /* number of chunks allocated to file */
    cruise_chunkmeta_t* chunk_meta; /* meta data for chunks */

} cruise_filemeta_t;

/* path to fid lookup struct */
typedef struct {
    int in_use; /* flag incidating whether slot is in use */
    const char filename[CRUISE_MAX_FILENAME];
                /* full path and name of file */
} cruise_filename_t;

/*burstfs structures*/
typedef struct {
	int fid;
	long offset;
	long length;
	char *buf;

}read_req_t;

typedef struct {
	int src_fid;
	long offset;
	long length;
}shm_meta_t; /*metadata format in the shared memory*/

typedef struct {
	off_t file_pos;
	off_t mem_pos;
	size_t length;
	int fid;
} burstfs_index_t;

typedef struct {
	off_t *ptr_num_entries;
	burstfs_index_t *index_entry;
} burstfs_index_buf_t;

typedef struct {
	int fid;
	int gfid;
	char filename[CRUISE_MAX_FILENAME];
	struct stat file_attr;
}burstfs_fattr_t;

typedef struct {
	off_t *ptr_num_entries;
	burstfs_fattr_t *meta_entry;
} burstfs_fattr_buf_t;

typedef struct {
	burstfs_index_t idxes[BURSTFS_MAX_SPLIT_CNT];
	int count;
} index_set_t;

typedef enum {
	CRUISEFS,
	BURSTFS_LOG,
	BURSTFS_STRIPE,
}fs_type_t;

typedef struct {
	read_req_t read_reqs[BURSTFS_MAX_READ_CNT];
	int count;
}read_req_set_t;

read_req_set_t read_req_set;
read_req_set_t tmp_read_req_set;
index_set_t tmp_index_set;

extern int *local_rank_lst;
extern int local_rank_cnt;
extern int local_rank_idx;
extern int local_del_cnt;
extern int client_sockfd;
extern struct pollfd cmd_fd;
extern long shm_req_size;
extern long shm_recv_size;
extern char *shm_recvbuf;
extern char *shm_reqbuf;
extern fs_type_t fs_type;
extern char cmd_buf[GEN_STR_LEN];
extern char ack_msg[3];
extern burstfs_fattr_buf_t burstfs_fattrs;


extern int glb_superblock_size;
extern int dbg_rank;
extern int app_id;
extern int glb_size;
extern int reqbuf_fd;
extern int recvbuf_fd;
extern int superblock_fd;
extern long burstfs_key_slice_range;

/* -------------------------------
 * Common includes
 * ------------------------------- */

/* TODO: move common includes to another file */
#include "cruise.h"
#include "cruise-stack.h"
#include "cruise-fixed.h"
#include "cruise-sysio.h"
#include "cruise-stdio.h"

/* -------------------------------
 * Global varaible declarations
 * ------------------------------- */

/*definition for burstfs*/
#define BURSTFS_CLI_TIME_OUT 5000

typedef enum{
	COMM_MOUNT,
	COMM_META,
	COMM_READ,
	COMM_UNMOUNT,
	COMM_DIGEST,
	COMM_SYNC_DEL,
}cmd_lst_t;

typedef enum{
	ACK_SUCCESS,
	ACK_FAIL,
}ack_status_t;

/* keep track of what we've initialized */
extern int cruise_initialized;

/* list of file names */
extern cruise_filename_t* cruise_filelist;

/* mount directory */
extern char*  cruise_mount_prefix;
extern size_t cruise_mount_prefixlen;

/* array of file descriptors */
extern cruise_fd_t cruise_fds[CRUISE_MAX_FILEDESCS];
extern rlim_t cruise_fd_limit;

/* array of file streams */
extern cruise_stream_t cruise_streams[CRUISE_MAX_FILEDESCS];

extern int cruise_use_memfs;
extern int cruise_use_spillover;

extern int    cruise_max_files;  /* maximum number of files to store */
extern size_t cruise_chunk_mem;  /* number of bytes in memory to be used for chunk storage */
extern int    cruise_chunk_bits; /* we set chunk size = 2^cruise_chunk_bits */
extern off_t  cruise_chunk_size; /* chunk size in bytes */
extern off_t  cruise_chunk_mask; /* mask applied to logical offset to determine physical offset within chunk */
extern long    cruise_max_chunks; /* maximum number of chunks that fit in memory */

extern void* free_chunk_stack;
extern void* free_spillchunk_stack;
extern char* cruise_chunks;
int cruise_spilloverblock;

/* -------------------------------
 * Common functions
 * ------------------------------- */

/* single function to route all unsupported wrapper calls through */
int cruise_unsupported(const char* fn_name, const char* file, int line, const char* fmt, ...);

/* returns 1 if two input parameters will overflow their type when
 * added together */
int cruise_would_overflow_offt(off_t a, off_t b);

/* returns 1 if two input parameters will overflow their type when
 * added together */
int cruise_would_overflow_long(long a, long b);

/* given an input mode, mask it with umask and return, can specify
 * an input mode==0 to specify all read/write bits */
mode_t cruise_getmode(mode_t perms);

int cruise_stack_lock();

int cruise_stack_unlock();

/* sets flag if the path is a special path */
int cruise_intercept_path(const char* path);

/* given an fd, return 1 if we should intercept this file, 0 otherwise,
 * convert fd to new fd value if needed */
int cruise_intercept_fd(int* fd);

/* given a FILE*, returns 1 if we should intercept this file,
 * 0 otherwise */
int cruise_intercept_stream(FILE* stream);

/* given a path, return the file id */
int cruise_get_fid_from_path(const char* path);

/* given a file descriptor, return the file id */
int cruise_get_fid_from_fd(int fd);

/* return address of file descriptor structure or NULL if fd is out
 * of range */
cruise_fd_t* cruise_get_filedesc_from_fd(int fd);

/* given a file id, return a pointer to the meta data,
 * otherwise return NULL */
inline cruise_filemeta_t* cruise_get_meta_from_fid(int fid);

/* given an CRUISE error code, return corresponding errno code */
int cruise_err_map_to_errno(int rc);

/* checks to see if fid is a directory
 * returns 1 for yes
 * returns 0 for no */
int cruise_fid_is_dir(int fid);

/* checks to see if a directory is empty
 * assumes that check for is_dir has already been made
 * only checks for full path matches, does not check relative paths,
 * e.g. ../dirname will not work
 * returns 1 for yes it is empty
 * returns 0 for no */
int cruise_fid_is_dir_empty(const char * path);

/* return current size of given file id */
off_t cruise_fid_size(int fid);

/* fill in limited amount of stat information */
int cruise_fid_stat(int fid, struct stat* buf);

/* allocate a file id slot for a new file 
 * return the fid or -1 on error */
int cruise_fid_alloc();

/* return the file id back to the free pool */
int cruise_fid_free(int fid);

/* add a new file and initialize metadata
 * returns the new fid, or negative value on error */
int cruise_fid_create_file(const char * path);

/* add a new directory and initialize metadata
 * returns the new fid, or a negative value on error */
int cruise_fid_create_directory(const char * path);

/* read count bytes from file starting from pos and store into buf,
 * all bytes are assumed to exist, so checks on file size should be
 * done before calling this routine */
int cruise_fid_read(int fid, off_t pos, void* buf, size_t count);

/* write count bytes from buf into file starting at offset pos,
 * all bytes are assumed to be allocated to file, so file should
 * be extended before calling this routine */
int cruise_fid_write(int fid, off_t pos, const void* buf, size_t count);

/* given a file id, write zero bytes to region of specified offset
 * and length, assumes space is already reserved */
int cruise_fid_write_zero(int fid, off_t pos, off_t count);

/* increase size of file if length is greater than current size,
 * and allocate additional chunks as needed to reserve space for
 * length bytes */
int cruise_fid_extend(int fid, off_t length);

/* truncate file id to given length, frees resources if length is
 * less than size and allocates and zero-fills new bytes if length
 * is more than size */
int cruise_fid_truncate(int fid, off_t length);

/* opens a new file id with specified path, access flags, and permissions,
 * fills outfid with file id and outpos with position for current file pointer,
 * returns CRUISE error code */
int cruise_fid_open(const char* path, int flags, mode_t mode, int* outfid, off_t* outpos);

int cruise_fid_close(int fid);

/* delete a file id and return file its resources to free pools */
int cruise_fid_unlink(int fid);


/*functions used in BurstFS*/
int burstfs_split_index(burstfs_index_t *cur_idx, index_set_t *index_set,\
		long slice_range);
int burstfs_split_read_requests(read_req_t *cur_read_req, read_req_set_t *read_req_set,\
		long slice_range);
int burstfs_coalesce_read_reqs(read_req_t *read_req, int count,\
		 read_req_set_t *tmp_read_req_set, long burstfs_key_slice_range,\
		 	 read_req_set_t *read_req_set);
int burstfs_match_received_ack(read_req_t *read_req, int count,\
		read_req_t *match_req);
int burstfs_locate_req(read_req_t *read_req, int count,\
		read_req_t *match_req);
#endif /* CRUISE_INTERNAL_H */
