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

#ifndef UNIFYFS_TESTUTIL_RDWR_H
#define UNIFYFS_TESTUTIL_RDWR_H

#include "testutil.h"

/* -------- Write Helper Methods -------- */

static inline
int issue_write_req(test_cfg* cfg, struct aiocb* req)
{
    int rc, err;
    ssize_t ss;
    size_t written, remaining;
    off_t off;
    void* src;

    assert(NULL != cfg);

    errno = 0;
    if (cfg->use_aio) { // aio_write(2)
        rc = aio_write(req);
        if (-1 == rc) {
            test_print(cfg, "aio_write() failed");
        }
        return rc;
    } else if (cfg->use_mapio) { // mmap(2)
        return ENOTSUP;
    } else if (cfg->use_prdwr) { // pwrite(2)
        written = 0;
        remaining = req->aio_nbytes;
        do {
            src = (void*)((char*)req->aio_buf + written);
            ss = pwrite(req->aio_fildes, src, remaining,
                        (req->aio_offset + written));
            if (-1 == ss) {
                err = errno;
                if ((EINTR == err) || (EAGAIN == err)) {
                    continue;
                }
                test_print(cfg, "pwrite() failed");
                return -1;
            }
            written += (size_t)ss;
            remaining -= (size_t)ss;
        } while (remaining);
    } else if (cfg->use_stdio) { // fwrite(3)
        return ENOTSUP;
    } else if (cfg->use_vecio) { // writev(2)
        return EINVAL;
    } else { // write(2)
        written = 0;
        remaining = req->aio_nbytes;
        off = lseek(req->aio_fildes, req->aio_offset, SEEK_SET);
        if (-1 == off) {
            test_print(cfg, "lseek() failed");
            return -1;
        }
        do {
            src = (void*)((char*)req->aio_buf + written);
            ss = write(req->aio_fildes, src, remaining);
            if (-1 == ss) {
                err = errno;
                if ((EINTR == err) || (EAGAIN == err)) {
                    continue;
                }
                test_print(cfg, "write() failed");
                return -1;
            }
            written += (size_t)ss;
            remaining -= (size_t)ss;
        } while (remaining);
    }
    return 0;
}

static inline
int issue_write_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs)
{
    int rc, ret, lio_mode;
    size_t i;

    assert(NULL != cfg);

    if (cfg->use_lio) { // lio_listio(2)
        struct aiocb* lio_vec[n_reqs];
        for (i = 0; i < n_reqs; i++) {
            lio_vec[i] = reqs + i;
        }
        lio_mode = LIO_WAIT;
        if (cfg->use_aio) {
            lio_mode = LIO_NOWAIT;
        }
        errno = 0;
        rc = lio_listio(lio_mode, lio_vec, (int)n_reqs, NULL);
        if (-1 == rc) {
            test_print(cfg, "lio_listio() failed");
        }
        return rc;
    } else if (cfg->use_mapio) { // mmap(2)
        return ENOTSUP;
    } else if (cfg->use_vecio) { // writev(2)
        return ENOTSUP;
    } else {
        ret = 0;
        for (i = 0; i < n_reqs; i++) {
            rc = issue_write_req(cfg, reqs + i);
            if (rc) {
                test_print(cfg, "write req %zu failed", i);
                ret = -1;
            }
        }
        return ret;
    }
}

static inline
int wait_write_req(test_cfg* cfg, struct aiocb* req)
{
    assert(NULL != cfg);

    if (cfg->use_aio) {
        ssize_t ss;
        const struct aiocb* creq = (const struct aiocb*)req;
        int rc = aio_suspend(&creq, 1, NULL);
        if (-1 == rc) {
            test_print(cfg, "aio_suspend() failed");
            return rc;
        }
        do {
            rc = aio_error(creq);
            if (EINPROGRESS == rc) {
                continue;
            }
            if (rc) {
                errno = rc;
                test_print(cfg, "aio_error() reported async write failure");
            }
            break;
        } while (1);
        ss = aio_return(req);
        if (ss != req->aio_nbytes) {
            test_print(cfg, "partial async write (%zd of %zu)",
                       ss, req->aio_nbytes);
            return -1;
        }
    }
    return 0;
}

