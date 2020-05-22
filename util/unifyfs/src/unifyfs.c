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

#define SIZE_OF_WC_OUTPUT 20
int unifyfs_lines_in_manifest_file(char* manifest_filename);

#include <libgen.h> // basename
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "unifyfs.h"

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
static unifyfs_args_t cli_args;
static unifyfs_resource_t resource;

static struct option const long_opts[] = {
    { "cleanup", no_argument, NULL, 'c' },
    { "consistency", required_argument, NULL, 'C' },
    { "debug", no_argument, NULL, 'd' },
    { "exe", required_argument, NULL, 'e' },
    { "help", no_argument, NULL, 'h' },
    { "mount", required_argument, NULL, 'm' },
    { "script", required_argument, NULL, 's' },
    { "share-dir", required_argument, NULL, 'S' },
    { "stage-in", required_argument, NULL, 'i' },
    { "stage-out", required_argument, NULL, 'o' },
    { "timeout", required_argument, NULL, 't' },
    { "transfer-exe", required_argument, NULL, 'x' },
    { 0, 0, 0, 0 },
};

static char* program;
static char* short_opts = ":cC:de:hi:m:o:s:S:t:x:";
static char* usage_str =
    "\n"
    "Usage: %s <command> [options...]\n"
    "\n"
    "<command> should be one of the following:\n"
    "  start       start the UnifyFS server daemons\n"
    "  terminate   terminate the UnifyFS server daemons\n"
    "\n"
    "Common options:\n"
    "  -d, --debug               enable debug output\n"
    "  -h, --help                print usage\n"
    "  -x, --transfer-exe=<path> [OPTIONAL] path to transfer executable\n"
    "        (pertains to --stage-in and --stageout)"
    "\n"
    "Command options for \"start\":\n"
    "  -C, --consistency=<model> [OPTIONAL] consistency model (NONE | LAMINATED | POSIX)\n"
    "  -e, --exe=<path>          [OPTIONAL] <path> where unifyfsd is installed\n"
    "  -m, --mount=<path>        [OPTIONAL] mount UnifyFS at <path>\n"
    "  -s, --script=<path>       [OPTIONAL] <path> to custom launch script\n"
    "  -t, --timeout=<sec>       [OPTIONAL] wait <sec> until all servers become ready\n"
    "  -S, --share-dir=<path>    [REQUIRED] shared file system <path> for use by servers\n"
    "  -c, --cleanup             [OPTIONAL] clean up the UnifyFS storage upon server exit\n"
    "  -i, --stage-in=<path>     [OPTIONAL] stage in manifest file(s) at <path>\n"
    "\n"
    "Command options for \"terminate\":\n"
    "  -s, --script=<path>       <path> to custom termination script\n"
    "  -o, --stage-out=<path>    [OPTIONAL] stage out manifest file(s) at <path>\n"
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
    int timeout = UNIFYFS_DEFAULT_INIT_TIMEOUT;
    unifyfs_cm_e consistency = UNIFYFS_CM_LAMINATED;
    char* mountpoint = NULL;
    char* script = NULL;
    char* share_dir = NULL;
    char* srvr_exe = NULL;
    char* stage_in = NULL;
    char* stage_out = NULL;
    char* transfer_exe = NULL;

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            printf("WARNING: cleanup not yet supported!\n");
            cleanup = 1;
            break;

        case 'C':
            consistency = unifyfs_cm_enum_from_str(optarg);
            if (consistency == UNIFYFS_CM_INVALID) {
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

        case 'S':
            share_dir = strdup(optarg);
            break;

        case 't':
            timeout = atoi(optarg);
            break;

        case 'i':
	  //            printf("WARNING: stage-in not yet supported!\n");
            stage_in = strdup(optarg);
            break;

        case 'o':
	  //            printf("WARNING: stage-out not yet supported!\n");
            stage_out = strdup(optarg);
            break;

	case 'x':
            transfer_exe = strdup(optarg);
	    //	    printf("specified transfer exec: >%s<\n",transfer_exe);
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
    cli_args.share_dir = share_dir;
    cli_args.stage_in = stage_in;
    cli_args.stage_out = stage_out;
    cli_args.timeout = timeout;
    cli_args.transfer_exe = transfer_exe;
}


