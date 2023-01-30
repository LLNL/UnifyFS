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
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int truncate_test(char* unifyfs_root)
{
    char path[64];
    int err, rc;
    int fd;
    off_t offset;
    ssize_t szrc;
    size_t global;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    /* write 1MB and fsync, expect 1MB */
    errno = 0;
    szrc = write(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    testutil_get_size(path, &global);
    ok(global == bufsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, bufsize);

    /* skip a 1MB hole, write another 1MB, and fsync expect 3MB */
    errno = 0;
    offset = lseek(fd, 2*bufsize, SEEK_SET);
    err = errno;
    ok((size_t)offset == 2*bufsize, "%s:%d lseek(%zu) (rc=%zu): %s",
        __FILE__, __LINE__, 2*bufsize, (size_t)offset, strerror(err));

    errno = 0;
    szrc = write(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    testutil_get_size(path, &global);
    ok(global == 3*bufsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, 3*bufsize);

    /* ftruncate at 5MB, expect 5MB */
    errno = 0;
    rc = ftruncate(fd, 5*bufsize);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, 5*bufsize, rc, strerror(err));

    testutil_get_size(path, &global);
    ok(global == 5*bufsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, 5*bufsize);

    close(fd);

    /* truncate at 0.5 MB, expect 0.5MB */
    errno = 0;
    rc = truncate(path, bufsize/2);
    err = errno;
    ok(rc == 0, "%s:%d truncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize/2, rc, strerror(err));

    testutil_get_size(path, &global);
    ok(global == bufsize/2, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, bufsize/2);

    /* truncate to 0, expect 0 */
    errno = 0;
    rc = truncate(path, 0);
    err = errno;
    ok(rc == 0, "%s:%d truncate(0) (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    free(buf);

    return 0;
}

int truncate_bigempty(char* unifyfs_root)
{
    char path[64];
    int err, rc;
    int fd;
    size_t global;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    /* ftruncate at 1TB, expect 1TB */
    off_t bigempty = 1024*1024*1024*1024ULL;
    errno = 0;
    rc = ftruncate(fd, bigempty);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, (size_t) bigempty, rc, strerror(err));

    testutil_get_size(path, &global);
    ok(global == (size_t)bigempty, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, (size_t)bigempty, strerror(err));

    close(fd);

    free(buf);

    return 0;
}

int truncate_eof(char* unifyfs_root)
{
    char path[64];
    int err, rc;
    int fd;
    ssize_t szrc;
    size_t global;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    /* write 1MB */
    errno = 0;
    szrc = write(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* ftruncate at 0.5MB */
    errno = 0;
    rc = ftruncate(fd, bufsize/2);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, bufsize/2, rc, strerror(err));

    close(fd);

    /* Open a file for reading */
    errno = 0;
    fd = open(path, O_RDONLY);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* ask for 1MB, should only get 0.5MB back */
    errno = 0;
    szrc = read(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize/2, "%s:%d read(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    /* then should get 0 since at EOF */
    errno = 0;
    szrc = read(fd, buf, bufsize);
    err = errno;
    ok(szrc == 0, "%s:%d read(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    close(fd);

    /* truncate to 0 */
    errno = 0;
    rc = truncate(path, 0);
    err = errno;
    ok(rc == 0, "%s:%d truncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, 0, rc, strerror(err));

    /* Open a file for reading */
    errno = 0;
    fd = open(path, O_RDONLY);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* should get 0 since immediately at EOF */
    errno = 0;
    szrc = read(fd, buf, bufsize);
    err = errno;
    ok(szrc == 0, "%s:%d read(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    close(fd);

    free(buf);

    return 0;
}

int truncate_truncsync(char* unifyfs_root)
{
    char path[64];
    int err, rc;
    int fd;
    ssize_t szrc;
    size_t global;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    /* write 1MB */
    errno = 0;
    szrc = write(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    /* ftruncate to 0.5MB */
    errno = 0;
    rc = ftruncate(fd, bufsize/2);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, bufsize/2, rc, strerror(err));

    /* file should be 0.5MB bytes at this point */
    testutil_get_size(path, &global);
    ok(global == bufsize/2, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, bufsize/2);

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* file should still be 0.5MB bytes at this point */
    testutil_get_size(path, &global);
    ok(global == bufsize/2, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, bufsize/2);

    close(fd);

    free(buf);

    return 0;
}

/* fill buffer with known pattern based on file offset */
int fill_pattern(char* buf, size_t size, size_t start)
{
    size_t i;
    for (i = 0; i < size; i++) {
        char expected = ((i + start) % 26) + 'A';
        buf[i] = expected;
    }
    return 0;
}

/* fill buffer with known pattern based on file offset */
int check_pattern(char* buf, size_t size, size_t start)
{
    size_t i;
    for (i = 0; i < size; i++) {
        char expected = ((i + start) % 26) + 'A';
        if (buf[i] != expected) {
            return (int)(i+1);
        }
    }
    return 0;
}

/* check that buffer is all zero */
int check_zeros(char* buf, size_t size)
{
    size_t i;
    for (i = 0; i < size; i++) {
        if (buf[i] != (char)0) {
            return (int)(i+1);
        }
    }
    return 0;
}

/* write a known pattern of a known size, truncate to something smaller,
 * read until EOF and verify contents along the way */
int truncate_pattern_size(char* unifyfs_root, off_t seekpos)
{
    char path[64];
    int err, rc;
    int fd;
    ssize_t szrc;
    size_t global;
    int i;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);

    /* write pattern out of 20 MB in size */
    size_t nwritten = 0;
    for (i = 0; i < 20; i++) {
        /* fill buffer with known pattern based on file offset */
        fill_pattern(buf, bufsize, nwritten);

        /* write data to file */
        errno = 0;
        szrc = write(fd, buf, bufsize);
        err = errno;
        ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
            __FILE__, __LINE__, bufsize, szrc, strerror(err));

        /* track number of bytes written so far */
        nwritten += (size_t)szrc;
    }

    /* set size we'll truncate file to */
    off_t truncsize = 5*bufsize + 42;

    /* ftruncate to 5MB + 42 bytes */
    errno = 0;
    rc = ftruncate(fd, truncsize);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, (size_t)truncsize, rc, strerror(err));

    /* file should be of size 5MB + 42 at this point */
    testutil_get_size(path, &global);
    ok(global == (size_t)truncsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, (size_t)truncsize);

    /* this kind of tests that the ftruncate above implied an fsync,
     * can't really since the writes may have gone to disk on their
     * own before ftruncate call */
    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* file should still be 5MB + 42 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == truncsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, (size_t)truncsize);

    close(fd);

    /* read file back from offset 0 and verify size and contents */
    errno = 0;
    fd = open(path, O_RDONLY);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* seek to position if file if seekpos is set */
    if (seekpos > 0) {
        errno = 0;
        off_t pos = lseek(fd, seekpos, SEEK_SET);
        err = errno;
        ok(pos == seekpos, "%s:%d lseek(%zu) (rc=%zu): %s",
            __FILE__, __LINE__, (size_t)seekpos, (size_t)pos, strerror(err));
    }

    off_t numread = 0;
    while (1) {
        /* compute number of bytes we expect to read on next attempt */
        size_t expected = bufsize;
        size_t remaining = (size_t)(truncsize - numread - seekpos);
        if (expected > remaining) {
            expected = remaining;
        }

        /* ask for 1MB */
        errno = 0;
        szrc = read(fd, buf, bufsize);
        err = errno;
        ok(szrc == expected, "%s:%d read(%zu) (rc=%zd) expected=%zu %s",
            __FILE__, __LINE__, bufsize, szrc, expected, strerror(err));

        /* check that contents we read are correct */
        if (szrc > 0) {
            size_t start = (size_t)(numread + seekpos);
            int check = check_pattern(buf, szrc, start);
            ok(check == 0, "%s:%d pattern check of bytes [%zu, %zu) rc=%d",
                __FILE__, __LINE__, start, (start + szrc), check);

            /* add to number of bytes read so far */
            numread += (off_t)szrc;
        }

        /* break if we hit end of file */
        if (szrc == 0) {
            /* check that total read is expected size */
            ok(numread == (truncsize - seekpos),
                "%s:%d read %zu bytes, expected %zu",
                __FILE__, __LINE__, (size_t)numread,
                (size_t)(truncsize - seekpos));
            break;
        }

        /* check that we don't run past expected
         * end of file (and hang the test) */
        if (numread > (truncsize - seekpos)) {
            ok(numread <= (truncsize - seekpos),
                "%s:%d read %zu bytes, expected %zu",
                __FILE__, __LINE__, (size_t)numread,
                (size_t)(truncsize - seekpos));
            break;
        }

        /* break if we hit an error, would have been
         * reported in read above */
        if (szrc < 0) {
            break;
        }
    }

    close(fd);

    free(buf);

    return 0;
}

/* truncate an empty file to something and read until EOF,
 * check size and contents of buffer */
int truncate_empty_read(char* unifyfs_root, off_t seekpos)
{
    char path[64];
    int err, rc;
    int fd;
    ssize_t szrc;
    size_t global;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    /* set size we'll truncate file to */
    off_t truncsize = 5*bufsize + 42;

    /* ftruncate to 5MB + 42 bytes */
    errno = 0;
    rc = ftruncate(fd, truncsize);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, (size_t)truncsize, rc, strerror(err));

    /* file should be of size 5MB + 42 at this point */
    testutil_get_size(path, &global);
    ok(global == truncsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, (size_t)truncsize);

    /* this kind of tests that the ftruncate above implied an fsync,
     * can't really since the writes may have gone to disk on their
     * own before ftruncate call */
    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* file should still be 5MB + 42 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == truncsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, (size_t)truncsize);

    close(fd);

    /* read file back from offset 0 and verify size and contents */
    errno = 0;
    fd = open(path, O_RDONLY);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* see to position if file if seekpos is set */
    if (seekpos > 0) {
        errno = 0;
        off_t pos = lseek(fd, seekpos, SEEK_SET);
        err = errno;
        ok(pos == seekpos, "%s:%d lseek(%zu) (rc=%zd): %s",
            __FILE__, __LINE__, (size_t)seekpos, (ssize_t)pos, strerror(err));
    }

    off_t numread = 0;
    while (1) {
        /* compute number of bytes we expect to read on next attempt */
        size_t expected = bufsize;
        size_t remaining = (size_t)(truncsize - numread - seekpos);
        if (expected > remaining) {
            expected = remaining;
        }

        /* ask for 1MB */
        errno = 0;
        szrc = read(fd, buf, bufsize);
        err = errno;
        ok(szrc == expected, "%s:%d read(%zu) (rc=%zd) expected=%zu %s",
            __FILE__, __LINE__, bufsize, szrc, expected, strerror(err));

        /* check that contents we read are correct */
        if (szrc > 0) {
            size_t start = (size_t)(numread + seekpos);
            int check = check_zeros(buf, szrc);
            ok(check == 0, "%s:%d pattern check of bytes [%zu, %zu) rc=%d",
                __FILE__, __LINE__, start, (start + rc), check);

            /* add to number of bytes read so far */
            numread += (off_t)szrc;
        }

        /* break if we hit end of file */
        if (szrc == 0) {
            /* check that total read is expected size */
            ok(numread == (truncsize - seekpos),
                "%s:%d read %zu bytes, expected %zu",
                __FILE__, __LINE__, (size_t)numread,
                (size_t)(truncsize - seekpos));
            break;
        }

        /* check that we don't run past expected
         * end of file (and hang the test) */
        if (numread > (truncsize - seekpos)) {
            ok(numread <= (truncsize - seekpos),
                "%s:%d read %zu bytes, expected %zu",
                __FILE__, __LINE__, (size_t)numread,
                (size_t)(truncsize - seekpos));
            break;
        }

        /* break if we hit an error, would have been
         * reported in read above */
        if (szrc < 0) {
            break;
        }
    }

    close(fd);

    free(buf);

    return 0;
}

