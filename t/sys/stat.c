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
  * Test stat, lstat, fstat, __xstat, __lxstst, __fxstat
  */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int stat_test(char* unifyfs_root)
{
    diag("Starting UNIFYFS_WRAP(stat) tests");

    char path[64];
    char dir_path[64];
    int err, fd, rc;
    struct stat sb = {0};

    testutil_rand_path(path, sizeof(path), unifyfs_root);
    testutil_rand_path(dir_path, sizeof(dir_path), unifyfs_root);

    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d stat() non-existent file fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = lstat(path, &sb);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d lstat() non-existent file fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    fd = creat(path, 0222);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d creat(%s) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fsync(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d stat(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = lstat(path, &sb);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d lstat(): %s",
       __FILE__, __LINE__, strerror(err));

    /*
    errno = 0;
    rc = fstat(fd, &sb);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fstat(): %s",
       __FILE__, __LINE__, strerror(err));
    */

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d unlink() empty file: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d stat() after unlink fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = lstat(path, &sb);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d lstat() after unlink fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /*
    errno = 0;
    rc = fstat(fd, &sb);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d fstat() after unlink fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));
    */

    diag("Finished UNIFYFS_WRAP(stat) tests");

    return 0;
}
