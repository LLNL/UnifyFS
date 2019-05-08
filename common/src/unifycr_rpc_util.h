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

#ifndef UNIFYCR_UTIL_H
#define UNIFYCR_UTIL_H

/* publish the address of the server */
void rpc_publish_local_server_addr(const char* addr);
void rpc_publish_remote_server_addr(const char* addr);

/* lookup address of server */
char* rpc_lookup_local_server_addr(void);
char* rpc_lookup_remote_server_addr(const char* hostname);

/* remove server rpc address file */
void rpc_clean_local_server_addr(void);

#endif // UNIFYCR_UTIL_H
