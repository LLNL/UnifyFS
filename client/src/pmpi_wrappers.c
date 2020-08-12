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


#include <stdio.h>

#include "pmpi_wrappers.h"
#include "unifyfs.h"
#include "unifyfs_rc.h"

int unifyfs_mpi_init(int* argc, char*** argv)
{
    int rc, ret;
    int rank;
    int world_sz = 0;
    int app_id = 0;

    //fprintf(stderr, "DEBUG: %s - before PMPI_Init()\n", __func__);

    ret = PMPI_Init(argc, argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_sz);

    //fprintf(stderr, "DEBUG: %s - after PMPI_Init(), rank=%d ret=%d\n",
    //        __func__, rank, ret);

    rc = unifyfs_mount("/unifyfs", rank, (size_t)world_sz, app_id);
    if (UNIFYFS_SUCCESS != rc) {
        fprintf(stderr, "UNIFYFS ERROR: unifyfs_mount() failed with '%s'\n",
                unifyfs_rc_enum_description((unifyfs_rc)rc));
    }

    return ret;
}

int MPI_Init(int* argc, char*** argv)
{
    return unifyfs_mpi_init(argc, argv);
}

void mpi_init_(MPI_Fint* ierr)
{
    int argc = 0;
    char** argv = NULL;
    int rc = unifyfs_mpi_init(&argc, &argv);

    if (NULL != ierr) {
        *ierr = (MPI_Fint)rc;
    }
}

int unifyfs_mpi_finalize(void)
{
    int rc, ret;

    rc = unifyfs_unmount();
    if (UNIFYFS_SUCCESS != rc) {
        fprintf(stderr, "UNIFYFS ERROR: unifyfs_unmount() failed with '%s'\n",
                unifyfs_rc_enum_description((unifyfs_rc)rc));
    }

    //fprintf(stderr, "DEBUG: %s - before PMPI_Finalize()\n", __func__);

    ret = PMPI_Finalize();

    //fprintf(stderr, "DEBUG: %s - after PMPI_Finalize(), ret=%d\n",
    //        __func__, ret);

    return ret;
}

int MPI_Finalize(void)
{
    return unifyfs_mpi_finalize();
}

void mpi_finalize_(MPI_Fint* ierr)
{
    int rc = unifyfs_mpi_finalize();

    if (NULL != ierr) {
        *ierr = (MPI_Fint)rc;
    }
}
