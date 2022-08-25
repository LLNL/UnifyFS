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

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "api_suite.h"

int api_transfer_test(char* unifyfs_root,
                      char* tmpdir,
                      unifyfs_handle* fshdl,
                      size_t filesize,
                      size_t chksize)
{
    /* Create a random file names at the mountpoint path to test */
    char testfile1[64];
    char testfile2[64];
    char testfile3[64];
    testutil_rand_path(testfile1, sizeof(testfile1), unifyfs_root);
    testutil_rand_path(testfile2, sizeof(testfile2), tmpdir);
    testutil_rand_path(testfile3, sizeof(testfile3), tmpdir);

    //-------------

    int rc, err;

    size_t n_chks = filesize / chksize;
    size_t extra = filesize % chksize;
    if (extra) {
        /* test only supports exact multiples of chunk size */
        filesize -= extra;
    }

    char* databuf = malloc(filesize);
    char* readbuf = malloc(filesize);
    rc = (databuf == NULL) || (readbuf == NULL);
    ok(rc == 0,
       "%s:%d malloc() of two buffers with size=%zu is successful",
       __FILE__, __LINE__, filesize);
    if (rc) {
        diag("Initial setup failed");
        return 1;
    }

    testutil_lipsum_generate(databuf, filesize, 0);

    diag("Starting API transfer tests");

    /**
     * Overview of test workflow:
     * (1) create new source file for transfer (testfile1)
     * (2) write and sync source file
     * (3) stat source file to verify size
     * (4) parallel copy transfer of source file to testfile2
     * (5) parallel move transfer of source file to testfile3
     * (6) verify source file has been removed due to (5)
     * (7) read and check full contents of both destination files
     */

    /* (1) create new source file for transfer (testfile1) */

    int t1_flags = 0;
    unifyfs_gfid t1_gfid = UNIFYFS_INVALID_GFID;
    rc = unifyfs_create(*fshdl, t1_flags, testfile1, &t1_gfid);
    ok((rc == UNIFYFS_SUCCESS) && (t1_gfid != UNIFYFS_INVALID_GFID),
       "%s:%d unifyfs_create(%s) is successful: gfid=%u rc=%d (%s)",
       __FILE__, __LINE__, testfile1, (unsigned int)t1_gfid,
       rc, unifyfs_rc_enum_description(rc));

    /* (2) write and sync source file */

    unifyfs_io_request t1_writes[n_chks + 1];
    for (size_t i = 0; i < n_chks; i++) {
        t1_writes[i].op = UNIFYFS_IOREQ_OP_WRITE;
        t1_writes[i].gfid = t1_gfid;
        t1_writes[i].nbytes = chksize;
        t1_writes[i].offset = (off_t)(i * chksize);
        t1_writes[i].user_buf = databuf + (i * chksize);
    }
    t1_writes[n_chks].op = UNIFYFS_IOREQ_OP_SYNC_META;
    t1_writes[n_chks].gfid = t1_gfid;

    rc = unifyfs_dispatch_io(*fshdl, n_chks + 1, t1_writes);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_dispatch_io(%s, OP_WRITE) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_wait_io(*fshdl, n_chks + 1, t1_writes, 1);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_wait_io(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    /* (3) stat source file to verify size */

    unifyfs_file_status t1_status = {0};
    rc = unifyfs_stat(*fshdl, t1_gfid, &t1_status);
    /* expected size=filesize since writes have been synced */
    ok((rc == UNIFYFS_SUCCESS) && (t1_status.global_file_size == filesize),
       "%s:%d unifyfs_stat(gfid=%u) is successful: filesize=%zu (expected=%zu),"
       " rc=%d (%s)", __FILE__, __LINE__, (unsigned int)t1_gfid,
       t1_status.global_file_size, filesize,
       rc, unifyfs_rc_enum_description(rc));


    /* (4) parallel copy transfer of source file to testfile2 */

    unifyfs_transfer_request t2_transfer = {0};
    t2_transfer.src_path = testfile1;
    t2_transfer.dst_path = testfile2;
    t2_transfer.mode = UNIFYFS_TRANSFER_MODE_COPY;
    t2_transfer.use_parallel = 1;
    rc = unifyfs_dispatch_transfer(*fshdl, 1, &t2_transfer);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_dispatch_transfer(%s -> %s, COPY, PARALLEL) succeeds:"
       " rc=%d (%s)", __FILE__, __LINE__, testfile1, testfile2,
       rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_wait_transfer(*fshdl, 1, &t2_transfer, 1);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_wait_transfer(%s -> %s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, testfile2,
       rc, unifyfs_rc_enum_description(rc));

    /* (5) parallel move transfer of source file to testfile3 */

    unifyfs_transfer_request t3_transfer = {0};
    t3_transfer.src_path = testfile1;
    t3_transfer.dst_path = testfile3;
    t3_transfer.mode = UNIFYFS_TRANSFER_MODE_MOVE;
    t3_transfer.use_parallel = 1;
    rc = unifyfs_dispatch_transfer(*fshdl, 1, &t3_transfer);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_dispatch_transfer(%s -> %s, MOVE, PARALLEL) succeeds:"
       " rc=%d (%s)", __FILE__, __LINE__, testfile1, testfile3,
       rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_wait_transfer(*fshdl, 1, &t3_transfer, 1);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_wait_transfer(%s -> %s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, testfile3,
       rc, unifyfs_rc_enum_description(rc));

    /* (6) verify source file has been removed due to move transfer in (5) */

    rc = unifyfs_stat(*fshdl, t1_gfid, &t1_status);
    /* expect EINVAL as testfile1/t1_gfid should no longer exist */
    ok(rc == EINVAL,
       "%s:%d unifyfs_stat(gfid=%u) fails with EINVAL: rc=%d (%s)",
       __FILE__, __LINE__, (unsigned int)t1_gfid,
       rc, unifyfs_rc_enum_description(rc));
    if (rc != EINVAL) {
        /* move transfer failed to remove source, try explicit remove */
        rc = unifyfs_remove(*fshdl, testfile1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));
    }

    /* (7) read and check full contents of both destination files */

    errno = 0;
    rc = open(testfile2, O_RDONLY);
    err = errno;
    ok(rc != -1 && err == 0,
       "%s:%d open(%s, RDONLY) is successful: fd=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, strerror(err));
    if (rc != -1) {
        int t2_fd = rc;
        memset(readbuf, (int)'?', filesize);
        struct aiocb t2_reads[n_chks];
        struct aiocb* t2_list[n_chks];
        memset(t2_reads, 0, sizeof(t2_reads));
        for (size_t i = 0; i < n_chks; i++) {
            t2_list[i] = t2_reads + i;
            t2_reads[i].aio_lio_opcode = LIO_READ;
            t2_reads[i].aio_fildes = t2_fd;
            t2_reads[i].aio_nbytes = chksize;
            t2_reads[i].aio_offset = (off_t)(i * chksize);
            t2_reads[i].aio_buf = readbuf + (i * chksize);
        }

        errno = 0;
        rc = lio_listio(LIO_WAIT, t2_list, (int)n_chks, NULL);
        err = errno;
        ok(rc == 0 && err == 0,
           "%s:%d lio_listio(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile2, rc, strerror(err));

        for (size_t i = 0; i < n_chks; i++) {
            const char* buf = (const char*) t2_reads[i].aio_buf;
            size_t bytes = t2_reads[i].aio_nbytes;
            off_t off = t2_reads[i].aio_offset;

            /* check read operation status */
            err = aio_error(t2_list[i]);
            size_t cnt = aio_return(t2_list[i]);
            ok((err == 0) && (cnt == bytes),
               "%s:%d read(%s, offset=%zu, sz=%zu) is successful: count=%zd,"
               " rc=%d (%s)", __FILE__, __LINE__, testfile2, (size_t)off,
               bytes, (ssize_t)cnt, err, strerror(err));

            /* check valid data */
            uint64_t error_offset;
            int check = testutil_lipsum_check(buf, (uint64_t)bytes,
                                              (uint64_t)off, &error_offset);
            ok(check == 0,
               "%s:%d read(%s, offset=%zu, sz=%zu) data check is successful",
               __FILE__, __LINE__, testfile2, (size_t)off, bytes);
        }

    }

    errno = 0;
    rc = open(testfile3, O_RDONLY);
    err = errno;
    ok(rc != -1 && err == 0,
       "%s:%d open(%s, RDONLY) is successful: fd=%d (%s)",
       __FILE__, __LINE__, testfile3, rc, strerror(err));
    if (rc != -1) {
        int t3_fd = rc;
        memset(readbuf, (int)'?', filesize);
        struct aiocb t3_reads[n_chks];
        struct aiocb* t3_list[n_chks];
        memset(t3_reads, 0, sizeof(t3_reads));
        for (size_t i = 0; i < n_chks; i++) {
            t3_list[i] = t3_reads + i;
            t3_reads[i].aio_lio_opcode = LIO_READ;
            t3_reads[i].aio_fildes = t3_fd;
            t3_reads[i].aio_nbytes = chksize;
            t3_reads[i].aio_offset = (off_t)(i * chksize);
            t3_reads[i].aio_buf = readbuf + (i * chksize);
        }

        errno = 0;
        rc = lio_listio(LIO_WAIT, t3_list, (int)n_chks, NULL);
        err = errno;
        ok(rc == 0 && err == 0,
           "%s:%d lio_listio(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile3, rc, strerror(err));

        for (size_t i = 0; i < n_chks; i++) {
            const char* buf = (const char*) t3_reads[i].aio_buf;
            size_t bytes = t3_reads[i].aio_nbytes;
            off_t off = t3_reads[i].aio_offset;

            /* check read operation status */
            err = aio_error(t3_list[i]);
            size_t cnt = aio_return(t3_list[i]);
            ok((err == 0) && (cnt == bytes),
               "%s:%d read(%s, offset=%zu, sz=%zu) is successful: count=%zd,"
               " rc=%d (%s)", __FILE__, __LINE__, testfile3, (size_t)off,
               bytes, (ssize_t)cnt, err, strerror(err));

            /* check valid data */
            uint64_t error_offset;
            int check = testutil_lipsum_check(buf, (uint64_t)bytes,
                                              (uint64_t)off, &error_offset);
            ok(check == 0,
               "%s:%d read(%s, offset=%zu, sz=%zu) data check is successful",
               __FILE__, __LINE__, testfile3, (size_t)off, bytes);
        }
    }

    diag("Finished API transfer tests");

    //-------------

    diag("Removing test files");

    errno = 0;
    rc = remove(testfile2);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d remove(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, err, strerror(err));

    errno = 0;
    rc = remove(testfile3);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d remove(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile3, err, strerror(err));

    //-------------

    return 0;
}
