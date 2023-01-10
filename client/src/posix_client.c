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

#include "posix_client.h"
#include "unifyfs_fid.h"
#include "unifyfs_wrap.h"
#include "unifyfs-sysio.h"

#ifdef USE_SPATH
#include <spath.h>
#endif

/* -------------------------------
 * Global variable declarations
 * ------------------------------- */

unifyfs_client* posix_client; // = NULL

int global_rank_cnt; /* count of world ranks */
int client_rank;     /* client-provided rank (for debugging) */

/* avoid duplicate mounts (for now) */
int unifyfs_mount_id = -1;

/* have we initialized? */
int unifyfs_initialized; // = 0

/* whether we can use fgetpos/fsetpos */
int unifyfs_fpos_enabled = 1;

/* array of file descriptors */
unifyfs_fd_t unifyfs_fds[UNIFYFS_CLIENT_MAX_FILES];
rlim_t unifyfs_fd_limit;

/* dup/dup2() support - given a regular fd with a value up to
 * UNIFYFS_CLIENT_MAX_FILES, the array holds the index of the
 * corresponding entry in unifyfs_fds[] */
int unifyfs_dup_fds[UNIFYFS_CLIENT_MAX_FILES];

/* array of file streams */
unifyfs_stream_t unifyfs_streams[UNIFYFS_CLIENT_MAX_FILES];

/*
 * TODO: the number of open directories clearly won't exceed the number of
 * file descriptors. however, the current MAX_FILES value of 128 will
 * quickly run out. if this value is fixed to be reasonably larger, then we
 * would need a way to dynamically allocate the dirstreams instead of the
 * following fixed size array.
 */

/* array of DIR* streams to be used */
unifyfs_dirstream_t unifyfs_dirstreams[UNIFYFS_CLIENT_MAX_FILES];

/* stack to track free file descriptor values,
 * each is an index into unifyfs_fds array */
void* posix_fd_stack;

/* stack to track free file streams,
 * each is an index into unifyfs_streams array */
void* posix_stream_stack;

/* stack to track free directory streams,
 * each is an index into unifyfs_dirstreams array */
void* posix_dirstream_stack;

/* mutex to lock stack operations */
pthread_mutex_t posix_stack_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------
 * Static variable declarations
 * ------------------------------- */

static int use_single_shm; // = 0

static off_t unifyfs_max_offt;
static off_t unifyfs_min_offt;
static off_t unifyfs_max_long;
static off_t unifyfs_min_long;


/* -------------------------------
 * Utility functions
 * ------------------------------- */

/* single function to route all unsupported wrapper calls through */
void unifyfs_vunsupported(const char* fn_name,
                          const char* file,
                          int line,
                          const char* fmt,
                          va_list args)
{
    /* print a message about where in the UNIFYFS code we are */
    printf("*** UnifyFS WARNING *** UNSUPPORTED I/O FUNCTION: "
           "%s() at %s:%d: ", fn_name, file, line);

    /* print string with more info about call, e.g., param values */
    va_list args2;
    va_copy(args2, args);
    vprintf(fmt, args2);
    va_end(args2);

    /* TODO: should we provide a config option to abort in this case?*/
}

void unifyfs_unsupported(const char* fn_name,
                         const char* file,
                         int line,
                         const char* fmt,
                         ...)
{
    /* print string with more info about call, e.g., param values */
    va_list args;
    va_start(args, fmt);
    unifyfs_vunsupported(fn_name, file, line, fmt, args);
    va_end(args);
}

/* returns 1 if two input parameters will overflow their type when
 * added together */
int unifyfs_would_overflow_offt(off_t a, off_t b)
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
int unifyfs_would_overflow_long(long a, long b)
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
int posix_stack_lock(void)
{
    if (use_single_shm) {
        return pthread_mutex_lock(&posix_stack_mutex);
    }
    return 0;
}

/* unlock access to shared data structures in superblock */
int posix_stack_unlock(void)
{
    if (use_single_shm) {
        return pthread_mutex_unlock(&posix_stack_mutex);
    }
    return 0;
}

