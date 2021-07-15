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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "testutil.h"

static unsigned long seed;

/*
 * Seed the pseudo random number generator if it hasn't already been
 * seeded. Call this before calling rand() if you want a unique
 * pseudo random sequence.
 *
 * Test suites currently each run off their own main function so that they can
 * be run individually if need be. If they run too fast, seeding srand() with
 * time(NULL) can happen more than once in a second, causing the pseudo random
 * sequence to repeat which causes each suite to create the same random files.
 * Using gettimeofday() allows us to increase the granularity to microseconds.
 */
static void test_util_srand(void)
{
    if (seed == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        /* Convert seconds since Epoch to microseconds and add the microseconds
         * in order to prevent the seed from rolling over and repeating. */
        seed = (tv.tv_sec * 1000000) + tv.tv_usec;
        srand(seed);
    }
}

/*
 * Store a random string of length @len into buffer @buf.
 */
void testutil_rand_string(char* buf, size_t len)
{
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz"
                           "0123456789"
                           ".,_-+@";
    int idx;
    int i;

    test_util_srand();

    for (i = 0; i < len - 1; i++) {
        idx = rand() % (sizeof(charset) - 1);
        buf[i] = charset[idx];
    }
    buf[i] = '\0';
}

/*
 * Generate a path of length @len and store it into buffer @buf. The
 * path will begin with the NUL-terminated string pointed to by @pfx,
 * followed by a slash (/), followed by a random sequence of characters.
 */
void testutil_rand_path(char* buf, size_t len, const char* pfx)
{
    int rc;

    memset(buf, 0, len);
    rc = snprintf(buf, len, "%s/", pfx);
    testutil_rand_string(buf + rc, len - rc);
}

/*
 * Return a pointer to the path name of the test temp directory. Use the
 * value of the environment variable UNIFYFS_TEST_TMPDIR if it exists,
 * otherwise use P_tmpdir (defined in stdio.h, typically '/tmp').
 */
char* testutil_get_tmp_dir(void)
{
    char* path;
    char* val = getenv("UNIFYFS_TEST_TMPDIR");

    if (val != NULL) {
        path = val;
    } else {
        path = P_tmpdir;
    }

    return path;
}

/*
 * Return a pointer to the path name of the UnifyFS mount point. Use the
 * value of the environment variable UNIFYFS_MOUNTPOINT if it exists,
 * otherwise use 'tmpdir/unifyfs'.
 */
char* testutil_get_mount_point(void)
{
    char* path;
    char* val = getenv("UNIFYFS_MOUNTPOINT");

    if (val != NULL) {
        path = val;
    } else {
        char* tmpdir = testutil_get_tmp_dir();
        size_t path_len = strlen(tmpdir) + strlen("/unifyfs") + 1;
        path = malloc(path_len);
        snprintf(path, path_len, "%s/unifyfs", tmpdir);
    }

    return path;
}



/* Stat the file associated to by path and store the global size of the
 * file at path in the address of the global pointer passed in. */
void testutil_get_size(char* path, size_t* global)
{
    struct stat sb = {0};
    int err, rc;

    errno = 0;
    rc = stat(path, &sb);
    if (rc != 0) {
        err = errno;
        printf("Test Error: stat(%s) failed - %s\n", path, strerror(err));
        exit(1);
    }
    if (global) {
        *global = sb.st_size;
    }
}

/*
 * Sequentially number every 8 bytes (uint64_t)
 */
void testutil_lipsum_generate(char* buf, uint64_t len, uint64_t offset)
{
    uint64_t i;
    uint64_t skip = 0;
    uint64_t remain = 0;
    uint64_t start = offset / sizeof(uint64_t);
    uint64_t count = len / sizeof(uint64_t);
    uint64_t* ibuf = (uint64_t*) buf;

    /* check if we have any extra bytes at the front and end */
    if (offset % sizeof(uint64_t)) {
        skip = sizeof(uint64_t) - (offset % sizeof(uint64_t));
        remain = (len - skip) % sizeof(uint64_t);

        ibuf = (uint64_t*) &buf[skip];
        start++;

        if (skip + remain >= sizeof(uint64_t)) {
            count--;
        }
    }

    for (i = 0; i < count; i++) {
        ibuf[i] = start + i;
    }
}

/*
 * Check buffer contains lipsum generated data.
 * returns 0 on success, -1 otherwise with @error_offset set.
 */
int testutil_lipsum_check(const char* buf, uint64_t len, uint64_t offset,
                          uint64_t* error_offset)
{
    uint64_t i, val;
    uint64_t skip = 0;
    uint64_t remain = 0;
    uint64_t start = offset / sizeof(uint64_t);
    uint64_t count = len / sizeof(uint64_t);
    const uint64_t* ibuf = (uint64_t*) buf;

    /* check if we have any extra bytes at the front and end */
    if (offset % sizeof(uint64_t)) {
        skip = sizeof(uint64_t) - (offset % sizeof(uint64_t));
        remain = (len - skip) % sizeof(uint64_t);

        ibuf = (uint64_t*) &buf[skip];
        start++;

        if (skip + remain >= sizeof(uint64_t)) {
            count--;
        }
    }

    for (i = 0; i < count; i++) {
        val = start + i;
        if (ibuf[i] != val) {
            *error_offset = offset + (i * sizeof(uint64_t));
            fprintf(stderr,
                    "LIPSUM CHECK ERROR: [%" PRIu64 "] @ offset %" PRIu64
                    ", expected=%" PRIu64 " found=%" PRIu64 "\n",
                    i, *error_offset, val, ibuf[i]);
            return -1;
        }
    }

    return 0;
}

/*
 * Check buffer contains all zero bytes.
 * returns 0 on success, -1 otherwise.
 */
int testutil_zero_check(const char* buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0) {
            fprintf(stderr,
                    "ZERO CHECK ERROR: byte @ offset %zu is non-zero\n", i);
            return -1;
        }
    }

    return 0;
}
