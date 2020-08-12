/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYFS_WRAP(open) found in
 * client/src/unifyfs-sysio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int open_test(char* unifyfs_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYFS_WRAP(open) tests");

    char path[64];
    char dir_path[64];
    int file_mode = 0600;
    int dir_mode = 0700;
    int err, fd, rc;

    /* Create a random file and dir name at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);
    testutil_rand_path(dir_path, sizeof(dir_path), unifyfs_root);

    /* Verify opening a non-existent file without O_CREAT fails with
     * errno=ENOENT */
    errno = 0;
    fd = open(path, O_RDWR, file_mode);
    err = errno;
    ok(fd < 0 && err == ENOENT,
       "open non-existing file %s w/out O_CREATE fails (fd=%d, errno=%d): %s",
       path, fd, err, strerror(err));

    /* Verify we can create a new file. */
    errno = 0;
    fd = open(path, O_CREAT|O_EXCL, file_mode);
    err = errno;
    ok(fd >= 0 && err == 0,
       "open non-existing file %s flags O_CREAT|O_EXCL (fd=%d): %s",
       path, fd, strerror(err));

    ok(close(fd) != -1, "close() worked");

    /* Verify opening an existing file with O_CREAT|O_EXCL fails with
     * errno=EEXIST. */
    errno = 0;
    fd = open(path, O_CREAT|O_EXCL, file_mode);
    err = errno;
    ok(fd < 0 && err == EEXIST,
       "open existing file %s O_CREAT|O_EXCL should fail (fd=%d, errno=%d): %s",
       path, fd, err, strerror(err));

    /* Verify opening an existing file with O_RDWR succeeds. */
    errno = 0;
    fd = open(path, O_RDWR, file_mode);
    err = errno;
    ok(fd >= 0 && err == 0,
       "open existing file %s O_RDWR (fd=%d): %s",
       path, fd, strerror(err));

    ok(close(fd) != -1, "close() worked");

    errno = 0;
    rc = mkdir(dir_path, dir_mode);
    err = errno;
    ok(rc == 0 && err == 0, "mkdir(%s, %o) worked, (errno=%d): %s",
       dir_path, dir_mode, err, strerror(err));

    errno = 0;
    fd = open(dir_path, O_RDWR, file_mode);
    err = errno;
    ok(fd < 0 && err == EISDIR,
       "open directory %s for write should fail (fd=%d, errno=%d): %s",
       dir_path, fd, err, strerror(err));

    /* CLEANUP
     *
     * Don't unlink `path` so that the final test (9020-mountpoint-empty) can
     * check if open left anything in the mountpoint and thus wasn't wrapped
     * properly. */
    ok(rmdir(dir_path) != -1, "rmdir() worked");

    diag("Finished UNIFYFS_WRAP(open) tests");

    return 0;
}
