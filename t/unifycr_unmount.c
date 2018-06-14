#include <stdio.h>
#include <mpi.h>
#include <unifycr.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int main(int argc, char *argv[])
{
    char *unifycr_root;
    int rank_num;
    int rank;
    int rc;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &rank_num);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    plan(NO_PLAN);

    unifycr_root = testutil_get_mount_point();

    /*
     * Verify unifycr_mount succeeds.
     */
    rc = unifycr_mount(unifycr_root, rank, rank_num, 0);
    ok(rc == 0, "unifycr_mount at %s (rc=%d)", unifycr_root, rc);

    rc = unifycr_unmount();
    ok(rc == 0, "unifycr_unmount succeeds (rc=%d)", rc);

    MPI_Finalize();
    done_testing();

    return 0;
}
