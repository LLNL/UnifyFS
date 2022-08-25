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

// generate N-to-1 or N-to-N checkpoints according to test config
size_t generate_checkpoint(test_cfg* cfg, char* data, uint64_t ckpt_id,
                           struct aiocb** req_out)
{
    off_t blk_off;
    size_t blk_sz = cfg->block_sz;

    size_t num_reqs = 1;
    struct aiocb* req = calloc(num_reqs, sizeof(struct aiocb));
    if (NULL == req) {
        *req_out = NULL;
        return 0;
    }

    // use beginning of data to store rank and checkpoint id
    uint64_t* u64p = (uint64_t*) data;
    *u64p = (uint64_t) cfg->rank;
    u64p++;
    *u64p = ckpt_id;
    u64p++;
    void* chkpt_data = (void*) u64p;

    if (IO_PATTERN_N1 == cfg->io_pattern) {
        // interleaved checkpoint blocks
        blk_off = blk_sz * cfg->rank;
    } else { // IO_PATTERN_NN
        blk_off = 0;
    }

    uint64_t data_off = blk_off + (2 * sizeof(uint64_t));
    uint64_t data_sz = blk_sz - (2 * sizeof(uint64_t));
    if (cfg->io_check) {
        // generate lipsum in source block
        lipsum_generate(chkpt_data, data_sz, data_off);
    } else {
        // fill data with unique character per rank
        int byte = (int)'0' + cfg->rank;
        memset(chkpt_data, byte, (size_t)data_sz);
    }

    req->aio_fildes = cfg->fd;
    req->aio_buf = (void*)data;
    req->aio_nbytes = blk_sz;
    req->aio_offset = blk_off;
    req->aio_lio_opcode = LIO_WRITE;

    *req_out = req;
    return num_reqs;
}

// generate N-to-1 or N-to-N restart according to test config
size_t generate_restart(test_cfg* cfg, char* data,
                        struct aiocb** req_out)
{
    off_t blk_off;
    size_t blk_sz = cfg->block_sz;

    size_t num_reqs = 1;
    struct aiocb* req = calloc(num_reqs, sizeof(struct aiocb));
    if (NULL == req) {
        *req_out = NULL;
        return 0;
    }

    if (IO_PATTERN_N1 == cfg->io_pattern) {
        // interleaved block writes
        blk_off = blk_sz * cfg->rank;
    } else { // IO_PATTERN_NN
        blk_off = 0;
    }

    req->aio_fildes = cfg->fd;
    req->aio_buf = (void*)data;
    req->aio_nbytes = blk_sz;
    req->aio_offset = blk_off;
    req->aio_lio_opcode = LIO_READ;

    *req_out = req;
    return num_reqs;
}

int verify_restart_data(test_cfg* cfg, char* data, uint64_t last_ckpt_id)
{
    int ret = 0;

    // check beginning of data for correct rank and checkpoint id
    uint64_t rank, ckptid;
    uint64_t* u64p = (uint64_t*) data;
    rank = *u64p;
    u64p++;
    ckptid = *u64p;
    u64p++;

    if (rank != (uint64_t)cfg->rank) {
        ret = -1;
        test_print(cfg, "ERROR - Failed to verify restart rank\n");
    }

    if (ckptid != last_ckpt_id) {
        ret = -1;
        test_print(cfg, "ERROR - Failed to verify restart checkpoint id\n");
    }

    if (cfg->io_check) {
        // check lipsum in restart block
        uint64_t blk_off;
        uint64_t blk_sz = cfg->block_sz;
        if (IO_PATTERN_N1 == cfg->io_pattern) {
            // interleaved block writes
            blk_off = blk_sz * cfg->rank;
        } else { // IO_PATTERN_NN
            blk_off = 0;
        }

        uint64_t err_off;
        uint64_t data_off = blk_off + (2 * sizeof(uint64_t));
        uint64_t data_sz = blk_sz - (2 * sizeof(uint64_t));
        const char* chkdata = (const char*) u64p;
        int rc = lipsum_check(chkdata, data_sz, data_off, &err_off);
        if (rc) {
            ret = -1;
            test_print(cfg, "ERROR - Failed to verify restart checkpoint data "
                       "@ offset %" PRIu64 "\n", err_off);
        }
    }

    return ret;
}


/* -------- Main Program -------- */

/* Description:
 *
 * [ Mode 1: N-to-1 shared-file checkpoint ]
 *    Each rank writes cfg.n_blocks checkpoints of size cfg.block_sz to
 *    the shared file. Checkpoint blocks are rank-interleaved (i.e., the
 *    block at offset 0 is written by rank 0, the block at offset
 *    cfg.block_sz is written by rank 1, and so on). After writing all
 *    checkpoint blocks, the data is synced (laminated) and the shared
 *    file is closed. Each rank then re-opens the shared file, reads
 *    back the last data written, and optionally checks the contents if
 *    cfg.io_check is set.
 *
 * [ Mode 2: N-to-N file-per-process checkpoint ]
 *    Each rank writes cfg.n_blocks checkpoints of size cfg.block_sz to
 *    its own file. After writing all checkpoint blocks, the data is
 *    synced (laminated) and the file is closed. Each rank then re-opens
 *    its file, reads back the last data written, and optionally checks
 *    the contents if cfg.io_check is set.
 *
 * [ Options for Both Modes ]
 *    cfg.use_aio - when enabled, aio(7) will be used to issue and
 *    deterimine completion of reads and writes.
 *
 *    cfg.use_lio - when enabled, lio_listio(3) will be used to batch
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
 *    cfg.io_check - when enabled, ranks will verify the last checkpoint
 *    written matches the data read on restart.
 *
 *    cfg.io_shuffle - ignored.
 *
 *    cfg.chunk_sz - ignored.
 */

