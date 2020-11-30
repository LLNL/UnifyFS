/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
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

 /*
  * Test fflush().  Currently this test is skipped until #374 is addressed.
  */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int fflush_test(char* unifyfs_root)
{
    char path[64];
    char buf[64] = {0};
    FILE* fp = NULL;
    int err, rc;

    /* Generate a random file name in the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Write "hello world" to a file */
    errno = 0;
    fp = fopen(path, "w");
    err = errno;
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(err));

    errno = 0;
    rc = fwrite("hello world", 12, 1, fp);
    err = errno;
    ok(rc == 1, "%s: fwrite(\"hello world\"): %s", __FILE__, strerror(err));

    /* Flush the extents */
    errno = 0;
    rc = fflush(fp);
    err = errno;
    ok(rc == 0, "%s: fflush() (rc=%d): %s", __FILE__, rc, strerror(err));

    errno = 0;
    rc = fclose(fp);
    err = errno;
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(err));

    /* Laminate */
    errno = 0;
    rc = chmod(path, 0444);
    err = errno;
    ok(rc == 0, "%s: chmod(0444) (rc=%d): %s", __FILE__, strerror(err));

    /* Read it back */
    errno = 0;
    fp = fopen(path, "r");
    err = errno;
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(err));

    errno = 0;
    rc = fread(buf, 12, 1, fp);
    err = errno;
    ok(rc == 1, "%s: fread() buf[]=\"%s\", (rc %d): %s", __FILE__, buf, rc,
        strerror(err));
    is(buf, "hello world", "%s: saw \"hello world\"", __FILE__);

    errno = 0;
    rc = fclose(fp);
    err = errno;
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(err));

    //end_skip;

    return 0;
}
