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
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* file should be 0 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* write 1MB and fsync, expect 1MB */
    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 1*bufsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 1*bufsize);
    ok(local == 1*bufsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 1*bufsize);
    ok(log == 1*bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 1*bufsize);

    /* skip a 1MB hole, write another 1MB, and fsync expect 3MB */
    rc = lseek(fd, 2*bufsize, SEEK_SET);
    ok(rc == 2*bufsize, "%s:%d lseek(%d) (rc=%d): %s",
        __FILE__, __LINE__, 2*bufsize, rc, strerror(errno));

    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 3*bufsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 3*bufsize);
    ok(local == 3*bufsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 3*bufsize);
    ok(log == 2*bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 2*bufsize);

    /* ftruncate at 5MB, expect 5MB */
    rc = ftruncate(fd, 5*bufsize);
    ok(rc == 0, "%s:%d ftruncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, 5*bufsize, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 5*bufsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 5*bufsize);
    ok(local == 5*bufsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 5*bufsize);
    ok(log == 2*bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 2*bufsize);

    close(fd);

    /* truncate at 0.5 MB, expect 0.5MB */
    rc = truncate(path, bufsize/2);
    ok(rc == 0, "%s:%d truncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize/2, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == bufsize/2, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, bufsize/2);
    ok(local == bufsize/2, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, bufsize/2);
    ok(log == 2*bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 2*bufsize);

    /* truncate to 0, expect 0 */
    rc = truncate(path, 0);
    ok(rc == 0, "%s:%d truncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, 0, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 2*bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 2*bufsize);

    free(buf);

    return 0;
}

int truncate_bigempty(char* unifyfs_root)
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
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* ftruncate at 1TB, expect 1TB */
    off_t bigempty = 1024*1024*1024*1024ULL;
    rc = ftruncate(fd, bigempty);
    ok(rc == 0, "%s:%d ftruncate(%llu) (rc=%d): %s",
        __FILE__, __LINE__, (unsigned long long) bigempty,
        rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == (size_t)bigempty, "%s:%d global size is %llu expected %llu",
        __FILE__, __LINE__, global, (unsigned long long)bigempty,
        strerror(errno));

    close(fd);

    free(buf);

    return 0;
}

int truncate_eof(char* unifyfs_root)
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
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* file should be 0 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* write 1MB */
    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* ftruncate at 0.5MB */
    rc = ftruncate(fd, bufsize/2);
    ok(rc == 0, "%s:%d ftruncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize/2, rc, strerror(errno));

    close(fd);

    /* Open a file for reading */
    fd = open(path, O_RDONLY);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* ask for 1MB, should only get 0.5MB back */
    rc = read(fd, buf, bufsize);
    ok(rc == bufsize/2, "%s:%d read(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    /* then should get 0 since at EOF */
    rc = read(fd, buf, bufsize);
    ok(rc == 0, "%s:%d read(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    close(fd);

    /* truncate to 0 */
    rc = truncate(path, 0);
    ok(rc == 0, "%s:%d truncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, 0, rc, strerror(errno));

    /* Open a file for reading */
    fd = open(path, O_RDONLY);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* should get 0 since immediately at EOF */
    rc = read(fd, buf, bufsize);
    ok(rc == 0, "%s:%d read(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    close(fd);

    free(buf);

    return 0;
}

int truncate_truncsync(char* unifyfs_root)
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
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* file should be 0 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* write 1MB */
    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    /* ftruncate to 0.5MB */
    rc = ftruncate(fd, bufsize/2);
    ok(rc == 0, "%s:%d ftruncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize/2, rc, strerror(errno));

    /* file should be 0.5MB bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == bufsize/2, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, bufsize/2);
    ok(local == bufsize/2, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, bufsize/2);
    ok(log == bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, bufsize);

    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* file should still be 0.5MB bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == bufsize/2, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, bufsize/2);
    ok(local == bufsize/2, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, bufsize/2);
    ok(log == bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, bufsize);

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
    int rc;
    int fd;
    size_t global, local, log;
    int i;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* file should be 0 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* write pattern out of 20 MB in size */
    size_t nwritten = 0;
    for (i = 0; i < 20; i++) {
        /* fill buffer with known pattern based on file offset */
        fill_pattern(buf, bufsize, nwritten);

        /* write data to file */
        rc = write(fd, buf, bufsize);
        ok(rc == bufsize, "%s:%d write(%d) (rc=%d): %s",
            __FILE__, __LINE__, bufsize, rc, strerror(errno));

        /* track number of bytes written so far */
        nwritten += (size_t)rc;
    }

    /* set size we'll truncate file to */
    off_t truncsize = 5*bufsize + 42;

    /* ftruncate to 5MB + 42 bytes */
    rc = ftruncate(fd, truncsize);
    ok(rc == 0, "%s:%d ftruncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, (int)truncsize, rc, strerror(errno));

    /* file should be of size 5MB + 42 at this point */
    get_size(path, &global, &local, &log);
    ok(global == truncsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, (int)truncsize);
    ok(local == truncsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, (int)truncsize);
    ok(log == 20*bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 20*bufsize);

    /* this kind of tests that the ftruncate above implied an fsync,
     * can't really since the writes may have gone to disk on their
     * own before ftruncate call */
    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* file should still be 5MB + 42 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == truncsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, (int)truncsize);
    ok(local == truncsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, (int)truncsize);
    ok(log == 20*bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 20*bufsize);

    close(fd);

    /* read file back from offset 0 and verify size and contents */
    fd = open(path, O_RDONLY);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* see to position if file if seekpos is set */
    if (seekpos > 0) {
        off_t pos = lseek(fd, seekpos, SEEK_SET);
        ok(pos == seekpos, "%s:%d lseek(%d) (rc=%d): %s",
            __FILE__, __LINE__, pos, rc, strerror(errno));
    }

    off_t numread = 0;
    while (1) {
        /* compute number of bytes we expect to read on next attempt */
        ssize_t expected = bufsize;
        ssize_t remaining = (ssize_t)(truncsize - numread - seekpos);
        if (expected > remaining) {
            expected = remaining;
        }

        /* ask for 1MB, should only get 0.5MB back */
        rc = read(fd, buf, bufsize);
        ok(rc == expected, "%s:%d read(%d) (rc=%d) expected=%d %s",
            __FILE__, __LINE__, bufsize, rc, expected, strerror(errno));

        /* check that contents we read are correct */
        if (rc > 0) {
            size_t start = numread + seekpos;
            int check = check_pattern(buf, rc, start);
            ok(check == 0, "%s:%d pattern check of bytes [%d, %d) rc=%d",
                __FILE__, __LINE__, (int)start, (int)(start + rc), check);

            /* add to number of bytes read so far */
            numread += rc;
        }

        /* break if we hit end of file */
        if (rc == 0) {
            /* check that total read is expected size */
            ok(numread == (truncsize - seekpos),
                "%s:%d read %d bytes, expected %d",
                __FILE__, __LINE__, (int)numread, (int)(truncsize - seekpos));
            break;
        }

        /* check that we don't run past expected
         * end of file (and hang the test) */
        if (numread > (truncsize - seekpos)) {
            ok(numread <= (truncsize - seekpos),
                "%s:%d read %d bytes, expected %d",
                __FILE__, __LINE__, (int)numread, (int)(truncsize - seekpos));
            break;
        }

        /* break if we hit an error, would have been
         * reported in read above */
        if (rc < 0) {
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
    int rc;
    int fd;
    size_t global, local, log;
    int i;

    size_t bufsize = 1024*1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* file should be 0 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* set size we'll truncate file to */
    off_t truncsize = 5*bufsize + 42;

    /* ftruncate to 5MB + 42 bytes */
    rc = ftruncate(fd, truncsize);
    ok(rc == 0, "%s:%d ftruncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, (int)truncsize, rc, strerror(errno));

    /* file should be of size 5MB + 42 at this point */
    get_size(path, &global, &local, &log);
    ok(global == truncsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, (int)truncsize);
    ok(local == truncsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, (int)truncsize);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* this kind of tests that the ftruncate above implied an fsync,
     * can't really since the writes may have gone to disk on their
     * own before ftruncate call */
    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* file should still be 5MB + 42 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == truncsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, (int)truncsize);
    ok(local == truncsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, (int)truncsize);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    close(fd);

    /* read file back from offset 0 and verify size and contents */
    fd = open(path, O_RDONLY);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* see to position if file if seekpos is set */
    if (seekpos > 0) {
        off_t pos = lseek(fd, seekpos, SEEK_SET);
        ok(pos == seekpos, "%s:%d lseek(%d) (rc=%d): %s",
            __FILE__, __LINE__, pos, rc, strerror(errno));
    }

    off_t numread = 0;
    while (1) {
        /* compute number of bytes we expect to read on next attempt */
        ssize_t expected = bufsize;
        ssize_t remaining = (ssize_t)(truncsize - numread - seekpos);
        if (expected > remaining) {
            expected = remaining;
        }

        /* ask for 1MB, should only get 0.5MB back */
        rc = read(fd, buf, bufsize);
        ok(rc == expected, "%s:%d read(%d) (rc=%d) expected=%d %s",
            __FILE__, __LINE__, bufsize, rc, expected, strerror(errno));

        /* check that contents we read are correct */
        if (rc > 0) {
            size_t start = numread + seekpos;
            int check = check_zeros(buf, rc);
            ok(check == 0, "%s:%d pattern check of bytes [%d, %d) rc=%d",
                __FILE__, __LINE__, (int)start, (int)(start + rc), check);

            /* add to number of bytes read so far */
            numread += rc;
        }

        /* break if we hit end of file */
        if (rc == 0) {
            /* check that total read is expected size */
            ok(numread == (truncsize - seekpos),
                "%s:%d read %d bytes, expected %d",
                __FILE__, __LINE__, (int)numread, (int)(truncsize - seekpos));
            break;
        }

        /* check that we don't run past expected
         * end of file (and hang the test) */
        if (numread > (truncsize - seekpos)) {
            ok(numread <= (truncsize - seekpos),
                "%s:%d read %d bytes, expected %d",
                __FILE__, __LINE__, (int)numread, (int)(truncsize - seekpos));
            break;
        }

        /* break if we hit an error, would have been
         * reported in read above */
        if (rc < 0) {
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
    int rc;
    int fd;
    size_t global, local, log;

    size_t bufsize = 1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* file should be 0 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* write a small amount, intended to be small enough that
     * the write itself does not cause an implicit fsync */

    /* write data to file */
    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    /* then truncate the file to 0 */
    off_t truncsize = 0;
    rc = ftruncate(fd, truncsize);
    ok(rc == 0, "%s:%d ftruncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, (int)truncsize, rc, strerror(errno));

    /* then fsync the file */
    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* finally, check that the file is 0 bytes,
     * i.e., check that the writes happened before the truncate
     * and not at the fsync */
    get_size(path, &global, &local, &log);
    ok(global == truncsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, (int)truncsize);
    ok(local == truncsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, (int)truncsize);
    ok(log == bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, bufsize);

    close(fd);

    free(buf);

    return 0;
}

int truncate_trunc_before_sync(char* unifyfs_root)
{
    char path[64];
    int rc;
    int fd;
    size_t global, local, log;

    size_t bufsize = 1024;
    char* buf = (char*) malloc(bufsize);

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a new file for writing */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
        __FILE__, __LINE__, path, fd, strerror(errno));

    /* file should be 0 bytes at this point */
    get_size(path, &global, &local, &log);
    ok(global == 0, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, 0);
    ok(local == 0, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, 0);
    ok(log == 0, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, 0);

    /* write a small amount, intended to be small enough that
     * the write itself does not cause an implicit fsync */

    /* write data to file */
    rc = write(fd, buf, bufsize);
    ok(rc == bufsize, "%s:%d write(%d) (rc=%d): %s",
        __FILE__, __LINE__, bufsize, rc, strerror(errno));

    /* then truncate the file to 0 */
    off_t truncsize = 0;
    rc = truncate(path, truncsize);
    ok(rc == 0, "%s:%d truncate(%d) (rc=%d): %s",
        __FILE__, __LINE__, (int)truncsize, rc, strerror(errno));

    /* then fsync the file */
    rc = fsync(fd);
    ok(rc == 0, "%s:%d fsync() (rc=%d): %s",
        __FILE__, __LINE__, rc, strerror(errno));

    /* finally, check that the file is 0 bytes,
     * i.e., check that the writes happened before the truncate
     * and not at the fsync */
    get_size(path, &global, &local, &log);
    ok(global == truncsize, "%s:%d global size is %d expected %d",
        __FILE__, __LINE__, global, (int)truncsize);
    ok(local == truncsize, "%s:%d local size is %d expected %d",
        __FILE__, __LINE__, local, (int)truncsize);
    ok(log == bufsize, "%s:%d log size is %d expected %d",
        __FILE__, __LINE__, log, bufsize);

    close(fd);

    free(buf);

    return 0;
}
