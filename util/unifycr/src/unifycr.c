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
#include <string.h>
#include <getopt.h>

#include "unifycr.h"

static char *program = "unifycr";

/**
 * @brief available actions.
 */
static char *actions[] = { "start", "terminate" };

enum {
    OP_START        = 0,
    OP_TERMINATE,
};

static int action = -1;
static unifycr_args_t opts_cmd;
static unifycr_env_t opts_env;
static unifycr_sysconf_t opts_sysconf;
static unifycr_resource_t resource;

static struct option const long_opts[] = {
    { "cleanup", 0, 0, 'c' },
    { "consistency", 1, 0, 'C' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "transfer-in", 1, 0, 'i' },
    { "transfer-out", 1, 0, 'o' },
    { 0, 0, 0, 0 },
};

static char *short_opts = "cC:hm:i:o:";

static void usage(int status)
{
    /*
    if (status) {
        fprintf(stderr, "`%s --help` for more information.\n", program);
        exit(status);
    }
    */

    printf("\nUsage: %s [command] [options...]\n", program);
    exit(status);
}

static void parse_cmd_arguments(int argc, char **argv)
{
    int ch = 0;
    int optidx = 2;
    int cleanup = 0;
    unifycr_cm_t consistency = UNIFYCR_CM_INVALID;
    char *mountpoint = NULL;
    char *transfer_in = NULL;
    char *transfer_out = NULL;

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0)
    {
        switch (ch) {
        case 'c':
            cleanup = 1;
            break;

        case 'C':
            consistency = unifycr_read_consistency(optarg);
            if (consistency < 0)
                usage(1);
            break;

        case 'm':
            mountpoint = strdup(optarg);
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

    opts_cmd.cleanup = cleanup;
    opts_cmd.consistency = consistency;
    opts_cmd.mountpoint = mountpoint;
    opts_cmd.transfer_in = transfer_in;
    opts_cmd.transfer_out = transfer_out;

#if 1
    printf("cleanup: %d\n", opts_cmd.cleanup);
    printf("consistency: %d\n", opts_cmd.consistency);
    printf("mountpoint: %s\n", opts_cmd.mountpoint);
    printf("transfer_in: %s\n", opts_cmd.transfer_in);
    printf("transfer_out: %s\n", opts_cmd.transfer_out);
#endif
}

int main(int argc, char **argv)
{
    int i = 0;
    int ret = 0;
    char *cmd = NULL;

    if (argc < 2)
        usage(1);

    cmd = argv[1];

    for (i = 0; i < sizeof(actions)/sizeof(char *); i++) {
        if (0 == strncmp(cmd, actions[i], strlen(actions[i])))
            action = i;
    }

    if (action < 0) {
        fprintf(stderr, "invalid command: %s\n", cmd);
        usage(1);
    }

    parse_cmd_arguments(argc, argv);

    ret = unifycr_read_resource(&resource);
    if (ret) {
    }

    ret = unifycr_read_env(&opts_env);
    if (ret) {
        fprintf(stderr, "failed to environment variables\n");
        goto out;
    }

    ret = unifycr_read_sysconf(&opts_sysconf);
    if (ret) {
        fprintf(stderr, "failed to read config file\n");
        goto out;
    }

    ret = unifycr_read_resource(&resource);
    if (ret) {
        fprintf(stderr, "failed to read config file\n");
        goto out;
    }

#if 1
    for (i = 0; i < resource.n_nodes; i++) {
        printf("%s\n", resource.nodes[i]);
    }
#endif

    ret = unifycr_write_runstate(&resource, &opts_sysconf, &opts_env,
                                 &opts_cmd);
    if (ret) {
    }

#if 1
    printf("%s\n", CONFDIR);
    printf("%s\n", BINDIR);
    printf("%s\n", SBINDIR);
    printf("%s\n", RUNDIR);
#endif

out:
    return ret;
}

