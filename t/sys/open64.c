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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <unifyfs.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYFS_WRAP(open64) found in
 * client/src/unifyfs-sysio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int open64_test(char* unifyfs_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYFS_WRAP(open64) tests");

    char path[64];
    int mode = 0600;
    int err, fd;

    /* Create a random file name at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Verify opening a non-existent file without O_CREAT fails with
     * errno=ENOENT */
    errno = 0;
    fd = open64(path, O_RDWR, mode);
    err = errno;
    ok(fd < 0 && err == ENOENT,
       "open64 non-existing file %s w/out O_CREATE fails (fd=%d, errno=%d): %s",
       path, fd, err, strerror(err));

    /* Verify we can create a new file. */
    errno = 0;
    fd = open64(path, O_CREAT|O_EXCL, mode);
    err = errno;
    ok(fd >= 0 && err == 0,
       "open64 non-existing file %s flags O_CREAT|O_EXCL (fd=%d): %s",
       path, fd, strerror(err));

    ok(close(fd) != -1, "close() worked");

    /* Verify opening an existing file with O_CREAT|O_EXCL fails with
     * errno=EEXIST. */
    errno = 0;
    fd = open64(path, O_CREAT|O_EXCL, mode);
    err = errno;
    ok(fd < 0 && err == EEXIST,
       "open64 existing file %s O_CREAT|O_EXCL fails (fd=%d, errno=%d): %s",
       path, fd, err, strerror(err));

    /* Verify opening an existing file with O_RDWR succeeds. */
    errno = 0;
    fd = open64(path, O_RDWR, mode);
    err = errno;
    ok(fd >= 0 && err == 0,
       "open64 existing file %s O_RDWR (fd=%d): %s",
       path, fd, strerror(err));

    ok(close(fd) != -1, "close() worked");

    diag("Finished UNIFYFS_WRAP(open64) tests");

    return 0;
}