int main(int argc, char* argv[])
{
    char* chkpt_data;
    char* restart_data;
    char* target_file;
    struct aiocb* req;
    size_t num_reqs = 0;
    int rc;

    double min_write_time, max_write_time;
    double min_read_time, max_read_time;
    double min_sync_time, max_sync_time;

    test_cfg test_config;
    test_cfg* cfg = &test_config;
    test_timer time_ckpt;
    test_timer time_restart;
    test_timer time_sync;

    timer_init(&time_ckpt, "checkpoint");
    timer_init(&time_restart, "restart");
    timer_init(&time_sync, "sync");

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

    test_print_verbose_once(cfg,
        "DEBUG: creating target file %s", target_file);
    rc = test_create_file(cfg, target_file, O_RDWR);
    if (rc) {
        test_abort(cfg, rc);
    }

    chkpt_data = malloc(test_config.block_sz);
    if (NULL == chkpt_data) {
        test_abort(cfg, ENOMEM);
    }

    uint64_t chkpt_id;
    for (chkpt_id = 1; chkpt_id <= test_config.n_blocks; chkpt_id++) {
        // generate checkpoint data and write request
        test_print_verbose_once(cfg, "DEBUG: generating checkpoint %" PRIu64,
                                chkpt_id);
        num_reqs = generate_checkpoint(cfg, chkpt_data, chkpt_id, &req);
        if (0 == num_reqs) {
            test_abort(cfg, ENOMEM);
        }

        // do checkpoint
        test_print_verbose_once(cfg, "DEBUG: starting checkpoint %" PRIu64,
                                chkpt_id);
        test_barrier(cfg);
        timer_start(&time_ckpt);
        rc = issue_write_req(cfg, req);
        if (rc) {
            test_abort(cfg, rc);
        }
        rc = wait_write_req(cfg, req);
        if (rc) {
            test_abort(cfg, rc);
        }
        timer_stop(&time_ckpt);
        test_print_verbose_once(cfg, "DEBUG: finished checkpoint %" PRIu64,
                                chkpt_id);

        min_write_time = test_reduce_double_min(cfg, time_ckpt.elapsed_sec);
        max_write_time = test_reduce_double_max(cfg, time_ckpt.elapsed_sec);
        test_print_once(cfg, "Checkpoint %" PRIu64 " - Minimum Time is %.6lf s,"
                        " Maximum Time is %.6lf s\n\n",
                        chkpt_id, min_write_time, max_write_time);

        sleep(15); // fake compute time
        test_barrier(cfg);
    }

    // sync/laminate
    timer_start(&time_sync);
    rc = test_close_file(cfg);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop(&time_sync);
    test_barrier(cfg);
    test_print_verbose_once(cfg, "DEBUG: finished sync");

    min_sync_time = test_reduce_double_min(cfg, time_sync.elapsed_sec);
    max_sync_time = test_reduce_double_max(cfg, time_sync.elapsed_sec);
    test_print_once(cfg, "Minimum Sync Time is %.6lf s, "
                    "Maximum Sync Time is %.6lf s\n\n",
                    min_sync_time, max_sync_time);

    // post-checkpoint cleanup
    free(chkpt_data);
    free(req);
    req = NULL;

    // re-open file
    rc = test_open_file(cfg, target_file, O_RDONLY);
    if (rc) {
        test_abort(cfg, rc);
    }
    // generate restart read request
    test_print_verbose_once(cfg, "DEBUG: generating restart read request");
    restart_data = malloc(test_config.block_sz);
    if (NULL == restart_data) {
        test_abort(cfg, ENOMEM);
    }
    num_reqs = generate_restart(cfg, restart_data, &req);
    if (0 == num_reqs) {
        test_abort(cfg, ENOMEM);
    }

    // do restart read
    test_print_verbose_once(cfg, "DEBUG: starting restart read");
    test_barrier(cfg);
    timer_start(&time_restart);
    rc = issue_read_req(cfg, req);
    if (rc) {
        test_abort(cfg, rc);
    }
    rc = wait_read_req(cfg, req);
    if (rc) {
        test_abort(cfg, rc);
    }
    timer_stop(&time_restart);
    test_barrier(cfg);
    test_print_verbose_once(cfg, "DEBUG: finished restart read");

    min_read_time = test_reduce_double_min(cfg, time_restart.elapsed_sec);
    max_read_time = test_reduce_double_max(cfg, time_restart.elapsed_sec);
    test_print_once(cfg, "Minimum Restart Time is %.6lf s, "
                    "Maximum Restart Time is %.6lf s\n\n",
                    min_read_time, max_read_time);

    test_print_verbose_once(cfg, "DEBUG: verifying data");
    rc = verify_restart_data(cfg, restart_data, test_config.n_blocks);
    if (rc) {
        test_print(cfg, "ERROR - Restart data verification failed!");
    }

    if (cfg->remove_target) {
        test_print_verbose_once(cfg,
            "DEBUG: removing file %s", target_file);
        rc = test_remove_file(cfg, target_file);
        if (rc) {
            test_print(cfg, "ERROR - test_remove_file(%s) failed", target_file);
        }
    }

    // post-restart cleanup
    free(restart_data);
    free(req);
    req = NULL;

    // cleanup
    free(target_file);

    timer_fini(&time_ckpt);
    timer_fini(&time_restart);
    timer_fini(&time_sync);

    test_fini(cfg);

    return 0;
}
