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

#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/*
 * Test correctness of global file size.  Also, test opening a file for append
 */

int size_test(char* unifyfs_root)
{
    diag("Starting file size and fwrite/fread with append tests");

    char path[64];
    char buf[64] = {0};
    FILE* fp = NULL;
    char* tmp;
    size_t global;
    int fd;

    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Write "hello world" to a file */
    fp = fopen(path, "w");
    ok(fp != NULL, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(errno));
    ok(fwrite("hello world", 12, 1, fp) == 1,
       "%s:%d fwrite(\"hello world\": %s", __FILE__, __LINE__, strerror(errno));
    ok(fclose(fp) == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(errno));

    testutil_get_size(path, &global);
    ok(global == 12, "%s:%d global size after fwrite(\"hello world\") = %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    /* Open the file again with append, write to it. */
    fp = fopen(path, "a");
    ok(fp != NULL, "%s:%d fopen(%s) in append mode: %s",
       __FILE__, __LINE__, path, strerror(errno));
    ok(fwrite("HELLO WORLD", 12, 1, fp) == 1,
       "%s:%d fwrite(\"HELLO WORLD\") with file %s open for append: %s",
       __FILE__, __LINE__, path, strerror(errno));

    ok(ftell(fp) == 24, "%s:%d ftell() after appending to file: %s",
       __FILE__, __LINE__, strerror(errno));

    /*
     * Set our position to somewhere in the middle of the file.  Since the file
     * is in append mode, this new position should be ignored, and writes
     * should still go to the end of the file.
     */
    ok(fseek(fp, 11, SEEK_SET) == 0, "%s:%d fseek(11) before append: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(fwrite("<end>", 6, 1, fp) == 1,
       "%s:%d fwrite(\"<end>\") to append after seek to middle of file: %s",
       __FILE__, __LINE__, strerror(errno));

    ok(ftell(fp) == 30, "%s:%d ftell() after seek and appending to file: %s",
       __FILE__, __LINE__, strerror(errno));

    ok(fclose(fp) == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(errno));

    testutil_get_size(path, &global);
    ok(global == 30, "%s:%d global size after append is %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    /* Sync extents */
    fd = open(path, O_RDWR);
    ok(fd >= 0, "%s:%d open file for fsync: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(fsync(fd) == 0, "%s:%d fsync(): %s",
       __FILE__, __LINE__, strerror(errno));
    ok(close(fd) != -1, "%s:%d close after fsync: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Laminate */
    ok(chmod(path, 0444) == 0, "%s:%d chmod(0444): %s",
       __FILE__, __LINE__, strerror(errno));

    /* Global size should be correct */
    testutil_get_size(path, &global);
    ok(global == 30, "%s:%d global size after laminate is %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    /* Read it back */
    fp = fopen(path, "r");
    ok(fp != NULL, "%s%d: fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(errno));

    memset(buf, 0, sizeof(buf));
    ok(fread(buf, 30, 1, fp) == 1, "%s:%d fread() buf[]=\"%s\", : %s",
       __FILE__, __LINE__, buf, strerror(errno));

     /*
      * We wrote three strings to the file: "hello world" "HELLO WORLD" and
      * "<END>".  Replace the '\0' after the first two strings with spaces
      * so we can compare the file contents as one big string.
      */
    buf[11] = ' ';  /* after "hello world" */
    buf[23] = ' ';  /* after "HELLO WORLD" */

    is(buf, "hello world HELLO WORLD <end>",
        "%s:%d saw \"hello world HELLO WORLD <end>\"", __FILE__, __LINE__);

    /* Try seeking and reading at various positions */
    ok(fseek(fp, 6, SEEK_SET) == 0, "%s:%d fseek(6): %s",
       __FILE__, __LINE__, strerror(errno));

    ok(fread(buf, 6, 1, fp) == 1, "%s:%d fread() at offset 6 buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "world", "%s:%d saw \"world\"", __FILE__, __LINE__);

    rewind(fp);
    ok(fread(buf, 12, 1, fp) == 1,
       "%s:%d fread() after rewind() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "hello world", "%s:%d saw \"hello world\"", __FILE__, __LINE__);

    rewind(fp);
    memset(buf, 0, sizeof(buf));
    tmp = fgets(buf, 12, fp);
    ok(tmp == buf, "%s:%d fgets() after rewind() buf[]=\"%s\": %s",
       __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "hello world", "%s:%d saw \"hello world\"", __FILE__, __LINE__);

    rewind(fp);
    memset(buf, 0, sizeof(buf));
    tmp = fgets(buf, 6, fp);
    ok(tmp == buf, "%s:%d fgets() w/ size = 6 after rewind() buf[]=\"%s\": %s",
        __FILE__, __LINE__, buf, strerror(errno));
    is(buf, "hello", "%s:%d saw \"hello\"", __FILE__, __LINE__);

    rewind(fp);
    ok(fread(buf, sizeof(buf), 1, fp) != 1,
       "%s:%d fread() past EOF: %s", __FILE__, __LINE__, strerror(errno));

    ok(feof(fp) != 0, "%s:%d feof() past EOF: %s",
       __FILE__, __LINE__, strerror(errno));

    ok(fclose(fp) == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(errno));

    diag("Finished file size and fwrite/fread with append tests");

    return 0;
}

int truncate_on_open(char* unifyfs_root)
{
    diag("Starting truncate on fopen tests");

    char path[64];
    FILE* fp = NULL;
    size_t global;

    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* Write "hello world" to a file */
    fp = fopen(path, "w");
    ok(fp != NULL, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(errno));
    ok(fwrite("hello world", 12, 1, fp) == 1,
       "%s:%d fwrite(\"hello world\": %s", __FILE__, __LINE__, strerror(errno));
    ok(fclose(fp) == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(errno));

    testutil_get_size(path, &global);
    ok(global == 12, "%s:%d global size after fwrite(\"hello world\") = %d: %s",
       __FILE__, __LINE__, global, strerror(errno));

    /* Opening an existing file for writing with fopen should truncate file */
    fp = fopen(path, "w");
    ok(fp != NULL, "%s:%d fopen(%s): %s",
       __FILE__, __LINE__, path, strerror(errno));
    ok(fclose(fp) == 0, "%s:%d fclose(): %s",
       __FILE__, __LINE__, strerror(errno));

    testutil_get_size(path, &global);
    ok(global == 0, "%s:%d global size after fopen(%s, \"w\") = %d: %s",
       __FILE__, __LINE__, path, global, strerror(errno));

    diag("Finished truncate on fopen tests");

    return 0;
}