int main(int argc, char** argv)
{
    int i = 0;
    int ret = 0;
    char* cmd = NULL;

    char* stage_in_filename = NULL;
    char* stage_out_filename = NULL;
    int lines_in_manifest_file;

    program = strdup(argv[0]);
    program = basename(program);

    FILE* trans_exe_fp = NULL;

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
               unifyfs_cm_enum_str(cli_args.consistency));
        printf("mountpoint:\t%s\n", cli_args.mountpoint);
        printf("script:\t%s\n", cli_args.script);
        printf("share_dir:\t%s\n", cli_args.share_dir);
        printf("server:\t%s\n", cli_args.server_path);
        printf("stage_in:\t%s\n", cli_args.stage_in);
        printf("stage_out:\t%s\n", cli_args.stage_out);
	printf("transfer_exec:\t%s\n", cli_args.transfer_exe);
    }

    ret = unifyfs_detect_resources(&resource);
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
        if (NULL == cli_args.share_dir) {
            printf("USAGE ERROR: shared directory (-S) is required!\n");
            usage(1);
            return -EINVAL;
        }

	if (cli_args.stage_out != NULL) {
	    fprintf(stderr, "ERROR! called unifyfs start with --stage-out!\n");
	    usage(1);
            return -EINVAL;
	}

	stage_in_filename = cli_args.stage_in;
	if (stage_in_filename != NULL) {
            if (cli_args.script) {
	        fprintf(stderr,
			"WARNING! You are using a script with --stage-in.\n");
	        fprintf(stderr, "This is unlikely to work.\n");
            }
            if (cli_args.transfer_exe == NULL) {
	        fprintf(stderr,
		      "ERROR!  You must specify --transfer-exe with --stage-in!\n");
	        return -EINVAL;
            }
	    trans_exe_fp = fopen(cli_args.transfer_exe, "r");
            if (trans_exe_fp == NULL) {
	        fprintf(stderr,
			"ERROR!  The trans exe file you specified >%s<doesn't exist!\n",
			cli_args.transfer_exe);
		return -ENOENT;
            }
	    fclose(trans_exe_fp);
	    trans_exe_fp = NULL;
	    lines_in_manifest_file =
	      unifyfs_lines_in_manifest_file(stage_in_filename);
	    // fprintf(stderr,"lines_in_manifest_file returned >%d< lines\n",
	    //		    lines_in_manifest_file);
            if (lines_in_manifest_file < 0) {
	        fprintf(stderr,
			"ERROR! Cannot read stage-in manifest file >%s<!\n",
		      stage_in_filename);
                return -ENOENT;
            }
	    // possibly nest this in a if(debug) check in the future
	    fprintf(stdout, "Stage-in manifest file contains %d lines.\n",
		    lines_in_manifest_file);
	} // if (stage_in_filename != NULL) {
	else {
            if (cli_args.transfer_exe) {
	        fprintf(stderr,
			"WARNING! You specified transfer_exe but not stage_in!\n");
            }
	}
        return unifyfs_start_servers(&resource, &cli_args);
    } // if (action == ACT_START)
    else if (action == ACT_TERMINATE) {

	if (cli_args.stage_in != NULL) {
	    fprintf(stderr,
		    "ERROR! called unifyfs terminate with --stage-in!\n");
	    usage(1);
            return -EINVAL;
	}

	stage_out_filename = cli_args.stage_out;
	if (stage_out_filename != NULL) {
            if (cli_args.script) {
	        fprintf(stderr,
			"WARNING! You are using a script with --stage-out.\n");
	        fprintf(stderr, "This is unlikely to work.\n");
            }
            if (cli_args.transfer_exe == NULL) {
	        fprintf(stderr,
		      "ERROR!  You must specify --transfer-exe with --stage-out!\n");
	        return -EINVAL;
            }
            if (trans_exe_fp == NULL) {
	        fprintf(stderr,
			"ERROR!  The trans exe file you specified >%s<doesn't exist!\n",
			cli_args.transfer_exe);
		return -ENOENT;
            }
	    fclose(trans_exe_fp);
	    trans_exe_fp = NULL;
	    lines_in_manifest_file =
	      unifyfs_lines_in_manifest_file(stage_out_filename);
            if (lines_in_manifest_file < 0) {
	        fprintf(stderr,
			"ERROR! Cannot read stage-out manifest file >%s<!\n",
		      stage_in_filename);
                return -ENOENT;
            }
	    // possibly nest this in a if(debug) check in the future
	    fprintf(stdout, "Stage-in manifest file contains %d lines.\n",
		    lines_in_manifest_file);
	} else {
            if (cli_args.transfer_exe) {
	        fprintf(stderr,
			"WARNING! You specified transfer_exe but not stage_out!\n");
            }
	}
        return unifyfs_stop_servers(&resource, &cli_args);
    } else {
        fprintf(stderr, "INTERNAL ERROR: unhandled action %d\n", (int)action);
        return -1;
    }
} // int main(...

