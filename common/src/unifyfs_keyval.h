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

#ifndef UNIFYFS_KEYVAL_H
#define UNIFYFS_KEYVAL_H

#include "unifyfs_configurator.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int glb_pmi_rank;
extern int glb_pmi_size;

int unifyfs_pmix_init(void);
int unifyfs_pmi2_init(void);

// keys we use
extern const char* const key_unifyfsd_socket;    // server domain socket path
extern const char* const key_unifyfsd_margo_shm; // client-server margo address
extern const char* const key_unifyfsd_margo_svr; // server-server margo address

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

// block until a particular key-value pair published by all servers
int unifyfs_keyval_fence_remote(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_KEYVAL_H
