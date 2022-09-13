/*
 * Copyright (c) 2022, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2022, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "api_suite.h"

#define NUM_TEST_FILES 3
// 3 files is probably enough

int api_get_gfids_and_metadata_test(char* unifyfs_root,
                                    unifyfs_handle* fshdl,
                                    size_t filesize)
{
    int rc;
    /* Create a random file names at the mountpoint path to test */
    char testfiles[NUM_TEST_FILES][64];
    for (unsigned int i = 0; i < NUM_TEST_FILES; i++) {
        testutil_rand_path(testfiles[i], sizeof(testfiles[i]), unifyfs_root);
    }

    //-------------
    diag("Creating test files");

    int tf_flags[NUM_TEST_FILES];
    unifyfs_gfid tf_gfids[NUM_TEST_FILES];
    for (unsigned int i = 0; i < NUM_TEST_FILES; i++) {
        tf_flags[i] = 0;
        tf_gfids[i] = 0;

        rc = unifyfs_create(*fshdl, tf_flags[i], testfiles[i], &tf_gfids[i]);
        ok((rc == UNIFYFS_SUCCESS) && (tf_gfids[i] != UNIFYFS_INVALID_GFID),
            "%s:%d unifyfs_create(%s) is successful: rc=%d (%s)",
            __FILE__, __LINE__, testfiles[i], rc,
            unifyfs_rc_enum_description(rc));
    }

    //-------------

    diag("Starting API get_gfid_list/get_server_file_metadata tests");

    /**
     * Overview of test workflow:
     * (1) Create the 3 test fielswrite and sync testfile1 (no hole)
     * (2) Get the list of gfids - should be (1+NUM_TEST_FILES) since the
     *     mountpoint is one
     * (4) Get the metadata struct for each gfid and verify each member of
     *     the struct.
     */

    char* databuf = malloc(filesize);
    char* readbuf = malloc(filesize);
    if ((NULL != databuf) && (NULL != readbuf)) {
        testutil_lipsum_generate(databuf, filesize, 0);

        /* (1) write the test files  */
        unifyfs_io_request tf_writes[NUM_TEST_FILES];
        for (unsigned int file_num = 0;
             file_num < NUM_TEST_FILES; file_num++) {
            tf_writes[file_num].op = UNIFYFS_IOREQ_OP_WRITE;
            tf_writes[file_num].gfid = tf_gfids[file_num];
            tf_writes[file_num].nbytes = filesize;
            tf_writes[file_num].offset = (off_t)0;
            tf_writes[file_num].user_buf = databuf;

            rc = unifyfs_dispatch_io(*fshdl, 1, &tf_writes[file_num]);
            if (!ok((rc == UNIFYFS_SUCCESS),
                    "unifyfs_dispatch_io(%s, OP_WRITE)",
                    testfiles[file_num])) {
                diag("unifyfs_dispatch_io(%s, OP_WRITE) failed: rc=%d (%s)",
                     testfiles[file_num], rc, unifyfs_rc_enum_description(rc));
            }
        }

        for (unsigned int file_num = 0; file_num < NUM_TEST_FILES; file_num++) {
            rc = unifyfs_wait_io(*fshdl, 1, &tf_writes[file_num], 1);
            if (!ok((rc == UNIFYFS_SUCCESS),
                    "unifyfs_wait_io(%s, OP_WRITE)", testfiles[file_num])) {
                diag("unifyfs_wait_io(%s, OP_WRITE) failed: rc=%d (%s)",
                     testfiles[file_num], rc, unifyfs_rc_enum_description(rc));
            }
        }

        for (unsigned int file_num = 0; file_num < NUM_TEST_FILES; file_num++) {
            rc = unifyfs_sync(*fshdl, tf_gfids[file_num]);
            if (!ok((rc == UNIFYFS_SUCCESS),
                    "unifyfs_sync()  gfid: %d", tf_gfids[file_num])) {
                diag("unifyfs_sync() for gfid %d failed: rc=%d (%s)",
                     tf_gfids[file_num], rc, unifyfs_rc_enum_description(rc));
            }
        }

        /* (2) Get the list of all the GFIDs that the server knows about */
        int num_gfids;
        unifyfs_gfid* gfid_list;
        rc = unifyfs_get_gfid_list(*fshdl, &num_gfids, &gfid_list);
        if (!ok((rc == UNIFYFS_SUCCESS),
                "unifyfs_get_gfid_list() is successful")) {
            diag("unifyfs_get_gfid_list() failed: rc=%d (%s)",
                 rc, unifyfs_rc_enum_description(rc));
        }
#if 0
/* We have to comment out this check because other unit tests leave files
 * behind when they run, so we can't complain if we get back more gfids than
 * we created above.  If we clean up all the other unit tests, then we can
 * re-enable this check.
 */
        if (!ok((NUM_TEST_FILES + 1) == num_gfids,
                "unifyfs_get_gfid_list() returned the expected number "
                "of GFIDs")) {
            diag("unifyfs_get_gfid_list() returned %d gfids.  Expected %d",
                 num_gfids, NUM_TEST_FILES + 1);
        }
#else
        /* This test isn't as precise as the one above, but at least it
         * checks something.
         */
        if (!cmp_ok(num_gfids, ">=", (NUM_TEST_FILES + 1),
                    "check number of GFIDs returned by "
                    "unifyfs_get_gfid_list()")) {
            diag("unifyfs_get_gfid_list() returned %d gfids.  "
                 "Expected at least %d", num_gfids, NUM_TEST_FILES + 1);
        }
#endif

        /* (3) Check each file's metadata */

        for (unsigned int i = 0; i < num_gfids; i++) {
            unifyfs_server_file_meta fmeta;
            rc = unifyfs_get_server_file_meta(*fshdl, gfid_list[i], &fmeta);
            if (!ok(rc == UNIFYFS_SUCCESS,
                    "unifyfs_get_server_file_meta() is successful")) {
                diag("unifyfs_get_server_file_meta() failed: rc=%d (%s)",
                     rc, unifyfs_rc_enum_description(rc));
            }

            /* unifyfs_get_gfid_list() isn't guaranteed to return the GFIDs in
             * any particular order.  We could sort the list, but the number of
             * test files is small enough that it's not worth the effort.
             */
            bool gfid_found = false;
            for (unsigned int j = 0; j < NUM_TEST_FILES; j++) {
                if (fmeta.gfid == tf_gfids[j]) {
                    gfid_found = true;
                    if (!is(fmeta.filename, testfiles[j],
                            "unifyfs_get_server_file_meta(): "
                            "checking filename")) {
                        diag("filename mismatch:  fmeta.filename: %s    "
                             "Expected filename: %s", fmeta.filename,
                             testfiles[j]);
                    }
                    if (!ok((fmeta.size == filesize),
                            "unifyfs_get_server_file_meta(): checking "
                            "file size")) {
                        diag("file size mismatch:  fmeta.size: %d,  "
                             "Expected size: %d", fmeta.size, filesize);
                    }
                    if (!ok((false == fmeta.is_laminated),
                            "unifyfs_get_server_file_meta(): is "
                            "file laminated")) {
                        diag("is_laminated mismatch:  fmeta.is_laminated: %d"
                             "   Expected: %d (boolean comparison)",
                             fmeta.is_laminated, false);
                    }
                    if (!ok((true == fmeta.is_shared),
                            "unifyfs_get_server_file_meta(): is file shared")) {
                        diag("is_shared mismatch:  fmeta.is_shared: %d"
                              "   Expected: %d (boolean comparison)",
                              fmeta.is_shared, true);
                    }
                    if (!ok((0100644 == fmeta.mode),  // mode number is in octal
                            "unifyfs_get_server_file_meta(): checking "
                            "mode value")) {
                        diag("mode value mismatch:  fmeta.mode: %o  "
                             "Expected mode: %o", fmeta.mode, 0100644);
                    }
                    if (!ok((fmeta.uid == geteuid()),
                            "%s:%d unifyfs_get_server_file_meta(): checking"
                            " uid value", __FILE__, __LINE__)) {
                        diag("uid value mismatch: fmeta.uid: %d     "
                             "Expected uid: %d", fmeta.uid, geteuid());
                    }
                    if (!ok((fmeta.gid == getegid()),
                            "%s:%d unifyfs_get_server_file_meta(): "
                            "checking gid value", __FILE__, __LINE__)) {
                        diag("gid value mismatch:  fmeta.gid: %d    "
                             "Expected gid: %d", fmeta.gid, getegid());
                    }
                }
            }
            if (false == gfid_found) {
#if 0
/* We have to comment out this check because other unit tests leave files
 * behind when they run, so we can't complain if we find files that this
 * unit test didn't create.  If we clean up all the other unit tests,
 * then we can re-enable this check.
 */
                // We didn't find the gfid in the list of files we created.
                // If this gfid is for the mountpoint (presumably /unifyfs),
                // then all is fine.  If not, it's an error.
                if (!is(fmeta.filename, unifyfs_root,
                        "unifyfs_get_server_file_meta(): look for mount"
                        " point entry")) {
                    diag("Was expecting the filname to match the mount "
                         "point (%s), but got %s instead", unifyfs_root,
                         fmeta.filename);
                }
                // TODO: add directory-specific tests here!
#endif
            }
        }
    }

    diag("Finished API get_gfid_list/get_server_file_metadata tests");

    //-------------

    diag("Removing test files");

    for (unsigned int i = 0; i < NUM_TEST_FILES; i++) {
        if (tf_gfids[i] != UNIFYFS_INVALID_GFID) {
            rc = unifyfs_remove(*fshdl, testfiles[i]);
            ok(rc == UNIFYFS_SUCCESS,
               "%s:%d unifyfs_remove(%s) is successful: rc=%d (%s)",
               __FILE__, __LINE__, testfiles[i], rc,
               unifyfs_rc_enum_description(rc));
        }
    }

    //-------------

    return 0;
}
