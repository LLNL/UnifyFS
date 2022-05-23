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

int api_storage_test(char* unifyfs_root,
                     unifyfs_handle* fshdl,
                     size_t filesize,
                     size_t chksize)
{
    /* Create a random file names at the mountpoint path to test */
    char testfile1[64];
    char testfile2[64];
    testutil_rand_path(testfile1, sizeof(testfile1), unifyfs_root);
    testutil_rand_path(testfile2, sizeof(testfile2), unifyfs_root);

    int rc;

    size_t n_chks = filesize / chksize;
    size_t extra = filesize % chksize;
    if (extra) {
        /* test only supports exact multiples of chunk size */
        filesize -= extra;
    }

    char* databuf = malloc(chksize);
    ok(databuf != NULL,
       "%s:%d malloc() of buffer with size=%zu is successful",
       __FILE__, __LINE__, chksize);
    if (NULL == databuf) {
        diag("Initial setup failed");
        return 1;
    }

    testutil_lipsum_generate(databuf, chksize, 0);

    diag("Starting API storage tests");

    /**
     * Overview of test workflow:
     * (1) create new file (testfile1)
     * (2) write and sync testfile1 to use most/all of spillover storage
     * (3) stat testfile1 to verify size
     * (4) create new file (testfile2)
     * (5) 1st attempt to write to testfile2, which should fail due to ENOSPC
     * (6) remove testfile1 to free some storage space
     * (7) 2nd attempt to write to testfile2, which should succeed now
     *     that testfile1 storage allocations have been released
     * (8) stat testfile2 to verify size
     * (9) remove testfile2
     */

    /* (1) create new file (testfile1) */

    int t1_flags = 0;
    unifyfs_gfid t1_gfid = UNIFYFS_INVALID_GFID;
    rc = unifyfs_create(*fshdl, t1_flags, testfile1, &t1_gfid);
    ok((rc == UNIFYFS_SUCCESS) && (t1_gfid != UNIFYFS_INVALID_GFID),
       "%s:%d unifyfs_create(%s) is successful: gfid=%u rc=%d (%s)",
       __FILE__, __LINE__, testfile1, (unsigned int)t1_gfid,
       rc, unifyfs_rc_enum_description(rc));

    /* (2) write and sync testfile1 to use most/all of spillover storage */

    unifyfs_io_request t1_writes[n_chks + 1];
    memset(t1_writes, 0, sizeof(t1_writes));
    for (size_t i = 0; i < n_chks; i++) {
        t1_writes[i].op = UNIFYFS_IOREQ_OP_WRITE;
        t1_writes[i].gfid = t1_gfid;
        t1_writes[i].nbytes = chksize;
        t1_writes[i].offset = (off_t)(i * chksize);
        t1_writes[i].user_buf = databuf;
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

    /* (3) stat testfile1 to verify size */

    unifyfs_file_status t1_status = {0};
    rc = unifyfs_stat(*fshdl, t1_gfid, &t1_status);
    /* expected size=filesize since writes have been synced */
    ok((rc == UNIFYFS_SUCCESS) && (t1_status.global_file_size == filesize),
       "%s:%d unifyfs_stat(gfid=%u) is successful: filesize=%zu (expected=%zu),"
       " rc=%d (%s)", __FILE__, __LINE__, (unsigned int)t1_gfid,
       t1_status.global_file_size, filesize,
       rc, unifyfs_rc_enum_description(rc));

    /* (4) create new file (testfile2) */

    int t2_flags = 0;
    unifyfs_gfid t2_gfid = UNIFYFS_INVALID_GFID;
    rc = unifyfs_create(*fshdl, t2_flags, testfile2, &t2_gfid);
    ok((rc == UNIFYFS_SUCCESS) && (t2_gfid != UNIFYFS_INVALID_GFID),
       "%s:%d unifyfs_create(%s) is successful: gfid=%u rc=%d (%s)",
       __FILE__, __LINE__, testfile2, (unsigned int)t2_gfid,
       rc, unifyfs_rc_enum_description(rc));

    /* (5) 1st attempt to write to testfile2, which should fail due to ENOSPC */

    unifyfs_io_request t2_writes[n_chks];
    memset(t2_writes, 0, sizeof(t2_writes));
    for (size_t i = 0; i < n_chks; i++) {
        t2_writes[i].op = UNIFYFS_IOREQ_OP_WRITE;
        t2_writes[i].gfid = t2_gfid;
        t2_writes[i].nbytes = chksize;
        t2_writes[i].offset = (off_t)(i * chksize);
        t2_writes[i].user_buf = databuf;
    }

    rc = unifyfs_dispatch_io(*fshdl, n_chks, t2_writes);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_dispatch_io(%s, OP_WRITE) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_wait_io(*fshdl, n_chks, t2_writes, 1);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_wait_io(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

    /* check that at least one of the writes hit ENOSPC */
    int no_space_seen = 0;
    for (size_t i = 0; i < n_chks; i++) {
        if (ENOSPC == t2_writes[i].result.error) {
            no_space_seen = 1;
        }
    }
    ok(no_space_seen == 1,
       "%s:%d 1st attempt to write to %s hit ENOSPC",
       __FILE__, __LINE__, testfile2);

    /* (6) remove testfile1 to free some storage space */

    rc = unifyfs_remove(*fshdl, testfile1);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    /* (7) 2nd attempt to write to testfile2, which should succeed */

    unifyfs_io_request t2_writes_2[n_chks + 1];
    memset(t2_writes_2, 0, sizeof(t2_writes_2));
    for (size_t i = 0; i < n_chks; i++) {
        t2_writes_2[i].op = UNIFYFS_IOREQ_OP_WRITE;
        t2_writes_2[i].gfid = t2_gfid;
        t2_writes_2[i].nbytes = chksize;
        t2_writes_2[i].offset = (off_t)(i * chksize);
        t2_writes_2[i].user_buf = databuf;
    }
    t2_writes_2[n_chks].op = UNIFYFS_IOREQ_OP_SYNC_META;
    t2_writes_2[n_chks].gfid = t2_gfid;

    rc = unifyfs_dispatch_io(*fshdl, n_chks + 1, t2_writes_2);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_dispatch_io(%s, OP_WRITE) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_wait_io(*fshdl, n_chks + 1, t2_writes_2, 1);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_wait_io(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    /* check that the writes were successful */
    int err_count = 0;
    for (size_t i = 0; i < n_chks; i++) {
        size_t bytes = t2_writes_2[i].nbytes;
        off_t off = t2_writes_2[i].offset;

        /* check write operation status */
        int err = t2_writes_2[i].result.error;
        size_t cnt = t2_writes_2[i].result.count;
        ok((err == 0) && (cnt == bytes),
           "%s:%d write(%s, offset=%zu, sz=%zu) is successful: count=%zu,"
           " rc=%d (%s)", __FILE__, __LINE__, testfile2, (size_t)off,
           bytes, cnt, err, unifyfs_rc_enum_description(err));
        if (0 != err) {
            err_count++;
        }
    }
    ok(err_count == 0,
       "%s:%d 2nd attempt to write to %s was successful",
       __FILE__, __LINE__, testfile2);

    /* (8) stat testfile2 to verify size */

    unifyfs_file_status t2_status = {0};
    rc = unifyfs_stat(*fshdl, t2_gfid, &t2_status);
    /* expected size=filesize since writes have been synced */
    ok((rc == UNIFYFS_SUCCESS) && (t2_status.global_file_size == filesize),
       "%s:%d unifyfs_stat(gfid=%u) is successful: filesize=%zu (expected=%zu),"
       " rc=%d (%s)", __FILE__, __LINE__, (unsigned int)t2_gfid,
       t2_status.global_file_size, filesize,
       rc, unifyfs_rc_enum_description(rc));

    /* (9) remove testfile2 */

    rc = unifyfs_remove(*fshdl, testfile2);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

    diag("Finished API storage tests");

    return 0;
}