void unifyfs_normalize_path(const char* path, char* normalized)
{
    /* if we have a relative path, prepend the current working directory */
    if ((path[0] != '/') && (posix_client->cwd != NULL)) {
        /* got a relative path, add our cwd */
        snprintf(normalized, UNIFYFS_MAX_FILENAME, "%s/%s",
                 posix_client->cwd, path);
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

/* Given a path, which may relative or absolute,
 * return 1 if we should intercept the path, 0 otherwise.
 * If path is to be intercepted, returned a normalized version in upath. */
int unifyfs_intercept_path(const char* path, char* upath)
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
int unifyfs_intercept_fd(int* fd)
{
    int newfd;
    int oldfd = *fd;

    /* don't intercept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    if (oldfd < 0) {
        /* this is an invalid fd, so we should not intercept it */
        return 0;
    } else if (oldfd < unifyfs_fd_limit) {
        /* check if it has a valid entry in unifyfs_dup_fds[] */
        if (oldfd < UNIFYFS_CLIENT_MAX_FILES) {
            newfd = unifyfs_dup_fds[oldfd];
            if (-1 != newfd) {
                *fd = newfd;
                LOGDBG("Changing fd from exposed dup2=%d to internal %d",
                       oldfd, newfd);
                return 1;
            }
        }

        /* this fd is a real system fd, so leave it as is */
        return 0;
    } else { /* >= unifyfs_fd_limit */
        /* this is an fd we generated and returned to the user,
         * so intercept the call and shift the fd */
        newfd = oldfd - unifyfs_fd_limit;
        *fd = newfd;
        LOGDBG("Changing fd from exposed %d to internal %d", oldfd, newfd);
        return 1;
    }
}

/* given a file stream, return 1 if we should intercept this file,
 * 0 otherwise */
int unifyfs_intercept_stream(FILE* stream)
{
    /* don't intercept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * file stream array */
    unifyfs_stream_t* ptr   = (unifyfs_stream_t*) stream;
    unifyfs_stream_t* start = unifyfs_streams;
    unifyfs_stream_t* end   = start + UNIFYFS_CLIENT_MAX_FILES;
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* given an directory stream, return 1 if we should intercept this
 * direcotry, 0 otherwise */
int unifyfs_intercept_dirstream(DIR* dirp)
{
    /* don't intercept anything until we're initialized */
    if (!unifyfs_initialized) {
        return 0;
    }

    /* check whether this pointer lies within range of our
     * directory stream array */

    unifyfs_dirstream_t* ptr   = (unifyfs_dirstream_t*) dirp;
    unifyfs_dirstream_t* start = unifyfs_dirstreams;
    unifyfs_dirstream_t* end   = start + UNIFYFS_CLIENT_MAX_FILES;
    if (ptr >= start && ptr < end) {
        return 1;
    }

    return 0;
}

/* initialize file descriptor structure for given fd value */
int unifyfs_fd_init(int fd)
{
    /* get pointer to file descriptor struct for this fd value */
    unifyfs_fd_t* filedesc = &(unifyfs_fds[fd]);

    /* set fid to -1 to indicate fd is not active,
     * set file position to max value,
     * disable read and write flags */
    filedesc->fid = -1;
    filedesc->pos = (off_t) -1;
    filedesc->read = 0;
    filedesc->write = 0;
    filedesc->use_count = 0;

    return UNIFYFS_SUCCESS;
}

/* initialize duped internal fd for given array index */
int unifyfs_dup_fd_init(int fd)
{
    unifyfs_dup_fds[fd] = -1;
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
int unifyfs_get_fid_from_fd(int fd)
{
    /* check that file descriptor is within range */
    if (fd < 0 || fd >= UNIFYFS_CLIENT_MAX_FILES) {
        return -1;
    }

    /* get local file id that file descriptor is assocated with,
     * will be -1 if not active */
    int fid = unifyfs_fds[fd].fid;
    return fid;
}

/* return address of file descriptor structure or NULL if fd is out
 * of range */
unifyfs_fd_t* unifyfs_get_filedesc_from_fd(int fd)
{
    if (fd >= 0 && fd < UNIFYFS_CLIENT_MAX_FILES) {
        unifyfs_fd_t* filedesc = &(unifyfs_fds[fd]);
        return filedesc;
    }
    return NULL;
}


/* given a file descriptor, return 1 if file is laminated,
 * and 0 otherwise */
int unifyfs_fd_is_laminated(int fd)
{
    int fid = unifyfs_get_fid_from_fd(fd);
    int laminated = unifyfs_fid_is_laminated(posix_client, fid);
    return laminated;
}



/* -------------------------------
 * POSIX client management
 * ------------------------------- */

int posix_client_init(void)
{
    int rc;
    int i;

    if (!unifyfs_initialized) {

        unifyfs_handle fshdl;
        rc = unifyfs_initialize(unifyfs_mount_prefix, NULL, 0, &fshdl);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("client initialization failed - %s",
                   unifyfs_rc_enum_description(rc));
            return rc;
        }
        posix_client = (unifyfs_client*) fshdl;
        assert(NULL != posix_client);

        posix_client->state.app_rank = client_rank;

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

        /* compute min and max off_t values */
        unsigned long long bits = sizeof(off_t) * 8;
        unifyfs_max_offt = (off_t)((1ULL << (bits - 1ULL)) - 1ULL);
        unifyfs_min_offt = (off_t)(-(1ULL << (bits - 1ULL)));

        /* compute min and max long values */
        unifyfs_max_long = LONG_MAX;
        unifyfs_min_long = LONG_MIN;

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
        int num_fds = UNIFYFS_CLIENT_MAX_FILES;
        for (i = 0; i < num_fds; i++) {
            unifyfs_fd_init(i);
            unifyfs_dup_fd_init(i);
        }

        /* initialize file stream structures */
        int num_streams = UNIFYFS_CLIENT_MAX_FILES;
        for (i = 0; i < num_streams; i++) {
            unifyfs_stream_init(i);
        }

        /* initialize directory stream structures */
        int num_dirstreams = UNIFYFS_CLIENT_MAX_FILES;
        for (i = 0; i < num_dirstreams; i++) {
            unifyfs_dirstream_init(i);
        }

        /* initialize stack of free fd values */
        size_t free_fd_size = unifyfs_stack_bytes(num_fds);
        posix_fd_stack = malloc(free_fd_size);
        unifyfs_stack_init(posix_fd_stack, num_fds);

        /* initialize stack of free stream values */
        size_t free_stream_size = unifyfs_stack_bytes(num_streams);
        posix_stream_stack = malloc(free_stream_size);
        unifyfs_stack_init(posix_stream_stack, num_streams);

        /* initialize stack of free directory stream values */
        size_t free_dirstream_size = unifyfs_stack_bytes(num_dirstreams);
        posix_dirstream_stack = malloc(free_dirstream_size);
        unifyfs_stack_init(posix_dirstream_stack, num_dirstreams);

        /* remember that we've now initialized the library */
        unifyfs_initialized = 1;
        unifyfs_mount_id = posix_client->state.app_id;
    }

    return UNIFYFS_SUCCESS;
}

/* free resources allocated during posix_client_init().
 * generally, we do this in reverse order with respect to
 * how things were initialized */
int posix_client_fini(void)
{
    int rc = UNIFYFS_SUCCESS;

    if (!unifyfs_initialized) {
        /* not initialized yet, so we shouldn't call finalize */
        return UNIFYFS_FAILURE;
    }

    unifyfs_handle fshdl = (unifyfs_handle) posix_client;
    rc = unifyfs_finalize(fshdl);
    if (UNIFYFS_SUCCESS != rc) {
        return rc;
    }

    /* free directory stream stack */
    if (posix_dirstream_stack != NULL) {
        free(posix_dirstream_stack);
        posix_dirstream_stack = NULL;
    }

    /* free file stream stack */
    if (posix_stream_stack != NULL) {
        free(posix_stream_stack);
        posix_stream_stack = NULL;
    }

    /* free file descriptor stack */
    if (posix_fd_stack != NULL) {
        free(posix_fd_stack);
        posix_fd_stack = NULL;
    }

    /* no longer initialized, so update the flag */
    unifyfs_initialized = 0;
    unifyfs_mount_id = -1;
    posix_client = NULL;

    return rc;
}

static
int do_transfer_data(int fd_src,
                     int fd_dst,
                     off_t offset,
                     size_t count)
{
    int ret = UNIFYFS_SUCCESS;
    int err;
    off_t pos = 0;
    ssize_t n_written = 0;
    ssize_t n_read = 0;
    ssize_t n_processed = 0;
    size_t len = UNIFYFS_TRANSFER_BUF_SIZE;
    char* buf = NULL;

    buf = malloc(UNIFYFS_TRANSFER_BUF_SIZE);
    if (NULL == buf) {
        LOGERR("failed to allocate transfer buffer");
        return ENOMEM;
    }

    errno = 0;
    pos = UNIFYFS_WRAP(lseek)(fd_src, offset, SEEK_SET);
    err = errno;
    if (pos == (off_t) -1) {
        LOGERR("lseek failed (%d: %s)\n", err, strerror(err));
        ret = err;
        goto out;
    }

    errno = 0;
    pos = UNIFYFS_WRAP(lseek)(fd_dst, offset, SEEK_SET);
    err = errno;
    if (pos == (off_t) -1) {
        LOGERR("lseek failed (%d: %s)\n", err, strerror(err));
        ret = err;
        goto out;
    }

    while (count > n_processed) {
        if (len > count) {
            len = count;
        }

        errno = 0;
        n_read = UNIFYFS_WRAP(read)(fd_src, buf, len);
        err = errno;
        if (n_read == 0) {  /* EOF */
            break;
        } else if (n_read < 0) {   /* error */
            ret = err;
            goto out;
        }

        do {
            errno = 0;
            n_written = UNIFYFS_WRAP(write)(fd_dst, buf, n_read);
            err = errno;
            if (n_written < 0) {
                ret = err;
                goto out;
            } else if ((n_written == 0) && err && (err != EAGAIN)) {
                ret = err;
                goto out;
            }

            n_read -= n_written;
            n_processed += n_written;
        } while (n_read > 0);
    }

out:
    if (NULL != buf) {
        free(buf);
    }

    return ret;
}

int transfer_file_serial(const char* src,
                         const char* dst,
                         struct stat* sb_src,
                         int direction)
{
    /* NOTE: we currently do not use the @direction */

    int err;
    int ret = UNIFYFS_SUCCESS;
    int fd_src = 0;
    int fd_dst = 0;

    errno = 0;
    fd_src = UNIFYFS_WRAP(open)(src, O_RDONLY);
    err = errno;
    if (fd_src < 0) {
        LOGERR("failed to open() source file %s", src);
        return err;
    }

    errno = 0;
    fd_dst = UNIFYFS_WRAP(open)(dst, O_WRONLY);
    err = errno;
    if (fd_dst < 0) {
        LOGERR("failed to open() destination file %s", dst);
        close(fd_src);
        return err;
    }

    LOGDBG("serial transfer (rank=%d of %d): length=%zu",
           client_rank, global_rank_cnt, sb_src->st_size);

    ret = do_transfer_data(fd_src, fd_dst, 0, sb_src->st_size);
    if (UNIFYFS_SUCCESS != ret) {
        LOGERR("failed to transfer data (ret=%d, %s)",
               ret, unifyfs_rc_enum_description(ret));
    } else {
        UNIFYFS_WRAP(fsync)(fd_dst);
    }

    UNIFYFS_WRAP(close)(fd_dst);
    UNIFYFS_WRAP(close)(fd_src);

    return ret;
}

int transfer_file_parallel(const char* src,
                           const char* dst,
                           struct stat* sb_src,
                           int direction)
{
    /* NOTE: we currently do not use the @direction */

    int err;
    int ret = UNIFYFS_SUCCESS;
    int fd_src = 0;
    int fd_dst = 0;
    uint64_t total_chunks = 0;
    uint64_t chunk_start = 0;
    uint64_t n_chunks_remainder = 0;
    uint64_t n_chunks_per_rank = 0;
    uint64_t offset = 0;
    uint64_t len = 0;
    uint64_t size = sb_src->st_size;
    uint64_t last_chunk_size = 0;

    /* calculate total number of chunk transfers */
    total_chunks = size / UNIFYFS_TRANSFER_BUF_SIZE;
    last_chunk_size = size % UNIFYFS_TRANSFER_BUF_SIZE;
    if (last_chunk_size) {
        total_chunks++;
    }

    /* calculate chunks per rank */
    n_chunks_per_rank = total_chunks / global_rank_cnt;
    n_chunks_remainder = total_chunks % global_rank_cnt;

    /*
     * if the file is smaller than (rank_count * transfer_size), just
     * use the serial mode.
     *
     * FIXME: is this assumption fair even for the large rank count?
     */
    if (total_chunks <= (uint64_t)global_rank_cnt) {
        if (client_rank == 0) {
            LOGDBG("using serial transfer for small file");
            ret = transfer_file_serial(src, dst, sb_src, direction);
            if (ret) {
                LOGERR("transfer_file_serial() failed");
            }
        } else {
            ret = UNIFYFS_SUCCESS;
        }
        return ret;
    }

    errno = 0;
    fd_src = UNIFYFS_WRAP(open)(src, O_RDONLY);
    err = errno;
    if (fd_src < 0) {
        LOGERR("failed to open() source file %s", src);
        return err;
    }

    errno = 0;
    fd_dst = UNIFYFS_WRAP(open)(dst, O_WRONLY);
    err = errno;
    if (fd_dst < 0) {
        LOGERR("failed to open() destination file %s", dst);
        UNIFYFS_WRAP(close)(fd_src);
        return err;
    }

    chunk_start = n_chunks_per_rank * client_rank;
    offset = chunk_start * UNIFYFS_TRANSFER_BUF_SIZE;
    len = n_chunks_per_rank * UNIFYFS_TRANSFER_BUF_SIZE;

    LOGDBG("parallel transfer (rank=%d of %d): "
           "#chunks=%zu, offset=%zu, length=%zu",
           client_rank, global_rank_cnt,
           (size_t)n_chunks_per_rank, (size_t)offset, (size_t)len);

    ret = do_transfer_data(fd_src, fd_dst, (off_t)offset, (size_t)len);
    if (ret) {
        LOGERR("failed to transfer data (ret=%d, %s)",
               ret, unifyfs_rc_enum_description(ret));
    } else {
        if (n_chunks_remainder && (client_rank < n_chunks_remainder)) {
            /* do single chunk transfer per rank of remainder portion */
            len = UNIFYFS_TRANSFER_BUF_SIZE;
            if (last_chunk_size && (client_rank == (n_chunks_remainder - 1))) {
                len = last_chunk_size;
            }
            chunk_start = (total_chunks - n_chunks_remainder) + client_rank;
            offset = chunk_start * UNIFYFS_TRANSFER_BUF_SIZE;

            LOGDBG("parallel transfer (rank=%d of %d): "
                   "#chunks=1, offset=%zu, length=%zu",
                   client_rank, global_rank_cnt,
                   (size_t)offset, (size_t)len);
            ret = do_transfer_data(fd_src, fd_dst, (off_t)offset, (size_t)len);
            if (ret) {
                LOGERR("failed to transfer data (ret=%d, %s)",
                       ret, unifyfs_rc_enum_description(ret));
            }
        }
        fsync(fd_dst);
    }

    UNIFYFS_WRAP(close)(fd_dst);
    UNIFYFS_WRAP(close)(fd_src);

    return ret;
}

int unifyfs_transfer_file(const char* src,
                          const char* dst,
                          int mode)
{
    int rc, err;
    int ret = 0;

    int unify_src = 0;
    int unify_dst = 0;
    int direction = 0;

    char* src_path = NULL;
    char* dst_path = NULL;

    char src_upath[UNIFYFS_MAX_FILENAME] = {0};
    char dst_upath[UNIFYFS_MAX_FILENAME] = {0};

    struct stat sb_src = {0};
    struct stat sb_dst = {0};

    if (unifyfs_intercept_path(src, src_upath)) {
        direction = UNIFYFS_TRANSFER_DIRECTION_OUT;
        unify_src = 1;
        src_path = strdup(src_upath);
    } else {
        src_path = strdup(src);
    }
    if (NULL == src_path) {
        return -ENOMEM;
    }

    errno = 0;
    rc = UNIFYFS_WRAP(stat)(src_path, &sb_src);
    err = errno;
    if (rc < 0) {
        free(src_path);
        return -err;
    }

    if (unifyfs_intercept_path(dst, dst_upath)) {
        direction = UNIFYFS_TRANSFER_DIRECTION_IN;
        unify_dst = 1;
        dst_path = strdup(dst_upath);
    } else {
        /* check if destination is a directory */
        errno = 0;
        rc = UNIFYFS_WRAP(stat)(dst, &sb_dst);
        err = errno;
        if (rc == 0 && S_ISDIR(sb_dst.st_mode)) {
            /* if destination path is a directory, append the
             * basename of the source file */
            char* src_base = basename(src_path);
            size_t dst_len = strlen(dst) + strlen(src_base) + 2;
            dst_path = (char*) malloc(dst_len);
            snprintf(dst_path, dst_len, "%s/%s", dst, src_base);
        } else {
            dst_path = strdup(dst);
        }
    }
    if (NULL == dst_path) {
        free(src_path);
        return -ENOMEM;
    }

    if ((unify_src + unify_dst) == 0) {
        /* this should still work, but print a warning */
        LOGWARN("neither source nor destination are UnifyFS files");
    } else if ((unify_src + unify_dst) == 2) {
        /* both not allowed, fail the operation with EINVAL */
        LOGERR("both source and destination are UnifyFS files");
        free(src_path);
        free(dst_path);
        return -EINVAL;
    }

    /* create the destination file */
    if ((UNIFYFS_TRANSFER_SERIAL == mode) || (0 == client_rank)) {
        errno = 0;
        int create_flags = O_CREAT | O_WRONLY | O_TRUNC;
        int dst_mode;

        if (unify_src) {
            /* Destination file needs to be writable; file in UnifyFS may have
             * been laminated */
            dst_mode = 0640;
        } else {
            /* Use the source file's mode */
            dst_mode = sb_src.st_mode;
        }

        int fd = UNIFYFS_WRAP(open)(dst_path, create_flags, dst_mode);
        err = errno;
        if (fd < 0) {
            LOGERR("failed to create destination file %s", dst_path);
            free(src_path);
            free(dst_path);
            return -err;
        }
        UNIFYFS_WRAP(close)(fd);
    }

    if (mode == UNIFYFS_TRANSFER_PARALLEL) {
        rc = transfer_file_parallel(src_path, dst_path, &sb_src, direction);
    } else {
        rc = transfer_file_serial(src_path, dst_path, &sb_src, direction);
    }

    if (rc != UNIFYFS_SUCCESS) {
        ret = -unifyfs_rc_errno(rc);
    } else {
        ret = 0;

        /* If the destination file is in UnifyFS, then laminate it so that it
         * will be readable by other clients. */
        if (unify_dst) {
            /* remove the write bits from the source file's mode bits to set
             * the new file mode. use chmod with the new mode to ask for file
             * lamination. */
            mode_t no_write = (sb_src.st_mode) & ~(0222);
            UNIFYFS_WRAP(chmod)(dst_path, no_write);
        }
    }

    free(src_path);
    free(dst_path);

    return ret;
}
