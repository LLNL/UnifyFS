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
    int err, rc;
    int fd;
    size_t global;

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
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* write "1" to [0MB, 1MB) */
    errno = 0;
    rc = write(fd, buf, bufsize);
    err = errno;
    ok(rc == bufsize, "%s:%d write() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* skip over [1MB, 2MB) for implied "0" */
    errno = 0;
    rc = lseek(fd, 2*bufsize, SEEK_SET);
    err = errno;
    ok(rc == 2*bufsize, "%s:%d lseek() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* write "1" to [2MB, 3MB) */
    errno = 0;
    rc = write(fd, buf, bufsize);
    err = errno;
    ok(rc == bufsize, "%s:%d write() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == 3*bufsize, "%s:%d global size is %zu: %s",
        __FILE__, __LINE__, global, strerror(err));

    /* flush writes */
    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == 3*bufsize, "%s:%d global size is %zu: %s",
        __FILE__, __LINE__, global, strerror(err));

    /* truncate file at 4MB, extends file so that
     * [3MB, 4MB) is implied "0" */
    errno = 0;
    rc = ftruncate(fd, 4*bufsize);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* Laminate */
    errno = 0;
    rc = chmod(path, 0444);
    err = errno;
    ok(rc == 0, "%s:%d chmod(0444) (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == 4*bufsize, "%s:%d global size is %zu: %s",
        __FILE__, __LINE__, global, strerror(err));

    close(fd);

    /***************
     * open file for reading
     ***************/

    errno = 0;
    fd = open(path, O_RDONLY);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));


    /* read segment [0, 1MB) -- should be all "1"
     * this should be a full read, all from actual data */
    memset(buf, 2, bufsize);
    errno = 0;
    ssize_t nread = pread(fd, buf, bufsize, 0);
    err = errno;
    ok(nread == bufsize,
        "%s:%d pread expected=%zu got=%zd: errno=%s",
        __FILE__, __LINE__, bufsize, nread, strerror(err));

    /* check that full buffer is "1" */
    int valid = check_contents(buf, bufsize, 1);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);


    /* read segment [1MB, 2MB) -- should be all "0"
     * this should be a full read, all from a hole */
    memset(buf, 2, bufsize);
    errno = 0;
    nread = pread(fd, buf, bufsize, 1*bufsize);
    err = errno;
    ok(nread == bufsize,
        "%s:%d pread expected=%zu got=%zd: errno=%s",
        __FILE__, __LINE__, bufsize, nread, strerror(err));

    /* check that full buffer is "0" */
    valid = check_contents(buf, bufsize, 0);
    ok(valid == 1, "%s:%d data check",
        __FILE__, __LINE__);


    /* read segment [0.5MB, 1.5MB)
     * should be a full read, half data, half hole */
    memset(buf, 2, bufsize);
    errno = 0;
    nread = pread(fd, buf, bufsize, bufsize/2);
    err = errno;
    ok(nread == bufsize,
        "%s:%d pread expected=%zu got=%zd: errno=%s",
        __FILE__, __LINE__, bufsize, nread, strerror(err));

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
    errno = 0;
    nread = pread(fd, buf, bufsize, 3*bufsize + bufsize/2);
    err = errno;
    ok(nread == bufsize/2,
        "%s:%d pread expected=%zu got=%zd: errno=%s",
        __FILE__, __LINE__, bufsize/2, nread, strerror(err));

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
