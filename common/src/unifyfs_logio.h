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

#ifndef UNIFYFS_LOGIO_H
#define UNIFYFS_LOGIO_H

#include <sys/types.h>

#include "unifyfs_configurator.h"
#include "unifyfs_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* convenience method to return system page size */
size_t get_page_size(void);

/* log-based I/O context structure */
typedef struct logio_context {
    shm_context* shmem;   /* shmem region for memory storage */
    void*  spill_hdr;     /* mmap() address for spillover file log header */
    char*  spill_file;    /* pathname of spillover file */
    size_t spill_sz;      /* size of spillover file */
    int    spill_fd;      /* spillover file descriptor */
} logio_context;

/**
 * Initialize logio context for server.
 *
 * @param app_id application id
 * @param client_id client id
 * @param mem_size shared memory region size for storing data
 * @param spill_size spillfile size for storing data
 * @param spill_dir path to spillfile parent directory
 * @param[out] pctx address of logio context pointer, set to new context
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_init(const int app_id,
                       const int client_id,
                       const size_t mem_size,
                       const size_t spill_size,
                       const char* spill_dir,
                       logio_context** pctx);

/**
 * Initialize logio context for client.
 *
 * @param app_id application id
 * @param client_id client id
 * @param client_cfg pointer to client configuration
 * @param[out] ctx address of logio context pointer, set to new context
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_init_client(const int app_id,
                              const int client_id,
                              const unifyfs_cfg_t* client_cfg,
                              logio_context** ctx);

/**
 * Close logio context.
 *
 * @param ctx            pointer to logio context
 * @param clean_storage  set to non-zero to have server remove data storage
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_close(logio_context* ctx,
                        int clean_storage);

/**
 * Allocate write space from logio context.
 *
 * @param ctx pointer to logio context
 * @param nbytes size of allocation in bytes
 * @param[out] log_offset set to log offset to use for writing
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_alloc(logio_context* ctx,
                        const size_t nbytes,
                        off_t* log_offset);

/**
 * Release previously allocated write space from logio context.
 *
 * @param ctx pointer to logio context
 * @param log_offset log offset of allocation to release
 * @param nbytes size of allocation in bytes
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_free(logio_context* ctx,
                       const off_t log_offset,
                       const size_t nbytes);

/**
 * Read data from logio context at given log offset.
 *
 * @param ctx pointer to logio context
 * @param log_offset log offset to read from
 * @param nbytes number of bytes to read
 * @param buf destination data buffer
 * @param[out] obytes set to number of bytes actually read
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_read(logio_context* ctx,
                       const off_t log_offset,
                       const size_t nbytes,
                       char* buf,
                       size_t* obytes);

/**
 * Write data to logio context at given log offset.
 *
 * @param ctx pointer to logio context
 * @param log_offset log offset to write to
 * @param nbytes number of bytes to write
 * @param buf source data buffer
 * @param[out] obytes set to number of bytes actually written
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_write(logio_context* ctx,
                        const off_t log_offset,
                        const size_t nbytes,
                        const char* buf,
                        size_t* obytes);

/**
 * Sync any spill data to disk for given logio context.
 *
 * @param ctx pointer to logio context
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_sync(logio_context* ctx);

/**
 * Get the shmem and spill data sizes.
 *
 * @param ctx pointer to logio context
 * @param[out] shmem_sz if non-NULL, set to size of shmem data storage
 * @param[out] spill_sz if non-NULL, set to size of spillover data storage
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_logio_get_sizes(logio_context* ctx,
                            off_t* shmem_sz,
                            off_t* spill_sz);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_LOGIO_H */
