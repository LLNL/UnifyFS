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

#include "testutil.h"
#include "testutil_rdwr.h"

// generate N-to-1 or N-to-N writes according to test config
size_t generate_write_reqs(test_cfg* cfg, char* srcbuf,
                           struct aiocb** reqs_out)
{
    off_t blk_off, chk_off;
    size_t i, j, ndx = 0;
    size_t blk_sz = cfg->block_sz;
    size_t tran_sz = cfg->chunk_sz;
    size_t n_tran_per_blk = blk_sz / tran_sz;
    int rankbyte = (int)'0' + cfg->rank;

    size_t num_reqs = cfg->n_blocks * n_tran_per_blk;
    struct aiocb* req;
    struct aiocb* reqs = calloc(num_reqs, sizeof(struct aiocb));
    if (NULL == reqs) {
        *reqs_out = NULL;
        return 0;
    }

    req = reqs;
    for (i = 0; i < cfg->n_blocks; i++) {
        // interleaved block writes
        blk_off = (i * blk_sz * cfg->n_ranks) + (cfg->rank * blk_sz);

        if (cfg->io_check) {
            // generate lipsum in source block
            lipsum_generate((srcbuf + ndx), blk_sz, blk_off);
        } else {
            // fill srcbuf with unique rank character
            memset((srcbuf + ndx), rankbyte, blk_sz);
        }

        for (j = 0; j < n_tran_per_blk; j++) {
            chk_off = blk_off + (j * tran_sz);

            req->aio_fildes = cfg->fd;
            req->aio_buf = (void*)(srcbuf + ndx);
            req->aio_nbytes = tran_sz;
            req->aio_offset = chk_off;
            req->aio_lio_opcode = LIO_WRITE;

            req++;
            ndx += tran_sz;
        }
    }

    *reqs_out = reqs;
    return num_reqs;
}

// transfer test target file to the destination file using UnifyFS library API
int transfer_target_to_destination(test_cfg* cfg,
                                   char* target_file,
                                   char* dest_file)
{
    int ret = 0;
    // use a single rank to initiate and wait for completion of transfer
    if (cfg->rank == 0) {
        unifyfs_transfer_request transfer = {0};
        transfer.src_path = target_file;
        transfer.dst_path = dest_file;
        transfer.mode = UNIFYFS_TRANSFER_MODE_MOVE;
        transfer.use_parallel = 1;
        unifyfs_rc urc = unifyfs_dispatch_transfer(cfg->fshdl, 1, &transfer);
        if (urc != UNIFYFS_SUCCESS) {
            fprintf(stderr, "ERROR - Test transfer dispatch failed! %s",
                    unifyfs_rc_enum_description(urc));
            ret = urc;
        } else {
            urc = unifyfs_wait_transfer(cfg->fshdl, 1, &transfer, 1);
            if (urc != UNIFYFS_SUCCESS) {
                fprintf(stderr, "ERROR - Test transfer wait failed! %s",
                    unifyfs_rc_enum_description(urc));
                ret = urc;
            }
        }
    }
    return ret;
}

// generate N-to-1 or N-to-N reads according to test config
size_t generate_read_reqs(test_cfg* cfg, char* dstbuf,
                          struct aiocb** reqs_out)
{
    int read_rank = cfg->rank;
    off_t blk_off, chk_off;
    size_t i, j, ndx = 0;
    size_t blk_sz = cfg->block_sz;
    size_t tran_sz = cfg->chunk_sz;
    size_t n_tran_per_blk = blk_sz / tran_sz;

    size_t num_reqs = cfg->n_blocks * n_tran_per_blk;
    struct aiocb* req;
    struct aiocb* reqs = calloc(num_reqs, sizeof(struct aiocb));
    if (NULL == reqs) {
        *reqs_out = NULL;
        return 0;
    }

    if (cfg->io_shuffle) {
        // 0 reads data written by N-1, N-1 reads from 0, etc.
        read_rank = (cfg->n_ranks - 1) - cfg->rank;
    }

    req = reqs;
    for (i = 0; i < cfg->n_blocks; i++) {
        // interleaved block writes
        blk_off = (i * blk_sz * cfg->n_ranks)
                  + (read_rank * blk_sz);

        for (j = 0; j < n_tran_per_blk; j++) {
            chk_off = blk_off + (j * tran_sz);

            req->aio_fildes = cfg->dest_fd;
            req->aio_buf = (void*)(dstbuf + ndx);
            req->aio_nbytes = tran_sz;
            req->aio_offset = chk_off;
            req->aio_lio_opcode = LIO_READ;

            req++;
            ndx += tran_sz;
        }
    }

    *reqs_out = reqs;
    return num_reqs;
}


/* -------- Main Program -------- */

