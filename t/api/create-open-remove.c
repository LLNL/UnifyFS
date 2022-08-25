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
#include <fcntl.h>

int api_create_open_remove_test(char* unifyfs_root,
                                unifyfs_handle* fshdl)
{
    /* Create a random file names at the mountpoint path to test */
    char testfile1[64];
    char testfile2[64];
    testutil_rand_path(testfile1, sizeof(testfile1), unifyfs_root);
    testutil_rand_path(testfile2, sizeof(testfile2), unifyfs_root);

    //-------------

    diag("Starting API create tests");

    int t1_flags = 0;
    int t2_flags = 0;
    unifyfs_gfid t1_gfid = UNIFYFS_INVALID_GFID;
    unifyfs_gfid t2_gfid = UNIFYFS_INVALID_GFID;
    unifyfs_gfid dummy_gfid = UNIFYFS_INVALID_GFID;

    int rc = unifyfs_create(*fshdl, t1_flags, testfile1, &t1_gfid);
    ok(rc == UNIFYFS_SUCCESS && t1_gfid != UNIFYFS_INVALID_GFID,
       "%s:%d unifyfs_create(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_create(*fshdl, t2_flags, testfile2, &t2_gfid);
    ok(rc == UNIFYFS_SUCCESS && t2_gfid != UNIFYFS_INVALID_GFID,
       "%s:%d unifyfs_create(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_create(*fshdl, t2_flags, testfile2, &dummy_gfid);
    ok(rc != UNIFYFS_SUCCESS && dummy_gfid == UNIFYFS_INVALID_GFID,
       "%s:%d unifyfs_create(%s) for existing file fails: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

    diag("Finished API create tests");

    //-------------

    diag("Starting API open tests");

    int rdwr_flags = O_RDWR;
    int rd_flags = O_RDONLY;
    unifyfs_gfid t3_gfid = UNIFYFS_INVALID_GFID;
    unifyfs_gfid t4_gfid = UNIFYFS_INVALID_GFID;

    rc = unifyfs_open(*fshdl, rdwr_flags, testfile1, &t3_gfid);
    ok(rc == UNIFYFS_SUCCESS && t3_gfid == t1_gfid,
       "%s:%d unifyfs_open(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));

    rc = unifyfs_open(*fshdl, rd_flags, testfile2, &t4_gfid);
    ok(rc == UNIFYFS_SUCCESS && t4_gfid == t2_gfid,
       "%s:%d unifyfs_open(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));

    diag("Finished API open tests");

    //-------------

    diag("Starting API remove tests");

    rc = unifyfs_remove(*fshdl, testfile1);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));
    if (UNIFYFS_SUCCESS == rc) {
        unifyfs_gfid t5_gfid = UNIFYFS_INVALID_GFID;
        rc = unifyfs_open(*fshdl, rd_flags, testfile1, &t5_gfid);
        ok(rc != UNIFYFS_SUCCESS && t5_gfid == UNIFYFS_INVALID_GFID,
           "%s:%d unifyfs_open(%s) after unifyfs_remove() fails: rc=%d (%s)",
           __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));
    }

    rc = unifyfs_remove(*fshdl, testfile2);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
       __FILE__, __LINE__, testfile2, rc, unifyfs_rc_enum_description(rc));
    if (UNIFYFS_SUCCESS == rc) {
        unifyfs_gfid t6_gfid = UNIFYFS_INVALID_GFID;
        rc = unifyfs_open(*fshdl, rd_flags, testfile1, &t6_gfid);
        ok(rc != UNIFYFS_SUCCESS && t6_gfid == UNIFYFS_INVALID_GFID,
           "%s:%d unifyfs_open(%s) after unifyfs_remove() fails: rc=%d (%s)",
           __FILE__, __LINE__, testfile1, rc, unifyfs_rc_enum_description(rc));
    }

    diag("Finished API remove tests");

    //-------------

    return 0;
}
