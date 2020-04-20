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
    size_t global;

    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* write to bad file descriptor should fail with errno=EBADF */
    ok(write(fd, "hello world", 12) == -1 && errno == EBADF,
       "%s:%d write() to bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0; /* Reset after test for failure */

    /* read from bad file descriptor should fail with errno=EBADF */
    ok(read(fd, buf, 12) == -1 && errno == EBADF,
       "%s:%d read() from bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* Write "hello world" to the file */
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd != -1, "%s:%d open(%s) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(errno));
    ok(write(fd, "hello world", 12) == 12,
       "%s:%d write(\"hello world\") to file: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Write to a different offset by overwriting "world" with "universe" */
    ok(lseek(fd, 6, SEEK_SET) == 6, "%s:%d lseek(6) to \"world\": %s",
       __FILE__, __LINE__, strerror(errno));
    ok(write(fd, "universe", 9) == 9,
       "%s:%d overwrite \"world\" at offset 6 with \"universe\": %s",
       __FILE__, __LINE__, strerror(errno));

    /* Check global size on our un-laminated and un-synced file */
    testutil_get_size(path, &global);
    ok(global == 15, "%s:%d global size before fsync is %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    ok(fsync(fd) == 0, "%s:%d fsync() worked: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == 15, "%s:%d global size after fsync is %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    /* read from file open as write-only should fail with errno=EBADF */
    ok(lseek(fd, 0, SEEK_SET) == 0, "%s:%d lseek(0): %s",
       __FILE__, __LINE__, strerror(errno));
    todo("Successfully reads and gets 0 bytes back");
    ok(read(fd, buf, 15) == -1 && errno == EBADF,
       "%s:%d read() from file open as write-only (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    end_todo;
    errno = 0;

    ok(close(fd) == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Test O_APPEND */
    fd = open(path, O_WRONLY | O_APPEND, 0222);
    ok(fd != -1, "%s:%d open(%s, O_APPEND) (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(errno));

    /*
     * Seek to an offset in the file and write.  Since it's O_APPEND, the
     * offset we seeked to doesn't matter - all writes go to the end.
     */
    ok(lseek(fd, 3, SEEK_SET) == 3, "%s:%d lseek(3) worked: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(write(fd, "<end>", 6) == 6, "%s:%d append write(\"<end>\"): %s",
       __FILE__, __LINE__, strerror(errno));
    ok(close(fd) == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Check global size on our un-laminated file */
    testutil_get_size(path, &global);
    ok(global == 21, "%s:%d global size before laminate is %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    /* Laminate */
    ok(chmod(path, 0444) == 0, "%s:%d chmod(0444): %s",
       __FILE__, __LINE__, strerror(errno));

    /* Verify we're getting the correct file size */
    testutil_get_size(path, &global);
    ok(global == 21, "%s:%d global size after laminate is %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    /* open laminated file for write should fail with errno=EROFS */
    fd = open(path, O_WRONLY | O_CREAT, 0222);
    ok(fd == -1 && errno == EROFS,
       "%s:%d open() laminated file for write fails (fd=%d, errno=%d): %s",
       __FILE__, __LINE__, fd, errno, strerror(errno));
    errno = 0;

    /* read() tests */
    fd = open(path, O_RDONLY, 0444);
    ok(fd != -1, "%s:%d open(%s, O_RDONLY) for read (fd=%d): %s",
       __FILE__, __LINE__, path, fd, strerror(errno));

    /* write to file open as read-only should fail with errno=EBADF */
    ok(write(fd, "hello world", 12) == -1 && errno == EBADF,
       "%s:%d write() to file open as read-only fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    ok(read(fd, buf, 21) == 21, "%s:%d read() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    buf[14] = ' '; /* replace '\0' between initial write and append */
    is(buf, "hello universe <end>", "%s:%d read() saw \"hello universe <end>\"",
       __FILE__, __LINE__);

    /* Seek and read at a different position */
    ok(lseek(fd, 6, SEEK_SET) == 6, "%s:%d lseek(6) worked: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(read(fd, buf, 9) == 9, "%s:%d read() at offset 6 buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "universe", "%s:%d read() saw \"universe\"", __FILE__, __LINE__);

    ok(lseek(fd, 0, SEEK_SET) == 0, "%s:%d lseek(0) worked: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(read(fd, buf, sizeof(buf)) == 21, "%s:%d read() past end of file: %s",
       __FILE__, __LINE__, strerror(errno));

    ok(close(fd) == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(errno));

    /* write to closed file descriptor should fail with errno=EBADF */
    ok(write(fd, "hello world", 12) == -1 && errno == EBADF,
       "%s:%d write() to bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0; /* Reset after test for failure */

    /* read from closed file descriptor should fail with errno=EBADF */
    ok(read(fd, buf, 12) == -1 && errno == EBADF,
       "%s:%d read() from bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    diag("Finished UNIFYFS_WRAP(write/read) tests");

    return 0;
}
