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

/*
 * Test correctness of local and global file size.  Also, test opening a file
 * for append, and test file positioning (fseek, ftell, etc).
 */

/* Get global, local, or log sizes (or all) */
static
void get_size(char* path, size_t* global, size_t* local, size_t* log)
{
    struct stat sb = {0};
    int rc;

    rc = stat(path, &sb);
    if (rc != 0) {
        printf("Error: %s\n", strerror(errno));
        exit(1);    /* die on failure */
    }
    if (global) {
        *global = sb.st_size;
    }

    if (local) {
        *local = sb.st_rdev & 0xFFFFFFFF;
    }

    if (log) {
        *log = (sb.st_rdev >> 32) & 0xFFFFFFFF;
    }
}

int size_test(char* unifyfs_root)
{
    char path[64];
    char buf[64] = {0};
    FILE* fp = NULL;
    int rc;
    char* tmp;
    size_t global, local, log;

    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Write "hello world" to a file */
    fp = fopen(path, "w");
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(errno));

    rc = fwrite("hello world", 12, 1, fp);
    ok(rc == 1, "%s: fwrite(\"hello world\"): %s", __FILE__, strerror(errno));

    rc = fclose(fp);
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 0, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 12, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 12, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));

    /* Open the file again with append, write to it. */
    fp = fopen(path, "a");
    ok(fp != NULL, "%s: fopen(%s) in append mode: %s", __FILE__, path,
        strerror(errno));

    rc = fwrite("HELLO WORLD", 12, 1, fp);
    ok(rc == 1, "%s: fwrite(\"HELLO WORLD\"): %s", __FILE__, strerror(errno));

    rc = ftell(fp);
    ok(rc == 24, "%s: ftell() (rc=%d) %s", __FILE__, rc, strerror(errno));

    /*
     * Set our position to somewhere in the middle of the file.  Since the file
     * is in append mode, this new position should be ignored, and writes
     * should still go to the end of the file.
     */
    rc = fseek(fp, 11, SEEK_SET);
    ok(rc == 0, "%s: fseek(11) (rc=%d) %s", __FILE__, rc, strerror(errno));

    rc = fwrite("<end>", 6, 1, fp);
    ok(rc == 1, "%s: fwrite(\" \") (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Test seeking to SEEK_END */
    rc = fseek(fp, 0, SEEK_END);
    ok(rc == 0, "%s: fseek(SEEK_END) (rc=%d) %s", __FILE__, rc,
        strerror(errno));

    rc = ftell(fp);
    ok(rc == 30, "%s: ftell() (rc=%d) %s", __FILE__, rc, strerror(errno));

    rc = fclose(fp);
    ok(rc == 0, "%s: fclose() (rc=%d): %s", __FILE__, rc, strerror(errno));

    get_size(path, &global, &local, &log);
    ok(global == 0, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 30, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 30, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));


    /* Sync extents */
    int fd;
    fd = open(path, O_RDWR);
    ok(fd >= 0, "%s: open() (fd=%d): %s", __FILE__, fd, strerror(errno));

    rc = fsync(fd);
    ok(rc == 0, "%s: fsync() (rc=%d): %s", __FILE__, rc, strerror(errno));
    close(fd);

    /* Laminate */
    rc = chmod(path, 0444);
    ok(rc == 0, "%s: chmod(0444) (rc=%d): %s", __FILE__, rc, strerror(errno));

    /* Both local and global size should be correct */
    get_size(path, &global, &local, &log);
    ok(global == 30, "%s: global size is %d: %s",  __FILE__, global,
        strerror(errno));
    ok(local == 30, "%s: local size is %d: %s",  __FILE__, local,
        strerror(errno));
    ok(log == 30, "%s: log size is %d: %s",  __FILE__, log,
        strerror(errno));

    /* Read it back */
    fp = fopen(path, "r");
    ok(fp != NULL, "%s: fopen(%s): %s", __FILE__, path, strerror(errno));

    memset(buf, 0, sizeof(buf));
    rc = fread(buf, 30, 1, fp);
    ok(rc == 1, "%s: fread() buf[]=\"%s\", (rc %d): %s", __FILE__, buf, rc,
        strerror(errno));

     /*
      * We wrote three strings to the file: "hello world" "HELLO WORLD" and
      * "<END>".  Replace the '\0' after the first two strings with spaces
      * so we can compare the file contents as one big string.
      */
    buf[11] = ' ';  /* after "hello world" */
    buf[23] = ' ';  /* after "HELLO WORLD" */

    is(buf, "hello world HELLO WORLD <end>",
        "%s: saw \"hello world HELLO WORLD <end>\"", __FILE__);

    /* Try seeking and reading at various positions */
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
