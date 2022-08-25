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

#include "api_suite.h"
#include <string.h>

/* Tests file laminate, with subsequent write/read/stat */
int api_laminate_test(char* unifyfs_root,
                      unifyfs_handle* fshdl)
{
    size_t filesize = (size_t)32 * KIB;
    size_t chksize = (size_t)4 * KIB;

    /* Create a random file name at the mountpoint path to test */
    char testfile[64];
    testutil_rand_path(testfile, sizeof(testfile), unifyfs_root);

    //-------------

    diag("Creating test file");

    int flags = 0;
    unifyfs_gfid gfid = UNIFYFS_INVALID_GFID;
    int rc = unifyfs_create(*fshdl, flags, testfile, &gfid);
    ok(rc == UNIFYFS_SUCCESS && gfid != UNIFYFS_INVALID_GFID,
       "%s:%d unifyfs_create(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

    //-------------

    diag("Starting API lamination tests");

    /**
     * (1) write and sync testfile (no hole)
     * (2) stat testfile, should report not laminated
     * (3) laminate testfile
     * (4) stat testfile, should report laminated
     * (5) try write, should fail
     * (6) read and check file contents
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

        /* (1) write and sync testfile (no hole) */
        unifyfs_io_request fops[n_chks + 1];
        for (size_t i = 0; i < n_chks; i++) {
            fops[i].op = UNIFYFS_IOREQ_OP_WRITE;
            fops[i].gfid = gfid;
            fops[i].nbytes = chksize;
            fops[i].offset = (off_t)(i * chksize);
            fops[i].user_buf = databuf + (i * chksize);
        }
        fops[n_chks].op = UNIFYFS_IOREQ_OP_SYNC_META;
        fops[n_chks].gfid = gfid;

        rc = unifyfs_dispatch_io(*fshdl, n_chks + 1, fops);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, n_chks + 1, fops, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

        /* (2) stat testfile, should report not laminated */
        unifyfs_file_status status;
        memset(&status, 0, sizeof(status));
        rc = unifyfs_stat(*fshdl, gfid, &status);

        /* expected size=filesize since writes have been synced */
        ok((rc == UNIFYFS_SUCCESS) &&
           (status.global_file_size == filesize) && (status.laminated == 0),
           "%s:%d unifyfs_stat(%s) is successful: filesize=%zu (expected=%zu),"
           " laminated=%d (expected=0), rc=%d (%s)",
           __FILE__, __LINE__, testfile,
           status.global_file_size, filesize, status.laminated,
           rc, unifyfs_rc_enum_description(rc));

        /* (3) laminate testfile */
        rc = unifyfs_laminate(*fshdl, testfile);
        ok((rc == UNIFYFS_SUCCESS),
           "%s:%d unifyfs_laminate(%s) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile,
           rc, unifyfs_rc_enum_description(rc));

        /* (4) stat testfile again, should report laminated */
        memset(&status, 0, sizeof(status));
        rc = unifyfs_stat(*fshdl, gfid, &status);
        ok((rc == UNIFYFS_SUCCESS) && (status.laminated == 1),
           "%s:%d unifyfs_stat(%s) is successful: laminated=%d (expected=1),"
           " rc=%d (%s)",
           __FILE__, __LINE__, testfile,
           status.laminated, rc, unifyfs_rc_enum_description(rc));

        /* (5) try write, should fail */
        rc = unifyfs_dispatch_io(*fshdl, 1, fops);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, 1, fops, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_WRITE) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

        int err = fops[0].result.error;
        size_t cnt = fops[0].result.count;
        ok((err == EROFS) && (cnt == 0),
           "%s:%d write(%s) after laminate fails: rc=%d (%s) expected EROFS",
           __FILE__, __LINE__, testfile,
           err, unifyfs_rc_enum_description(err));

        /* (6) read and check full contents of all files */
        memset(readbuf, (int)'?', filesize);
        unifyfs_io_request reads[n_chks];
        for (size_t i = 0; i < n_chks; i++) {
            reads[i].op = UNIFYFS_IOREQ_OP_READ;
            reads[i].gfid = gfid;
            reads[i].nbytes = chksize;
            reads[i].offset = (off_t)(i * chksize);
            reads[i].user_buf = readbuf + (i * chksize);
        }

        rc = unifyfs_dispatch_io(*fshdl, n_chks, reads);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_dispatch_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

        rc = unifyfs_wait_io(*fshdl, n_chks, reads, 1);
        ok(rc == UNIFYFS_SUCCESS,
           "%s:%d unifyfs_wait_io(%s, OP_READ) is successful: rc=%d (%s)",
           __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

        for (size_t i = 0; i < n_chks; i++) {
            size_t bytes = reads[i].nbytes;
            off_t off = reads[i].offset;

            /* check read operation status */
            err = reads[i].result.error;
            cnt = reads[i].result.count;
            ok((err == 0) && (cnt == bytes),
               "%s:%d read(%s, offset=%zu, sz=%zu) is successful: count=%zu,"
               " rc=%d (%s)", __FILE__, __LINE__, testfile, (size_t)off,
               bytes, cnt, err, unifyfs_rc_enum_description(err));

            /* check valid data */
            uint64_t error_offset;
            int check = testutil_lipsum_check(reads[i].user_buf,
                                              (uint64_t)bytes,
                                              (uint64_t)off, &error_offset);
            ok(check == 0,
               "%s:%d read(%s, offset=%zu, sz=%zu) data check is successful",
               __FILE__, __LINE__, testfile, (size_t)off, bytes);
        }
    }

    diag("Finished API lamination tests");

    //-------------

    diag("Removing test file");

    rc = unifyfs_remove(*fshdl, testfile);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile, rc, unifyfs_rc_enum_description(rc));

    //-------------

    return 0;
}
