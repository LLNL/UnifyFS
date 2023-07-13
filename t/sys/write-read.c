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
    diag("Starting UNIFYFS_WRAP(write/read) tests");

    char path[64];
    char buf[64] = {0};
    int fd = -1;
    int err, rc;
    size_t global;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* write to bad file descriptor should fail with errno=EBADF */
    errno = 0;
    rc = (int) write(fd, "hello world", 12);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d write() to bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* read from bad file descriptor should fail with errno=EBADF */
    errno = 0;
    rc = (int) read(fd, buf, 12);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d read() from bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* Write "hello world" to the file */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open(%s) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    errno = 0;
    rc = (int) write(fd, "hello world", 12);
    err = errno;
    ok(rc == 12 && err == 0,
       "%s:%d write(\"hello world\") to file: %s",
       __FILE__, __LINE__, strerror(err));

    /* Write to a different offset by overwriting "world" with "universe" */
    errno = 0;
    rc = (int) lseek(fd, 6, SEEK_SET);
    err = errno;
    ok(rc == 6 && err == 0,
       "%s:%d lseek(6) to \"world\": %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) write(fd, "universe", 9);
    err = errno;
    ok(rc == 9 && err == 0,
       "%s:%d overwrite \"world\" at offset 6 with \"universe\": %s",
       __FILE__, __LINE__, strerror(err));

    /* Check global size on our un-laminated and un-synced file */
    testutil_get_size(path, &global);
    ok(global == 15, "%s:%d global size before fsync is %zu: %s",
       __FILE__, __LINE__, global, strerror(err));

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fsync() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == 15, "%s:%d global size after fsync is %zu: %s",
       __FILE__, __LINE__, global, strerror(err));

    /* read from file open as write-only should fail with errno=EBADF */
    errno = 0;
    rc = (int) lseek(fd, 0, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d lseek(0): %s",
       __FILE__, __LINE__, strerror(err));

    todo("Successfully reads and gets 0 bytes back");
    errno = 0;
    rc = (int) read(fd, buf, 15);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d read() from file open as write-only (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));
    end_todo;

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* Test O_APPEND */
    errno = 0;
    fd = open(path, O_WRONLY | O_APPEND, 0);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open(%s, O_APPEND) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    /*
     * Seek to an offset in the file and write.  Since it's O_APPEND, the
     * offset we seeked to doesn't matter - all writes go to the end.
     */
    errno = 0;
    rc = (int) lseek(fd, 3, SEEK_SET);
    err = errno;
    ok(rc == 3 && err == 0,
       "%s:%d lseek(3) worked: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) write(fd, "<end>", 6);
    err = errno;
    ok(rc == 6 && err == 0,
       "%s:%d append write(\"<end>\"): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == 21, "%s:%d global size before laminate is %zu: %s",
       __FILE__, __LINE__, global, strerror(err));

    /* Laminate */
    errno = 0;
    rc = chmod(path, 0444);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chmod(0444): %s",
       __FILE__, __LINE__, strerror(err));

    /* Verify we're getting the correct file size */
    testutil_get_size(path, &global);
    ok(global == 21, "%s:%d global size after laminate is %zu: %s",
       __FILE__, __LINE__, global, strerror(err));

    /* open laminated file for write should fail with errno=EROFS */
    errno = 0;
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    err = errno;
    ok(fd == -1 && err == EROFS,
       "%s:%d open() laminated file for write fails (fd=%d, errno=%d): %s",
       __FILE__, __LINE__, fd, err, strerror(err));

    /* read() tests */
    errno = 0;
    fd = open(path, O_RDONLY, 0);
    err = errno;
    ok(fd != -1 && err == 0,
       "%s:%d open(%s, O_RDONLY) for read (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    /* write to file open as read-only should fail with errno=EBADF */
    errno = 0;
    rc = (int) write(fd, "hello world", 12);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d write() to file open as read-only fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = (int) read(fd, buf, 21);
    err = errno;
    ok(rc == 21 && err == 0,
       "%s:%d read() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(err));
    buf[14] = ' '; /* replace '\0' between initial write and append */
    is(buf, "hello universe <end>", "%s:%d read() saw \"hello universe <end>\"",
       __FILE__, __LINE__);

    /* Seek and read at a different position */
    errno = 0;
    rc = (int) lseek(fd, 6, SEEK_SET);
    err = errno;
    ok(rc == 6 && err == 0,
       "%s:%d lseek(6) worked: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) read(fd, buf, 9);
    err = errno;
    ok(rc == 9 && err == 0,
       "%s:%d read() at offset 6 buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(err));
    is(buf, "universe", "%s:%d read() saw \"universe\"", __FILE__, __LINE__);

    errno = 0;
    rc = (int) lseek(fd, 0, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d lseek(0) worked: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) read(fd, buf, sizeof(buf));
    err = errno;
    ok(rc == 21 && err == 0,
       "%s:%d read() past end of file: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* write to closed file descriptor should fail with errno=EBADF */
    errno = 0;
    rc = (int) write(fd, "hello world", 12);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d write() to bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* read from closed file descriptor should fail with errno=EBADF */
    errno = 0;
    rc = (int) read(fd, buf, 12);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d read() from bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d unlink(%s) worked: %s",
       __FILE__, __LINE__, path, strerror(err));

    diag("Finished UNIFYFS_WRAP(write/read) tests");

    return 0;
}

int write_max_read_test(char* unifyfs_root)
{
    diag("Starting write/max-read tests");

    char path[64];
    char* buf;
    int fd = -1;
    int err, rc;
    size_t bufsz, global;
    ssize_t szrc;

    bufsz = 1024 * 1024;

    errno = 0;
    buf = calloc(1, bufsz);
    err = errno;
    ok(NULL != buf,
       "%s:%d calloc(%zu) write buffer succeeds (errno=%d): %s",
       __FILE__, __LINE__, bufsz, err, strerror(err));
    memset(buf, 1, bufsz/2); /* write '1' bytes into first half of buffer */

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open test file */
    errno = 0;
    fd = open(path, O_RDWR | O_CREAT, 0);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open(%s) fd=%d: %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    /* Write 64 MiB to the file */
    for (int i = 0; i < 64; i++) {
        errno = 0;
        szrc = write(fd, buf, bufsz);
        err = errno;
        ok(szrc == bufsz && err == 0,
           "%s:%d write(fd=%d, sz=%zu) rc=%zd: %s",
           __FILE__, __LINE__, fd, bufsz, szrc, strerror(err));
    }
    if (NULL != buf) {
        free(buf);
    }

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fsync() rc=%d: %s",
       __FILE__, __LINE__, rc, strerror(err));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == (64 * bufsz), "%s:%d global size after fsync is %zu: %s",
       __FILE__, __LINE__, global, strerror(err));

    /* Read data from file, starting at size 1 KiB, up to full file size */
    errno = 0;
    buf = malloc(global);
    err = errno;
    ok(NULL != buf,
       "%s:%d malloc(%zu) read buffer succeeds (errno=%d): %s",
       __FILE__, __LINE__, global, err, strerror(err));
    if (NULL != buf) {
        for (size_t sz = 1024; sz <= global; sz *= 2) {
            errno = 0;
            szrc = pread(fd, buf, sz, 0);
            err = errno;
            ok(szrc == sz && err == 0,
               "%s:%d pread(fd=%d, sz=%zu) rc=%zd: %s",
                __FILE__, __LINE__, fd, sz, szrc, strerror(err));
        }
        free(buf);
    }

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d unlink(%s) worked: %s",
       __FILE__, __LINE__, path, strerror(err));

    diag("Finished write/max-read tests");

    return 0;
}

