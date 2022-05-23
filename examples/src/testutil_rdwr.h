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

#ifndef UNIFYFS_TESTUTIL_RDWR_H
#define UNIFYFS_TESTUTIL_RDWR_H

#include "testutil.h"

#ifndef DISABLE_UNIFYFS
static struct {
    size_t n_reqs;
    struct aiocb* reqs;
    unifyfs_io_request* writes;
} unify_writes;
static struct {
    size_t n_reqs;
    struct aiocb* reqs;
    unifyfs_io_request* reads;
} unify_reads;
#endif

static inline
void test_print_aiocb(test_cfg* cfg, struct aiocb* cbp)
{
    test_print(cfg, "aiocb(fd=%d, op=%d, count=%zu, offset=%zu, buf=%p)",
               cbp->aio_fildes, cbp->aio_lio_opcode, cbp->aio_nbytes,
               cbp->aio_offset, cbp->aio_buf);
}

/* -------- Write Helper Methods -------- */

static inline
int issue_write_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs);

static inline
int issue_write_req(test_cfg* cfg, struct aiocb* req)
{
    int rc, err;
    ssize_t ss;
    off_t off;
    void* src;

    assert(NULL != cfg);

    size_t written = 0;
    size_t remaining = req->aio_nbytes;

    if (cfg->use_aio) { // aio_write(2)
        errno = 0;
        rc = aio_write(req);
        if (-1 == rc) {
            test_print(cfg, "aio_write() failed");
        }
        return rc;
    } else if (cfg->use_api || cfg->use_lio) {
        return issue_write_req_batch(cfg, 1, req);
    } else if (cfg->use_mapio) { // mmap(2)
        return ENOTSUP;
    } else if (cfg->use_mpiio) { // MPI-IO
        MPI_Status mst;
        MPI_Offset off = (MPI_Offset) req->aio_offset;
        void* src_buf = (void*) req->aio_buf;
        int count = (int) remaining;
        MPI_CHECK(cfg, (MPI_File_write_at(cfg->mpifh, off, src_buf,
                                          count, MPI_CHAR, &mst)));
    } else if (cfg->use_prdwr) { // pwrite(2)
        do {
            src = (void*)((char*)req->aio_buf + written);
            errno = 0;
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
        errno = 0;
        off = lseek(req->aio_fildes, req->aio_offset, SEEK_SET);
        if (-1 == off) {
            test_print(cfg, "lseek() failed");
            return -1;
        }
        do {
            src = (void*)((char*)req->aio_buf + written);
            errno = 0;
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


    if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
        return ENOTSUP;
#else
        if (unify_writes.writes != NULL) {
            /* release allocation from previous call */
            free(unify_writes.writes);
        }
        unify_writes.n_reqs = n_reqs;
        unify_writes.reqs = reqs;
        unify_writes.writes = calloc(n_reqs, sizeof(unifyfs_io_request));
        for (size_t i = 0; i < n_reqs; i++) {
            struct aiocb* req = reqs + i;
            unifyfs_io_request* wr = unify_writes.writes + i;
            wr->op       = UNIFYFS_IOREQ_OP_WRITE;
            wr->gfid     = cfg->gfid;
            wr->nbytes   = req->aio_nbytes;
            wr->offset   = req->aio_offset;
            wr->user_buf = (void*) req->aio_buf;
        }

        unifyfs_rc urc = unifyfs_dispatch_io(cfg->fshdl, n_reqs,
                                             unify_writes.writes);
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "unifyfs_dispatch_io(%s, OP_WRITE) failed - %s",
                       cfg->filename, unifyfs_rc_enum_description(urc));
            return -1;
        }
        return 0;
#endif
    } else if (cfg->use_lio) { // lio_listio(2)
        struct aiocb* lio_vec[n_reqs];
        for (i = 0; i < n_reqs; i++) {
            lio_vec[i] = reqs + i;
            //test_print_aiocb(cfg, lio_vec[i]);
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
int wait_write_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs);

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
    } else if (cfg->use_api) {
        return wait_write_req_batch(cfg, 1, req);
    }
    return 0;
}

