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
  * Test reading from file with holes
  */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
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

static int check_contents(char* buf, size_t len, char c)
{
    int valid = 1;
    size_t i;
    for (i = 0; i < len; i++) {
        if (buf[i] != c) {
            valid = 0;
        }
    }
    return valid;
}

int write_read_hole_test(char* unifyfs_root)
{
    char path[64];
    int rc;
    int fd;
    size_t global, local, log;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);
    int i;
    for (i = 0; i < bufsize; i++) {
        buf[i] = 1;
    }

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* create a file that contains:
     * [0, 1MB)   - data = "1"
     * [1MB, 2MB) - hole = "0" implied
     * [2MB, 3MB) - data = "1"
     * [3MB, 4MB) - hole = "0" implied */

    /* Write to the file */
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* write "1" to [0MB, 1MB) */
    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* skip over [1MB, 2MB) for implied "0" */
    rc = lseek(fd, 2*bufsize, SEEK_SET);
    ok(rc == 2*bufsize, "%s:%d lseek() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* write "1" to [2MB, 3MB) */
    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* Check global and local size on our un-laminated file */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d: %s",
        __FILE__, __LINE__, global, strerror(errno));
    ok(local == 3*bufsize, "%s:%d local size is %d: %s",
        __FILE__, __LINE__, local, strerror(errno));
    ok(log == 2*bufsize, "%s:%d log size is %d: %s",
        __FILE__, __LINE__, log, strerror(errno));

    /* flush writes */
    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* Check global and local size on our un-laminated file */
    get_size(path, &global, &local, &log);
    ok(global == 3*bufsize, "%s:%d global size is %d: %s",
        __FILE__, __LINE__, global, strerror(errno));
    ok(local == 3*bufsize, "%s:%d local size is %d: %s",
        __FILE__, __LINE__, local, strerror(errno));
    ok(log == 2*bufsize, "%s:%d log size is %d: %s",
        __FILE__, __LINE__, log, strerror(errno));

    /* truncate file at 4MB, extends file so that
     * [3MB, 4MB) is implied "0" */
    rc = ftruncate(fd, 4*bufsize);
    ok(rc == 0, "%s:%d ftruncate() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* Laminate */
    rc = chmod(path, 0444);
    ok(rc == 0, "%s:%d chmod(0444) (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* Check global and local size on our un-laminated file */
    get_size(path, &global, &local, &log);
    ok(global == 4*bufsize, "%s:%d global size is %d: %s",
        __FILE__, __LINE__, global, strerror(errno));
    ok(local == 4*bufsize, "%s:%d local size is %d: %s",
        __FILE__, __LINE__, local, strerror(errno));
    ok(log == 2*bufsize, "%s:%d log size is %d: %s",
        __FILE__, __LINE__, log, strerror(errno));

    close(fd);

    /***************
     * open file for reading
     ***************/

    fd = open(path, O_RDONLY);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));


    /* read segment [0, 1MB) -- should be all "1"
     * this should be a full read, all from actual data */
    memset(buf, 2, bufsize);
    ssize_t nread = pread(fd, buf, bufsize, 0*bufsize);
    ok(nread == bufsize,
        "%s:%d pread expected=%llu got=%llu: errno=%s",
        __FILE__, __LINE__,
        (unsigned long long) bufsize, (unsigned long long) nread,
        strerror(errno));

    /* check that full buffer is "1" */
    int valid = check_contents(buf, bufsize, 1);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);


    /* read segment [1MB, 2MB) -- should be all "0"
     * this should be a full read, all from a hole */
    memset(buf, 2, bufsize);
    nread = pread(fd, buf, bufsize, 1*bufsize);
    ok(nread == bufsize,
        "%s:%d pread expected=%llu got=%llu: errno=%s",
        __FILE__, __LINE__,
        (unsigned long long) bufsize, (unsigned long long) nread,
        strerror(errno));

    /* check that full buffer is "0" */
    valid = check_contents(buf, bufsize, 0);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);


    /* read segment [0.5MB, 1.5MB)
     * should be a full read, half data, half hole */
    memset(buf, 2, bufsize);
    nread = pread(fd, buf, bufsize, bufsize/2);
    ok(nread == bufsize,
        "%s:%d pread expected=%llu got=%llu: errno=%s",
        __FILE__, __LINE__,
        (unsigned long long) bufsize, (unsigned long long) nread,
        strerror(errno));

    /* check that data portion is "1" */
    valid = check_contents(buf, bufsize/2, 1);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);

    /* check that hole portion is "0" */
    valid = check_contents(buf + bufsize/2, bufsize/2, 0);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);


    /* read segment [3.5MB, 4.5MB)
     * should read only half of requested amount,
     * half hole, half past end of file */
    memset(buf, 2, bufsize);
    nread = pread(fd, buf, bufsize, 3*bufsize + bufsize/2);
    ok(nread == bufsize/2,
        "%s:%d pread expected=%llu got=%llu: errno=%s",
        __FILE__, __LINE__,
        (unsigned long long) bufsize/2, (unsigned long long) nread,
        strerror(errno));

    /* first half of buffer should be "0" */
    valid = check_contents(buf, bufsize/2, 0);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);

    /* second half of buffer should not be changed, still "2" */
    valid = check_contents(buf + bufsize/2, bufsize/2, 2);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);


    close(fd);

    free(buf);

    return 0;
}
