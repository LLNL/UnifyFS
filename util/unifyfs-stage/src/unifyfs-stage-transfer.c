/*
 * Copyright (c) 2022, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2022, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <mpi.h>
#include <openssl/md5.h>  // still needed for the MD5_DIGEST_LENGTH define
#include <openssl/evp.h>

#include "unifyfs-stage.h"

static
int read_unify_file_block(unifyfs_handle fshdl,
                          unifyfs_gfid gfid,
                          off_t file_offset,
                          size_t bufsize,
                          char* databuf,
                          size_t* nread)
{
    int ret = 0;
    size_t len = 0;

    unifyfs_io_request rd_req;
    rd_req.op       = UNIFYFS_IOREQ_OP_READ;
    rd_req.gfid     = gfid;
    rd_req.nbytes   = bufsize;
    rd_req.offset   = file_offset;
    rd_req.user_buf = (void*) databuf;

    unifyfs_rc urc = unifyfs_dispatch_io(fshdl, 1, &rd_req);
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                "unifyfs_dispatch_io(OP_READ) failed - %s",
                unifyfs_rc_enum_description(urc));
        ret = unifyfs_rc_errno(urc);
    } else {
        urc = unifyfs_wait_io(fshdl, 1, &rd_req, 1);
        if (UNIFYFS_SUCCESS != urc) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "unifyfs_wait_io(OP_READ) failed - %s",
                    unifyfs_rc_enum_description(urc));
            ret = unifyfs_rc_errno(urc);
        } else {
            if (0 == rd_req.result.error) {
                len = rd_req.result.count;
            } else {
                fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                        "OP_READ req failed - %s",
                        strerror(rd_req.result.error));
                ret = rd_req.result.error;
            }
        }
    }
    *nread = len;
    return ret;
}

static
int write_unify_file_block(unifyfs_handle fshdl,
                           unifyfs_gfid gfid,
                           off_t file_offset,
                           size_t bufsize,
                           char* databuf,
                           size_t* nwrite)
{
    int ret = 0;
    size_t len = 0;

    unifyfs_io_request wr_req;
    wr_req.op       = UNIFYFS_IOREQ_OP_WRITE;
    wr_req.gfid     = gfid;
    wr_req.nbytes   = bufsize;
    wr_req.offset   = file_offset;
    wr_req.user_buf = (void*) databuf;

    unifyfs_rc urc = unifyfs_dispatch_io(fshdl, 1, &wr_req);
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                "unifyfs_dispatch_io(OP_WRITE) failed - %s",
                unifyfs_rc_enum_description(urc));
        ret = unifyfs_rc_errno(urc);
    } else {
        urc = unifyfs_wait_io(fshdl, 1, &wr_req, 1);
        if (UNIFYFS_SUCCESS != urc) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "unifyfs_wait_io(OP_WRITE) failed - %s",
                    unifyfs_rc_enum_description(urc));
            ret = unifyfs_rc_errno(urc);
        } else {
            if (0 == wr_req.result.error) {
                len = wr_req.result.count;
            } else {
                fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                        "OP_WRITE req failed - %s",
                        strerror(wr_req.result.error));
                ret = wr_req.result.error;
            }
        }
    }
    *nwrite = len;
    return ret;
}

static
int read_file_block(int fd,
                    off_t file_offset,
                    size_t bufsize,
                    char* databuf,
                    size_t* nread)
{
    int err;
    int ret = 0;
    errno = 0;
    ssize_t len = pread(fd, (void*) databuf, bufsize, file_offset);
    if (-1 == len) {
        err = errno;
        fprintf(stderr, "UNIFYFS-STAGE ERROR: pread() failed - %s",
                strerror(err));
        ret = err;
        len = 0;
    }
    *nread = len;
    return ret;
}

/**
 * @brief Run md5 checksum on specified file, send back
 *        digest.
 *
 * @param path      path to the target file
 * @param digest    hash of the file
 *
 * @return 0 on success, errno otherwise
 */
