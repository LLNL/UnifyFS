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

#ifndef UNIFYFS_SHM_H
#define UNIFYFS_SHM_H

#include <pthread.h>
#include <stddef.h>

#include "unifyfs_rc.h"

/* Set default region name len if not already defined */
#ifndef SHMEM_NAME_LEN
#define SHMEM_NAME_LEN 64
#endif

/* printf() format strings used by both client and server to name shared
 * memory regions. First %d is application id, second is client id. */
#define SHMEM_DATA_FMTSTR  "%d-data-%d"
#define SHMEM_SUPER_FMTSTR "%d-super-%d"

#ifdef __cplusplus
extern "C" {
#endif

/* Header for read-request reply in client shared memory region.
 * The associated data payload immediately follows the header in
 * the shmem region.
 *   offset  - offset within file
 *   length  - data size
 *   gfid    - global file id
 *   errcode - read error code (zero on success) */
typedef struct shm_data_meta {
    size_t offset;
    size_t length;
    int gfid;
    int errcode;
} shm_data_meta;

/* State values for client shared memory region */
typedef enum {
    SHMEM_REGION_EMPTY = 0,        // set by client to indicate drain complete
    SHMEM_REGION_DATA_READY = 1,   // set by server to initiate client drain
    SHMEM_REGION_DATA_COMPLETE = 2 // set by server when done writing data
} shm_data_state_e;

/* Header for client shared memory region.
 *   sync     - for synchronizing updates/access by server threads
 *   meta_cnt - number of shm_data_meta (i.e., read replies) currently in shmem
 *   bytes    - total bytes of shmem region in use (shm_data_meta + payloads)
 *   state    - region state variable used for client-server coordination */
 typedef struct shm_data_header {
    pthread_mutex_t sync;
    volatile size_t meta_cnt;
    volatile size_t bytes;
    volatile shm_data_state_e state;
} shm_data_header;

/* Context structure for maintaining state on an active
 * shared memory region */
typedef struct shm_context {
    char   name[SHMEM_NAME_LEN];
    void*  addr;  /* base address of shmem region mapping */
    size_t size;  /* size of shmem region */
} shm_context;

/**
 * Allocate a shared memory region with given name and size,
 * and map it into memory.
 * @param name region name
 * @param size region size in bytes
 * @return shmem context pointer (NULL on failure)
 */
shm_context* unifyfs_shm_alloc(const char* name, size_t size);

/**
 * Unmaps shared memory region and frees its context. Context pointer
 * is set to NULL on success.
 * @param pctx address of pointer to the shmem context
 * @return UNIFYFS_SUCCESS or error code
 */
int unifyfs_shm_free(shm_context** pctx);

/**
 * Unlinks the file used to attach to shared memory region.
 * @param ctx shmem context pointer
 * @return UNIFYFS_SUCCESS or error code
 */
int unifyfs_shm_unlink(shm_context* ctx);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_SHM_H
