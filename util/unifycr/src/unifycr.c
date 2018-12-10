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

typedef enum {
    INVALID_ACTION   = -1,
    ACT_START        = 0,
    ACT_TERMINATE    = 1,
    N_ACT            = 2
} action_e;

static char* actions[N_ACT] = { "start", "terminate" };

static action_e action = INVALID_ACTION;
static unifycr_args_t cli_args;
static unifycr_resource_t resource;

static struct option const long_opts[] = {
    { "cleanup", no_argument, NULL, 'c' },
    { "consistency", required_argument, NULL, 'C' },
    { "debug", no_argument, NULL, 'd' },
    { "exe", required_argument, NULL, 'e' },
    { "help", no_argument, NULL, 'h' },
    { "mount", required_argument, NULL, 'm' },
    { "script", required_argument, NULL, 's' },
    { "stage-in", required_argument, NULL, 'i' },
    { "stage-out", required_argument, NULL, 'o' },
    { 0, 0, 0, 0 },
};

static char* program;
static char* short_opts = ":cC:de:hi:m:o:s:";
static char* usage_str =
    "\n"
    "Usage: %s <command> [options...]\n"
    "\n"
    "<command> should be one of the following:\n"
    "  start       start the UnifyCR server daemons\n"
    "  terminate   terminate the UnifyCR server daemons\n"
    "\n"
    "Common options:\n"
    "  -d, --debug               enable debug output\n"
    "  -h, --help                print usage\n"
    "\n"
    "Command options for \"start\":\n"
    "  -C, --consistency=<model> consistency model (NONE | LAMINATED | POSIX)\n"
    "  -e, --exe=<path>          <path> where unifycrd is installed\n"
    "  -m, --mount=<path>        mount UnifyCR at <path>\n"
    "  -s, --script=<path>       <path> to custom launch script\n"
    "  -c, --cleanup             (NOT YET SUPPORTED) clean up the UnifyCR storage upon server exit\n"
    "  -i, --stage-in=<path>     (NOT YET SUPPORTED) stage in file(s) at <path>\n"
    "  -o, --stage-out=<path>    (NOT YET SUPPORTED) stage out file(s) to <path> on termination\n"
    "\n"
    "Command options for \"terminate\":\n"
    "  -s, --script=<path>       <path> to custom termination script\n"
    "\n";

static int debug;

static void usage(int status)
{
    printf(usage_str, program);
    exit(status);
}

static void parse_cmd_arguments(int argc, char** argv)
{
    int ch = 0;
    int optidx = 2;
    int cleanup = 0;
    unifycr_cm_e consistency = UNIFYCR_CM_LAMINATED;
    char* mountpoint = NULL;
    char* script = NULL;
    char* srvr_exe = NULL;
    char* stage_in = NULL;
    char* stage_out = NULL;

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            printf("WARNING: cleanup not yet supported!\n");
            cleanup = 1;
            break;

        case 'C':
            consistency = unifycr_cm_enum_from_str(optarg);
            if (consistency == UNIFYCR_CM_INVALID) {
                usage(1);
            }
            break;

        case 'd':
            debug = 5;
            break;

        case 'e':
            srvr_exe = strdup(optarg);
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 's':
            script = strdup(optarg);
            break;

        case 'i':
            printf("WARNING: stage-in not yet supported!\n");
            stage_in = strdup(optarg);
            break;

        case 'o':
            printf("WARNING: stage-out not yet supported!\n");
            stage_out = strdup(optarg);
            break;

        case 'h':
        default:
            usage(0);
            break;
        }
    }

    cli_args.debug = debug;
    cli_args.cleanup = cleanup;
    cli_args.consistency = consistency;
    cli_args.script = script;
    cli_args.mountpoint = mountpoint;
    cli_args.server_path = srvr_exe;
    cli_args.stage_in = stage_in;
    cli_args.stage_out = stage_out;
}

int main(int argc, char** argv)
{
    int i = 0;
    int ret = 0;
    char* cmd = NULL;

    program = strdup(argv[0]);
    program = basename(program);

    if (argc < 2) {
        usage(1);
    }

    cmd = argv[1];

    for (i = 0; i < N_ACT; i++) {
        if (strcmp(cmd, actions[i]) == 0) {
            action = (action_e)i;
            break;
        }
    }

    if (action == INVALID_ACTION) {
        usage(1);
    }

    parse_cmd_arguments(argc, argv);

    if (debug) {
        printf("\n## options from the command line ##\n");
        printf("cleanup:\t%d\n", cli_args.cleanup);
        printf("consistency:\t%s\n",
               unifycr_cm_enum_str(cli_args.consistency));
        printf("mountpoint:\t%s\n", cli_args.mountpoint);
        printf("script:\t%s\n", cli_args.script);
        printf("server:\t%s\n", cli_args.server_path);
        printf("stage_in:\t%s\n", cli_args.stage_in);
        printf("stage_out:\t%s\n", cli_args.stage_out);
    }

    ret = unifycr_detect_resources(&resource);
    if (ret) {
        fprintf(stderr, "ERROR: no supported resource manager detected\n");
        return ret;
    }

    if (debug) {
        printf("\n## job allocation (%zu nodes) ##\n",
               resource.n_nodes);
        for (i = 0; i < resource.n_nodes; i++) {
            printf("%s\n", resource.nodes[i]);
        }
    }
    fflush(stdout);

    if (action == ACT_START) {
        return unifycr_start_servers(&resource, &cli_args);
    } else if (action == ACT_TERMINATE) {
        return unifycr_stop_servers(&resource, &cli_args);
    } else {
        fprintf(stderr, "INTERNAL ERROR: unhandled action %d\n", (int)action);
        return -1;
    }
}
