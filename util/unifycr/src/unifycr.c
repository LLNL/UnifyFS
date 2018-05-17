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
 *  All rights reserved.
 *
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include <config.h>
#endif

#include <libgen.h> // basename
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "unifycr.h"

/**
 * @brief available actions.
 */

enum {
    ACT_START        = 0,
    ACT_TERMINATE,
    N_ACT,
};

static char *actions[N_ACT] = { "start", "terminate" };

static int action = -1;
static unifycr_args_t cli_args;
static unifycr_resource_t resource;

static struct option const long_opts[] = {
    { "cleanup", no_argument, NULL, 'c' },
    { "consistency", required_argument, NULL, 'C' },
    { "debug", no_argument, NULL, 'd' },
    { "help", no_argument, NULL, 'h' },
    { "mount", required_argument, NULL, 'm' },
    { "server", required_argument, NULL, 's' },
    { "transfer-in", required_argument, NULL, 'i' },
    { "transfer-out", required_argument, NULL, 'o' },
    { 0, 0, 0, 0 },
};

static char *program;
static char *short_opts = ":cC:dhi:m:o:s:";
static char *usage_str =
    "\n"
    "Usage: %s <command> [options...]\n"
    "\n"
    "<command> should be one of the following:\n"
    "  start       start the unifycr server daemon\n"
    "  terminate   terminate the unifycr server daemon\n"
    "\n"
    "Available options for \"start\":\n"
    "  -C, --consistency=<model> consistency model (none, laminated, or posix)\n"
    "  -m, --mount=<path>        mount unifycr at <path>\n"
    "  -s, --server=<path>       <path> where unifycrd is installed\n"
    "  -i, --transfer-in=<path>  stage in file(s) at <path>\n"
    "  -o, --transfer-out=<path> transfer file(s) to <path> on termination\n"
    "\n"
    "Available options for \"terminate\":\n"
    "  -c, --cleanup             clean up the unifycr storage on termination\n"
    "\n";

static int debug;

static void usage(int status)
{
    printf(usage_str, program);
    exit(status);
}

static void parse_cmd_arguments(int argc, char **argv)
{
    int ch = 0;
    int optidx = 2;
    int cleanup = 0;
    unifycr_cm_e consistency = UNIFYCR_CM_INVALID;
    char *mountpoint = NULL;
    char *srvrpath = NULL;
    char *transfer_in = NULL;
    char *transfer_out = NULL;

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            cleanup = 1;
            break;

        case 'C':
            consistency = unifycr_cm_enum_from_str(optarg);
            if (consistency == UNIFYCR_CM_INVALID)
                usage(1);
            break;

        case 'd':
            debug = 1;
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 's':
            srvrpath = strdup(optarg);
            break;

        case 'i':
            transfer_in = strdup(optarg);
            break;

        case 'o':
            transfer_out = strdup(optarg);
            break;

        case 'h':
        default:
            usage(0);
            break;
        }
    }

    cli_args.cleanup = cleanup;
    cli_args.consistency = consistency;
    cli_args.mountpoint = mountpoint;
    cli_args.server_path = srvrpath;
    cli_args.transfer_in = transfer_in;
    cli_args.transfer_out = transfer_out;
}

int main(int argc, char **argv)
{
    int i = 0;
    int ret = 0;
    char *cmd = NULL;

    program = strdup(argv[0]);
    program = basename(program);

    if (argc < 2)
        usage(1);

    cmd = argv[1];

    for (i = 0; i < N_ACT; i++) {
        if (strcmp(cmd, actions[i]) == 0) {
            action = i;
            break;
        }
    }

    if (action < 0)
        usage(1);

    parse_cmd_arguments(argc, argv);

    if (debug) {
        printf("\n## options from the command line ##\n");
        printf("cleanup:\t%d\n", cli_args.cleanup);
        printf("consistency:\t%s\n",
               unifycr_cm_enum_str(cli_args.consistency));
        printf("mountpoint:\t%s\n", cli_args.mountpoint);
        printf("server:\t%s\n", cli_args.server_path);
        printf("transfer_in:\t%s\n", cli_args.transfer_in);
        printf("transfer_out:\t%s\n", cli_args.transfer_out);
    }

    ret = unifycr_read_resource(&resource);
    if (ret) {
        fprintf(stderr, "failed to detect a resource manager\n");
        goto out;
    }

    if (debug) {
        printf("\n## job allocation (%llu nodes) ##\n",
               (unsigned long long) resource.n_nodes);
        for (i = 0; i < resource.n_nodes; i++)
            printf("%s\n", resource.nodes[i]);
    }

    ret = unifycr_launch_daemon(&resource, &cli_args);

out:
    return ret;
}