static int md5_checksum(unifyfs_stage* ctx,
                        bool is_unify_file,
                        const char* path,
                        unsigned char* digest)
{
    int err, fd, md5_rc, rc;
    int ret = 0;
    unifyfs_gfid gfid;
    unifyfs_rc urc;
    size_t len = 0;
    off_t file_offset;
    EVP_MD_CTX* md5;
    unsigned char data[UNIFYFS_STAGE_MD5_BLOCKSIZE];
    unsigned int digest_len = UNIFYFS_STAGE_MD5_BLOCKSIZE;

    if (is_unify_file) {
        fd = -1;
        urc = unifyfs_open(ctx->fshdl, O_RDONLY, path, &gfid);
        if (UNIFYFS_SUCCESS != urc) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "failed to unifyfs_open(%s) - %s\n",
                    path, unifyfs_rc_enum_description(urc));
            return unifyfs_rc_errno(urc);
        }
    } else {
        errno = 0;
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            err = errno;
            fprintf(stderr, "UNIFYFS-STAGE ERROR: failed to open(%s) - %s\n",
                    path, strerror(err));
            return err;
        }
    }

    /* NOTE: EVP_DigestInit_ex() returns 1 for success */
    md5 = EVP_MD_CTX_create();
    md5_rc = EVP_DigestInit_ex(md5, EVP_md5(), NULL);
    if (md5_rc != 1) {
        fprintf(stderr,
                "UNIFYFS-STAGE ERROR: failed to initialize MD5 context\n");
        ret = EIO;
    } else {
        file_offset = 0;
        do {
            len = 0;
            memset(data, 0, sizeof(data));
            if (is_unify_file) {
                rc = read_unify_file_block(ctx->fshdl, gfid, file_offset,
                                           sizeof(data), (char*)data, &len);
            } else {
                rc = read_file_block(fd, file_offset,
                                     sizeof(data), (char*)data, &len);
            }
            if (rc != 0) {
                ret = EIO;
                break;
            } else if (len) {
                file_offset += (off_t) len;
                md5_rc = EVP_DigestUpdate(md5, data, len);
                if (md5_rc != 1) {
                    fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                            "MD5 checksum update failed\n");
                    ret = EIO;
                    break;
                }
            }
        } while (len != 0);

        md5_rc = EVP_DigestFinal_ex(md5, data, &digest_len);
        if (md5_rc != 1) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: failed to finalize MD5\n");
            ret = EIO;
        }

        EVP_MD_CTX_destroy(md5);
    }

    if (-1 != fd) {
        close(fd);
    }

    return ret;
}

/**
 * @brief prints md5 checksum into string
 *
 * @param buf       buffer to print into
 * @param digest    hash of the file
 *
 * @return buffer that has been printed to
 */
static char* checksum_str(char* buf, unsigned char* digest)
{
    int i = 0;
    char* pos = buf;

    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        pos += sprintf(pos, "%02x", digest[i]);
    }

    pos[0] = '\0';

    return buf;
}

/**
 * @brief takes check sums of two files and compares
 *
 * @param src_in_unify     is source file in UnifyFS?
 * @param dst_in_unify     is destination file in UnifyFS?
 * @param src              source file path
 * @param dst              destination filepath
 *
 * @return 0 if files are identical, non-zero if not, or other error
 */
static int verify_checksum(unifyfs_stage* ctx,
                           bool src_in_unify,
                           bool dst_in_unify,
                           const char* src,
                           const char* dst)
{
    int ret = 0;
    int i = 0;
    char md5src[2 * MD5_DIGEST_LENGTH + 1] = { 0, };
    char md5dst[2 * MD5_DIGEST_LENGTH + 1] = { 0, };
    unsigned char src_digest[MD5_DIGEST_LENGTH + 1] = { 0, };
    unsigned char dst_digest[MD5_DIGEST_LENGTH + 1] = { 0, };

    src_digest[MD5_DIGEST_LENGTH] = '\0';
    dst_digest[MD5_DIGEST_LENGTH] = '\0';

    ret = md5_checksum(ctx, src_in_unify, src, src_digest);
    if (ret) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: MD5 checksum failure "
                "for source file %s\n",
                src);
        return ret;
    }

    ret = md5_checksum(ctx, dst_in_unify, dst, dst_digest);
    if (ret) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: MD5 checksum failure "
                "for destination file %s\n",
                dst);
        return ret;
    }

    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        if (src_digest[i] != dst_digest[i]) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: checksums do not match! "
                    "(src=%s, dst=%s)\n",
                    checksum_str(md5src, src_digest),
                    checksum_str(md5dst, dst_digest));
            ret = EIO;
            break;
        }
    }
    if (verbose && (0 == ret)) {
        printf("UNIFYFS-STAGE INFO: checksums (src=%s, dst=%s)\n",
               checksum_str(md5src, src_digest),
               checksum_str(md5dst, dst_digest));
    }

    return ret;
}