/* Description:
 *
 * [ Mode 1: N-to-1 shared-file ]
 *    Each rank writes cfg.n_blocks blocks of size cfg.block_sz to the
 *    shared file, using I/O operation sizes of cfg.chunk_sz. Blocks are
 *    rank-interleaved (i.e., the block at offset 0 is written by rank 0,
 *    the block at offset cfg.block_sz is written by rank 1, and so on).
 *
 *    After writing all blocks, the written data is synced (laminated) and
 *    the shared file is transferred using the library API to the
 *    destination path.
 *
 *    Each rank then reads back the written blocks from the destination
 *    file using I/O operation sizes of cfg.chunk_sz. If cfg.io_shuffle is
 *    enabled, each rank will read different blocks than it wrote.
 *
 * [ Mode 2: N-to-N file-per-process ]
 *    This mode is not supported.
 *
 * [ Options for Both Modes ]
 *
 *    cfg.use_lio - when enabled, lio_listio(3) will be used for batching
 *    reads and writes.
 *
 *    cfg.use_mpiio - when enabled, MPI-IO will be used.
 *
 *    cfg.use_prdwr - when enabled, pread(2) and pwrite(2) will be used.
 *
 *    cfg.io_check - when enabled, lipsum data is used when writing
 *    the file and verified when reading.
 *
 */