static inline
int wait_write_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs)
{
    int rc, ret = 0;
    size_t i;

    assert(NULL != cfg);

    for (i = 0; i < n_reqs; i++) {
        rc = wait_write_req(cfg, reqs + i);
        if (rc) {
            test_print(cfg, "write req %zu failed", i);
            ret = -1;
        }
    }
    return ret;
}

static inline
int write_sync(test_cfg* cfg)
{
    int rc;

    assert(NULL != cfg);

    if (NULL != cfg->fp) { // fflush(3)
        rc = fflush(cfg->fp);
        if (-1 == rc) {
            test_print(cfg, "fflush() failed");
            return -1;
        }
    } else if (NULL != cfg->mapped) { // msync(2)
        rc = msync(cfg->mapped, cfg->mapped_sz, MS_SYNC);
        if (-1 == rc) {
            test_print(cfg, "msync() failed");
            return -1;
        }
    } else if (-1 != cfg->fd) { // fsync(2)
        rc = fsync(cfg->fd);
        if (-1 == rc) {
            test_print(cfg, "fsync() failed");
            return -1;
        }
    }
    return 0;
}

static inline
int write_laminate(test_cfg* cfg, const char* filepath)
{
    /* need one process to laminate each file,
     * we use the same process that created the file */
    int rc = 0;
    if (cfg->rank == 0 || cfg->io_pattern == IO_PATTERN_NN) {
        /* laminate by setting permissions to read-only */
        int chmod_rc = chmod(filepath, 0444);
        if (-1 == chmod_rc) {
            /* lamination failed */
            test_print(cfg, "chmod() during lamination failed");
            rc = -1;
        }
    }
    if (cfg->io_pattern == IO_PATTERN_N1) {
        test_barrier(cfg);
        if (cfg->rank != 0) {
            /* call stat() to update global metadata */
            struct stat st;
            int stat_rc = stat(filepath, &st);
            if (-1 == stat_rc) {
                /* lamination failed */
                test_print(cfg, "stat() update during lamination failed");
                rc = -1;
            }
        }
    }
    return rc;
}

/* -------- Read Helper Methods -------- */

static inline
int issue_read_req(test_cfg* cfg, struct aiocb* req)
{
    int rc, err;
    ssize_t ss;
    size_t nread, remaining;
    off_t off;
    void* dst;

    assert(NULL != cfg);

    errno = 0;
    if (cfg->use_aio) { // aio_read(2)
        rc = aio_read(req);
        if (-1 == rc) {
            test_print(cfg, "aio_read() failed");
        }
        return rc;
    } else if (cfg->use_mapio) { // mmap(2)
        return ENOTSUP;
    } else if (cfg->use_prdwr) { // pread(2)
        nread = 0;
        remaining = req->aio_nbytes;
        do {
            dst = (void*)((char*)req->aio_buf + nread);
            ss = pread(req->aio_fildes, dst, remaining,
                       (req->aio_offset + nread));
            if (-1 == ss) {
                err = errno;
                if ((EINTR == err) || (EAGAIN == err)) {
                    continue;
                }
                test_print(cfg, "pread() failed");
                return -1;
            } else if (0 == ss) {
                test_print(cfg, "pread() EOF");
                return -1;
            }
            nread += (size_t)ss;
            remaining -= (size_t)ss;
        } while (remaining);
    } else if (cfg->use_stdio) { // fread(3)
        return ENOTSUP;
    } else if (cfg->use_vecio) { // readv(2)
        return EINVAL;
    } else { // read(2)
        nread = 0;
        remaining = req->aio_nbytes;
        off = lseek(req->aio_fildes, req->aio_offset, SEEK_SET);
        if (-1 == off) {
            test_print(cfg, "lseek() failed");
            return -1;
        }
        do {
            dst = (void*)((char*)req->aio_buf + nread);
            ss = read(req->aio_fildes, dst, remaining);
            if (-1 == ss) {
                err = errno;
                if ((EINTR == err) || (EAGAIN == err)) {
                    continue;
                }
                test_print(cfg, "read() failed");
                return -1;
            } else if (0 == ss) {
                test_print(cfg, "read() EOF");
                return -1;
            }
            nread += (size_t)ss;
            remaining -= (size_t)ss;
        } while (remaining);
    }
    return 0;
}

