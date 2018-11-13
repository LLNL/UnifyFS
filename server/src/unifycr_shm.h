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

#ifndef UNIFYCR_SHM_H
#define UNIFYCR_SHM_H

/* TODO: same functions exist in client code, move this to common */

/* allocate and attach a named shared memory region of a particular size
 * and mmap into our memory, returns starting memory address on success,
 * returns NULL on failure */
void *unifycr_shm_alloc(const char *name, size_t size);

/* unmaps shared memory region from memory, and releases it,
 * caller should povider the address of a pointer to the region
 * in paddr, sets paddr to NULL on return,
 * returns UNIFYCR_SUCCESS on success */
int unifycr_shm_free(const char *name, size_t size, void **paddr);

/* release a shared memory region mapping */

#endif // UNIFYCR_SHM_H
