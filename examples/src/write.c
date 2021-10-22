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

    size_t num_reqs = cfg->n_blocks * n_tran_per_blk;
    struct aiocb* req;
    struct aiocb* reqs = calloc(num_reqs, sizeof(struct aiocb));
    if (NULL == reqs) {
        *reqs_out = NULL;
        return 0;
    }

    if (!cfg->io_check) {
        // fill srcbuf with unique character per rank
        int byte = (int)'0' + cfg->rank;
        memset(srcbuf, byte, tran_sz);
    }

    req = reqs;
    for (i = 0; i < cfg->n_blocks; i++) {
        if (IO_PATTERN_N1 == cfg->io_pattern) {
            // interleaved block writes
            blk_off = (i * blk_sz * cfg->n_ranks)
                      + (cfg->rank * blk_sz);
        } else { // IO_PATTERN_NN
            // blocked writes
            blk_off = (i * blk_sz);
        }

        if (cfg->io_check) {
            // generate lipsum in source block
            lipsum_generate((srcbuf + ndx), blk_sz, blk_off);
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

/* -------- Main Program -------- */

/* Description:
 *
 * [ Mode 1: N-to-1 shared-file ]
 *    Each rank writes cfg.n_blocks blocks of size cfg.block_sz to the
 *    shared file, using I/O operation sizes of cfg.chunk_sz. Blocks are
 *    rank-interleaved (i.e., the block at offset 0 is written by rank 0,
 *    the block at offset cfg.block_sz is written by rank 1, and so on).
 *    After writing all blocks, the written data is synced (laminated).
 *    Rank 0 then checks that the file size is as expected.
 *
 * [ Mode 2: N-to-N file-per-process ]
 *    Each rank writes cfg.n_blocks blocks of size cfg.block_sz using
 *    I/O operation sizes of cfg.chunk_sz. After writing all blocks,
 *    the written data is synced (laminated). Each rank then checks that
 *    the file size is as expected.
 *
 * [ Options for Both Modes ]
 *    cfg.use_aio - when enabled, aio(7) will be used for issuing and
 *    completion of writes.
 *
 *    cfg.use_lio - when enabled, lio_listio(3) will be used for batching
 *    writes. When cfg.use_aio is also enabled, the mode will be
 *    LIO_NOWAIT.
 *
 *    cfg.use_mapio - support is not yet implemented. When enabled,
 *    direct memory loads and stores will be used for writes.
 *
 *    cfg.use_mpiio - when enabled, MPI-IO will be used.
 *
 *    cfg.use_prdwr - when enabled, pwrite(2) will be used.
 *
 *    cfg.use_stdio - when enabled, fwrite(2) will be used.
 *
 *    cfg.use_vecio - support is not yet implemented. When enabled,
 *    writev(2) will be used for batching writes.
 *
 *    cfg.io_check - when enabled, lipsum data is used when writing
 *    the file.
 *
 *    cfg.io_shuffle - ignored.
 */

int main(int argc, char* argv[])
{
    char* wr_buf;
    char* target_file;
    struct aiocb* reqs;
    size_t num_reqs = 0;
    int rc;

    test_cfg test_config;
    test_cfg* cfg = &test_config;
    test_timer time_create2laminate;
    test_timer time_create;
    test_timer time_wr;
    test_timer time_sync;
    test_timer time_laminate;
    test_timer time_stat;

    timer_init(&time_create2laminate, "create2laminate");
    timer_init(&time_create, "create");
    timer_init(&time_wr, "write");
    timer_init(&time_sync, "sync");
    timer_init(&time_laminate, "laminate");
    timer_init(&time_stat, "stat");

    rc = test_init(argc, argv, cfg);
    if (rc) {
        fprintf(stderr, "ERROR - Test %s initialization failed!",
                argv[0]);
        fflush(NULL);
        return rc;
    }

    if (!test_config.use_mpi) {
        fprintf(stderr, "ERROR - Test %s requires MPI!",
                argv[0]);
        fflush(NULL);
        return -1;
    }

    target_file = test_target_filename(cfg);

    // if reusing filename, remove old target file before starting timers
    if (cfg->reuse_filename) {
        test_print_verbose_once(cfg,
            "DEBUG: removing file %s for reuse", target_file);
        rc = test_remove_file(cfg, target_file);
        if (rc) {
            test_print(cfg, "ERROR - test_remove_file(%s) failed", target_file);
        }
    }

    // timer to wrap all parts of write operation
    timer_start_barrier(cfg, &time_create2laminate);

    // create file
    test_print_verbose_once(cfg,
        "DEBUG: creating target file %s", target_file);
    timer_start_barrier(cfg, &time_create);
    rc = test_create_file(cfg, target_file, O_RDWR);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_create);
    test_print_verbose_once(cfg, "DEBUG: finished create");

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
    timer_start_barrier(cfg, &time_wr);
    rc = issue_write_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    rc = wait_write_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_wr);
    test_print_verbose_once(cfg, "DEBUG: finished write requests");

    // sync
    timer_start_barrier(cfg, &time_sync);
    rc = write_sync(cfg);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_sync);
    test_print_verbose_once(cfg, "DEBUG: finished sync");

    // post-write cleanup
    free(wr_buf);
    free(reqs);
    reqs = NULL;

    if (cfg->laminate) {
        // laminate
        timer_start_barrier(cfg, &time_laminate);
        rc = write_laminate(cfg, target_file);
        if (rc) {
            test_abort(cfg, rc);
        }
        timer_stop_barrier(cfg, &time_laminate);
        test_print_verbose_once(cfg, "DEBUG: finished laminate");
    }

    // timer to wrap all parts of write operation
    timer_stop_barrier(cfg, &time_create2laminate);

    // file size check
    timer_start_barrier(cfg, &time_stat);
    size_t rank_bytes = test_config.n_blocks * test_config.block_sz;
    size_t total_bytes = rank_bytes * test_config.n_ranks;
    size_t expected = total_bytes;
    if (IO_PATTERN_NN == test_config.io_pattern) {
        expected = rank_bytes;
    }
    if ((test_config.rank == 0) ||
        (IO_PATTERN_NN == test_config.io_pattern)) {
        struct stat s;
        rc = stat(target_file, &s);
        if (-1 == rc) {
            test_print(cfg, "ERROR - stat(%s) failed", target_file);
        } else {
            if (s.st_size != expected) {
                test_print(cfg, "ERROR - file size check failed - "
                                "actual size is %zu B, expected %zu B",
                           s.st_size, expected);
            }
        }
    }
    timer_stop_barrier(cfg, &time_stat);
    test_print_verbose_once(cfg, "DEBUG: finished stat");

    // calculate achieved bandwidth rates
    double max_write_time, max_sync_time;
    double write_bw, aggr_write_bw, eff_write_bw;

    max_write_time = test_reduce_double_max(cfg, time_wr.elapsed_sec);
    max_sync_time = test_reduce_double_max(cfg, time_sync.elapsed_sec);

    write_bw = bandwidth_mib(rank_bytes, time_wr.elapsed_sec);
    aggr_write_bw = test_reduce_double_sum(cfg, write_bw);
    eff_write_bw = bandwidth_mib(total_bytes, max_write_time);

    if (test_config.rank == 0) {
        errno = 0; /* just in case there was an earlier error */
        test_print_once(cfg,
                        "\n"
                        "I/O pattern:               %s\n"
                        "I/O block size:            %.2lf KiB\n"
                        "I/O request size:          %.2lf KiB\n"
                        "Number of processes:       %d\n"
                        "Each process wrote:        %.2lf MiB\n"
                        "Total data written:        %.2lf MiB\n"
                        "File create time:          %.6lf sec\n"
                        "Maximum write time:        %.6lf sec\n"
                        "Global write time:         %.6lf sec\n"
                        "Maximum sync time:         %.6lf sec\n"
                        "Global sync time:          %.6lf sec\n"
                        "File laminate time:        %.6lf sec\n"
                        "Full write time:           %.6lf sec\n"
                        "File stat time:            %.6lf sec\n"
                        "Aggregate write bandwidth: %.3lf MiB/s\n"
                        "Effective write bandwidth: %.3lf MiB/s\n",
                        io_pattern_str(test_config.io_pattern),
                        bytes_to_kib(test_config.block_sz),
                        bytes_to_kib(test_config.chunk_sz),
                        test_config.n_ranks,
                        bytes_to_mib(rank_bytes),
                        bytes_to_mib(total_bytes),
                        time_create.elapsed_sec_all,
                        max_write_time,
                        time_wr.elapsed_sec_all,
                        max_sync_time,
                        time_sync.elapsed_sec_all,
                        time_laminate.elapsed_sec_all,
                        time_create2laminate.elapsed_sec_all,
                        time_stat.elapsed_sec_all,
                        aggr_write_bw,
                        eff_write_bw);
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

    timer_fini(&time_create2laminate);
    timer_fini(&time_create);
    timer_fini(&time_wr);
    timer_fini(&time_sync);
    timer_fini(&time_laminate);
    timer_fini(&time_stat);

    test_fini(cfg);

    return 0;
}
