/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "unifyfs-shell.h"

int unifyfs_shell_get_absolute_path(unifyfs_shell_env_t* env,
                                    const char* path, char* outpath)
{
    int ret = 0;
    size_t pathlen = 0;

    if (!env || !path) {
        return -EINVAL;
    }

    if (strlen(path) > PATH_MAX - 1) {
        return -ENAMETOOLONG;
    }

    if (path[0] == '/') {
        if (0 != strncmp(path, env->mountpoint, strlen(env->mountpoint))) {
            return -EINVAL;
        }

        strcpy(outpath, path);
    } else {
        pathlen = strlen(path) + strlen(env->cwd);
        if (pathlen > PATH_MAX - 1) {
            return -ENAMETOOLONG;
        }

        sprintf(outpath, "%s/%s", env->cwd, path);

        if (outpath[strlen(outpath) - 1] == '/') {
            outpath[strlen(outpath) - 1] = '\0';
        }
    }

    return ret;
}