int truncate_ftrunc_before_sync(char* unifyfs_root)
{
    char path[64];
    int err, rc;
    int fd;
    ssize_t szrc;
    size_t global;

    size_t bufsize = 1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    /* write a small amount, intended to be small enough that
     * the write itself does not cause an implicit fsync */

    /* write data to file */
    errno = 0;
    szrc = write(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    /* then truncate the file to 0 */
    off_t truncsize = 0;
    errno = 0;
    rc = ftruncate(fd, truncsize);
    err = errno;
    ok(rc == 0, "%s:%d ftruncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, (size_t)truncsize, rc, strerror(err));

    /* then fsync the file */
    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* finally, check that the file is 0 bytes,
     * i.e., check that the writes happened before the truncate
     * and not at the fsync */
    testutil_get_size(path, &global);
    ok(global == (size_t)truncsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, (size_t)truncsize);

    close(fd);

    free(buf);

    return 0;
}

int truncate_trunc_before_sync(char* unifyfs_root)
{
    char path[64];
    int err, rc;
    int fd;
    ssize_t szrc;
    size_t global;

    size_t bufsize = 1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);

    /* write a small amount, intended to be small enough that
     * the write itself does not cause an implicit fsync */

    /* write data to file */
    errno = 0;
    szrc = write(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    /* then truncate the file to 0 */
    off_t truncsize = 0;
    errno = 0;
    rc = truncate(path, truncsize);
    err = errno;
    ok(rc == 0, "%s:%d truncate(%zu) (rc=%d): %s",
        __FILE__, __LINE__, (size_t)truncsize, rc, strerror(err));

    /* then fsync the file */
    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    /* finally, check that the file is 0 bytes,
     * i.e., check that the writes happened before the truncate
     * and not at the fsync */
    testutil_get_size(path, &global);
    ok(global == (size_t)truncsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, (size_t)truncsize);

    close(fd);

    free(buf);

    return 0;
}

int truncate_twice(char* unifyfs_root)
{
    char path[64];
    int err, rc;
    int fd;
    ssize_t szrc;
    size_t global;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    /* file should be 0 bytes at this point */
    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    /* write 1MB and fsync, expect 1MB */
    errno = 0;
    szrc = write(fd, buf, bufsize);
    err = errno;
    ok(szrc == bufsize, "%s:%d write(%zu) (rc=%zd): %s",
        __FILE__, __LINE__, bufsize, szrc, strerror(err));

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(err));

    testutil_get_size(path, &global);
    ok(global == bufsize, "%s:%d global size is %zu, expected %zu",
        __FILE__, __LINE__, global, bufsize);

    close(fd);

    /* Open same file with O_TRUNC, should now be 0 bytes */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    err = errno;
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(err));

    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size is %zu, expected 0",
        __FILE__, __LINE__, global);

    close(fd);

    free(buf);

    return 0;
}
