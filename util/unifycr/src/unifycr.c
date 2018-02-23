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

/**
 * @brief available actions.
 */

enum {
    ACT_START        = 0,
    ACT_TERMINATE,
    N_ACT,
};

static char *actions[N_ACT] = { "start", "terminate" };

static int unifycr_launch_daemon(void)
{
    return 0;
}

static int action = -1;
static unifycr_args_t opts_cmd;
static unifycr_env_t opts_env;
static unifycr_sysconf_t opts_sysconf;
static unifycr_resource_t resource;

static struct option const long_opts[] = {
    { "cleanup", 0, 0, 'c' },
    { "consistency", 1, 0, 'C' },
    { "debug", 0, 0, 'd' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "transfer-in", 1, 0, 'i' },
    { "transfer-out", 1, 0, 'o' },
    { 0, 0, 0, 0 },
};

static const char *program;
static char *short_opts = "cC:dhm:i:o:";
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

static const char *basename(const char *path)
{
    const char *str = strrchr(path, '/');

    if (str)
        return &str[1];
    else
        return path;
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
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            cleanup = 1;
            break;

        case 'C':
            consistency = unifycr_read_consistency(optarg);
            if (consistency < 0)
                usage(1);
            break;

        case 'd':
            debug = 1;
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
}

int main(int argc, char **argv)
{
    int i = 0;
    int ret = 0;
    char *cmd = NULL;

    program = basename(argv[0]);

    if (argc < 2)
        usage(1);

    cmd = argv[1];

    for (i = 0; i < N_ACT; i++) {
        if (!strcmp(cmd, actions[i]))
            action = i;
    }

    if (action < 0)
        usage(1);

    parse_cmd_arguments(argc, argv);

    if (debug) {
        printf("\n## options from the command line ##\n");
        printf("cleanup:\t%d\n", opts_cmd.cleanup);
        printf("consistency:\t%s\n",
               unifycr_write_consistency(opts_cmd.consistency));
        printf("mountpoint:\t%s\n", opts_cmd.mountpoint);
        printf("transfer_in:\t%s\n", opts_cmd.transfer_in);
        printf("transfer_out:\t%s\n", opts_cmd.transfer_out);
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

    ret = unifycr_read_env(&opts_env);
    if (ret) {
        fprintf(stderr, "failed to environment variables\n");
        goto out;
    }

    if (debug) {
        printf("\n## options from the environmental variables ##\n");
        printf("UNIFYCR_MT:\t%s\n", opts_env.unifycr_mt);
    }

    ret = unifycr_read_sysconf(&opts_sysconf);
    if (ret) {
        fprintf(stderr, "failed to read config file\n");
        goto out;
    }

    if (debug) {
        printf("\n## options read from %s ##\n", CONFDIR "/unifycr.conf");
        printf("runstatedir:\t%s\n", opts_sysconf.runstatedir);
        printf("consistency:\t%s\n",
               unifycr_write_consistency(opts_sysconf.consistency));
        printf("mountpoint:\t%s\n", opts_sysconf.mountpoint);
    }

    ret = unifycr_write_runstate(&resource, &opts_sysconf, &opts_env,
                                 &opts_cmd);
    if (ret) {
        fprintf(stderr, "failed to write the runstate file\n");
        goto out;
    }

    if (debug) {
        char linebuf[4096] = { 0, };
        FILE *fp = NULL;

        sprintf(linebuf, "%s/unifycr-runstate.conf",
                         opts_sysconf.runstatedir);

        fp = fopen(linebuf, "r");
        if (!fp)
            perror("fopen");

        while (fgets(linebuf, 4096, fp) != NULL)
            fputs(linebuf, stdout);

        if (ferror(fp))
            perror("fgets");

        fclose(fp);
    }

    ret = unifycr_launch_daemon();

out:
    return ret;
}

