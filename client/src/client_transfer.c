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

#include "client_transfer.h"
#include "unifyfs-sysio.h"

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
    size_t len = UNIFYFS_TX_BUFSIZE;
    char* buf = NULL;

    buf = malloc(UNIFYFS_TX_BUFSIZE);
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

int do_transfer_file_serial(const char* src,
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

int do_transfer_file_parallel(const char* src,
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
    total_chunks = size / UNIFYFS_TX_BUFSIZE;
    last_chunk_size = size % UNIFYFS_TX_BUFSIZE;
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
            ret = do_transfer_file_serial(src, dst, sb_src, direction);
            if (ret) {
                LOGERR("do_transfer_file_serial() failed");
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
    offset = chunk_start * UNIFYFS_TX_BUFSIZE;
    len = n_chunks_per_rank * UNIFYFS_TX_BUFSIZE;

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
            len = UNIFYFS_TX_BUFSIZE;
            if (last_chunk_size && (client_rank == (n_chunks_remainder - 1))) {
                len = last_chunk_size;
            }
            chunk_start = (total_chunks - n_chunks_remainder) + client_rank;
            offset = chunk_start * UNIFYFS_TX_BUFSIZE;

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
                          int parallel)
{
    int rc, err;
    int ret = 0;
    int txdir = 0;
    struct stat sb_src = { 0, };
    mode_t mode_no_write;
    struct stat sb_dst = { 0, };
    int unify_src = 0;
    int unify_dst = 0;

    char* src_path = strdup(src);
    if (NULL == src_path) {
        return -ENOMEM;
    }

    char src_upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(src, src_upath)) {
        txdir = UNIFYFS_TX_STAGE_OUT;
        unify_src = 1;
    }

    errno = 0;
    rc = UNIFYFS_WRAP(stat)(src, &sb_src);
    err = errno;
    if (rc < 0) {
        return -err;
    }

    char dst_path[UNIFYFS_MAX_FILENAME] = { 0, };
    char* pos = dst_path;
    pos += sprintf(pos, "%s", dst);

    errno = 0;
    rc = UNIFYFS_WRAP(stat)(dst, &sb_dst);
    err = errno;
    if (rc == 0 && S_ISDIR(sb_dst.st_mode)) {
        /* if the given destination path is a directory, append the
         * basename of the source file */
        sprintf(pos, "/%s", basename((char*) src_path));
    }

    char dst_upath[UNIFYFS_MAX_FILENAME];
    if (unifyfs_intercept_path(dst_path, dst_upath)) {
        txdir = UNIFYFS_TX_STAGE_IN;
        unify_dst = 1;
    }

    if (unify_src + unify_dst != 1) {
        // we may fail the operation with EINVAL, but useful for testing
        LOGDBG("WARNING: none of pathnames points to unifyfs volume");
    }

    /* for both serial and parallel transfers, use rank 0 client to
     * create the destination file using the source file's mode*/
    if (0 == client_rank) {
        errno = 0;
        int create_flags = O_CREAT | O_WRONLY | O_TRUNC;
        int fd = UNIFYFS_WRAP(open)(dst_path, create_flags, sb_src.st_mode);
        err = errno;
        if (fd < 0) {
            LOGERR("failed to create destination file %s", dst);
            return -err;
        }
        close(fd);
    }

    if (parallel) {
        rc = do_transfer_file_parallel(src_path, dst_path, &sb_src, txdir);
    } else {
        rc = do_transfer_file_serial(src_path, dst_path, &sb_src, txdir);
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
            mode_no_write = (sb_src.st_mode) & ~(0222);
            UNIFYFS_WRAP(chmod)(dst_path, mode_no_write);
        }
    }

    return ret;
}