static
int distribute_source_file_data(unifyfs_stage* ctx,
                                const char* src_file_path,
                                const char* dst_file_path,
                                size_t transfer_blksz,
                                size_t num_file_blocks)
{
    int rc;
    int fd = -1;
    int ret = 0;
    unifyfs_gfid gfid;
    unifyfs_rc urc;

    size_t blocks_per_client = num_file_blocks / ctx->total_ranks;
    if (blocks_per_client < 8) {
        /* somewhat arbitrary choice of minimum 8 blocks per client.
         * also avoids distribution of small files */
        blocks_per_client = 8;
    }

    /* rank 0 creates destination file */
    if (ctx->rank == 0) {
        urc = unifyfs_create(ctx->fshdl, O_WRONLY, dst_file_path, &gfid);
        if (UNIFYFS_SUCCESS != urc) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "failed to unifyfs_create(%s) - %s\n",
                    dst_file_path, unifyfs_rc_enum_description(urc));
            ret = unifyfs_rc_errno(urc);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    size_t start_block_ndx = ctx->rank * blocks_per_client;
    if (start_block_ndx < num_file_blocks) {
        /* open source file */
        errno = 0;
        fd = open(src_file_path, O_RDONLY);
        if (fd < 0) {
            ret = errno;
            fprintf(stderr, "UNIFYFS-STAGE ERROR: failed to open(%s) - %s\n",
                    src_file_path, strerror(ret));
        }

        /* non-zero ranks just open destination file */
        if (ctx->rank != 0) {
            urc = unifyfs_open(ctx->fshdl, O_WRONLY, dst_file_path, &gfid);
            if (UNIFYFS_SUCCESS != urc) {
                fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                        "failed to unifyfs_open(%s) - %s\n",
                        dst_file_path, unifyfs_rc_enum_description(urc));
                ret = unifyfs_rc_errno(urc);
            }
        }

        /* make sure all the open/create calls succeeded */
        if (ret) {
            goto err_ret;
        }

        char* block_data = malloc(transfer_blksz);
        if (NULL == block_data) {
            ret = ENOMEM;
            goto err_ret;
        }

        for (size_t i = 0; i < blocks_per_client; i++) {
            memset(block_data, 0, transfer_blksz);
            size_t block_ndx = start_block_ndx + i;
            if (block_ndx < num_file_blocks) {
                size_t nread = 0;
                off_t block_offset = block_ndx * transfer_blksz;
                rc = read_file_block(fd, block_offset, transfer_blksz,
                                     block_data, &nread);
                if (rc) {
                    ret = rc;
                    break;
                } else {
                    size_t nwrite = 0;
                    rc = write_unify_file_block(ctx->fshdl, gfid, block_offset,
                                                nread, block_data, &nwrite);
                    if (rc) {
                        ret = rc;
                        break;
                    } else if (verbose && (nread != nwrite)) {
                        printf("[rank=%d] UNIFYFS-STAGE DEBUG: "
                               "mismatch on read=%zu / write=%zu bytes\n",
                               ctx->rank, nread, nwrite);
                    }
                }
            }
        }

        /* synchronize local writes */
        urc = unifyfs_sync(ctx->fshdl, gfid);
        if (UNIFYFS_SUCCESS != urc) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "failed to unifyfs_sync(%s) - %s\n",
                    dst_file_path, unifyfs_rc_enum_description(urc));
            ret = unifyfs_rc_errno(urc);
        }
    }

err_ret:
    if (fd != -1) {
        close(fd);
    }

    return ret;
}

/**
 * @brief transfer source file to destination according to stage context
 *
 * @param ctx               stage context
 * @param file_index        index of file in manifest (>=1)
 * @param src_file_size     size in bytes of source file
 * @param src_file_path     source file path
 * @param dst_file_path     destination file path
 *
 * @return 0 on success, errno otherwise
 */
