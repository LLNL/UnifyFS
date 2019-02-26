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

/* This function contains the tests for UNIFYCR_WRAP(creat64) found in
 * client/src/unifycr-sysio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int creat64_test(char* unifycr_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYCR_WRAP(creat64) tests");

    char path[64];
    int mode = 0600;
    int fd;
    int rc;

    /* Create a random file name at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifycr_root);

    skip(1, 2, "remove when UNIFYCR(create64) has been implemented");
    /* Verify we can create a non-existent file. */
    errno = 0;
    fd = creat64(path, mode);
    ok(fd >= 0, "creat64 non-existing file %s (fd=%d): %s",
       path, fd, strerror(errno));

    rc = close(fd);

    /* Verify creating an already created file succeeds. */
    errno = 0;
    fd = creat64(path, mode);
    ok(fd >= 0, "creat64 existing file %s (fd=%d): %s",
       path, fd, strerror(errno));

    rc = close(fd);
    end_skip;

    diag("Finished UNIFYCR_WRAP(creat64) tests");

    return 0;
}
