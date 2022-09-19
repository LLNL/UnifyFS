/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "api_suite.h"
#include <stdlib.h>

/* This is the collection of library API tests.
 *
 * To add new subtests to existing API functionality tests:
 * 1. Simply add the tests (order matters) to the appropriate
 *    t/api/<function>.c file.
 *
 * When new API functionality needs to be tested:
 * 1. Create a t/api/<function>.c source file with a function called:
 *       api_<function>_test(char *unifyfs_root)
 *    to contain all the TAP tests for that API functionality.
 * 2. Add the function name to t/api/api_suite.h, with comments.
 * 3. In t/Makefile.am, add the new file to the source file list for
 *    the api test suite (api_client_api_test_t_SOURCES).
 * 4. The api_<function>_test function can now be used in this suite. */

int main(int argc, char* argv[])
{
    int rc;
    char* unifyfs_root = testutil_get_mount_point();
    char* tmp_dir = testutil_get_tmp_dir();

    unifyfs_handle fshdl;

    //MPI_Init(&argc, &argv);

    plan(NO_PLAN);

    /* Add tests for new functionality below in the order desired for testing.
     *
     * *** NOTE ***
     * The order of the tests does matter as some subsequent tests use
     * functionality or files that were already tested.
     */

    size_t spill_sz = (size_t)512 * MIB;
    char* spill_size_env = getenv("UNIFYFS_LOGIO_SPILL_SIZE");
    if (NULL != spill_size_env) {
        spill_sz = (size_t) strtoul(spill_size_env, NULL, 0);
    }

    rc = api_initialize_test(unifyfs_root, &fshdl);
    if (rc == UNIFYFS_SUCCESS) {
        api_config_test(unifyfs_root, &fshdl);

        api_create_open_remove_test(unifyfs_root, &fshdl);

        api_write_read_sync_stat_test(unifyfs_root, &fshdl,
                                      (size_t)64 * KIB, (size_t)4 * KIB);
        api_write_read_sync_stat_test(unifyfs_root, &fshdl,
                                      (size_t)1 * MIB, (size_t)32 * KIB);
        api_write_read_sync_stat_test(unifyfs_root, &fshdl,
                                      (size_t)4 * MIB, (size_t)128 * KIB);

        api_get_gfids_and_metadata_test(unifyfs_root, &fshdl,
                                        (size_t)64 * KIB);

        api_laminate_test(unifyfs_root, &fshdl);

        api_storage_test(unifyfs_root, &fshdl,
                         spill_sz, (spill_sz / 8));

        api_transfer_test(unifyfs_root, tmp_dir, &fshdl,
                          (size_t)64 * MIB, (size_t)4 * MIB);

        api_finalize_test(unifyfs_root, &fshdl);
    }

    //MPI_Finalize();

    done_testing();

    return 0;
}
