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
    int err, rc;

    /* Create a random file at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* lseek in bad file descriptor should fail with errno=EBADF */
    errno = 0;
    rc = (int) lseek(fd, 0, SEEK_SET);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d lseek in bad file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* Open a file and write to it to test lseek() */
    errno = 0;
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, file_mode);
    err = errno;
    ok(fd >= 0 && err == 0, "%s:%d open worked: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = (int) write(fd, "hello world", 12);
    err = errno;
    ok(rc == 12 && err == 0,
       "%s:%d write worked: %s", __FILE__, __LINE__, strerror(err));

    /* lseek with invalid whence fails with errno=EINVAL. */
    errno = 0;
    rc = (int) lseek(fd, 0, -1);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d lseek with invalid whence should fail (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /*--- lseek() with SEEK_SET tests ---*/

    /* lseek to negative offset with SEEK_SET should fail with errno=EINVAL */
    errno = 0;
    rc = (int) lseek(fd, -1, SEEK_SET);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d lseek(-1, SEEK_SET) to invalid offset fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* lseek to valid offset with SEEK_SET succeeds */
    errno = 0;
    rc = (int) lseek(fd, 7, SEEK_SET);
    err = errno;
    ok(rc == 7 && err == 0,
       "%s:%d lseek(7, SEEK_SET) to valid offset: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek beyond end of file with SEEK_SET succeeds */
    errno = 0;
    rc = (int) lseek(fd, 25, SEEK_SET);
    err = errno;
    ok(rc == 25 && err == 0,
       "%s:%d lseek(25, SEEK_SET) beyond EOF: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek to beginning of file with SEEK_SET succeeds */
    errno = 0;
    rc = (int) lseek(fd, 0, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d lseek(0, SEEK_SET): %s",
       __FILE__, __LINE__, strerror(err));

    /*--- lseek() with SEEK_CUR tests ---*/

    /* lseek to end of file with SEEK_CUR succeeds */
    errno = 0;
    rc = (int) lseek(fd, 12, SEEK_CUR);
    err = errno;
    ok(rc == 12 && err == 0,
       "%s:%d lseek(12, SEEK_CUR) to EOF: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek to negative offset with SEEK_CUR should fail with errno=EINVAL */
    errno = 0;
    rc = (int) lseek(fd, -15, SEEK_CUR);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d lseek(-15, SEEK_CUR) to invalid offset fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* lseek to beginning of file with SEEK_CUR succeeds */
    errno = 0;
    rc = (int) lseek(fd, -12, SEEK_CUR);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d lseek(-12, SEEK_CUR) to beginning of file: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek beyond end of file with SEEK_CUR succeeds */
    errno = 0;
    rc = (int) lseek(fd, 25, SEEK_CUR);
    err = errno;
    ok(rc == 25 && err == 0,
       "%s:%d lseek(25, SEEK_CUR) beyond EOF: %s",
       __FILE__, __LINE__, strerror(err));

    /*--- lseek() with SEEK_END tests ---*/

    /* lseek to negative offset with SEEK_END should fail with errno=EINVAL */
    errno = 0;
    rc = (int) lseek(fd, -15, SEEK_END);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d lseek(-15, SEEK_END) to invalid offset fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* lseek back one from end of file with SEEK_END succeeds */
    errno = 0;
    rc = (int) lseek(fd, -1, SEEK_END);
    err = errno;
    ok(rc == 11 && err == 0,
       "%s:%d lseek(-1, SEEK_END) from EOF: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek to beginning of file with SEEK_END succeeds */
    errno = 0;
    rc = (int) lseek(fd, -12, SEEK_END);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d lseek(-12, SEEK_END) to beginning of file: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek beyond end of file with SEEK_END succeeds */
    errno = 0;
    rc = (int) lseek(fd, 25, SEEK_END);
    err = errno;
    ok(rc == 37 && err == 0,
       "%s:%d lseek(25, SEEK_END) beyond EOF: %s",
       __FILE__, __LINE__, strerror(err));

    /*--- lseek() with SEEK_DATA tests ---*/

    /* Write beyond end of file to create a hole */
    errno = 0;
    rc = (int) write(fd, "hello universe", 15);
    err = errno;
    ok(rc == 15 && err == 0,
       "%s:%d write to create hole: %s",
        __FILE__, __LINE__, strerror(err));

    /* lseek to negative offset with SEEK_DATA should fail with errno=ENXIO */
    errno = 0;
    rc = (int) lseek(fd, -1, SEEK_DATA);
    err = errno;
    ok(rc == -1 && err == ENXIO,
       "%s:%d lseek(-1, SEEK_DATA) to invalid offset fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* lseek to beginning of file with SEEK_DATA succeeds */
    errno = 0;
    rc = (int) lseek(fd, 0, SEEK_DATA);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d lseek(0, SEEK_DATA) w/ SEEK_DATA: %s",
       __FILE__, __LINE__, strerror(err));

    /* Fallback implementation: lseek to data after hole with SEEK_DATA returns
     * current offset */
    errno = 0;
    rc = (int) lseek(fd, 15, SEEK_DATA);
    err = errno;
    ok(rc == 15 && err == 0,
       "%s:%d lseek(15, SEEK_DATA) to data after hole returns offset: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek beyond end of file with SEEK_DATA should fail with errno=ENXIO */
    errno = 0;
    rc = (int) lseek(fd, 75, SEEK_DATA);
    err = errno;
    ok(rc == -1 && err == ENXIO,
       "%s:%d lseek(75, SEEK_DATA) beyond EOF fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /*--- lseek() with SEEK_HOLE tests ---*/

    /* lseek to negative offset with SEEK_HOLE should fail with errno=ENXIO */
    errno = 0;
    rc = (int) lseek(fd, -1, SEEK_HOLE);
    err = errno;
    ok(rc == -1 && err == ENXIO,
       "%s:%d lseek(-1, SEEK_HOLE) to invalid offset fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* Fallback implementation: lseek to first hole of file with SEEK_HOLE
     * returns or EOF */
    errno = 0;
    rc = (int) lseek(fd, 0, SEEK_HOLE);
    err = errno;
    ok(rc == 52 && err == 0,
       "%s:%d lseek(0, SEEK_HOLE) to first hole in file returns EOF: %s",
       __FILE__, __LINE__, strerror(err));

    /* Fallback implementation: lseek to middle of hole with SEEK_HOLE returns
     * EOF */
    errno = 0;
    rc = (int) lseek(fd, 18, SEEK_HOLE);
    err = errno;
    ok(rc == 52 && err == 0,
       "%s:%d lseek(18, SEEK_HOLE) to middle of hole returns EOF: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek to end of file with SEEK_HOLE succeeds */
    errno = 0;
    rc = (int) lseek(fd, 42, SEEK_HOLE);
    err = errno;
    ok(rc == 52 && err == 0,
       "%s:%d lseek(42, SEEK_HOLE) to EOF w/ SEEK_HOLE: %s",
       __FILE__, __LINE__, strerror(err));

    /* lseek beyond end of file with SEEK_HOLE should fail with errno= ENXIO */
    errno = 0;
    rc = (int) lseek(fd, 75, SEEK_HOLE);
    err = errno;
    ok(rc == -1 && err == ENXIO,
       "%s:%d lseek(75, SEEK_HOLE) beyond EOF fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    close(fd);

    /* lseek in non-open file descriptor should fail with errno=EBADF */
    errno = 0;
    rc = (int) lseek(fd, 0, SEEK_SET);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d lseek in non-open file descriptor fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    diag("Finished UNIFYFS_WRAP(lseek) tests");

    return 0;
}