int unifyfs_stage_transfer(unifyfs_stage* ctx,
                           int file_index,
                           const char* src_file_path,
                           const char* dst_file_path)
{
    int err, rc;
    int ret = 0;
    unifyfs_rc urc;

    const char* src = src_file_path;
    const char* dst = dst_file_path;

    if (!ctx || (NULL == src) || (NULL == dst)) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                "invalid stage_transfer() params\n");
        return EINVAL;
    }

    /* decide which rank gets to manage the transfer */
    int mgr_rank = (file_index - 1) % ctx->total_ranks;

    bool src_in_unify = is_unifyfs_path(ctx->fshdl, src);
    bool dst_in_unify = is_unifyfs_path(ctx->fshdl, dst);
    if (src_in_unify && dst_in_unify) {
        if (mgr_rank == ctx->rank) {
            fprintf(stderr,
                    "UNIFYFS-STAGE ERROR: staging is not supported "
                    "for source (%s) and destination (%s) both in UnifyFS!\n",
                    src, dst);
        }
        return EINVAL;
    }

    if (ctx->mode == UNIFYFS_STAGE_MODE_SERIAL) {
        /* use barrier to force sequential processing */
        MPI_Barrier(MPI_COMM_WORLD);
    }

    if (src_in_unify && (mgr_rank == ctx->rank)) {
        /* transfer manager rank initiates transfer using library API,
         * other ranks do nothing */
        if (verbose) {
            printf("[rank=%d] UNIFYFS-STAGE INFO: "
                   "transfer src=%s, dst=%s\n",
                   ctx->rank, src, dst);
        }

        unifyfs_transfer_request transfer = {0};
        transfer.src_path = src;
        transfer.dst_path = dst;
        transfer.mode = UNIFYFS_TRANSFER_MODE_COPY;
        transfer.use_parallel = 1;
        urc = unifyfs_dispatch_transfer(ctx->fshdl, 1, &transfer);
        if (urc != UNIFYFS_SUCCESS) {
            fprintf(stderr, "[rank=%d] UNIFYFS-STAGE ERROR: "
                    "transfer dispatch failed! %s\n",
                    ctx->rank, unifyfs_rc_enum_description(urc));
            ret = unifyfs_rc_errno(urc);
        } else {
            urc = unifyfs_wait_transfer(ctx->fshdl, 1, &transfer, 1);
            if (urc != UNIFYFS_SUCCESS) {
                fprintf(stderr, "[rank=%d] UNIFYFS-STAGE ERROR: "
                        "transfer wait failed! %s\n",
                        ctx->rank, unifyfs_rc_enum_description(urc));
                ret = unifyfs_rc_errno(urc);
            } else {
                if (transfer.result.rc == UNIFYFS_SUCCESS) {
                    if (verbose) {
                        printf("[rank=%d] UNIFYFS-STAGE INFO: "
                               "file size = %zu B, "
                               "transfer time = %.3f sec\n",
                               ctx->rank,
                               transfer.result.file_size_bytes,
                               transfer.result.transfer_time_seconds);
                    }
                } else {
                    ret = transfer.result.error;
                }
            }
        }
    }

    if (dst_in_unify) {
        unsigned long src_file_size = 0;
        if (mgr_rank == ctx->rank) {
            struct stat ss;
            errno = 0;
            rc = stat(src_file_path, &ss);
            if (rc) {
                err = errno;
                fprintf(stderr, "[rank=%d] UNIFYFS-STAGE ERROR: "
                        "failed to stat(src=%s) - %s\n",
                        ctx->rank, src, strerror(err));
                ret = err;
            } else {
                src_file_size = (unsigned long) ss.st_size;
            }
        }
        rc = MPI_Bcast((void*)&src_file_size, 1, MPI_UNSIGNED_LONG,
                       mgr_rank, MPI_COMM_WORLD);
        if (rc != MPI_SUCCESS) {
            char mpi_errstr[MPI_MAX_ERROR_STRING];
            int errstr_len = 0;
            MPI_Error_string(rc, mpi_errstr, &errstr_len);
            fprintf(stderr, "[rank=%d] UNIFYFS-STAGE ERROR: "
                    "MPI_Bcast() of source file size failed - %s\n",
                    ctx->rank, mpi_errstr);
            ret = UNIFYFS_FAILURE;
        } else {
            size_t transfer_blksz = UNIFYFS_STAGE_TRANSFER_BLOCKSIZE;
            size_t n_blocks = src_file_size / transfer_blksz;
            if (src_file_size % transfer_blksz) {
                n_blocks++;
            }

            if (ctx->data_dist == UNIFYFS_STAGE_DATA_BALANCED) {
                /* spread source file data evenly across clients */
                rc = distribute_source_file_data(ctx, src, dst,
                                                transfer_blksz, n_blocks);
                if (rc) {
                    ret = rc;
                    fprintf(stderr, "[rank=%d] UNIFYFS-STAGE ERROR: "
                            "failed to distribute src=%s to dst=%s - %s\n",
                            ctx->rank, src, dst, strerror(ret));
                }
            } else { // UNIFYFS_STAGE_DATA_SKEWED
                // TODO: implement skewed data distribution
                ret = UNIFYFS_ERROR_NYI;
            }
        }
    }

    if (0 == ret) { /* transfer completed OK */
        if ((mgr_rank == ctx->rank) && (ctx->checksum)) {
            rc = verify_checksum(ctx, src_in_unify, dst_in_unify,
                                 src, dst);
            if (rc) {
                fprintf(stderr,
                        "[rank=%d] UNIFYFS-STAGE ERROR: "
                        "checksums differ for src=%s, dst=%s !\n",
                        ctx->rank, src, dst);
                ret = rc;
            }
        }
    }

    if (0 != ret) { /* something went wrong */
        if (mgr_rank == ctx->rank) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "stage transfer failed for index[%d] "
                    "src=%s, dst=%s (error=%d)\n",
                    file_index, src, dst, ret);
        }
    }
    return ret;
}

