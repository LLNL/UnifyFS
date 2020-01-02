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

/* Get global, local, or log sizes (or all) */
static
void get_size(char* path, size_t* global, size_t* local, size_t* log)
{
    struct stat sb = {0};
    int rc;

    rc = stat(path, &sb);
    if (rc != 0) {
        printf("Error: %s\n", strerror(errno));
        exit(1);    /* die on failure */
    }
    if (global) {
        *global = sb.st_size;
    }

    if (local) {
        *local = sb.st_rdev & 0xFFFFFFFF;
    }

    if (log) {
        *log = (sb.st_rdev >> 32) & 0xFFFFFFFF;
    }
}

int write_read_test(char* unifyfs_root)
{
    char path[64];
    int rc;
    int fd;
    size_t global, local, log;

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

    /* Check global and local size on our un-laminated file */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 15, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 21, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));


    rc = fsync(fd);
    ok(rc == 0, "%s: fsync() (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Check global and local size on our un-laminated file */
    get_size(path, &global, &local, &log);
    ok(global == 15, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 15, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 21, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));


    close(fd);

    /* Test O_APPEND */
    fd = open(path, O_WRONLY | O_APPEND, 0222);
    ok(fd != -1, "%s: open(%s, O_APPEND) (fd=%d): %s", __FILE__, path, fd,
        strerror(errno));

    /*
     * Seek to an offset in the file and write.  Since it's O_APPEND, the
     * offset we seeked to doesn't matter - all writes go to the end.
     */
    rc = lseek(fd, 3, SEEK_SET);
    ok(rc == 3, "%s: lseek() (rc=%d): %s", __FILE__, rc, strerror(errno));

    rc = write(fd, "<end>", 6);
    ok(rc == 6, "%s: write() (rc=%d): %s", __FILE__, rc, strerror(errno));
    close(fd);

    /* Check global and local size on our un-laminated file */
    get_size(path, &global, &local, &log);
    ok(global == 21, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 21, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 27, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));


    /* Laminate */
    rc = chmod(path, 0444);
    ok(rc == 0, "%s: chmod(0444) (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Verify we're getting the correct file size */
    get_size(path, &global, &local, &log);
    ok(global == 21, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 21, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 27, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));


    return 0;
}
