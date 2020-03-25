/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYFS_WRAP(fseek),
 * UNIFYFS_WRAP(ftell), and UNIFYFS_WRAP(rewind) found in
 * client/src/unifyfs-stdio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int fseek_ftell_test(char* unifyfs_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYFS_WRAP(fseek/ftell/rewind) tests");

    char path[64];
    FILE* fd;
    int ret;
    long pos;

    /* Create a random file at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a file and write to it to test fseek() */
    fd = fopen(path, "w");
    fwrite("hello world", 12, 1, fd);

    /* fseek with invalid whence fails with errno=EINVAL. */
    errno = 0;
    ret = fseek(fd, 0, -1);
    ok(ret == -1 && errno == EINVAL,
       "%s: fseek with invalid whence should fail (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* fseek() with SEEK_SET tests */
    /* fseek to negative offset with SEEK_SET should fail with errno=EINVAL */
    errno = 0;
    ret = fseek(fd, -1, SEEK_SET);
    ok(ret == -1 && errno == EINVAL,
       "%s: fseek to negative offset w/ SEEK_SET fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* ftell after invalid fseek should return last offset */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 12,
       "%s: ftell after fseek to negative offset w/ SEEK_SET (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek to valid offset with SEEK_SET succeeds */
    errno = 0;
    ret = fseek(fd, 7, SEEK_SET);
    ok(ret == 0, "%s: fseek to valid offset w/ SEEK_SET (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after valid fseek with SEEK_SET */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 7, "%s: ftell after valid fseek w/ SEEK_SET (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek beyond end of file with SEEK_SET succeeds */
    errno = 0;
    ret = fseek(fd, 25, SEEK_SET);
    ok(ret == 0, "%s: fseek beyond end of file w/ SEEK_SET (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek beyond end of file with SEEK_SET */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 25,
       "%s: ftell after fseek beyond end of file w/ SEEK_SET (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek to beginning of file with SEEK_SET succeeds */
    errno = 0;
    ret = fseek(fd, 0, SEEK_SET);
    ok(ret == 0, "%s: fseek to beginning of file w/ SEEK_SET (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek to beginning of file with SEEK_SET */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 0,
       "%s: ftell after fseek to beginning of file w/ SEEK_SET (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek() with SEEK_CUR tests */
    /* fseek to end of file with SEEK_CUR succeeds */
    errno = 0;
    ret = fseek(fd, 12, SEEK_CUR);
    ok(ret == 0, "%s: fseek to end of file w/ SEEK_CUR (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek to end of file with SEEK_CUR */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 12,
       "%s: ftell after fseek to beginning of file w/ SEEK_CUR (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek to negative offset with SEEK_CUR should fail with errno=EINVAL */
    errno = 0;
    ret = fseek(fd, -15, SEEK_CUR);
    ok(ret == -1 && errno == EINVAL,
       "%s: fseek to negative offset w/ SEEK_CUR fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* ftell after fseek to negative offset with SEEK_CUR */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 12,
       "%s: ftell after fseek to negative offset w/ SEEK_CUR (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek to beginning of file with SEEK_CUR succeeds */
    errno = 0;
    ret = fseek(fd, -12, SEEK_CUR);
    ok(ret == 0, "%s: fseek to beginning of file w/ SEEK_CUR (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek to beginning of file with SEEK_CUR */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 0,
       "%s: ftell after fseek to beginnig of file w/ SEEK_CUR (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek beyond end of file with SEEK_CUR succeeds */
    errno = 0;
    ret = fseek(fd, 25, SEEK_CUR);
    ok(ret == 0, "%s: fseek beyond end of file w/ SEEK_CUR (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek beyond end of file with SEEK_CUR */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 25,
       "%s: ftell after fseek beyond end of file w/ SEEK_CUR (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* rewind test */
    /* ftell after rewind reports beginning of file */
    rewind(fd);
    errno = 0;
    pos = ftell(fd);
    ok(pos == 0,
       "%s: ftell after rewind reports beginning of file (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek() with SEEK_END tests */
    /* fseek to negative offset with SEEK_END should fail with errno=EINVAL */
    errno = 0;
    ret = fseek(fd, -15, SEEK_END);
    ok(ret == -1 && errno == EINVAL,
       "%s: fseek to negative offset w/ SEEK_END fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* ftell after fseek to negative offset with SEEK_END */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 0,
       "%s: ftell after fseek to negative offset w/ SEEK_END (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek back one from end of file with SEEK_END succeeds */
    errno = 0;
    ret = fseek(fd, -1, SEEK_END);
    ok(ret == 0,
       "%s: fseek back one from end of file w/ SEEK_END (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek back one from end of file with SEEK_END */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 11,
       "%s: ftell after fseek back one from end w/ SEEK_END (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek to beginning of file with SEEK_END succeeds */
    errno = 0;
    ret = fseek(fd, -12, SEEK_END);
    ok(ret == 0, "%s: fseek to beginning of file w/ SEEK_END (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek to beginning of file with SEEK_END */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 0,
       "%s: ftell after fseek to beginning of file w/ SEEK_END (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    /* fseek beyond end of file with SEEK_END succeeds */
    errno = 0;
    ret = fseek(fd, 25, SEEK_END);
    ok(ret == 0, "%s: fseek beyond end of file w/ SEEK_END (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* ftell after fseek beyond end of file with SEEK_END */
    errno = 0;
    pos = ftell(fd);
    ok(pos == 37,
       "%s: ftell after fseek beyond end of file w/ SEEK_END (pos=%ld): %s",
       __FILE__, pos, strerror(errno));

    fclose(fd);

    /* fseek in non-open file descriptor should fail with errno=EBADF */
    errno = 0;
    ret = fseek(fd, 0, SEEK_SET);
    ok(ret == -1 && errno == EBADF,
       "%s: fseek in non-open file descriptor fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* ftell on non-open file descriptor should fail with errno=EBADF */
    errno = 0;
    pos = ftell(fd);
    ok(pos == -1 && errno == EBADF,
       "%s: ftell on non-open file descriptor fails (pos=%ld, errno=%d): %s",
       __FILE__, pos, errno, strerror(errno));

    /* rewind on non-open file descriptor should fail with errno=EBADF */
    errno = 0;
    rewind(fd);
    ok(errno == EBADF,
       "%s: rewind on non-open file descriptor fails (errno=%d): %s",
       __FILE__, errno, strerror(errno));

    diag("Finished UNIFYFS_WRAP(fseek/ftell/rewind) tests");

    return 0;
}
