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

#ifndef UNIFYFS_SHM_H
#define UNIFYFS_SHM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate and attach a named shared memory region of a particular size
 * and mmap into our memory.  Returns starting memory address on success.
 * Returns NULL on failure. */
void* unifyfs_shm_alloc(const char* name, size_t size);

/* Unmaps shared memory region from memory.
 * Caller should povider the address of a pointer to the region
 * in paddr.  Sets paddr to NULL on return.
 * Returns UNIFYFS_SUCCESS on success. */
int unifyfs_shm_free(const char* name, size_t size, void** paddr);

/* Delete file used to attach to shared memory segment.
 * Returns UNIFYFS_SUCCESS on success. */
int unifyfs_shm_unlink(const char* name);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_SHM_H