static inline
int wait_write_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs)
{
    int rc, ret = 0;
    size_t i;

    assert(NULL != cfg);

    if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
        return ENOTSUP;
#else
        if ((unify_writes.n_reqs != n_reqs) || (unify_writes.reqs != reqs)) {
            /* wait args do not match previous write batch */
            test_print(cfg, "mismatched wait on unify_writes");
            return EINVAL;
        }
        unifyfs_rc urc = unifyfs_wait_io(cfg->fshdl, n_reqs,
                                         unify_writes.writes, 1);
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "unifyfs_wait_io(%s, WAITALL) failed - %s",
                       cfg->filename, unifyfs_rc_enum_description(urc));
            return -1;
        }
        return 0;
#endif
    }

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
int write_truncate(test_cfg* cfg)
{
    int rc = 0;

    if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
        return ENOTSUP;
#else
        unifyfs_io_request req = {0};
        req.op       = UNIFYFS_IOREQ_OP_TRUNC;
        req.gfid     = cfg->gfid;
        req.offset   = cfg->trunc_size;

        unifyfs_rc urc = unifyfs_dispatch_io(cfg->fshdl, 1, &req);
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "unifyfs_dispatch_io(%s, OP_TRUNC) failed - %s",
                       cfg->filename, unifyfs_rc_enum_description(urc));
            return -1;
        }
        urc = unifyfs_wait_io(cfg->fshdl, 1, &req, 1);
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "unifyfs_wait_io(%s, WAITALL) failed - %s",
                       cfg->filename, unifyfs_rc_enum_description(urc));
            return -1;
        }
        return 0;
#endif
    } else if (cfg->use_mpiio) {
        MPI_Offset mpi_off = (MPI_Offset) cfg->trunc_size;
        MPI_CHECK(cfg, (MPI_File_set_size(cfg->mpifh, mpi_off)));
    } else {
        if (cfg->rank == 0 || cfg->io_pattern == IO_PATTERN_NN) {
            if (-1 != cfg->fd) { // ftruncate(2)
                rc = ftruncate(cfg->fd, cfg->trunc_size);
                if (-1 == rc) {
                    test_print(cfg, "ftruncate() failed");
                    return -1;
                }
            }
        }
    }

    return rc;
}

static inline
int write_sync(test_cfg* cfg)
{
    int rc;

    assert(NULL != cfg);

    if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
        return ENOTSUP;
#else
        unifyfs_rc urc = unifyfs_sync(cfg->fshdl, cfg->gfid);
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "unifyfs_sync(%s, gfid=%d) failed - %s",
                       cfg->filename, cfg->gfid,
                       unifyfs_rc_enum_description(urc));
            return -1;
        }
        return 0;
#endif
    } else if (NULL != cfg->fp) { // fflush(3)
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
    } else if (cfg->use_mpiio) {
        MPI_CHECK(cfg, (MPI_File_sync(cfg->mpifh)));
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
        if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
            return ENOTSUP;
#else
            unifyfs_rc urc = unifyfs_laminate(cfg->fshdl, filepath);
            if (UNIFYFS_SUCCESS != urc) {
                test_print(cfg, "unifyfs_laminate(%s) failed - %s",
                           cfg->filename, unifyfs_rc_enum_description(urc));
                rc = -1;
            }
#endif
        } else {
            /* laminate by setting permissions to read-only */
            int chmod_rc = chmod(filepath, 0444);
            if (-1 == chmod_rc) {
                /* lamination failed */
                test_print(cfg, "chmod() during lamination failed");
                rc = -1;
            }
        }
    }
    if (cfg->io_pattern == IO_PATTERN_N1) {
        test_barrier(cfg);
    }
    return rc;
}

static inline
int stat_file(test_cfg* cfg, const char* filepath)
{
    int rc = 0;
    if (cfg->rank == 0 || cfg->io_pattern == IO_PATTERN_NN) {
        if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
            return ENOTSUP;
#else
            unifyfs_file_status us;
            unifyfs_rc urc = unifyfs_stat(cfg->fshdl, cfg->gfid, &us);
            if (UNIFYFS_SUCCESS != urc) {
                test_print(cfg, "unifyfs_stat(%s, gfid=%d) failed - %s",
                           cfg->filename, cfg->gfid,
                           unifyfs_rc_enum_description(urc));
                rc = -1;
            }
#endif
        } else {
            struct stat s;
            int stat_rc = stat(filepath, &s);
            if (-1 == stat_rc) {
                test_print(cfg, "ERROR - stat(%s) failed", filepath);
                rc = -1;
            }
        }
    }
    return rc;
}

/* -------- Read Helper Methods -------- */

static inline
int issue_read_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs);

