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

#include "sysio_suite.h"

/* The test suite for sysio wrappers found in client/src/unifycr-sysio.c.
 *
 *
 * To add new tests to existing sysio tests:
 * 1. Simply add the tests (order matters) to the appropriate
 *    <sysio_function_name>.c file.
 *
 *
 * When a new wrapper in unifycr-sysio.c needs to be tested:
 * 1. Create a <sysio_function_name>.c file with a function called
 *    <sysio_function_name>_test(char* unifycr_root) that contains all the TAP
 *    tests specific to that wrapper.
 * 2. Add the <sysio_function_name>_test to sysio_suite.h.
 * 3. Add the <sysio_function_name>.c file to the /t/Makefile.am under the
 *    appropriate test suite at the bottom.
 * 4. The <sysio_function_name>_test function can now be called from this test
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

    /* If the mount fails, bailout, as there is no point in running the tests */
    if (rc != 0) {
        BAIL_OUT("unifycr_mount in sysio_suite failed");
    }

    /* Add tests for new functions below in the order desired for testing. */

    open_test(unifycr_root);

    done_testing();

    MPI_Finalize();

    return 0;
}
