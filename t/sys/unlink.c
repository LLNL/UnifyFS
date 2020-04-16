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
    int fd;
    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    ok(write(fd, "hello world", 12) == 12, "%s:%d write(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(fsync(fd) == 0, "%s:%d fsync(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(close(fd) == 0, "%s:%d close(): %s",
        __FILE__, __LINE__, strerror(errno));

    struct stat sb = {0};
    ok(stat(path, &sb) == 0, "%s:%d stat(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(unlink(path) == 0, "%s:%d unlink(): %s",
        __FILE__, __LINE__, strerror(errno));

    todo("Failing with wrong errno. Should fail with errno=ENOENT=2");
    ok(stat(path, &sb) == -1 && errno == ENOENT,
       "%s:%d stat() after unlink fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    end_todo;
    errno = 0; /* Reset errno after test for failure */

    ok(unlink(path) == -1 && errno == ENOENT,
       "%s:%d unlink() already unlinked file fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    return 0;
}

static int unlink_after_sync_laminate_test(char* unifyfs_root)
{
    char path[64];
    int fd;
    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    ok(write(fd, "hello world", 12) == 12, "%s:%d write(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(fsync(fd) == 0, "%s:%d fsync(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(close(fd) == 0, "%s:%d close(): %s",
        __FILE__, __LINE__, strerror(errno));

    /* Laminate */
    ok(chmod(path, 0444) == 0, "%s:%d chmod(0444): %s",
        __FILE__, __LINE__, strerror(errno));

    struct stat sb = {0};
    ok(stat(path, &sb) == 0, "%s:%d stat(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(unlink(path) == 0, "%s:%d unlink(): %s",
        __FILE__, __LINE__, strerror(errno));

    todo("Failing with wrong errno. Should fail with errno=ENOENT=2");
    ok(stat(path, &sb) == -1 && errno == ENOENT,
       "%s:%d stat() after unlink fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    end_todo;
    errno = 0;

    ok(unlink(path) == -1 && errno == ENOENT,
       "%s:%d unlink() already unlinked, laminated file fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    return 0;
}

int unlink_test(char* unifyfs_root)
{
    diag("Finished UNIFYFS_WRAP(unlink) tests");

    char path[64];
    char dir_path[64];
    int fd;
    int rc = 0;
    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);
    testutil_rand_path(dir_path, sizeof(dir_path), unifyfs_root);

    ok(unlink(path) == -1 && errno == ENOENT,
       "%s:%d unlink() non-existent file fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    errno = 0; /* Reset errno after test for failure */

    fd = creat(path, 0222);
    ok(fd != -1, "%s:%d creat(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    ok(fsync(fd) == 0, "%s:%d fsync(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(close(fd) == 0, "%s:%d close(): %s",
        __FILE__, __LINE__, strerror(errno));

    struct stat sb = {0};
    ok(stat(path, &sb) == 0, "%s:%d stat(): %s",
        __FILE__, __LINE__, strerror(errno));

    ok(unlink(path) == 0, "%s:%d unlink() empty file: %s",
        __FILE__, __LINE__, strerror(errno));

    todo("Failing with wrong errno. Should fail with errno=ENOENT=2");
    ok(stat(path, &sb) == -1 && errno == ENOENT,
       "%s:%d stat() after unlink fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    end_todo;
    errno = 0;

    ok(unlink(path) == -1 && errno == ENOENT,
       "%s:%d unlink() already unlinked, empty file fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    errno = 0; /* Reset errno after test for failure */

    /* Calling unlink() on a directory should fail */
    ok(mkdir(dir_path, 0777) == 0, "%s:%d mkdir(%s): %s",
        __FILE__, __LINE__, dir_path, strerror(errno));

    ok(unlink(dir_path) == -1 && errno == EISDIR,
       "%s:%d unlink() a directory fails (errno=%d): %s",
        __FILE__, __LINE__, errno, strerror(errno));
    errno = 0; /* Reset errno after test for failure */

    ok(rmdir(dir_path) == 0, "%s:%d rmdir(): %s",
        __FILE__, __LINE__, strerror(errno));


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