static inline
int issue_read_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs)
{
    int rc, ret, lio_mode;
    size_t i;

    assert(NULL != cfg);

    if (cfg->use_lio) { // lio_listio(2)
        struct aiocb* lio_vec[n_reqs];
        for (i = 0; i < n_reqs; i++) {
            lio_vec[i] = reqs + i;
        }
        lio_mode = LIO_WAIT;
        if (cfg->use_aio) {
            lio_mode = LIO_NOWAIT;
        }
        errno = 0;
        rc = lio_listio(lio_mode, lio_vec, (int)n_reqs, NULL);
        if (-1 == rc) {
            test_print(cfg, "lio_listio() failed");
        }
        return rc;
    } else if (cfg->use_mapio) { // mmap(2)
        return ENOTSUP;
    } else if (cfg->use_vecio) { // readv(2)
        return ENOTSUP;
    } else {
        ret = 0;
        for (i = 0; i < n_reqs; i++) {
            rc = issue_read_req(cfg, reqs + i);
            if (rc) {
                test_print(cfg, "read req %zu failed", i);
                ret = -1;
            }
        }
        return ret;
    }
}

static inline
int wait_read_req(test_cfg* cfg, struct aiocb* req)
{
    assert(NULL != cfg);

    if (cfg->use_aio) {
        ssize_t ss;
        const struct aiocb* creq = (const struct aiocb*)req;
        int rc = aio_suspend(&creq, 1, NULL);
        if (-1 == rc) {
            test_print(cfg, "aio_suspend() failed");
            return rc;
        }
        do {
            rc = aio_error(creq);
            if (EINPROGRESS == rc) {
                continue;
            }
            if (rc) {
                errno = rc;
                test_print(cfg, "aio_error() reported async read failure");
            }
            break;
        } while (1);
        ss = aio_return(req);
        if (ss != req->aio_nbytes) {
            test_print(cfg, "partial async read (%zd of %zu)",
                       ss, req->aio_nbytes);
            return -1;
        }
    }
    return 0;
}

static inline
int wait_read_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs)
{
    int rc, ret = 0;
    size_t i;

    assert(NULL != cfg);

    for (i = 0; i < n_reqs; i++) {
        rc = wait_read_req(cfg, reqs + i);
        if (rc) {
            test_print(cfg, "read req %zu failed", i);
            ret = -1;
        }
    }
    return ret;
}

static inline
int check_read_req(test_cfg* cfg, struct aiocb* req)
{
    int ret = 0;

    assert(NULL != cfg);

    if (cfg->io_check) {
        uint64_t error_offset = 0;
        uint64_t len = (uint64_t) req->aio_nbytes;
        uint64_t off = (uint64_t) req->aio_offset;
        int rc = lipsum_check((const char*)req->aio_buf, len, off,
                              &error_offset);
        if (-1 == rc) {
            test_print(cfg, "data check failed at offset %" PRIu64,
                       error_offset);
            ret = -1;
        }
    }
    return ret;
}

static inline
int check_read_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs)
{
    int ret = 0;

    assert(NULL != cfg);

    if (cfg->io_check) {
        size_t i;
        for (i = 0; i < n_reqs; i++) {
            int rc = check_read_req(cfg, reqs + i);
            if (rc) {
                test_print(cfg, "read req %zu data check failed", i);
                ret = -1;
            }
        }
    }
    return ret;
}

#endif /* UNIFYFS_TESTUTIL_RDWR_H */
