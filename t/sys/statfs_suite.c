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

#include <string.h>
#include <mpi.h>
#include <unifyfs.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

#include "statfs_suite.h"

/* The test suite for statfs wrappers found in client/src/unifyfs-sysio.c.
 *
 * This is specifically designed to test stafs when client super magic has
 * been disabled, so that statfs returns TMPFS_MAGIC. */
int main(int argc, char* argv[])
{
    int rank_num;
    int rank;
    char* unifyfs_root;
    int rc;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &rank_num);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    plan(NO_PLAN);

    unifyfs_root = testutil_get_mount_point();

    /* Verify unifyfs_mount succeeds. */
    rc = unifyfs_mount(unifyfs_root, rank, rank_num);
    ok(rc == 0, "unifyfs_mount(%s) (rc=%d)", unifyfs_root, rc);

    /* If the mount fails, bailout, as there is no point in running the tests */
    if (rc != 0) {
        BAIL_OUT("unifyfs_mount in statfs_suite failed");
    }

    /* check that statfs returns TMPFS_MAGIC
     * when UNIFYFS_CLIENT_SUPER_MAGIC=0 */
    statfs_test(unifyfs_root, 0);

    rc = unifyfs_unmount();
    ok(rc == 0, "unifyfs_unmount(%s) (rc=%d)", unifyfs_root, rc);

    MPI_Finalize();

    done_testing();

    return 0;
}
