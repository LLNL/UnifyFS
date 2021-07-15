#include <stdio.h>
#include <mpi.h>
#include <unifyfs.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int main(int argc, char* argv[])
{
    char* unifyfs_root;
    int rank_num;
    int rank;
    int rc;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &rank_num);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    plan(NO_PLAN);

    unifyfs_root = testutil_get_mount_point();

    /*
     * Verify unifyfs_mount succeeds.
     */
    rc = unifyfs_mount(unifyfs_root, rank, rank_num);
    ok(rc == 0, "unifyfs_mount at %s (rc=%d)", unifyfs_root, rc);

    rc = unifyfs_unmount();
    ok(rc == 0, "unifyfs_unmount succeeds (rc=%d)", rc);

    MPI_Finalize();

    done_testing();

    return 0;
}
