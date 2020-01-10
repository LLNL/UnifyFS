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
  * Test unlink
  */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

static int unlink_after_sync_test(char* unifyfs_root)
{
    char path[64];
    int rc;
    int fd;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    rc = write(fd, "hello world", 12);
    ok(rc == 12, "%s:%d write() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = close(fd);
    ok(rc == 0, "%s:%d close() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    struct stat sb = {0};
    rc = stat(path, &sb);
    ok(rc == 0, "%s:%d stat() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = unlink(path);
    ok(rc == 0, "%s:%d unlink() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = stat(path, &sb);
    ok(rc == -1, "%s:%d stat() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    return 0;
}

static int unlink_after_sync_laminate_test(char* unifyfs_root)
{
    char path[64];
    int rc;
    int fd;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    rc = write(fd, "hello world", 12);
    ok(rc == 12, "%s:%d write() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = close(fd);
    ok(rc == 0, "%s:%d close() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* Laminate */
    rc = chmod(path, 0444);
    ok(rc == 0, "%s:%d chmod(0444) (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    struct stat sb = {0};
    rc = stat(path, &sb);
    ok(rc == 0, "%s:%d stat() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = unlink(path);
    ok(rc == 0, "%s:%d unlink() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    rc = stat(path, &sb);
    ok(rc == -1, "%s:%d stat() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    return 0;
}

int unlink_test(char* unifyfs_root)
{
    int rc = 0;

    int ret = unlink_after_sync_test(unifyfs_root);
    if (ret != 0) {
        rc = ret;
    }

    ret = unlink_after_sync_laminate_test(unifyfs_root);
    if (ret != 0) {
        rc = ret;
    }

    return rc;
}
