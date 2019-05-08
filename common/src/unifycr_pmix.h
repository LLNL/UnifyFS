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

// NOTE: this file compiles to empty if PMIx not detected at configure time
#include <config.h>
#ifdef HAVE_PMIX_H

#ifndef UNIFYCR_PMIX_H
#define UNIFYCR_PMIX_H

#include <stddef.h>
#include <pmix.h>

// PMIx keys we use
const char* pmix_key_runstate;           // path to runstate file
const char* pmix_key_unifycrd_socket;    // server domain socket path
const char* pmix_key_unifycrd_margo_shm; // client-server margo address
const char* pmix_key_unifycrd_margo_svr; // server-server margo address

// initialize PMIx
int unifycr_pmix_init(int* orank,
                      size_t* ouniv);

// finalize PMIx
int unifycr_pmix_fini(void);

// publish a key-value pair
int unifycr_pmix_publish(const char* key,
                         const char* val);

// lookup a key-value pair
int unifycr_pmix_lookup(const char* key,
                        int wait,
                        char** oval);

int unifycr_pmix_lookup_remote(const char* host,
                               const char* key,
                               int wait,
                               char** oval);

#endif // UNIFYCR_PMIX_H

#endif // HAVE_PMIX_H

