/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Hyogi Sim
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 *
 * Copyright (c) 2014, Los Alamos National Laboratory
 *	All rights reserved.
 *
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "unifycr.h"

int unifycr_read_env(unifycr_env_t *env)
{
    char *val = NULL;

    val = getenv("UNIFYCR_MT");
    if (val)
        env->unifycr_mt = strdup(val);

    val = getenv("UNIFYCR_META_DB_NAME");
    if (val)
        env->unifycr_meta_db_name = strdup(val);

    val = getenv("UNIFYCR_META_DB_PATH");
    if (val)
        env->unifycr_meta_db_path = strdup(val);

    val = getenv("UNIFYCR_SERVER_DEBUG_LOG");
    if (val)
        env->unifycr_server_debug_log = strdup(val);

    val = getenv("UNIFYCR_META_SERVER_RATIO");
    if (val)
        env->unifycr_meta_server_ratio = strtoul(val, NULL, 0);

    val = getenv("UNIFYCR_CHUNK_MEM");
    if (val)
        env->unifycr_chunk_mem = strtoul(val, NULL, 0);

    val = getenv("UNIFYCR_EXTERNAL_META_DIR");
    if (val)
        env->unifycr_external_meta_dir = strdup(val);

    val = getenv("UNIFYCR_EXTERNAL_DATA_DIR");
    if (val)
        env->unifycr_external_data_dir = strdup(val);

    return 0;
}

