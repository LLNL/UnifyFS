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
    int rc;

    /* Generate a random file name in the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);
    skip(1, 9, "remove when fflush() sync extents (issue #374)");

    /* Write "hello world" to a file */
    fp = fopen(path, "w");
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(errno));

    rc = fwrite("hello world", 12, 1, fp);
    ok(rc == 1, "%s: fwrite(\"hello world\"): %s", __FILE__, strerror(errno));

    /* Flush the extents */
    rc = fflush(fp);
    ok(rc == 0, "%s: fflush() (rc=%d): %s", __FILE__, rc, strerror(errno));

    rc = fclose(fp);
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Laminate */
    rc = chmod(path, 0444);
    ok(rc == 0, "%s: chmod(0444) (rc=%d): %s", __FILE__, strerror(errno));

    /* Read it back */
    fp = fopen(path, "r");
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(errno));

    rc = fread(buf, 12, 1, fp);
    ok(rc == 1, "%s: fread() buf[]=\"%s\", (rc %d): %s", __FILE__, buf, rc,
        strerror(errno));
    is(buf, "hello world", "%s: saw \"hello world\"", __FILE__);

    rc = fclose(fp);
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(errno));

    end_skip;

    return 0;
}
