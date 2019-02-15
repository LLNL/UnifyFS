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
void addr_publish_server(const char* addr);

/* publish the address of the client */
void addr_publish_client(const char* addr, int local_rank_idx, int app_id);

/* lookup address of server */
char* addr_lookup_server(void);

/* lookup address of client */
char* addr_lookup_client(int app_id, int client_id);

#endif // UNIFYCR_UTIL_H

