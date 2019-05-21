/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#ifndef UNIFYCR_KEYVAL_H
#define UNIFYCR_KEYVAL_H

#include "unifycr_configurator.h"

// keys we use
const char* key_runstate;           // path to runstate file
const char* key_unifycrd_socket;    // server domain socket path
const char* key_unifycrd_margo_shm; // client-server margo address
const char* key_unifycrd_margo_svr; // server-server margo address

// initialize key-value store
int unifycr_keyval_init(unifycr_cfg_t* cfg,
                        int* rank,
                        int* nranks);

// finalize key-value store
int unifycr_keyval_fini(void);

// publish a key-value pair with local visibility
int unifycr_keyval_publish_local(const char* key,
                                 const char* val);

// publish a key-value pair with remote visibility
int unifycr_keyval_publish_remote(const char* key,
                                  const char* val);

// lookup a local key-value pair
int unifycr_keyval_lookup_local(const char* key,
                                char** oval);

// lookup a remote key-value pair
int unifycr_keyval_lookup_remote(int rank,
                                 const char* key,
                                 char** oval);

#endif // UNIFYCR_KEYVAL_H
