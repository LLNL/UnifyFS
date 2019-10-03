/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef UNIFYFS_KEYVAL_H
#define UNIFYFS_KEYVAL_H

#include "unifyfs_configurator.h"

#ifdef __cplusplus
extern "C" {
#endif

// keys we use
const char* key_runstate;           // path to runstate file
const char* key_unifyfsd_socket;    // server domain socket path
const char* key_unifyfsd_margo_shm; // client-server margo address
const char* key_unifyfsd_margo_svr; // server-server margo address
const char* key_unifyfsd_mpi_rank;  // server-server MPI rank

// initialize key-value store
int unifyfs_keyval_init(unifyfs_cfg_t* cfg,
                        int* rank,
                        int* nranks);

// finalize key-value store
int unifyfs_keyval_fini(void);

// publish a key-value pair with local visibility
int unifyfs_keyval_publish_local(const char* key,
                                 const char* val);

// publish a key-value pair with remote visibility
int unifyfs_keyval_publish_remote(const char* key,
                                  const char* val);

// lookup a local key-value pair
int unifyfs_keyval_lookup_local(const char* key,
                                char** oval);

// lookup a remote key-value pair
int unifyfs_keyval_lookup_remote(int rank,
                                 const char* key,
                                 char** oval);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_KEYVAL_H
