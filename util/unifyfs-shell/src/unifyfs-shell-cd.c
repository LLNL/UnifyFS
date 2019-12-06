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

static unifyfs_shell_env_t* env;

static char path[PATH_MAX];

static inline int cd_root(void)
{
    sprintf(env->cwd, "%s", env->mountpoint);

    return 0;
}

static inline int cd_parent(void)
{
    char* pos = strrchr(env->cwd, '/');

    if (pos != env->cwd) {
        pos[0] = '\0';
    }

    return 0;
}

static int cd_dir(const char* str)
{
    int ret = 0;
    struct stat sb = { 0, };

    if (strncmp("..", str, strlen("..")) == 0) {
        return cd_parent();
    }

    ret = unifyfs_shell_get_absolute_path(env, str, path);
    if (ret < 0) {
        fprintf(env->output, "invalid path %s\n", str);
        goto fin;
    }

    ret = stat(path, &sb);
    if (ret < 0) {
        fprintf(env->output, "invalid path %s\n", str);
        goto fin;
    }

    if (!S_ISDIR(sb.st_mode)) {
        fprintf(env->output, "%s is not a directory\n", str);
        goto fin;
    }

    sprintf(env->cwd, "%s", path);

fin:
    return ret;
}

static const char* usage_str =
    "Usage: cd DIRECTORY\n"
    "move to the DIRECTORY.\n"
    "\n";

static inline void print_usage(void)
{
    fputs(usage_str, stdout);
}

static int cd_main(int argc, char** argv, unifyfs_shell_env_t* e)
{
    int ret = 0;

    env = e;

    switch (argc) {
    case 1:
        cd_root();
        break;

    case 2:
        cd_dir(argv[1]);
        break;

    default:
        print_usage();
        goto fin;
        break;
    }

fin:
    return ret;
}

unifyfs_shell_cmd_t unifyfs_shell_cmd_cd = {
    .name = "cd",
    .func = cd_main,
};

