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
#include <ctype.h>
#include <libgen.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mpi.h>
#include <unifyfs.h>

#include "unifyfs-shell.h"

char* unifyfs_shell_program;

static int appid;
static char* mountpoint = "/unifyfs";

static int rank;
static int total_ranks;

unifyfs_shell_env_t shell_env;

static struct option long_opts[] = {
    { "appid", 1, 0, 'a' },
    { "help", 0, 0, 'h' },
    { "mountpoint", 1, 0, 'm' },
    { 0, 0, 0, 0},
};

static char* short_opts = "a:hm:";

static char* usage_str =
    "Usage: %s [OPTION]...\n"
    "\n"
    "-a, --appid=ID             use appid ID (default: 0)\n"
    "-h, --help                 print this help message\n"
    "-m, --mountpoint=NAME      use mountpoint NAME (default: /unifyfs)\n"
    "\n";

static inline void print_usage(int status)
{
    printf(usage_str, unifyfs_shell_program);
    exit(status);
}

int main(int argc, char** argv)
{
    int ret = 0;
    int ch = 0;
    int optidx;
    char* program_path = NULL;

    program_path = strdup(argv[0]);
    unifyfs_shell_program = basename(program_path);

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'a':
            appid = atoi(optarg);
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        default:
            print_usage(1);
            break;
        }
    }

    if (argc - optind != 0) {
        print_usage(1);
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    ret = unifyfs_mount(mountpoint, rank, total_ranks, appid);
    if (ret) {
        perror("unifyfs_mount failed");
        goto fin;
    }

    shell_env.rank = rank;
    shell_env.total_ranks = total_ranks;
    shell_env.mountpoint = mountpoint;
    sprintf(shell_env.cwd, "%s", mountpoint);

    ret = unifyfs_shell(&shell_env);

    unifyfs_unmount();

fin:
    MPI_Finalize();

    free(program_path);

    return ret;
}

