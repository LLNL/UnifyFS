/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYCR_WRAP(open) found in
 * client/src/unifycr-sysio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 *  desired results. */
int open_test(char* unifycr_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYCR_WRAP(open) tests");

    char path[64];
    int mode = 0600;
    int fd;
    int rc;

    /* Create a random file name at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifycr_root);

    /* Verify we can create a new file. */
    errno = 0;
    fd = open(path, O_CREAT|O_EXCL, mode);
    ok(fd >= 0, "open non-existing file %s flags O_CREAT|O_EXCL (fd=%d): %s",
       path, fd, strerror(errno));

    /* Verify close succeeds. */
    errno = 0;
    rc = close(fd);
    ok(rc == 0, "close new file %s (rc=%d): %s", path, rc, strerror(errno));

    /* Verify opening an existing file with O_CREAT|O_EXCL fails with
     * errno=EEXIST. */
    errno = 0;
    fd = open(path, O_CREAT|O_EXCL, mode);
    ok(fd < 0 && errno == EEXIST,
       "open existing file %s O_CREAT|O_EXCL should fail (fd=%d, errno=%d): %s",
       path, fd, errno, strerror(errno));

    /* Verify opening an existing file with O_RDWR succeeds. */
    errno = 0;
    fd = open(path, O_RDWR, mode);
    ok(fd >= 0, "open existing file %s O_RDWR (fd=%d): %s",
       path, fd, strerror(errno));

    /* Verify close succeeds. */
    errno = 0;
    rc = close(fd);
    ok(rc == 0, "close %s (rc=%d): %s", path, rc, strerror(errno));

    diag("Finished UNIFYCR_WRAP(open) tests");

    return 0;
}
