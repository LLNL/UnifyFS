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

// generate N-to-1 or N-to-N reads according to test config
size_t generate_read_reqs(test_cfg* cfg, char* dstbuf,
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

    req = reqs;
    for (i = 0; i < cfg->n_blocks; i++) {
        if (IO_PATTERN_N1 == cfg->io_pattern) {
            // interleaved block reads
            blk_off = (i * blk_sz * cfg->n_ranks)
                      + (cfg->rank * blk_sz);
        } else { // IO_PATTERN_NN
            // blocked reads
            blk_off = (i * blk_sz);
        }

        for (j = 0; j < n_tran_per_blk; j++) {
            chk_off = blk_off + (j * tran_sz);

            req->aio_fildes = cfg->fd;
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
 *    Rank 0 first checks that the file size is as expected. Then, each
 *    rank reads cfg.n_blocks blocks of size cfg.block_sz from the
 *    shared file, using I/O operation sizes of cfg.chunk_sz. Blocks are
 *    rank-interleaved (i.e., the block at offset 0 is written by rank 0,
 *    the block at offset cfg.block_sz is written by rank 1, and so on).
 *
 * [ Mode 2: N-to-N file-per-process ]
 *    Each rank checks that its file size is as expected. Then, each
 *    rank reads cfg.n_blocks blocks of size cfg.block_sz using
 *    I/O operation sizes of cfg.chunk_sz.
 *
 * [ Options for Both Modes ]
 *    cfg.use_aio - when enabled, aio(7) will be used for issuing and
 *    completion of reads.
 *
 *    cfg.use_lio - when enabled, lio_listio(3) will be used for batching
 *    reads. When cfg.use_aio is also enabled, the mode will be
 *    LIO_NOWAIT.
 *
 *    cfg.use_mapio - support is not yet implemented. When enabled,
 *    direct memory loads will be used for reads.
 *
 *    cfg.use_mpiio - when enabled, MPI-IO will be used.
 *
 *    cfg.use_prdwr - when enabled, pread(2) will be used.
 *
 *    cfg.use_stdio - when enabled, fread(3) will be used.
 *
 *    cfg.use_vecio - support is not yet implemented. When enabled,
 *    readv(2) will be used for batching reads.
 *
 *    cfg.io_check - when enabled, lipsum data is checked when reading
 *    the file.
 *
 *    cfg.io_shuffle - ignored.
 */

int main(int argc, char* argv[])
{
    char* rd_buf;
    char* target_file;
    struct aiocb* reqs;
    size_t num_reqs = 0;
    int rc;

    test_cfg test_config;
    test_cfg* cfg = &test_config;
    test_timer time_stat;
    test_timer time_open;
    test_timer time_rd;
    test_timer time_check;

    timer_init(&time_stat, "stat");
    timer_init(&time_open, "open");
    timer_init(&time_rd, "read");
    timer_init(&time_check, "check");

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
    test_print_verbose_once(cfg, "DEBUG: opening target file %s",
                            target_file);

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

    // open file
    timer_start_barrier(cfg, &time_open);
    rc = test_open_file(cfg, target_file, O_RDONLY);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_open);
    test_print_verbose_once(cfg, "DEBUG: finished open");

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
    timer_start_barrier(cfg, &time_rd);
    rc = issue_read_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    rc = wait_read_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_rd);
    test_print_verbose_once(cfg, "DEBUG: finished read requests");

    // check file data
    test_print_verbose_once(cfg, "DEBUG: starting data check");
    timer_start_barrier(cfg, &time_check);
    rc = check_read_req_batch(cfg, num_reqs, reqs);
    if (rc) {
        test_print_once(cfg, "ERROR: data check failed!");
    }
    timer_stop_barrier(cfg, &time_check);
    test_print_verbose_once(cfg, "DEBUG: finished data check");

    // post-read cleanup
    free(rd_buf);
    free(reqs);
    reqs = NULL;

    // calculate achieved bandwidth rates
    double max_read_time, max_check_time;
    double read_bw, aggr_read_bw, eff_read_bw;

    max_read_time = test_reduce_double_max(cfg, time_rd.elapsed_sec);
    max_check_time = test_reduce_double_max(cfg, time_check.elapsed_sec);

    read_bw = bandwidth_mib(rank_bytes, time_rd.elapsed_sec);
    aggr_read_bw = test_reduce_double_sum(cfg, read_bw);
    eff_read_bw = bandwidth_mib(total_bytes, max_read_time);

    if (test_config.rank == 0) {
        errno = 0; /* just in case there was an earlier error */
        test_print_once(cfg,
                        "\n"
                        "I/O pattern:               %s\n"
                        "I/O block size:            %.2lf KiB\n"
                        "I/O request size:          %.2lf KiB\n"
                        "Number of processes:       %d\n"
                        "Each process read:         %.2lf MiB\n"
                        "Total data read:           %.2lf MiB\n"
                        "File stat time:            %.6lf sec\n"
                        "File open time:            %.6lf sec\n"
                        "Maximum read time:         %.6lf sec\n"
                        "Maximum check time:        %.6lf sec\n"
                        "Aggregate read bandwidth:  %.3lf MiB/s\n"
                        "Effective read bandwidth:  %.3lf MiB/s\n",
                        io_pattern_str(test_config.io_pattern),
                        bytes_to_kib(test_config.block_sz),
                        bytes_to_kib(test_config.chunk_sz),
                        test_config.n_ranks,
                        bytes_to_mib(rank_bytes),
                        bytes_to_mib(total_bytes),
                        time_stat.elapsed_sec_all,
                        time_open.elapsed_sec_all,
                        max_read_time,
                        max_check_time,
                        aggr_read_bw,
                        eff_read_bw);
    }

    // cleanup
    free(target_file);

    timer_fini(&time_stat);
    timer_fini(&time_open);
    timer_fini(&time_rd);
    timer_fini(&time_check);

    test_fini(cfg);

    return 0;
}
