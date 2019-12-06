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

static int pwd_main(int argc, char** argv, unifyfs_shell_env_t* e)
{
    fprintf(e->output, "%s\n", e->cwd);

    return 0;
}

unifyfs_shell_cmd_t unifyfs_shell_cmd_pwd = {
    .name = "pwd",
    .func = pwd_main,
};

