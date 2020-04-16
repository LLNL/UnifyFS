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
    int fd = -1;

    errno = 0;

    /* Create a random file at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* lseek in bad file descriptor should fail with errno=EBADF */
    ok(lseek(fd, 0, SEEK_SET) == -1 && errno == EBADF,
       "%s:%d lseek in bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0; /* reset errno after test for failure */

    /* Open a file and write to it to test lseek() */
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, file_mode);
    ok(fd >= 0, "%s:%d open worked: %s", __FILE__, __LINE__, strerror(errno));
    ok(write(fd, "hello world", 12) == 12, "%s:%d write worked: %s",
        __FILE__, __LINE__, strerror(errno));

    /* lseek with invalid whence fails with errno=EINVAL. */
    ok(lseek(fd, 0, -1) == -1 && errno == EINVAL,
       "%s:%d lseek with invalid whence should fail (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* lseek() with SEEK_SET tests */
    /* lseek to negative offset with SEEK_SET should fail with errno=EINVAL */
    ok(lseek(fd, -1, SEEK_SET) == -1 && errno == EINVAL,
       "%s:%d lseek(-1) to invalid offset w/ SEEK_SET fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* lseek to valid offset with SEEK_SET succeeds */
    ok(lseek(fd, 7, SEEK_SET) == 7,
       "%s:%d lseek(7) to valid offset w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek beyond end of file with SEEK_SET succeeds */
    ok(lseek(fd, 25, SEEK_SET) == 25,
       "%s:%d lseek(25) beyond EOF w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek to beginning of file with SEEK_SET succeeds */
    ok(lseek(fd, 0, SEEK_SET) == 0, "%s:%d lseek(0) w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek() with SEEK_CUR tests */
    /* lseek to end of file with SEEK_CUR succeeds */
    ok(lseek(fd, 12, SEEK_CUR) == 12, "%s:%d lseek(12) to EOF w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek to negative offset with SEEK_CUR should fail with errno=EINVAL */
    ok(lseek(fd, -15, SEEK_CUR) == -1 && errno == EINVAL,
       "%s:%d lseek(-15) to invalid offset w/ SEEK_CUR fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* lseek to beginning of file with SEEK_CUR succeeds */
    ok(lseek(fd, -12, SEEK_CUR) == 0,
       "%s:%d lseek(-12) to beginning of file w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek beyond end of file with SEEK_CUR succeeds */
    ok(lseek(fd, 25, SEEK_CUR) == 25,
       "%s:%d lseek(25) beyond EOF w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek() with SEEK_END tests */
    /* lseek to negative offset with SEEK_END should fail with errno=EINVAL */
    ok(lseek(fd, -15, SEEK_END) == -1 && errno == EINVAL,
       "%s:%d lseek(-15) to invalid offset w/ SEEK_END fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* lseek back one from end of file with SEEK_END succeeds */
    ok(lseek(fd, -1, SEEK_END) == 11,
       "%s:%d lseek(-1) from EOF w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek to beginning of file with SEEK_END succeeds */
    ok(lseek(fd, -12, SEEK_END) == 0,
       "%s:%d lseek(-12) to beginning of file w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek beyond end of file with SEEK_END succeeds */
    ok(lseek(fd, 25, SEEK_END) == 37,
       "%s:%d lseek(25) beyond EOF w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek() with SEEK_DATA tests */
    /* Write beyond end of file to create a hole */
    ok(write(fd, "hello universe", 15) == 15, "%s:%d write to create hole: %s",
        __FILE__, __LINE__, strerror(errno));

    /* lseek to negative offset with SEEK_DATA should fail with errno=ENXIO */
    ok(lseek(fd, -1, SEEK_DATA) == -1 && errno == ENXIO,
       "%s:%d lseek(-1) to invalid offset w/ SEEK_DATA fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* lseek to beginning of file with SEEK_DATA succeeds */
    ok(lseek(fd, 0, SEEK_DATA) == 0, "%s:%d lseek(0) w/ SEEK_DATA: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Fallback implementation: lseek to data after hole with SEEK_DATA returns
     * current offset */
    ok(lseek(fd, 15, SEEK_DATA) == 15,
       "%s:%d lseek(15) to data after hole w/ SEEK_DATA returns offset: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek beyond end of file with SEEK_DATA should fail with errno=ENXIO */
    ok(lseek(fd, 75, SEEK_DATA) == -1 && errno == ENXIO,
       "%s:%d lseek(75) beyond EOF w/ SEEK_DATA fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* lseek() with SEEK_HOLE tests */
    /* lseek to negative offset with SEEK_HOLE should fail with errno=ENXIO */
    ok(lseek(fd, -1, SEEK_HOLE) == -1 && errno == ENXIO,
       "%s:%d lseek(-1) to invalid offset w/ SEEK_HOLE fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    /* Fallback implementation: lseek to first hole of file with SEEK_HOLE
     * returns or EOF */
    ok(lseek(fd, 0, SEEK_HOLE) == 52,
       "%s:%d lseek(0) to first hole in file w/ SEEK_HOLE returns EOF: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Fallback implementation: lseek to middle of hole with SEEK_HOLE returns
     * EOF */
    ok(lseek(fd, 18, SEEK_HOLE) == 52,
       "%s:%d lseek(18) to middle of hole w/ SEEK_HOLE returns EOF: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek to end of file with SEEK_HOLE succeeds */
    ok(lseek(fd, 42, SEEK_HOLE) == 52,
       "%s:%d lseek(42) to EOF w/ SEEK_HOLE: %s",
       __FILE__, __LINE__, strerror(errno));

    /* lseek beyond end of file with SEEK_HOLE should fail with errno= ENXIO */
    ok(lseek(fd, 75, SEEK_HOLE) == -1 && errno == ENXIO,
       "%s:%d lseek beyond EOF w/ SEEK_HOLE fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    close(fd);

    /* lseek in non-open file descriptor should fail with errno=EBADF */
    ok(lseek(fd, 0, SEEK_SET) == -1 && errno == EBADF,
       "%s:%d lseek in non-open file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, errno, strerror(errno));
    errno = 0;

    diag("Finished UNIFYFS_WRAP(lseek) tests");

    return 0;
}
