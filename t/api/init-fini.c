/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "client_api_suite.h"

int api_initialize_test(char* unifyfs_root,
                        unifyfs_handle* fshdl)
{
    diag("Starting API initialization tests");

    int n_configs = 1;
    unifyfs_cfg_option chk_size = { .opt_name = "logio.chunk_size",
                                    .opt_value = "32768" };

    int rc = unifyfs_initialize(unifyfs_root, &chk_size, n_configs, fshdl);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_initialize() is successful: rc=%d (%s)",
       __FILE__, __LINE__, rc, unifyfs_rc_enum_description(rc));

    diag("Finished API initialization tests");
    return rc;
}

int api_finalize_test(char* unifyfs_root,
                      unifyfs_handle* fshdl)
{
    diag("Starting API finalization tests");

    int rc = unifyfs_finalize(*fshdl);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_finalize() is successful: rc=%d (%s)",
       __FILE__, __LINE__, rc, unifyfs_rc_enum_description(rc));

    diag("Finished API finalization tests");
    return rc;
}
