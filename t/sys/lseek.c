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
#include <string.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYFS_WRAP(lseek) found in
 * client/src/unifyfs-sysio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int lseek_test(char* unifyfs_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYFS_WRAP(lseek) tests");

    char path[64];
    int file_mode = 0600;
    int fd;
    off_t ret;

    /* Create a random file at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Open a file and write to it to test lseek() */
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, file_mode);
    write(fd, "hello world", 12);

    /* lseek with invalid whence fails with errno=EINVAL. */
    errno = 0;
    ret = lseek(fd, 0, -1);
    ok(ret == -1 && errno == EINVAL,
       "%s: lseek with invalid whence should fail (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* lseek() with SEEK_SET tests */
    /* lseek to negative offset with SEEK_SET should fail with errno=EINVAL */
    errno = 0;
    ret = lseek(fd, -1, SEEK_SET);
    ok(ret == -1 && errno == EINVAL,
       "%s: lseek to negative offset w/ SEEK_SET fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* lseek to valid offset with SEEK_SET succeeds */
    errno = 0;
    ret = lseek(fd, 7, SEEK_SET);
    ok(ret == 7, "%s: lseek to valid offset w/ SEEK_SET (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek beyond end of file with SEEK_SET succeeds */
    errno = 0;
    ret = lseek(fd, 25, SEEK_SET);
    ok(ret == 25, "%s: lseek beyond end of file w/ SEEK_SET (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek to beginning of file with SEEK_SET succeeds */
    errno = 0;
    ret = lseek(fd, 0, SEEK_SET);
    ok(ret == 0, "%s: lseek to beginning of file w/ SEEK_SET (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek() with SEEK_CUR tests */
    /* lseek to end of file with SEEK_CUR succeeds */
    errno = 0;
    ret = lseek(fd, 12, SEEK_CUR);
    ok(ret == 12, "%s: lseek to end of file w/ SEEK_CUR (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek to negative offset with SEEK_CUR should fail with errno=EINVAL */
    errno = 0;
    ret = lseek(fd, -15, SEEK_CUR);
    ok(ret == -1 && errno == EINVAL,
       "%s: lseek to negative offset w/ SEEK_CUR fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* lseek to beginning of file with SEEK_CUR succeeds */
    errno = 0;
    ret = lseek(fd, -12, SEEK_CUR);
    ok(ret == 0, "%s: lseek to beginning of file w/ SEEK_CUR (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek beyond end of file with SEEK_CUR succeeds */
    errno = 0;
    ret = lseek(fd, 25, SEEK_CUR);
    ok(ret == 25, "%s: lseek beyond end of file w/ SEEK_CUR (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek() with SEEK_END tests */
    /* lseek to negative offset with SEEK_END should fail with errno=EINVAL */
    errno = 0;
    ret = lseek(fd, -15, SEEK_END);
    ok(ret == -1 && errno == EINVAL,
       "%s: lseek to negative offset w/ SEEK_END fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* lseek back one from end of file with SEEK_END succeeds */
    errno = 0;
    ret = lseek(fd, -1, SEEK_END);
    ok(ret == 11,
       "%s: lseek back one from end of file w/ SEEK_END (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek to beginning of file with SEEK_END succeeds */
    errno = 0;
    ret = lseek(fd, -12, SEEK_END);
    ok(ret == 0, "%s: lseek to beginning of file w/ SEEK_END (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek beyond end of file with SEEK_END succeeds */
    errno = 0;
    ret = lseek(fd, 25, SEEK_END);
    ok(ret == 37, "%s: lseek beyond end of file w/ SEEK_END (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek() with SEEK_DATA tests */
    /* Write beyond end of file to create a hole */
    write(fd, "hello universe", 15);

    /* lseek to negative offset with SEEK_DATA should fail with errno=ENXIO */
    errno = 0;
    ret = lseek(fd, -1, SEEK_DATA);
    ok(ret == -1 && errno == ENXIO,
       "%s: lseek to negative offset w/ SEEK_DATA fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* lseek to beginning of file with SEEK_DATA succeeds */
    errno = 0;
    ret = lseek(fd, 0, SEEK_DATA);
    ok(ret == 0, "%s: lseek to beginning of file w/ SEEK_DATA (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek to data after hole with SEEK_DATA returns current offset or offset
     * of first set of data */
    errno = 0;
    ret = lseek(fd, 15, SEEK_DATA);
    ok(ret == 15 || ret == 37,
       "%s: lseek to data after hole w/ SEEK_DATA (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek beyond end of file with SEEK_DATA should fail with errno=ENXIO */
    errno = 0;
    ret = lseek(fd, 75, SEEK_DATA);
    ok(ret == -1 && errno == ENXIO,
       "%s: lseek beyond end of file w/ SEEK_DATA fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* lseek() with SEEK_HOLE tests */

    /* lseek to negative offset with SEEK_HOLE should fail with errno=ENXIO */
    errno = 0;
    ret = lseek(fd, -1, SEEK_HOLE);
    ok(ret == -1 && errno == ENXIO,
       "%s: lseek to negative offset w/ SEEK_HOLE fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    /* lseek to first hole of file with SEEK_HOLE succeeds returns start of hole
     * or EOF */
    errno = 0;
    ret = lseek(fd, 0, SEEK_HOLE);
    ok(ret == 12 || ret == 52,
       "%s: lseek to first hole in file w/ SEEK_HOLE (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek to middle of hole with SEEK_HOLE returns position in hole or EOF */
    errno = 0;
    ret = lseek(fd, 18, SEEK_HOLE);
    ok(ret == 18 || ret == 52,
       "%s: lseek to middle of hole w/ SEEK_HOLE (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek to end of file with SEEK_HOLE succeeds */
    errno = 0;
    ret = lseek(fd, 42, SEEK_HOLE);
    ok(ret == 52, "%s: lseek to end of file w/ SEEK_HOLE (ret=%d): %s",
       __FILE__, ret, strerror(errno));

    /* lseek beyond end of file with SEEK_HOLE should fail with errno= ENXIO */
    errno = 0;
    ret = lseek(fd, 75, SEEK_HOLE);
    ok(ret == -1 && errno == ENXIO,
       "%s: lseek beyond end of file w/ SEEK_HOLE fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    close(fd);

    /* lseek in non-open file descriptor should fail with errno=EBADF */
    errno = 0;
    ret = lseek(fd, 0, SEEK_SET);
    ok(ret == -1 && errno == EBADF,
       "%s: lseek in non-open file descriptor fails (ret=%d, errno=%d): %s",
       __FILE__, ret, errno, strerror(errno));

    diag("Finished UNIFYFS_WRAP(lseek) tests");

    return 0;
}
