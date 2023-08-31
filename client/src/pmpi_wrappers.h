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

#ifndef UNIFYFS_PMPI_WRAPPERS_H
#define UNIFYFS_PMPI_WRAPPERS_H

#include <mpi.h>

/* MPI_Init PMPI wrapper */
int unifyfs_mpi_init(int* argc, char*** argv, int required, int* provided);
int MPI_Init(int* argc, char*** argv);
int MPI_Init_thread(int* argc, char*** argv, int required, int* provided);
void mpi_init_(MPI_Fint* ierr);
void mpi_init_thread_(MPI_Fint* required, MPI_Fint* provided, MPI_Fint* ierr);

/* MPI_Finalize PMPI wrapper */
int unifyfs_mpi_finalize(void);
int MPI_Finalize(void);
void mpi_finalize_(MPI_Fint* ierr);

#endif /* UNIFYFS_PMPI_WRAPPERS_H */
