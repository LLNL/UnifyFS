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

#ifndef UNIFYFS_UTIL_H
#define UNIFYFS_UTIL_H

#include <mercury_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* publish the address of the server */
void rpc_publish_local_server_addr(const char* addr);
void rpc_publish_remote_server_addr(const char* addr);

/* lookup address of server */
char* rpc_lookup_local_server_addr(void);
char* rpc_lookup_remote_server_addr(int srv_rank);

/* remove server rpc address file */
void rpc_clean_local_server_addr(void);

/* use passed bulk handle to pull data into a newly allocated buffer.
 * returns buffer, or NULL on failure. */
void* pull_margo_bulk_buffer(hg_handle_t rpc_hdl,
                             hg_bulk_t bulk_in,
                             hg_size_t bulk_sz,
                             hg_bulk_t* local_bulk);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_UTIL_H

