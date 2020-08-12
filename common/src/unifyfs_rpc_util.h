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

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_UTIL_H

