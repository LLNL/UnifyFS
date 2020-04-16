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
  * Test fwrite/fread/fgets/rewind/feof/chmod
  */

#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int fwrite_fread_test(char* unifyfs_root)
{
    diag("Starting UNIFYFS_WRAP(fwrite/fread/fgets/feof) tests");

    char path[64];
    char buf[64] = {0};
    char* tmp;
    FILE* fp = NULL;
    int fd = 0;

    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* fwrite to bad FILE stream should segfault */
    dies_ok({ FILE* p = fopen("/tmp/fakefile", "r");
              fwrite("hello world", 12, 1, p); },
       "%s:%d fwrite() to bad FILE stream segfaults: %s",
       __FILE__, __LINE__, strerror(errno));

    /* fread from bad FILE stream should segfault  */
    dies_ok({ size_t rc = fread(buf, 15, 1, fp); ok(rc > 0); },
       "%s:%d fread() from bad FILE stream segfaults: %s",
       __FILE__, __LINE__, strerror(errno));

    /* fgets from bad FILE stream segfaults */
    memset(buf, 0, sizeof(buf));
    dies_ok({ char* tmp = fgets(buf, 15, fp); ok(tmp != NULL); },
       "%s:%d fgets() from bad FILE stream segfaults: %s",
       __FILE__, __LINE__, strerror(errno));

    dies_ok({ int rc = feof(fp); ok(rc > 0); },
            "%s:%d feof() on bad FILE stream segfaults: %s",
            __FILE__, __LINE__, strerror(errno));

    /* Write "hello world" to a file */
    fp = fopen(path, "w");
    ok(fp != NULL, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(errno));
    ok(fwrite("hello world", 12, 1, fp) == 1,
       "%s:%d fwrite(\"hello world\") to file %s: %s",
       __FILE__, __LINE__, path, strerror(errno));

    /* Seek to offset and overwrite "world" with "universe" */
    ok(fseek(fp, 6, SEEK_SET) == 0, "%s:%d fseek(6) to \"world\": %s",
       __FILE__, __LINE__, strerror(errno));
    ok(fwrite("universe", 9, 1, fp) == 1,
       "%s:%d overwrite \"world\" at offset 6 with \"universe\" : %s",
       __FILE__, __LINE__, strerror(errno));

    /* fread from file open as write-only should fail with errno=EBADF */
    ok(fseek(fp, 0, SEEK_SET) == 0, "%s:%d fseek(0): %s",
       __FILE__, __LINE__, strerror(errno));
    ok(fread(buf, 15, 1, fp) == 0 && errno == EBADF,
       "%s:%d fread() from file open as write-only should fail (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0; /* reset errno after test for failure */

    ok(fclose(fp) == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(errno));

    /* fsync() tests */
    /* fsync on bad file descriptor should fail with errno=EINVAL */
    ok(fsync(fd) == -1 && errno == EINVAL,
       "%s:%d fsync() on bad file descriptor should fail (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* Sync extents */
    fd = open(path, O_RDWR);
    ok(fd != -1, "%s:%d open() (fd=%d): %s",
       __FILE__, __LINE__, fd, strerror(errno));
    ok(fsync(fd) == 0, "%s:%d fsync(): %s",
       __FILE__, __LINE__, strerror(errno));
    ok(close(fd) == 0, "%s:%d close(): %s",
       __FILE__, __LINE__, strerror(errno));

    /* fsync on non-open file should fail with errno=EBADF */
    ok(fsync(fd) == -1 && errno == EBADF,
       "%s:%d fsync() on non-open file should fail (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* Laminate */
    ok(chmod(path, 0444) == 0, "%s:%d chmod(0444): %s",
       __FILE__, __LINE__, strerror(errno));

    /* fopen a laminated file for write should fail with errno=EROFS */
    fp = fopen(path, "w");
    ok(fp == NULL && errno == EROFS,
       "%s:%d fopen laminated file for write fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* fread() tests */
    fp = fopen(path, "r");
    ok(fp != NULL, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(errno));

    /* fwrite to file open as read-only should fail with errno=EBADF */
    ok(fwrite("hello world", 12, 1, fp) == 0 && errno == EBADF,
       "%s:%d fwrite() to file open for read-only fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    ok(fread(buf, 15, 1, fp) == 1, "%s:%d fread() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "hello universe", "%s:%d fread() saw \"hello universe\"",
       __FILE__, __LINE__);

    ok(fseek(fp, 6, SEEK_SET) == 0, "%s:%d fseek(6): %s",
       __FILE__, __LINE__, strerror(errno));
    ok(fread(buf, 9, 1, fp) == 1, "%s:%d fread() at offset 6 buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "universe", "%s:%d fread() saw \"universe\"", __FILE__, __LINE__);

    rewind(fp);
    ok(fread(buf, 15, 1, fp) == 1,
       "%s:%d fread() after rewind() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "hello universe", "%s:%d fread() saw \"hello universe\"",
       __FILE__, __LINE__);

    /* fgets() tests */
    rewind(fp);
    memset(buf, 0, sizeof(buf));
    tmp = fgets(buf, 15, fp);
    ok(tmp == buf, "%s:%d fgets() after rewind() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "hello universe", "%s:%d fgets() saw \"hello universe\"",
       __FILE__, __LINE__);

    rewind(fp);
    memset(buf, 0, sizeof(buf));
    tmp = fgets(buf, 6, fp);
    ok(tmp == buf, "%s:%d fgets() w/ size = 6 after rewind() buf[]=\"%s\": %s",
        __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "hello", "%s:%d fgets() saw \"hello\"", __FILE__, __LINE__);

    rewind(fp);
    ok(fread(buf, sizeof(buf), 1, fp) != 1, "%s:%d fread() EOF: %s",
       __FILE__, __LINE__, strerror(errno));

    ok(feof(fp) != 0, "%s:%d feof() past EOF:  %s",
       __FILE__, __LINE__, strerror(errno));

    ok(fclose(fp) == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(errno));

    /* fwrite to closed stream fails with errno=EBADF */
    ok(fwrite("hello world", 12, 1, fp) == 0 && errno == EBADF,
       "%s:%d fwrite() to closed stream fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* fread from closed stream fails with errno=EBADF */
    ok(fread(buf, 15, 1, fp) == 0 && errno == EBADF,
       "%s:%d fread() from closed stream fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* fgets from closed stream fails with errno=EBADF */
    memset(buf, 0, sizeof(buf));
    tmp = fgets(buf, 15, fp);
    ok(errno == EBADF, "%s:%d fgets() from closed stream fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    ok(feof(fp) != 0, "%s:%d feof() on closed stream: %s",
       __FILE__, __LINE__, strerror(errno));

    diag("Finished UNIFYFS_WRAP(fwrite/fread/fgets/feof) tests");

    return 0;
}
