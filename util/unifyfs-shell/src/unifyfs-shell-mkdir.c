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

static unsigned long int mode = 0755;
static char path[PATH_MAX];

static unifyfs_shell_env_t* env;

static struct option long_opts[] = {
    { "mode", 1, 0, 'm' },
    { "help", 0, 0, 'h' },
    { 0, 0, 0, 0},
};

static char* short_opts = "m:h";

static const char* usage_str =
    "Usage: mkdir [OPTION]... DIRECTORY\n"
    "Create the DIRECTORY, if they do not already exist.\n"
    "\n"
    "Available options:\n"
    "  -m, --mode=MODE   set file mode (as in chmod), not a=rwx - umask\n"
    "  -h, --help        display this help and exit\n"
    "\n";

static inline void print_usage(void)
{
    fputs(usage_str, env->output);
}

/*
 * TODO:
 * - check the intermediate parent directories
 * - support of parent (-p) option
 */
static int mkdir_main(int argc, char** argv, unifyfs_shell_env_t* e)
{
    int ret = 0;
    int ch = 0;
    int optidx = 0;

    env = e;

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'm':
            mode = strtoul(optarg, NULL, 8);
            break;

        case 'h':
        default:
            print_usage();
            goto fin;
            break;
        }
    }

    if (argc - optind != 1) {
        print_usage();
        goto fin;
    }

    ret = unifyfs_shell_get_absolute_path(e, argv[optind], path);
    if (ret < 0) {
        goto fin;
    }

    ret = mkdir(path, mode);
    if (ret < 0) {
        fprintf(e->output, "mkdir failed: %s\n", strerror(errno));
    }

fin:
    return ret;
}

unifyfs_shell_cmd_t unifyfs_shell_cmd_mkdir = {
    .name = "mkdir",
    .func = mkdir_main,
};

