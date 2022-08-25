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

#include <string.h>

#include "api_suite.h"

int api_write_read_sync_stat_test(char* unifyfs_root,
                                  unifyfs_handle* fshdl,
                                  size_t filesize,
                                  size_t chksize)
{
    /* Create a random file names at the mountpoint path to test */
    char testfile1[64];
    char testfile2[64];
    char testfile3[64];
    testutil_rand_path(testfile1, sizeof(testfile1), unifyfs_root);
    testutil_rand_path(testfile2, sizeof(testfile2), unifyfs_root);
    testutil_rand_path(testfile3, sizeof(testfile3), unifyfs_root);

    //-------------

    diag("Creating test files");

    int t1_flags = 0;
    int t2_flags = 0;
    int t3_flags = 0;
    unifyfs_gfid t1_gfid = UNIFYFS_INVALID_GFID;
    unifyfs_gfid t2_gfid = UNIFYFS_INVALID_GFID;
    unifyfs_gfid t3_gfid = UNIFYFS_INVALID_GFID;

    int rc = unifyfs_create(*fshdl, t1_flags, testfile1, &t1_gfid);
    ok((rc == UNIFYFS_SUCCESS) && (t1_gfid != UNIFYFS_INVALID_GFID),
       "%s:%d unifyfs_create(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_create(*fshdl, t2_flags, testfile2, &t2_gfid);
    ok((rc == UNIFYFS_SUCCESS) && (t2_gfid != UNIFYFS_INVALID_GFID),
       "%s:%d unifyfs_create(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_create(*fshdl, t3_flags, testfile3, &t3_gfid);
    ok((rc == UNIFYFS_SUCCESS) && (t3_gfid != UNIFYFS_INVALID_GFID),
       "%s:%d unifyfs_create(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile3, rc, unifyfs_rc_enum_description(rc));

    //-------------

    diag("Starting API write/read/truncate/sync/stat tests");

    /**
     * Overview of test workflow:
     * (1) write and sync testfile1 (no hole)
     * (2) write, but don't sync, testfile2 (with hole in middle)
     * (3) write, but don't sync, testfile3 (with hole at end)
     * (4) stat all files, checking expected size
     * (5) sync testfile2/3
     * (6) stat all files again
     * (7) read and check full contents of all files
     */

    size_t n_chks = filesize / chksize;
    size_t extra = filesize % chksize;
    if (extra) {
        /* test only supports exact multiples of chunk size */
        filesize -= extra;
    }

    char* databuf = malloc(filesize);
    char* readbuf = malloc(filesize);
    if ((NULL != databuf) && (NULL != readbuf)) {
        testutil_lipsum_generate(databuf, filesize, 0);

        /* (1) write and sync testfile1 (no hole) */
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
           "%s:%d unifyfs_wait_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

        /* (2) write, but don't sync, testfile2 (with hole in middle) */
        unifyfs_io_request t2_writes[n_chks];
        for (size_t i = 0; i < n_chks; i++) {
            if (i == (n_chks / 2)) {
                /* instead of writing middle chunk, use a no-op to
                 * leave a hole in the middle of the file */
                t2_writes[i].op = UNIFYFS_IOREQ_NOP;
            } else {
                t2_writes[i].op = UNIFYFS_IOREQ_OP_WRITE;
                t2_writes[i].gfid = t2_gfid;
                t2_writes[i].nbytes = chksize;
                t2_writes[i].offset = (off_t)(i * chksize);
                t2_writes[i].user_buf = databuf + (i * chksize);
            }
        }

        rc = unifyfs_dispatch_io(*fshdl, n_chks, t2_writes);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, n_chks, t2_writes, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

        /* (3) write, but don't sync, testfile3 (with hole at end) */
        unifyfs_io_request t3_writes[n_chks];
        for (size_t i = 0; i < n_chks; i++) {
            if (i == (n_chks - 1)) {
                /* instead of writing last chunk, truncate to filesize to
                 * leave a hole at the end of the file */
                t3_writes[i].op = UNIFYFS_IOREQ_OP_TRUNC;
                t3_writes[i].gfid = t3_gfid;
                t3_writes[i].offset = (off_t)(filesize);
            } else {
                t3_writes[i].op = UNIFYFS_IOREQ_OP_WRITE;
                t3_writes[i].gfid = t3_gfid;
                t3_writes[i].nbytes = chksize;
                t3_writes[i].offset = (off_t)(i * chksize);
                t3_writes[i].user_buf = databuf + (i * chksize);
            }
        }

        rc = unifyfs_dispatch_io(*fshdl, n_chks, t3_writes);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile3, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, n_chks, t3_writes, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile3, rc, unifyfs_rc_enum_description(rc));

        /* (4) stat all files */
        unifyfs_file_status t1_status, t2_status, t3_status;

        rc = unifyfs_stat(*fshdl, t1_gfid, &t1_status);
        /* expected size=filesize since writes have been synced */
        ok((rc == UNIFYFS_SUCCESS) && (t1_status.global_file_size == filesize),
           "%s:%d unifyfs_stat(%s) is successful: filesize=%zu (expected=%zu),"
           " rc=%d (%s)", __FILE__, __LINE__, testfile1,
           t1_status.global_file_size, filesize,
           rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_stat(*fshdl, t2_gfid, &t2_status);
        /* expected size=0 since writes have not been synced */
        ok((rc == UNIFYFS_SUCCESS) && (t2_status.global_file_size == 0),
           "%s:%d unifyfs_stat(%s) is successful: filesize=%zu (expected=0),"
           " rc=%d (%s)", __FILE__, __LINE__, testfile2,
           t2_status.global_file_size,
           rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_stat(*fshdl, t3_gfid, &t3_status);
        /* expected size=filesize since truncate is a sync point */
        ok((rc == UNIFYFS_SUCCESS) && (t3_status.global_file_size == filesize),
           "%s:%d unifyfs_stat(%s) is successful: filesize=%zu (expected=%zu),"
           " rc=%d (%s)", __FILE__, __LINE__, testfile3,
           t3_status.global_file_size, filesize,
           rc, unifyfs_rc_enum_description(rc));

        /* (5) sync testfile2/3 */
        rc = unifyfs_sync(*fshdl, t2_gfid);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_sync(%s) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_sync(*fshdl, t3_gfid);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_sync(%s) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile3, rc, unifyfs_rc_enum_description(rc));

        /* (6) stat files again */
        rc = unifyfs_stat(*fshdl, t1_gfid, &t1_status);
        ok((rc == UNIFYFS_SUCCESS) && (t1_status.global_file_size == filesize),
           "%s:%d unifyfs_stat(%s) is successful: filesize=%zu (expected=%zu),"
           " rc=%d (%s)", __FILE__, __LINE__, testfile1,
           t1_status.global_file_size, filesize,
           rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_stat(*fshdl, t2_gfid, &t2_status);
        ok((rc == UNIFYFS_SUCCESS) && (t2_status.global_file_size == filesize),
           "%s:%d unifyfs_stat(%s) is successful: filesize=%zu (expected=%zu),"
           " rc=%d (%s)", __FILE__, __LINE__, testfile2,
           t2_status.global_file_size, filesize,
           rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_stat(*fshdl, t3_gfid, &t3_status);
        ok((rc == UNIFYFS_SUCCESS) && (t3_status.global_file_size == filesize),
           "%s:%d unifyfs_stat(%s) is successful: filesize=%zu (expected=%zu),"
           " rc=%d (%s)", __FILE__, __LINE__, testfile3,
           t3_status.global_file_size, filesize,
           rc, unifyfs_rc_enum_description(rc));

        /* (7) read and check full contents of all files */
        memset(readbuf, (int)'?', filesize);
        unifyfs_io_request t1_reads[n_chks];
        for (size_t i = 0; i < n_chks; i++) {
            t1_reads[i].op = UNIFYFS_IOREQ_OP_READ;
            t1_reads[i].gfid = t1_gfid;
            t1_reads[i].nbytes = chksize;
            t1_reads[i].offset = (off_t)(i * chksize);
            t1_reads[i].user_buf = readbuf + (i * chksize);
        }

        rc = unifyfs_dispatch_io(*fshdl, n_chks, t1_reads);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, n_chks, t1_reads, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

        for (size_t i = 0; i < n_chks; i++) {
            size_t bytes = t1_reads[i].nbytes;
            off_t off = t1_reads[i].offset;

            /* check read operation status */
            int err = t1_reads[i].result.error;
            size_t cnt = t1_reads[i].result.count;
            ok((err == 0) && (cnt == bytes),
               "%s:%d read(%s, offset=%zu, sz=%zu) is successful: count=%zu,"
               " rc=%d (%s)", __FILE__, __LINE__, testfile1, (size_t)off,
               bytes, cnt, err, unifyfs_rc_enum_description(err));

            /* check valid data */
            uint64_t error_offset;
            int check = testutil_lipsum_check(t1_reads[i].user_buf,
                                              (uint64_t)bytes,
                                              (uint64_t)off, &error_offset);
            ok(check == 0,
               "%s:%d read(%s, offset=%zu, sz=%zu) data check is successful",
               __FILE__, __LINE__, testfile1, (size_t)off, bytes);
        }

        memset(readbuf, (int)'?', filesize);
        unifyfs_io_request t2_reads[n_chks];
        for (size_t i = 0; i < n_chks; i++) {
            t2_reads[i].op = UNIFYFS_IOREQ_OP_READ;
            t2_reads[i].gfid = t2_gfid;
            t2_reads[i].nbytes = chksize;
            t2_reads[i].offset = (off_t)(i * chksize);
            t2_reads[i].user_buf = readbuf + (i * chksize);
        }

        rc = unifyfs_dispatch_io(*fshdl, n_chks, t2_reads);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, n_chks, t2_reads, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

        for (size_t i = 0; i < n_chks; i++) {
            size_t bytes = t2_reads[i].nbytes;
            off_t off = t2_reads[i].offset;

            /* check read operation status */
            int err = t2_reads[i].result.error;
            size_t cnt = t2_reads[i].result.count;
            ok((err == 0) && (cnt == bytes),
               "%s:%d read(%s, offset=%zu, sz=%zu) is successful: count=%zu,"
               " rc=%d (%s)", __FILE__, __LINE__, testfile2, (size_t)off,
               bytes, cnt, err, unifyfs_rc_enum_description(err));

            /* check valid data */
            int check;
            if (i == (n_chks / 2)) {
                /* check middle chunk hole is zeroes */
                check = testutil_zero_check(t2_reads[i].user_buf, bytes);
            } else {
                uint64_t error_offset;
                check = testutil_lipsum_check(t2_reads[i].user_buf,
                                              (uint64_t)bytes,
                                              (uint64_t)off, &error_offset);
            }
            ok(check == 0,
               "%s:%d read(%s, offset=%zu, sz=%zu) data check is successful",
               __FILE__, __LINE__, testfile2, (size_t)off, bytes);
        }

        memset(readbuf, (int)'?', filesize);
        unifyfs_io_request t3_reads[n_chks];
        for (size_t i = 0; i < n_chks; i++) {
            t3_reads[i].op = UNIFYFS_IOREQ_OP_READ;
            t3_reads[i].gfid = t3_gfid;
            t3_reads[i].nbytes = chksize;
            t3_reads[i].offset = (off_t)(i * chksize);
            t3_reads[i].user_buf = readbuf + (i * chksize);
        }

        rc = unifyfs_dispatch_io(*fshdl, n_chks, t3_reads);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile3, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, n_chks, t3_reads, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile3, rc, unifyfs_rc_enum_description(rc));

        for (size_t i = 0; i < n_chks; i++) {
            size_t bytes = t3_reads[i].nbytes;
            off_t off = t3_reads[i].offset;

            /* check read operation status */
            int err = t3_reads[i].result.error;
            size_t cnt = t3_reads[i].result.count;
            ok((err == 0) && (cnt == bytes),
               "%s:%d read(%s, offset=%zu, sz=%zu) is successful: count=%zu,"
               " rc=%d (%s)", __FILE__, __LINE__, testfile3, (size_t)off,
               bytes, cnt, err, unifyfs_rc_enum_description(err));

            /* check valid data */
            int check;
            if (i == (n_chks - 1)) {
                /* check last chunk hole is zeroes */
                check = testutil_zero_check(t3_reads[i].user_buf, bytes);
            } else {
                uint64_t error_offset;
                check = testutil_lipsum_check(t3_reads[i].user_buf,
                                              (uint64_t)bytes,
                                              (uint64_t)off, &error_offset);
            }
            ok(check == 0,
               "%s:%d read(%s, offset=%zu, sz=%zu) data check is successful",
               __FILE__, __LINE__, testfile3, (size_t)off, bytes);
        }
    }

    diag("Finished API write/read/truncate/sync/stat tests");

    //-------------

    diag("Removing test files");

    if (t1_gfid != UNIFYFS_INVALID_GFID) {
        rc = unifyfs_remove(*fshdl, testfile1);
        ok(rc == UNIFYFS_SUCCESS,
        "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
        __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));
    }

    if (t2_gfid != UNIFYFS_INVALID_GFID) {
        rc = unifyfs_remove(*fshdl, testfile2);
        ok(rc == UNIFYFS_SUCCESS,
        "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
        __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));
    }

    if (t3_gfid != UNIFYFS_INVALID_GFID) {
        rc = unifyfs_remove(*fshdl, testfile3);
        ok(rc == UNIFYFS_SUCCESS,
        "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
        __FILE__, __LINE__, testfile3, rc, unifyfs_rc_enum_description(rc));
    }

    //-------------

    return 0;

}
