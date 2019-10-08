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
  * Test write/read/lseek/fsync/stat/chmod
  */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int write_read_test(char* unifyfs_root)
{
    char path[64];
    int rc;
    struct stat sb;
    int fd;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Write to the file */
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd != -1, "%s: open(%s) (fd=%d): %s", __FILE__, path, fd,
        strerror(errno));

    rc = write(fd, "hello world", 12);
    ok(rc == 12, "%s: write() (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Test writing to a different offset */
    rc = lseek(fd, 6, SEEK_SET);
    ok(rc == 6, "%s: lseek() (rc=%d): %s", __FILE__, rc, strerror(errno));

    rc = write(fd, "universe", 9);
    ok(rc == 9, "%s: write() (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Filesize should be zero, since we're not-laminated */
    rc = stat(path, &sb);
    ok(rc == 0, "%s: stat() on non-synced & non-laminated file (rc %d): %s",
        __FILE__, rc, strerror(errno));
    ok(sb.st_size == 0, "%s: file size %ld == 0", __FILE__, sb.st_size);

    rc = fsync(fd);
    ok(rc == 0, "%s: fsync() (rc=%d): %s", __FILE__, rc, strerror(errno));
    close(fd);

    /* Size should still be zero, despite syncing, since it's not-laminated */
    ok(rc == 0, "%s: stat() on synced & non-laminated file (rc %d): %s",
        __FILE__, rc, strerror(errno));
    ok(sb.st_size == 0, "%s: file size %ld == 0", __FILE__, sb.st_size);

    /* Laminate */
    rc = chmod(path, 0444);
    ok(rc == 0, "%s: chmod(0444) (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Verify we're getting the correct file size */
    rc = stat(path, &sb);
    ok(rc == 0, "%s: stat() (rc %d): %s", __FILE__, rc, strerror(errno));
    ok(sb.st_size == 15, "%s: file size %ld == 15", __FILE__, sb.st_size);

    return 0;
}
