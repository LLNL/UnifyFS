/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

/*
 * Test doing lots of writes to many open files and verify the data is written
 * correctly.  This can be used to exercise bugs.
 *
 * Test description:
 * 1. Fill bigbuf[] with repeating A-Z
 * 2. Do a bunch of writes with random offsets and lengths to multiple files,
 *    using bigbuf[] as the data.
 * 3. Laminate the files.
 * 4. Read them back, and verify the portions that did get written match the
 *    data from bigbuf[].
 */

#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>

#include "testutil.h"

#define NUM_FILES 10
#define NUM_WRITES 100

/* This is large enough write size to periodically cross 1MB slice boundaries */
#define MAX_WRITE (1024*1024)
#define SEED 1

char bigbuf[1024*1024*10];
char tmpbuf[1024*1024*10];

void fill_bigbuf(void)
{
    char r;
    int i;

    /* Fill bigbuf[] repeating A-Z chars */
    for (i = 0; i < sizeof(bigbuf); i++) {
        bigbuf[i] = 'A'+ (i % 26);
    }
}

/* Compare a file with the data in bigbuf[] */
int check_file(char* file)
{
    int fd;
    int rc;
    int matched = 0;
    fd = open(file, O_RDONLY);

    memset(tmpbuf, 0, sizeof(tmpbuf));
    rc = read(fd, tmpbuf, sizeof(tmpbuf));
    printf("%s: read %d bytes\n", file, rc);

    for (int i = 0; i < rc; i++) {
        if (tmpbuf[i] == bigbuf[i]) {
            matched++;
        }

        if (tmpbuf[i] != bigbuf[i] && tmpbuf[i] != 0) {
            printf("%s failed at offset %d (tmpbuf['%c'] != bigbuf['%c'])\n",
                file, i, tmpbuf[i], bigbuf[i]);
            printf("Comparing last 10 bytes before/after:\n");
            printf("expected: ");
            for (int j = i - 10; j < i; j++) {
                printf("%c", bigbuf[j] ? bigbuf[j] : ' ');
            }

            printf("|%c|", bigbuf[i]);

            for (int j = i + 1; j < i + 11; j++) {
                printf("%c", bigbuf[j] ? bigbuf[j] : ' ');
            }
            printf("\n");

            printf("got:      ");

            for (int j = i - 10; j < i; j++) {
                printf("%c", tmpbuf[j] ? tmpbuf[j] : ' ');
            }

            printf("|%c|", tmpbuf[i]);

            for (int j = i + 1; j < i + 11; j++) {
                printf("%c", tmpbuf[j] ? tmpbuf[j] : ' ');
            }

            printf("\n");


            return 1;
        }
    }
    if (rc > 0 && matched == 0) {
        printf("%s: No matches with file %s\n", __func__, file);
        return 1;
    }
    return 0;
}

int do_test(test_cfg* cfg)
{
    int rc;
    int fds[NUM_FILES], fd;
    char* file[NUM_FILES];
    char buf[40] = {0};
    int i;
    int rnd;
    int start, count;
    fill_bigbuf();
    srand(SEED);

    /* Create our files */
    for (i = 0; i < NUM_FILES; i++) {
        file[i] = mktemp_cmd(cfg, "/unifyfs");
        fds[i] = open(file[i], O_WRONLY | O_CREAT, 0222);
    }

    /* Write our files */
    for (i = 0; i < NUM_WRITES; i++) {
        /* Randomly pick one of our files to write to */
        rnd = rand() % NUM_FILES;
        fd = fds[rnd];

        /* Pick a random offset and count */
        start = rand() % (sizeof(bigbuf) - MAX_WRITE);

        /* + 1 so we always write at least 1 byte */
        count = (rand() % (MAX_WRITE-1)) + 1;
        lseek(fd, start, SEEK_SET);
        if (write(fd, &bigbuf[start], count) != count) {
            perror("Couldn't write");
            exit(1);
        }
    }

    /* Sync extents of all our files and laminate them */
    for (i = 0; i < NUM_FILES; i++) {
        rc = fsync(fds[i]);
        if (rc != 0) {
            printf("%s %d/%d failed to sync, rc = %d, (errno %d %s)\n",
                    file[i], i+1, NUM_FILES, rc, errno, strerror(errno));
            exit(1);
        }
        close(fds[i]);

        if (cfg->laminate) {
            rc = chmod(file[i], 0444);
            if (rc != 0) {
                printf("%s failed to chmod, rc = %d\n", file[i], rc);
                exit(1);
            }
        }
    }

    /* Verify the writes to the files match the values in bigbuf[] */
    for (i = 0; i < NUM_FILES; i++) {
        if (check_file(file[i]) != 0) {
            printf("file %d/%d failed\n", i+1, NUM_FILES);
            exit(1);    /* Error */
        }

        free(file[i]);
    }
    printf("Passed!\n");
}

int main(int argc, char* argv[])
{
    test_cfg test_config;
    test_cfg* cfg = &test_config;
    int rc;

    rc = test_init(argc, argv, cfg);
    if (rc) {
        test_print(cfg, "ERROR - Test %s initialization failed!", argv[0]);
        fflush(NULL);
        return rc;
    }
    do_test(cfg);

    test_fini(cfg);

    return 0;
}