static inline
int issue_read_req(test_cfg* cfg, struct aiocb* req)
{
    int rc, err;
    ssize_t ss;
    size_t nread, remaining;
    off_t off;
    void* dst;

    assert(NULL != cfg);

    if (cfg->use_aio) { // aio_read(2)
        errno = 0;
        rc = aio_read(req);
        if (-1 == rc) {
            test_print(cfg, "aio_read() failed");
        }
        return rc;
    } else if (cfg->use_api || cfg->use_lio) {
        return issue_read_req_batch(cfg, 1, req);
    } else if (cfg->use_mapio) { // mmap(2)
        return ENOTSUP;
    } else if (cfg->use_mpiio) { // MPI-IO
        MPI_Status mst;
        MPI_Offset off = (MPI_Offset) req->aio_offset;
        void* dst_buf = (void*) req->aio_buf;
        int count = (int) req->aio_nbytes;
        if (req->aio_fildes == cfg->fd) {
            MPI_CHECK(cfg, (MPI_File_read_at(cfg->mpifh, off, dst_buf,
                                             count, MPI_CHAR, &mst)));
        } else if (req->aio_fildes == cfg->dest_fd) {
            MPI_CHECK(cfg, (MPI_File_read_at(cfg->dest_mpifh, off, dst_buf,
                                             count, MPI_CHAR, &mst)));
        }
    } else if (cfg->use_prdwr) { // pread(2)
        nread = 0;
        remaining = req->aio_nbytes;
        do {
            dst = (void*)((char*)req->aio_buf + nread);
            errno = 0;
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
        errno = 0;
        off = lseek(req->aio_fildes, req->aio_offset, SEEK_SET);
        if (-1 == off) {
            test_print(cfg, "lseek() failed");
            return -1;
        }
        do {
            dst = (void*)((char*)req->aio_buf + nread);
            errno = 0;
            ss = read(req->aio_fildes, dst, remaining);
            if (-1 == ss) {
                err = errno;
                if ((EINTR == err) || (EAGAIN == err)) {
                    continue;
                }
                test_print(cfg, "read(offset=%zu, count=%zu) failed",
                           (size_t)(req->aio_offset + nread), remaining);
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

    if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
        return ENOTSUP;
#else
        if (unify_reads.reads != NULL) {
            /* release allocation from previous call */
            free(unify_reads.reads);
        }
        unify_reads.n_reqs = n_reqs;
        unify_reads.reqs = reqs;
        unify_reads.reads = calloc(n_reqs, sizeof(unifyfs_io_request));
        for (size_t i = 0; i < n_reqs; i++) {
            struct aiocb* req = reqs + i;
            unifyfs_io_request* rd = unify_reads.reads + i;
            rd->op       = UNIFYFS_IOREQ_OP_READ;
            rd->gfid     = cfg->gfid;
            rd->nbytes   = req->aio_nbytes;
            rd->offset   = req->aio_offset;
            rd->user_buf = (void*) req->aio_buf;
        }

        unifyfs_rc urc = unifyfs_dispatch_io(cfg->fshdl, n_reqs,
                                             unify_reads.reads);
        if (UNIFYFS_SUCCESS != rc) {
            test_print(cfg, "unifyfs_dispatch_io(%s, OP_READ) failed - %s",
                       cfg->filename, unifyfs_rc_enum_description(urc));
            return -1;
        }
        return 0;
#endif
    } else if (cfg->use_lio) { // lio_listio(2)
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
int wait_read_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs);

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
    } else if (cfg->use_api) {
        return wait_read_req_batch(cfg, 1, req);
    }
    return 0;
}

static inline
int wait_read_req_batch(test_cfg* cfg, size_t n_reqs, struct aiocb* reqs)
{
    int rc, ret = 0;
    size_t i;

    assert(NULL != cfg);

    if (cfg->use_api) {
#ifdef DISABLE_UNIFYFS
        return ENOTSUP;
#else
        if ((unify_reads.n_reqs != n_reqs) || (unify_reads.reqs != reqs)) {
            /* wait args do not match previous read batch */
            test_print(cfg, "mismatched wait on unify_reads");
            return EINVAL;
        }
        unifyfs_rc urc = unifyfs_wait_io(cfg->fshdl, n_reqs,
                                         unify_reads.reads, 1);
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "unifyfs_wait_io(%s, WAITALL) failed - %s",
                       cfg->filename, unifyfs_rc_enum_description(urc));
            return -1;
        }
        return 0;
#endif
    }

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