/* Test to reproduce issue 488 */
int write_pre_existing_file_test(char* unifyfs_root)
{
    diag("Starting write-to-pre-existing-file tests");

#define TEST_LEN 300
#define TEST_LEN_SHORT 100
/* TEST_LEN_SHORT must be less than TEST_LEN */
    char path[64];
    char buf[TEST_LEN] = {0};
    int fd = -1;
    int err, rc;
    size_t global;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    errno = 0;
    fd = open(path, O_RDWR | O_CREAT, 0222);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open(%s) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    /* Write TEST_LEN bytes to a file */
    errno = 0;
    rc = (int) write(fd, buf, TEST_LEN);
    err = errno;
    ok(rc == TEST_LEN && err == 0,
       "%s:%d write() a %d byte file: %s",
       __FILE__, __LINE__, TEST_LEN, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* Check global size is correct */
    testutil_get_size(path, &global);
    ok(global == TEST_LEN, "%s:%d global size of %d byte file is %zu: %s",
       __FILE__, __LINE__, TEST_LEN, global, strerror(err));

    /* Reopen the same file */
    errno = 0;
    fd = open(path, O_RDWR, 0222);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open(%s) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(err));

    /* Overwrite the first part of same file */
    errno = 0;
    rc = (int) write(fd, buf, TEST_LEN_SHORT);
    err = errno;
    ok(rc == TEST_LEN_SHORT && err == 0,
       "%s:%d overwrite first %d bytes of same file: %s",
       __FILE__, __LINE__, TEST_LEN_SHORT, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* Check global size is still correct */
    testutil_get_size(path, &global);
    ok(global == TEST_LEN, "%s:%d global size of %d byte file is %zu: %s",
       __FILE__, __LINE__, TEST_LEN, global, strerror(err));

    errno = 0;
    rc = unlink(path);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d unlink(%s) worked: %s",
       __FILE__, __LINE__, path, strerror(err));

    diag("Finished write-to-pre-existing-file tests");

    return 0;
}
