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
    FILE* fp = NULL;
    int err, rc;

    /* Create a random file at the mountpoint path to test on */
    testutil_rand_path(path, sizeof(path), unifyfs_root);

    skip(1, 3, "causing a hang on some architectures. Try after future update");
    /* fseek on bad file stream should fail with errno=EBADF */
    dies_ok({ fseek(fp, 0, SEEK_SET); },
            "%s:%d fseek on bad file stream segfaults",
            __FILE__, __LINE__);

    /* ftell on non-open file stream should fail with errno=EBADF
     * variable declaration and `ok` test are to avoid a compiler warning */
    dies_ok({ int rc = ftell(fp); ok(rc > 0); },
            "%s:%d ftell on bad file stream segfaults",
            __FILE__, __LINE__);

    /* rewind on non-open file stream should fail with errno=EBADF */
    dies_ok({ rewind(fp); }, "%s:%d rewind on bad file stream segfaults",
            __FILE__, __LINE__);
    end_skip;

    /* Open a file and write to it to test fseek() */
    errno = 0;
    fp = fopen(path, "w");
    err = errno;
    ok(fp != NULL && err == 0, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(err));

    errno = 0;
    rc = (int) fwrite("hello world", 12, 1, fp);
    err = errno;
    ok(rc == 1 && err == 0,
       "%s:%d fwrite() to file %s: %s",
        __FILE__, __LINE__, path, strerror(err));

    /* fseek with invalid whence fails with errno=EINVAL. */
    errno = 0;
    rc = (int) fseek(fp, 0, -1);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d fseek with invalid whence should fail (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /*--- fseek() with SEEK_SET tests ---*/

    /* fseek to negative offset with SEEK_SET should fail with errno=EINVAL */
    errno = 0;
    rc = (int) fseek(fp, -1, SEEK_SET);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d fseek(-1) to invalid offset w/ SEEK_SET fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* ftell after invalid fseek should return last offset */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 12 && err == 0,
       "%s:%d ftell after fseek(-1) to invalid offset w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek to valid offset with SEEK_SET succeeds */
    errno = 0;
    rc = (int) fseek(fp, 7, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(7) to valid offset w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after valid fseek with SEEK_SET */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 7 && err == 0,
       "%s:%d ftell after fseek(7) w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek beyond end of file with SEEK_SET succeeds */
    errno = 0;
    rc = (int) fseek(fp, 25, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(25) past EOF w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after fseek beyond end of file with SEEK_SET */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 25 && err == 0,
       "%s:%d ftell after fseek(25) w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek to beginning of file with SEEK_SET succeeds */
    errno = 0;
    rc = (int) fseek(fp, 0, SEEK_SET);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(0) w/ SEEK_SET: %s",
        __FILE__, __LINE__, strerror(err));

    /* ftell after fseek to beginning of file with SEEK_SET */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d ftell after fseek(0) w/ SEEK_SET: %s",
       __FILE__, __LINE__, strerror(err));

    /*--- fseek() with SEEK_CUR tests ---*/

    /* fseek to end of file with SEEK_CUR succeeds */
    errno = 0;
    rc = (int) fseek(fp, 12, SEEK_CUR);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(12) to EOF w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after fseek to end of file with SEEK_CUR */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 12 && err == 0,
       "%s:%d ftell after fseek(12) w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek to negative offset with SEEK_CUR should fail with errno=EINVAL */
    errno = 0;
    rc = (int) fseek(fp, -15, SEEK_CUR);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d fseek(-15) to invalid offset w/ SEEK_CUR fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* ftell after fseek to negative offset with SEEK_CUR */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 12 && err == 0,
       "%s:%d ftell after fseek(-15) to invalid offset w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek to beginning of file with SEEK_CUR succeeds */
    errno = 0;
    rc = (int) fseek(fp, -12, SEEK_CUR);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(-12) to beginning of file w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after fseek to beginning of file with SEEK_CUR */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d ftell after fseek(-12) to beginning of file w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek beyond end of file with SEEK_CUR succeeds */
    errno = 0;
    rc = (int) fseek(fp, 25, SEEK_CUR);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(25) past EOF w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after fseek beyond end of file with SEEK_CUR */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 25 && err == 0,
       "%s:%d ftell after fseek(25) past EOF w/ SEEK_CUR: %s",
       __FILE__, __LINE__, strerror(err));

    /*--- rewind test ---*/

    /* ftell after rewind reports beginning of file */
    rewind(fp);
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d ftell after rewind reports beginning of file: %s",
       __FILE__, __LINE__, strerror(err));

    /*--- fseek() with SEEK_END tests ---*/

    /* fseek to negative offset with SEEK_END should fail with errno=EINVAL */
    errno = 0;
    rc = (int) fseek(fp, -15, SEEK_END);
    err = errno;
    ok(rc == -1 && err == EINVAL,
       "%s:%d fseek(-15) to invalid offset w/ SEEK_END fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* ftell after fseek to negative offset with SEEK_END */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d ftell after fseek(-15) to negative offset w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek back one from end of file with SEEK_END succeeds */
    errno = 0;
    rc = (int) fseek(fp, -1, SEEK_END);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(-1) from EOF w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after fseek back one from end of file with SEEK_END */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 11 && err == 0,
       "%s:%d ftell after fseek(-1) from end w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek to beginning of file with SEEK_END succeeds */
    errno = 0;
    rc = (int) fseek(fp, -12, SEEK_END);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(-12) to beginning of file w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after fseek to beginning of file with SEEK_END */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d ftell after fseek(-12) to beginning of file w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(err));

    /* fseek beyond end of file with SEEK_END succeeds */
    errno = 0;
    rc = (int) fseek(fp, 25, SEEK_END);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d fseek(25) past EOF w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(err));

    /* ftell after fseek beyond end of file with SEEK_END */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == 37 && err == 0,
       "%s:%d ftell after fseek(25) past EOF w/ SEEK_END: %s",
       __FILE__, __LINE__, strerror(err));

    errno = 0;
    rc = fclose(fp);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(err));

    /*--- non-open file stream tests ---*/

    /* fseek in non-open file stream should fail with errno=EBADF */
    errno = 0;
    rc = (int) fseek(fp, 0, SEEK_SET);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d fseek in non-open file stream fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* ftell on non-open file stream should fail with errno=EBADF */
    errno = 0;
    rc = (int) ftell(fp);
    err = errno;
    ok(rc == -1 && err == EBADF,
       "%s:%d ftell on non-open file stream fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    /* rewind on non-open file stream should fail with errno=EBADF */
    errno = 0;
    rewind(fp);
    err = errno;
    ok(err == EBADF,
       "%s:%d rewind on non-open file stream fails (errno=%d): %s",
       __FILE__, __LINE__, err, strerror(err));

    diag("Finished UNIFYFS_WRAP(fseek/ftell/rewind) tests");

    return 0;
}
