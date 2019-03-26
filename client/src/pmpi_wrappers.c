/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#include "pmpi_wrappers.h"
#include "unifycr.h"
#include <mpi.h>
#include <stdio.h>

int unifycr_mpi_init(int* argc, char*** argv)
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

    rc = unifycr_mount("/unifycr", rank, (size_t)world_sz, app_id);
    if (UNIFYCR_SUCCESS != rc) {
        fprintf(stderr, "UNIFYCR ERROR: unifycr_mount() failed with '%s'\n",
                unifycr_error_enum_description((unifycr_error_e)rc));
    }

    return ret;
}

int MPI_Init(int* argc, char*** argv)
{
    return unifycr_mpi_init(argc, argv);
}

void mpi_init_(MPI_Fint* ierr)
{
    int argc = 0;
    char** argv = NULL;
    int rc = unifycr_mpi_init(&argc, &argv);

    if (NULL != ierr) {
        *ierr = (MPI_Fint)rc;
    }
}

int unifycr_mpi_finalize(void)
{
    int rc, ret;

    rc = unifycr_unmount();
    if (UNIFYCR_SUCCESS != rc) {
        fprintf(stderr, "UNIFYCR ERROR: unifycr_unmount() failed with '%s'\n",
                unifycr_error_enum_description((unifycr_error_e)rc));
    }

    //fprintf(stderr, "DEBUG: %s - before PMPI_Finalize()\n", __func__);

    ret = PMPI_Finalize();

    //fprintf(stderr, "DEBUG: %s - after PMPI_Finalize(), ret=%d\n",
    //        __func__, ret);

    return ret;
}

int MPI_Finalize(void)
{
    return unifycr_mpi_finalize();
}

void mpi_finalize_(MPI_Fint* ierr)
{
    int rc = unifycr_mpi_finalize();

    if (NULL != ierr) {
        *ierr = (MPI_Fint)rc;
    }
}