// validates that the named manifest file exists and is readable
// and returns the number of lines in it.
// negative return value for error.
int unifyfs_lines_in_manifest_file(char* manifest_filename)
{
    FILE* manifest_fp = NULL;
    int wc_command_length = -1;
    char* wc_command = NULL;
    size_t* size_of_wc_output;
    char** wc_output;
    int lines_in_manifest_file;

    FILE* wc_fp = NULL; // pointer for wc system command

    manifest_fp = fopen(manifest_filename, "r");
    if (manifest_fp == NULL) {
        fprintf(stderr, "ERROR! cannot read manifest file >%s<!\n",
		manifest_filename);
	return -1;
    }
    fclose(manifest_fp);
    manifest_fp = NULL;
    // manifest file exists and it's readable
    // the 8 here is length of "wc -l " and null term
    wc_command_length = strlen(manifest_filename) + 8;

    wc_command = malloc(wc_command_length);
    if (wc_command == NULL) {
        fprintf(stderr, "Could not allocate wc command string!\n");
	return -EINVAL;
    }
    sprintf(wc_command, "wc -l %s", manifest_filename);
    wc_fp = popen(wc_command, "r");
    if (wc_fp == NULL) {
        fprintf(stderr,
		"Could not run wc command for manifest file validation!\n");
        return -EIO;
    }
    size_of_wc_output = malloc(sizeof(int));
    if (size_of_wc_output == NULL) {
        fprintf(stderr, "could not allocate int for manifest validation!]n");
        return -ENOMEM;
    }
    *size_of_wc_output = SIZE_OF_WC_OUTPUT;
    wc_output = malloc(sizeof(char*));
    if (wc_output == NULL) {
        fprintf(stderr,
		"could not allocation char* for manifest validation!\n");
	return -ENOMEM;
    }
    *wc_output = malloc(sizeof(char)*(*size_of_wc_output));
    if ((*wc_output) == NULL) {
        fprintf(stderr,
		"could not allocate char string for manifest validation!\n");
	return -ENOMEM;
    }
    if (getline(wc_output, size_of_wc_output, wc_fp) < 0) {
        fprintf(stderr, "%s failed to run WC!\n", __func__);
        return -1;
    }
    lines_in_manifest_file = atoi(*wc_output);
    free(wc_command);
    wc_command = NULL;
    pclose(wc_fp);
    wc_fp = NULL;
    return lines_in_manifest_file;
}
