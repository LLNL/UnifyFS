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
  * Test truncate and ftruncate
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

int truncate_test(char* unifyfs_root)
{
    char path[64];
    int rc;
    int fd;
    size_t global, local, log;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    ok(fd != -1, "%s: open(%s) (fd=%d): %s", __FILE__, path, fd,
        strerror(errno));

    /* fsync -- file should be 0 bytes at this point */
    rc = fsync(fd);
    ok(rc == 0, "%s: fsync() (rc=%d): %s", __FILE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 0, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 0, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 0, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));

    /* write 1MB and fsync, expect 1MB */
    rc = write(fd, buf, bufsize);
    ok(rc == 12, "%s: write() (rc=%d): %s", __FILE__, rc, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s: fsync() (rc=%d): %s", __FILE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 1*bufsize, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 1*bufsize, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 1*bufsize, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));

    /* skip a 1MB hole, write another 1MB, and fsync expect 3MB */
    rc = lseek(fd, 2*bufsize, SEEK_SET);
    ok(rc == 2*bufsize, "%s: lseek() (rc=%d): %s", __FILE__, rc, strerror(errno));

    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s: write() (rc=%d): %s", __FILE__, rc, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s: fsync() (rc=%d): %s", __FILE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 3*bufsize, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 3*bufsize, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 2*bufsize, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));

    /* ftruncate at 5MB, expect 5MB */
    rc = ftruncate(fd, 5*bufsize);
    ok(rc == 0, "%s: ftruncate() (rc=%d): %s", __FILE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 5*bufsize, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 5*bufsize, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 2*bufsize, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));

    close(fd);

    /* truncate at 0.5 MB, expect 0.5MB */
    rc = truncate(path, bufsize/2);
    ok(rc == 0, "%s: truncate() (rc=%d): %s", __FILE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == bufsize/2, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == bufsize/2, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 2*bufsize, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));

    free(buf);

    return 0;
}
