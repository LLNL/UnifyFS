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

#ifndef UNIFYCR_PMPI_WRAPPERS_H
#define UNIFYCR_PMPI_WRAPPERS_H

/* MPI_Init PMPI wrapper */
int unifycr_mpi_init(int* argc, char*** argv);
int MPI_Init(int* argc, char*** argv);
void mpi_init_(MPI_Fint* ierr);

/* MPI_Finalize PMPI wrapper */
int unifycr_mpi_finalize(void);
int MPI_Finalize(void);
void mpi_finalize_(MPI_Fint* ierr);

#endif /* UNIFYCR_PMPI_WRAPPERS_H */
