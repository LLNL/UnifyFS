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

#include <string.h>
#include <mpi.h>
#include <unifycr.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

#include "stdio_suite.h"

/* The test suite for stdio wrappers found in client/src/unifycr-stdio.c.
 *
 *
 * To add new tests to existing stdio tests:
 * 1. Simply add the tests (order matters) to the appropriate
 *    <stdio_function_name>.c file.
 *
 *
 * When a new wrapper in unifycr-stdio.c needs to be tested:
 * 1. Create a <stdio_function_name>.c file with a function called
 *    <stdio_function_name>_test(char* unifycr_root) that contains all the TAP
 *    tests specific to that wrapper.
 * 2. Add the <stdio_function_name>_test to stdio_suite.h.
 * 3. Add the <stdio_function_name>.c file to the /t/Makefile.am under the
 *    appropriate test suite at the bottom.
 * 4. The <stdio_function_name>_test function can now be called from this test
 *    suite. */
int main(int argc, char* argv[])
{
    int rank_num;
    int rank;
    char* unifycr_root;
    int rc;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &rank_num);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    plan(NO_PLAN);

    unifycr_root = testutil_get_mount_point();

    /* Verify unifycr_mount succeeds. */
    rc = unifycr_mount(unifycr_root, rank, rank_num, 0);
    ok(rc == 0, "unifycr_mount at %s (rc=%d)", unifycr_root, rc);

    if (rc != 0) {
        BAIL_OUT("unifycr_mount in stdio_suite failed");
    }

    /* Add tests for new functions below in the order desired for testing.
     *
     * *** NOTE ***
     * The order of tests does matter as some subsequent tests use functions
     * that were already tested. Thus if an earlier test fails, it could cause
     * later tests to fail as well. If this occurs, fix the bugs causing the
     * tests that ran first to break as that is likely to cause subsequent
     * failures to start passing. */

    fopen_fclose_test(unifycr_root);

    done_testing();

    MPI_Finalize();

    return 0;
}
