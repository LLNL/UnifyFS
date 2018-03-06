/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "testutil.h"

static int seed;

/*
 * Seed the pseudo random number generator if it hasn't already been
 * seeded. Call this before calling rand() if you want a unique
 * pseudo random sequence.
 */
static void test_util_srand(void)
{
    if (seed == 0) {
        seed = time(NULL);
        srand(seed);
    }
}

/*
 * Store a random string of length @len into buffer @buf.
 */
void testutil_rand_string(char *buf, size_t len)
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
void testutil_rand_path(char *buf, size_t len, const char *pfx)
{
    int rc;

    rc = snprintf(buf, len, "%s/", pfx);
    testutil_rand_string(buf + rc, len - rc);
}

/*
 * Return a pointer to the path name of the UnifyCR mount point. Use the
 * value of the environment variable UNIFYCR_MOUNT_POINT if it exists,
 * otherwise use P_tmpdir which is defined in stdio.h and is typically
 * /tmp.
 */
char *testutil_get_mount_point(void)
{
    char *path;
    char *env = getenv("UNIFYCR_MOUNT_POINT");

    if (env != NULL)
        path = env;
    else
        path = P_tmpdir;

    return path;
}
