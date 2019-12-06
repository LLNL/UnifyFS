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

#include "unifyfs-shell.h"

static int status_main(int argc, char** argv, unifyfs_shell_env_t* e)
{
    fprintf(e->output, "\n## unifyfs is mounted at %s\n", unifyfs_mount_prefix);
    fprintf(e->output, "\n## configuration ##\n");
    unifyfs_config_print(&client_cfg, e->output);

    return 0;
}

unifyfs_shell_cmd_t unifyfs_shell_cmd_status = {
    .name = "status",
    .func = status_main,
};

