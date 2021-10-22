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
        if (IO_PATTERN_N1 == cfg->io_pattern) {
            // interleaved block writes
            blk_off = (i * blk_sz * cfg->n_ranks)
                      + (read_rank * blk_sz);
        } else { // IO_PATTERN_NN
            // blocked writes
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
 *    Each rank writes cfg.n_blocks blocks of size cfg.block_sz to the
 *    shared file, using I/O operation sizes of cfg.chunk_sz. Blocks are
 *    rank-interleaved (i.e., the block at offset 0 is written by rank 0,
 *    the block at offset cfg.block_sz is written by rank 1, and so on).
 *    After writing all blocks, the written data is synced (laminated).
 *    Each rank then reads back the written blocks using I/O operation
 *    sizes of cfg.chunk_sz. If cfg.io_shuffle is enabled, each rank will
 *    read different blocks than it wrote.
 *
 * [ Mode 2: N-to-N file-per-process ]
 *    Each rank writes cfg.n_blocks blocks of size cfg.block_sz using
 *    I/O operation sizes of cfg.chunk_sz. The cfg.io_shuffle option is
 *    ignored.
 *
 * [ Options for Both Modes ]
 *    cfg.use_aio - when enabled, aio(7) will be used for issuing and
 *    completion of reads and writes.
 *
 *    cfg.use_api - when enabled, the UnifyFS library API will be used
 *    for issuing and completion of reads and writes.
 *
 *    cfg.use_lio - when enabled, lio_listio(3) will be used for batching
 *    reads and writes. When cfg.use_aio is also enabled, the mode will
 *    be LIO_NOWAIT.
 *
 *    cfg.use_mapio - support is not yet implemented. When enabled,
 *    direct memory loads and stores will be used for reads and writes.
 *
 *    cfg.use_mpiio - when enabled, MPI-IO will be used.
 *
 *    cfg.use_prdwr - when enabled, pread(2) and pwrite(2) will be used.
 *
 *    cfg.use_stdio - when enabled, fread(2) and fwrite(2) will be used.
 *
 *    cfg.use_vecio - support is not yet implemented. When enabled,
 *    readv(2) and writev(2) will be used for batching reads and writes.
 *
 *    cfg.io_check - when enabled, lipsum data is used when writing
 *    the file and verified when reading.
 *
 *    cfg.pre_wr_truncate - when enabled, truncate the file to specified
 *    size before writing.
 *
 *    cfg.post_wr_truncate - when enabled, truncate the file to specified
 *    size after writing.
 */

int main(int argc, char* argv[])
{
    char* wr_buf;
    char* rd_buf;
    char* target_file;
    struct aiocb* reqs = NULL;
    size_t num_reqs = 0;
    int rc;

    test_cfg test_config;
    test_cfg* cfg = &test_config;
    test_timer time_create;
    test_timer time_wr;
    test_timer time_rd;
    test_timer time_sync;
    test_timer time_stat_pre;
    test_timer time_stat_pre2;
    test_timer time_laminate;
    test_timer time_stat_post;

    timer_init(&time_create, "create");
    timer_init(&time_wr, "write");
    timer_init(&time_rd, "read");
    timer_init(&time_sync, "sync");
    timer_init(&time_stat_pre, "statpre");
    timer_init(&time_stat_pre2, "statpre2");
    timer_init(&time_laminate, "laminate");
    timer_init(&time_stat_post, "statpost");

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

    // create file
    test_print_verbose_once(cfg,
        "DEBUG: creating target file %s", target_file);
    timer_start_barrier(cfg, &time_create);
    rc = test_create_file(cfg, target_file, O_RDWR);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop_barrier(cfg, &time_create);
    test_print_verbose_once(cfg,
        "DEBUG: finished create (elapsed=%.6lf sec)",
        time_create.elapsed_sec_all);

    if (cfg->pre_wr_trunc) {
        write_truncate(cfg);
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
    test_print_verbose_once(cfg,
        "DEBUG: finished write requests (elapsed=%.6lf sec)",
        time_wr.elapsed_sec_all);

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

    // stat file pre-laminate
    timer_start_barrier(cfg, &time_stat_pre);
    stat_file(cfg, target_file);
    timer_stop_barrier(cfg, &time_stat_pre);
    test_print_verbose_once(cfg,
        "DEBUG: finished stat pre-laminate (elapsed=%.6lf sec)",
        time_stat_pre.elapsed_sec_all);

    if (cfg->post_wr_trunc) {
        write_truncate(cfg);

        // stat file post-truncate
        timer_start_barrier(cfg, &time_stat_pre2);
        stat_file(cfg, target_file);
        timer_stop_barrier(cfg, &time_stat_pre2);
        test_print_verbose_once(cfg,
            "DEBUG: finished stat pre2 (post trunc, elapsed=%.6lf sec)",
            time_stat_pre2.elapsed_sec_all);
    }

    if (cfg->laminate) {
        // laminate
        timer_start_barrier(cfg, &time_laminate);
        rc = write_laminate(cfg, target_file);
        if (rc) {
            test_abort(cfg, rc);
        }
        timer_stop_barrier(cfg, &time_laminate);
        test_print_verbose_once(cfg,
            "DEBUG: finished laminate (elapsed=%.6lf sec)",
            time_laminate.elapsed_sec_all);

        // stat file post-laminate
        timer_start_barrier(cfg, &time_stat_post);
        stat_cmd(cfg, target_file);
        timer_stop_barrier(cfg, &time_stat_post);
        test_print_verbose_once(cfg,
            "DEBUG: finished stat post-laminate (elapsed=%.6lf sec)",
            time_stat_post.elapsed_sec_all);
    }

    // post-write cleanup
    free(wr_buf);
    free(reqs);
    reqs = NULL;

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
    test_print_verbose_once(cfg,
        "DEBUG: finished read requests (elapsed=%.6lf sec)",
        time_rd.elapsed_sec_all);

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

    double min_local_write_time = test_reduce_double_min(cfg,
        time_wr.elapsed_sec);
    double max_local_write_time = test_reduce_double_max(cfg,
        time_wr.elapsed_sec);
    double max_global_write_time = test_reduce_double_max(cfg,
        time_wr.elapsed_sec_all);

    double local_write_bw = bandwidth_mib(rank_bytes, time_wr.elapsed_sec);
    double min_local_write_bw = test_reduce_double_min(cfg, local_write_bw);
    double max_local_write_bw = test_reduce_double_max(cfg, local_write_bw);
    double aggr_local_write_bw = test_reduce_double_sum(cfg, local_write_bw);

    double global_write_bw = bandwidth_mib(total_bytes, max_global_write_time);

    double min_local_sync_time  = test_reduce_double_min(cfg,
        time_sync.elapsed_sec);
    double max_local_sync_time  = test_reduce_double_max(cfg,
        time_sync.elapsed_sec);
    double max_global_sync_time = test_reduce_double_max(cfg,
        time_sync.elapsed_sec_all);

    double global_write_sync_bw = bandwidth_mib(total_bytes,
        max_global_write_time + max_global_sync_time);

    double min_local_read_time = test_reduce_double_min(cfg,
        time_rd.elapsed_sec);
    double max_local_read_time = test_reduce_double_max(cfg,
        time_rd.elapsed_sec);
    double max_global_read_time = test_reduce_double_max(cfg,
        time_rd.elapsed_sec_all);

    double local_read_bw = bandwidth_mib(rank_bytes, time_rd.elapsed_sec);
    double min_local_read_bw = test_reduce_double_min(cfg, local_read_bw);
    double max_local_read_bw = test_reduce_double_max(cfg, local_read_bw);
    double aggr_local_read_bw = test_reduce_double_sum(cfg, local_read_bw);

    double global_read_bw = bandwidth_mib(total_bytes, max_global_read_time);

    if (test_config.rank == 0) {
        errno = 0; /* just in case there was an earlier error */
        test_print_once(cfg, "File Create Time is %.6lf s",
                        time_create.elapsed_sec_all);
        test_print_once(cfg, "Minimum Local Write BW is %.3lf MiB/s",
                        min_local_write_bw);
        test_print_once(cfg, "Maximum Local Write BW is %.3lf MiB/s",
                        max_local_write_bw);
        test_print_once(cfg, "Aggregate Local Write BW is %.3lf MiB/s",
                        aggr_local_write_bw);
        test_print_once(cfg, "Global Write BW is %.3lf MiB/s",
                        global_write_bw);
        test_print_once(cfg, "Minimum Local Sync Time is %.6lf s",
                        min_local_sync_time);
        test_print_once(cfg, "Maximum Local Sync Time is %.6lf s",
                        max_local_sync_time);
        test_print_once(cfg, "Global Sync Time is %.6lf s",
                        max_global_sync_time);
        test_print_once(cfg, "Global Write+Sync BW is %.3lf MiB/s",
                        global_write_sync_bw);
        test_print_once(cfg, "Stat Time Pre-Laminate is %.6lf s",
                        time_stat_pre.elapsed_sec_all);
        test_print_once(cfg, "Stat Time Pre-Laminate2 is %.6lf s",
                        time_stat_pre2.elapsed_sec_all);
        test_print_once(cfg, "File Laminate Time is %.6lf s",
                        time_laminate.elapsed_sec_all);
        test_print_once(cfg, "Stat Time Post-Laminate is %.6lf s",
                        time_stat_post.elapsed_sec_all);
        test_print_once(cfg, "Minimum Local Read BW is %.3lf MiB/s",
                        min_local_read_bw);
        test_print_once(cfg, "Maximum Local Read BW is %.3lf MiB/s",
                        max_local_read_bw);
        test_print_once(cfg, "Aggregate Local Read BW is %.3lf MiB/s",
                        aggr_local_read_bw);
        test_print_once(cfg, "Global Read BW is %.3lf MiB/s",
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

    timer_fini(&time_create);
    timer_fini(&time_wr);
    timer_fini(&time_rd);
    timer_fini(&time_sync);
    timer_fini(&time_stat_pre);
    timer_fini(&time_stat_pre2);
    timer_fini(&time_laminate);
    timer_fini(&time_stat_post);

    test_fini(cfg);

    return 0;
}
