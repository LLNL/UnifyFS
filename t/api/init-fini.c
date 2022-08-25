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

#include "api_suite.h"

int api_initialize_test(char* unifyfs_root,
                        unifyfs_handle* fshdl)
{
    diag("Starting API initialization test");

    int n_configs = 1;
    unifyfs_cfg_option chk_size = { .opt_name = "logio.chunk_size",
                                    .opt_value = "32768" };

    int rc = unifyfs_initialize(unifyfs_root, &chk_size, n_configs, fshdl);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_initialize() is successful: rc=%d (%s)",
       __FILE__, __LINE__, rc, unifyfs_rc_enum_description(rc));

    diag("Finished API initialization test");
    return rc;
}

int api_config_test(char* unifyfs_root,
                    unifyfs_handle* fshdl)
{
    diag("Starting API get-configuration test");

    int n_opt;
    unifyfs_cfg_option* options;
    int rc = unifyfs_get_config(*fshdl, &n_opt, &options);
    ok(rc == UNIFYFS_SUCCESS && NULL != options,
       "%s:%d unifyfs_get_config() is successful: rc=%d (%s)",
       __FILE__, __LINE__, rc, unifyfs_rc_enum_description(rc));

    if (NULL != options) {
        for (int i = 0; i < n_opt; i++) {
            unifyfs_cfg_option* opt = options + i;
            diag("UNIFYFS CONFIG: %s = %s", opt->opt_name, opt->opt_value);
            free((void*)opt->opt_name);
            free((void*)opt->opt_value);
        }
        free(options);
    }

    diag("Finished API get-configuration test");
    return rc;
}

int api_finalize_test(char* unifyfs_root,
                      unifyfs_handle* fshdl)
{
    diag("Starting API finalization test");

    int rc = unifyfs_finalize(*fshdl);
    ok(rc == UNIFYFS_SUCCESS,
       "%s:%d unifyfs_finalize() is successful: rc=%d (%s)",
       __FILE__, __LINE__, rc, unifyfs_rc_enum_description(rc));

    diag("Finished API finalization test");
    return rc;
}
