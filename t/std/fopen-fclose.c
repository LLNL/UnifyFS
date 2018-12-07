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

#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYCR_WRAP(fopen) and
 * UNIFYCR_WRAP(fclose) found in client/src/unifycr-stdio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int fopen_fclose_test(char* unifycr_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYCR_WRAP(fopen/fclose) tests");

    char path[64];
    char path2[64];
    FILE* fd = NULL;
    int rc;

    /* Generate a random file name in the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifycr_root);
    testutil_rand_path(path2, sizeof(path2), unifycr_root);

    /* Verify we can create a new file. */
    errno = 0;
    fd = fopen(path, "w");
    ok(fd != NULL, "fopen non-existing file %s mode w: %s",
       path, strerror(errno));

    /* Verify close succeeds. */
    errno = 0;
    rc = fclose(fd);
    ok(rc == 0, "fclose new file %s (rc=%d): %s", path, rc, strerror(errno));

    /* Verify we can create a new file with mode "a". */
    errno = 0;
    fd = fopen(path2, "a");
    ok(fd != NULL, "fopen non-existing file %s mode a: %s",
       path2, strerror(errno));

    /* Verify close succeeds. */
    errno = 0;
    rc = fclose(fd);
    ok(rc == 0, "fclose new file %s (rc=%d): %s", path, rc, strerror(errno));

    /* Verify opening an existing file with mode "r" succeeds. */
    errno = 0;
    fd = fopen(path, "r");
    ok(fd != NULL, "fopen existing file %s mode r: %s",
       path, strerror(errno));

    /* Verify close succeeds. */
    errno = 0;
    rc = fclose(fd);
    ok(rc == 0, "fclose %s (rc=%d): %s", path, rc, strerror(errno));

    /* Verify closing already closed file fails with errno=EBADF */
    errno = 0;
    rc = fclose(fd);
    ok(rc < 0 && errno == EBADF,
       "fclose already closed file %s should fail (rc=%d, errno=%d): %s",
       path, rc, errno, strerror(errno));

    diag("Finished UNIFYCR_WRAP(fopen/fclose) tests");

    return 0;
}