int main(int argc, char* argv[])
{
    char* wr_buf;
    char* rd_buf;
    char* target_file;
    char* destination_file;
    struct aiocb* reqs = NULL;
    size_t num_reqs = 0;
    int rc;

    test_cfg test_config;
    test_cfg* cfg = &test_config;

    test_timer time_write;
    test_timer time_sync;
    test_timer time_transfer;
    test_timer time_read;

    timer_init(&time_write, "write");
    timer_init(&time_sync, "sync");
    timer_init(&time_transfer, "transfer");
    timer_init(&time_read, "read");

    rc = test_init(argc, argv, cfg);
    if (rc) {
        fprintf(stderr, "ERROR - Test %s initialization failed!",
                argv[0]);
        fflush(NULL);
        return rc;
    }

    if (cfg->io_pattern != IO_PATTERN_N1) {
        fprintf(stderr, "ERROR - Test %s requires shared file pattern!",
                argv[0]);
        fflush(NULL);
        return -1;
    }

    if (!cfg->use_unifyfs) {
        fprintf(stderr, "ERROR - Test %s requires UnifyFS!",
                argv[0]);
        fflush(NULL);
        return -1;
    }

    if (!test_config.use_mpi) {
        fprintf(stderr, "ERROR - Test %s requires MPI!",
                argv[0]);
        fflush(NULL);
        return -1;
    }

    target_file = test_target_filename(cfg);
    destination_file = test_destination_filename(cfg);

    if (NULL == destination_file) {
        fprintf(stderr, "ERROR - Test %s requires destination file!",
                argv[0]);
        fflush(NULL);
        return -1;
    }

    // if reusing filename, remove old target file before starting timers
    if (cfg->reuse_filename) {
        test_print_verbose_once(cfg,
            "DEBUG: removing file %s for reuse", target_file);
        rc = test_remove_file(cfg, target_file);
        if (rc) {
            test_print(cfg, "ERROR - test_remove_file(%s) failed", target_file);
        }
    }

    // create file
    test_print_verbose_once(cfg,
        "DEBUG: creating target file %s", target_file);
    rc = test_create_file(cfg, target_file, O_RDWR);
    if (rc) {
        test_abort(cfg, rc);
    }

    // generate write requests
    test_print_verbose_once(cfg, "DEBUG: generating write requests");
    wr_buf = calloc(test_config.n_blocks, test_config.block_sz);
    if (NULL == wr_buf) {
        test_abort(cfg, ENOMEM);
    }
    num_reqs = generate_write_reqs(cfg, wr_buf, &reqs);
    if (0 == num_reqs) {
        test_abort(cfg, ENOMEM);
    }

    // do writes
    test_print_verbose_once(cfg, "DEBUG: starting write requests");
    timer_start_barrier(cfg, &time_write);
    rc = issue_write_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    rc = wait_write_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_write);
    test_print_verbose_once(cfg,
        "DEBUG: finished write requests (elapsed=%.6lf sec)",
        time_write.elapsed_sec_all);

    // sync
    timer_start_barrier(cfg, &time_sync);
    rc = write_sync(cfg);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_sync);
    test_print_verbose_once(cfg,
        "DEBUG: finished sync (elapsed=%.6lf sec)",
        time_sync.elapsed_sec_all);

    if (cfg->laminate) {
        // laminate
        test_print_verbose_once(cfg, "DEBUG: laminating target file");
        rc = write_laminate(cfg, target_file);
        if (rc) {
            test_abort(cfg, rc);
        }
    }

    // stat file
    test_print_verbose_once(cfg, "DEBUG: calling stat() on target file");
    stat_cmd(cfg, target_file);

    // post-write cleanup
    free(wr_buf);
    free(reqs);
    reqs = NULL;

    // need to initialize API handle for transfer
    unifyfs_rc urc;
    bool api_init = false;
    if (cfg->fshdl == UNIFYFS_INVALID_HANDLE) {
        urc = unifyfs_initialize(cfg->mountpt, NULL, 0, &(cfg->fshdl));
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "ERROR: unifyfs_initialize(%s) failed (%s)",
                       cfg->mountpt, unifyfs_rc_enum_description(urc));
            test_abort(cfg, (int)urc);
            return -1;
        }
        api_init = true;
    }

    // use single rank to initiate transfer to destination file
    test_print_verbose_once(cfg,
        "DEBUG: transferring target file to destination %s",
        destination_file);
    timer_start_barrier(cfg, &time_transfer);
    rc = transfer_target_to_destination(cfg, target_file, destination_file);
    timer_stop_barrier(cfg, &time_transfer);
    test_print_verbose_once(cfg,
        "DEBUG: finished transfer (elapsed=%.6lf sec)",
        time_transfer.elapsed_sec_all);

    // we're done with API handle now
    if (api_init) {
        urc = unifyfs_finalize(cfg->fshdl);
        if (UNIFYFS_SUCCESS != urc) {
            test_print(cfg, "ERROR: unifyfs_finalize() failed - %s",
                       unifyfs_rc_enum_description(urc));
        }
        cfg->fshdl = UNIFYFS_INVALID_HANDLE;
    }

    // open the destination file
    test_print_verbose_once(cfg, "DEBUG: opening destination file");
    rc = test_open_destination_file(cfg, O_RDONLY);
    if (rc) {
        test_abort(cfg, rc);
    }

    // generate read requests
    test_print_verbose_once(cfg, "DEBUG: generating read requests");
    rd_buf = calloc(test_config.n_blocks, test_config.block_sz);
    if (NULL == rd_buf) {
        test_abort(cfg, ENOMEM);
    }
    num_reqs = generate_read_reqs(cfg, rd_buf, &reqs);
    if (0 == num_reqs) {
        test_abort(cfg, ENOMEM);
    }

    // do reads
    test_print_verbose_once(cfg, "DEBUG: starting read requests");
    timer_start_barrier(cfg, &time_read);
    rc = issue_read_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    rc = wait_read_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_read);
    test_print_verbose_once(cfg,
        "DEBUG: finished read requests (elapsed=%.6lf sec)",
        time_read.elapsed_sec_all);

    if (test_config.io_check) {
        test_print_verbose_once(cfg, "DEBUG: verifying data");
        rc = check_read_req_batch(cfg, num_reqs, reqs);
    }

    // post-read cleanup
    free(rd_buf);
    free(reqs);
    reqs = NULL;

    // calculate achieved bandwidth rates
    size_t rank_bytes = test_config.n_blocks * test_config.block_sz;
    size_t total_bytes = rank_bytes * test_config.n_ranks;

    double max_global_write_time = test_reduce_double_max(cfg,
        time_write.elapsed_sec_all);
    double max_global_sync_time = test_reduce_double_max(cfg,
        time_sync.elapsed_sec_all);
    double global_write_bw = bandwidth_mib(total_bytes, max_global_write_time);
    double global_write_sync_bw = bandwidth_mib(total_bytes,
        max_global_write_time + max_global_sync_time);

    double global_transfer_bw = bandwidth_mib(total_bytes,
        time_transfer.elapsed_sec_all);

    double max_global_read_time = test_reduce_double_max(cfg,
        time_read.elapsed_sec_all);
    double global_read_bw = bandwidth_mib(total_bytes, max_global_read_time);

    if (test_config.rank == 0) {
        errno = 0; /* just in case there was an earlier error */
        test_print_once(cfg, "Target Write Time is %.6lf s",
                        max_global_write_time);
        test_print_once(cfg, "Target Write BW is %.3lf MiB/s",
                        global_write_bw);
        test_print_once(cfg, "Target Sync Time is %.6lf s",
                        max_global_sync_time);
        test_print_once(cfg, "Target Write+Sync BW is %.3lf MiB/s",
                        global_write_sync_bw);
        test_print_once(cfg, "Transfer Time is %.6lf s",
                        time_transfer.elapsed_sec_all);
        test_print_once(cfg, "Transfer BW is %.3lf MiB/s",
                        global_transfer_bw);
        test_print_once(cfg, "Destination Read BW is %.3lf MiB/s",
                        global_read_bw);
    }

    if (cfg->remove_target) {
        test_print_verbose_once(cfg,
            "DEBUG: removing file %s", target_file);
        rc = test_remove_file(cfg, target_file);
        if (rc) {
            test_print(cfg, "ERROR - test_remove_file(%s) failed", target_file);
        }
    }

    // cleanup
    free(target_file);
    free(destination_file);

    timer_fini(&time_write);
    timer_fini(&time_sync);
    timer_fini(&time_transfer);
    timer_fini(&time_read);

    test_fini(cfg);

    return 0;
}
