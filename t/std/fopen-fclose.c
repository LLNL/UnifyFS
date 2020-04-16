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

#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYFS_WRAP(fopen) and
 * UNIFYFS_WRAP(fclose) found in client/src/unifyfs-stdio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int fopen_fclose_test(char* unifyfs_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYFS_WRAP(fopen/fclose) tests");

    char path[64];
    char path2[64];
    FILE* fd = NULL;

    errno = 0;

    /* Generate a random file name in the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);
    testutil_rand_path(path2, sizeof(path2), unifyfs_root);

    /* Verify fopen a non-existent file as read-only fails with errno=ENOENT. */
    fd = fopen(path, "r");
    ok(fd == NULL && errno == ENOENT,
       "%s:%d fopen non-existent file %s w/ mode r: %s",
       __FILE__, __LINE__, path, strerror(errno));
    errno = 0; /* Reset errno after test for failure */

    /* Verify we can create a new file. */
    fd = fopen(path, "w");
    ok(fd != NULL, "%s:%d fopen non-existing file %s w/ mode w: %s",
       __FILE__, __LINE__, path, strerror(errno));

    /* Verify close succeeds. */
    ok(fclose(fd) == 0, "%s:%d fclose new file: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Verify we can create a new file with mode "a". */
    fd = fopen(path2, "a");
    ok(fd != NULL, "%s:%d fopen non-existing file %s mode a: %s",
       __FILE__, __LINE__, path2, strerror(errno));

    /* Verify close succeeds. */
    ok(fclose(fd) == 0, "%s:%d fclose new file: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Verify opening an existing file with mode "r" succeeds. */
    fd = fopen(path, "r");
    ok(fd != NULL, "%s:%d fopen existing file %s mode r: %s",
       __FILE__, __LINE__, path, strerror(errno));

    /* Verify close succeeds. */
    ok(fclose(fd) == 0, "%s:%d fclose worked: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Verify closing already closed file fails with errno=EBADF */
    ok(fclose(fd) == -1 && errno == EBADF,
       "%s:%d fclose already closed file %s should fail (errno=%d): %s",
       __FILE__, __LINE__, path, errno, strerror(errno));
    errno = 0;

    diag("Finished UNIFYFS_WRAP(fopen/fclose) tests");

    return 0;
}
