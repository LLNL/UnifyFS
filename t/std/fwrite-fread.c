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
  * Test fwrite/fread/fseek/fgets/rewind/ftell/feof/chmod
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
    char path[64];
    char buf[64] = {0};
    FILE* fp = NULL;
    int rc;
    char* tmp;

    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Write "hello world" to a file */
    fp = fopen(path, "w");
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(errno));

    rc = fwrite("hello world", 12, 1, fp);
    ok(rc == 1, "%s: fwrite(\"hello world\"): %s", __FILE__, strerror(errno));

    rc = fclose(fp);
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Sync extents */
    int fd;
    fd = open(path, O_RDWR);
    rc = fsync(fd);
    ok(rc == 0, "%s: fsync() (rc=%d): %s", __FILE__, rc, strerror(errno));
    close(fd);

    /* Laminate */
    rc = chmod(path, 0444);
    ok(rc == 0, "%s: chmod(0444) (rc=%d): %s", __FILE__, strerror(errno));

    /* Read it back */
    fp = fopen(path, "r");
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(errno));

    rc = fread(buf, 12, 1, fp);
    ok(rc == 1, "%s: fread() buf[]=\"%s\", (rc %d): %s", __FILE__, buf, rc,
        strerror(errno));
    is(buf, "hello world", "%s: saw \"hello world\"", __FILE__);

    fseek(fp, 6, SEEK_SET);
    rc = ftell(fp);
    ok(rc == 6, "%s: fseek() (rc %d): %s", __FILE__, rc, strerror(errno));

    rc = fread(buf, 6, 1, fp);
    ok(rc == 1, "%s: fread() at offset 6 buf[]=\"%s\", (rc %d): %s", __FILE__,
        buf, rc, strerror(errno));
    is(buf, "world", "%s: saw \"world\"", __FILE__);

    rewind(fp);
    rc = fread(buf, 12, 1, fp);
    ok(rc == 1, "%s: fread() after rewind() buf[]=\"%s\", (rc %d): %s",
        __FILE__, buf, rc, strerror(errno));
    is(buf, "hello world", "%s: saw \"hello world\"", __FILE__);

    rewind(fp);
    memset(buf, 0, sizeof(buf));
    tmp = fgets(buf, 12, fp);
    ok(tmp == buf, "%s: fgets() after rewind() buf[]=\"%s\": %s", __FILE__, buf,
        strerror(errno));
    is(buf, "hello world", "%s: saw \"hello world\"", __FILE__);

    rewind(fp);
    memset(buf, 0, sizeof(buf));
    tmp = fgets(buf, 6, fp);
    ok(tmp == buf, "%s: fgets() with size = 6 after rewind() buf[]=\"%s\": %s",
        __FILE__, buf, strerror(errno));
    is(buf, "hello", "%s: saw \"hello\"", __FILE__);

    rewind(fp);
    rc = fread(buf, sizeof(buf), 1, fp);
    ok(rc != 1, "%s: fread() past end of file (rc %d): %s", __FILE__, rc,
        strerror(errno));

    rc = feof(fp);
    ok(rc != 0, "%s: feof() past end of file (rc %d): %s", __FILE__, rc,
        strerror(errno));

    rc = fclose(fp);
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(errno));

    return 0;
}
