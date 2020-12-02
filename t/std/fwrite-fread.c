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

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
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
    int err, rc;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    skip(1, 4, "causing hang on some architectures. Try after future update");
    /* fwrite to bad FILE stream should segfault */
    dies_ok({ FILE* p = fopen("/tmp/fakefile", "r");
              fwrite("hello world", 12, 1, p); },
            "%s:%d fwrite() to bad FILE stream segfaults",
            __FILE__, __LINE__);

    /* fread from bad FILE stream should segfault  */
    dies_ok({ size_t rc = fread(buf, 15, 1, fp); ok(rc > 0); },
            "%s:%d fread() from bad FILE stream segfaults",
            __FILE__, __LINE__);

    /* fgets from bad FILE stream segfaults */
    memset(buf, 0, sizeof(buf));
    dies_ok({ char* tmp = fgets(buf, 15, fp); ok(tmp != NULL); },
            "%s:%d fgets() from bad FILE stream segfaults",
            __FILE__, __LINE__);

    dies_ok({ int rc = feof(fp); ok(rc > 0); },
            "%s:%d feof() on bad FILE stream segfaults",
            __FILE__, __LINE__);
    end_skip;

    /* Write "hello world" to a file */
    errno = 0;
    fp = fopen(path, "w");
    err = errno;
    ok(fp != NULL && err == 0, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(err));

    errno = 0;
    rc = (int) fwrite("hello world", 12, 1, fp);
    err = errno;
    ok(rc == 1 && err == 0,
       "%s:%d fwrite(\"hello world\") to file %s: %s",
       __FILE__, __LINE__, path, strerror(err));

    /* Seek to offset and overwrite "world" with "universe" */
    errno = 0;
    rc = fseek(fp, 6, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(6) to \"world\": %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) fwrite("universe", 9, 1, fp);
    err = errno;
    ok(rc == 1 && err == 0,
       "%s:%d overwrite \"world\" at offset 6 with \"universe\" : %s",
       __FILE__, __LINE__, strerror(err));

    /* fread from file open as write-only should fail with errno=EBADF */
    errno = 0;
    rc = fseek(fp, 0, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fseek(0): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) fread(buf, 15, 1, fp);
    err = errno;
    ok(rc == 0 && err == EBADF,
       "%s:%d fread() from file open as write-only should fail (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = fclose(fp);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(err));

    /* fsync() tests */
    /* fsync on bad file descriptor should fail with errno=EINVAL */
    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d fsync() on bad file descriptor should fail (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* Sync extents */
    errno = 0;
    fd = open(path, O_RDWR);
    err = errno;
    ok(fd != -1 && err == 0, "%s:%d open() (fd=%d): %s",
       __FILE__, __LINE__, fd, strerror(err));

    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fsync(): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close(): %s",
       __FILE__, __LINE__, strerror(err));

    /* fsync on non-open file should fail with errno=EBADF */
    errno = 0;
    rc = fsync(fd);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d fsync() on non-open file should fail (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* Laminate */
    errno = 0;
    rc = chmod(path, 0444);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chmod(0444): %s",
       __FILE__, __LINE__, strerror(err));

    /* fopen a laminated file for write should fail with errno=EROFS */
    errno = 0;
    fp = fopen(path, "w");
    err = errno;
    ok(fp == NULL && err == EROFS,
       "%s:%d fopen laminated file for write fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* fread() tests */
    errno = 0;
    fp = fopen(path, "r");
    err = errno;
    ok(fp != NULL && err == 0, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(err));

    /* fwrite to file open as read-only should fail with errno=EBADF */
    errno = 0;
    rc = (int) fwrite("hello world", 12, 1, fp);
    err = errno;
    ok(rc == 0 && err == EBADF,
       "%s:%d fwrite() to file open for read-only fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = (int) fread(buf, 15, 1, fp);
    err = errno;
    ok(rc == 1 && err == 0,
       "%s:%d fread() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(err));
    is(buf, "hello universe", "%s:%d fread() saw \"hello universe\"",
       __FILE__, __LINE__);

    errno = 0;
    rc = fseek(fp, 6, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(6): %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) fread(buf, 9, 1, fp);
    err = errno;
    ok(rc == 1 && err == 0,
       "%s:%d fread() at offset 6 buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(err));
    is(buf, "universe", "%s:%d fread() saw \"universe\"", __FILE__, __LINE__);

    rewind(fp);
    errno = 0;
    rc = (int) fread(buf, 15, 1, fp);
    err = errno;
    ok(rc == 1 && err == 0,
       "%s:%d fread() after rewind() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(err));
    is(buf, "hello universe", "%s:%d fread() saw \"hello universe\"",
       __FILE__, __LINE__);

    /* fgets() tests */
    rewind(fp);
    memset(buf, 0, sizeof(buf));
    errno = 0;
    tmp = fgets(buf, 15, fp);
    err = errno;
    ok(tmp == buf && err == 0,
       "%s:%d fgets() after rewind() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(err));
    is(buf, "hello universe", "%s:%d fgets() saw \"hello universe\"",
       __FILE__, __LINE__);

    rewind(fp);
    memset(buf, 0, sizeof(buf));
    errno = 0;
    tmp = fgets(buf, 6, fp);
    err = errno;
    ok(tmp == buf && err == 0,
       "%s:%d fgets() w/ size = 6 after rewind() buf[]=\"%s\": %s",
        __FILE__, __LINE__, buf, strerror(err));
    is(buf, "hello", "%s:%d fgets() saw \"hello\"", __FILE__, __LINE__);

    rewind(fp);
    errno = 0;
    rc = (int) fread(buf, sizeof(buf), 1, fp);
    err = errno;
    ok(rc != 1 && err == 0,
       "%s:%d fread() EOF: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = feof(fp);
    err = errno;
    ok(rc != 0 && err == 0, "%s:%d feof() past EOF: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = fclose(fp);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(err));

    /* fwrite to closed stream fails with errno=EBADF */
    errno = 0;
    rc = (int) fwrite("hello world", 12, 1, fp);
    err = errno;
    ok(rc == 0 && err == EBADF,
       "%s:%d fwrite() to closed stream fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* fread from closed stream fails with errno=EBADF */
    errno = 0;
    rc = (int) fread(buf, 15, 1, fp);
    err = errno;
    ok(rc == 0 && err == EBADF,
       "%s:%d fread() from closed stream fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* fgets from closed stream fails with errno=EBADF */
    memset(buf, 0, sizeof(buf));
    errno = 0;
    tmp = fgets(buf, 15, fp);
    err = errno;
    ok(err == EBADF,
       "%s:%d fgets() from closed stream fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    errno = 0;
    rc = feof(fp);
    err = errno;
    ok(rc != 0 && err == 0,
       "%s:%d feof() on closed stream: %s",
       __FILE__, __LINE__, strerror(err));

    diag("Finished UNIFYFS_WRAP(fwrite/fread/fgets/feof) tests");

    return 0;
}
