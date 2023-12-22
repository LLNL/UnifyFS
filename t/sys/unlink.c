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
    int err, fd, rc;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open(%s) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    errno = 0;
    rc = (int) write(fd, "hello world", 12);
    err = errno;
    ok(rc == 12 && err == 0,
       "%s:%d write(): %s",
       __FILE__, __LINE__, strerror(err));

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

    struct stat sb = {0};
    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d stat(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d unlink(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d stat() after unlink fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d unlink() already unlinked file fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    return 0;
}

static int unlink_after_sync_laminate_test(char* unifyfs_root)
{
    char path[64];
    int err, fd, rc;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open(%s) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    errno = 0;
    rc = (int) write(fd, "hello world", 12);
    err = errno;
    ok(rc == 12 && err == 0, "%s:%d write(): %s",
       __FILE__, __LINE__, strerror(err));

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

    /* Laminate */
    errno = 0;
    rc = chmod(path, 0444);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chmod(0444): %s",
       __FILE__, __LINE__, strerror(err));

    struct stat sb = {0};
    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d stat(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d unlink(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d stat() after unlink fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d unlink() already unlinked, laminated file fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    return 0;
}

int unlink_test(char* unifyfs_root)
{
    diag("Starting UNIFYFS_WRAP(unlink) tests");

    char path[64];
    char dir_path[64];
    int err, fd, rc;

    testutil_rand_path(path, sizeof(path), unifyfs_root);
    testutil_rand_path(dir_path, sizeof(dir_path), unifyfs_root);

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d unlink() non-existent file fails (errno=%d): %s",
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

    struct stat sb = {0};
    errno = 0;
    rc = stat(path, &sb);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d stat(): %s",
       __FILE__, __LINE__, strerror(err));

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
    rc = unlink(path);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d unlink() already unlinked, empty file fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* Calling unlink() on a directory should fail */
    errno = 0;
    rc = mkdir(dir_path, 0777);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d mkdir(%s): %s",
       __FILE__, __LINE__, dir_path, strerror(err));

    errno = 0;
    rc = unlink(dir_path);
    err = errno;
    ok(rc == -1 && err == EISDIR,
       "%s:%d unlink() a directory fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = rmdir(dir_path);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d rmdir(): %s",
       __FILE__, __LINE__, strerror(err));

    /* Tests for unlink after writing to a file */
    int ret = unlink_after_sync_test(unifyfs_root);
    if (ret != 0) {
        rc = ret;
    }

    ret = unlink_after_sync_laminate_test(unifyfs_root);
    if (ret != 0) {
        rc = ret;
    }

    diag("Finished UNIFYFS_WRAP(unlink) tests");

    return rc;
}
