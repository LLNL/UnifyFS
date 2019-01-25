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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "unifycr.h"

typedef int (*unifycr_rm_read_resource_t)(unifycr_resource_t* resource);

typedef int (*unifycr_rm_launch_t)(unifycr_resource_t* resource,
                                   unifycr_args_t* args);

typedef int (*unifycr_rm_terminate_t)(unifycr_resource_t* resource,
                                      unifycr_args_t* args);

struct _ucr_resource_manager {
    const char* type;

    unifycr_rm_read_resource_t read_resource;
    unifycr_rm_launch_t launch;
    unifycr_rm_terminate_t terminate;
};

typedef struct _ucr_resource_manager _ucr_resource_manager_t;

/*
 * TODO: currently, we cannot launch if no resource manager is detected
 * (UNIFYCR_RM_INVALID).
 */

/**
 * @brief Default resource detection routine
 *
 * @param resource  Not used
 *
 * @return -ENOSYS
 */
static int invalid_read_resource(unifycr_resource_t* resource)
{
    return -ENOSYS;
}



/**
 * @brief Parse a hostfile containing one host per line
 *
 * @param resource  The job resource record to be filled
 * @param hostfile  The hostfile to be parsed
 * @param n_nodes   The number of nodes allocated by resource manager
 *
 * @return 0 on success, negative errno otherwise
 */
static int parse_hostfile(unifycr_resource_t* resource,
                          char* hostfile,
                          size_t n_nodes)
{
    int ret = 0;
    int i = 0;
    FILE* fp = NULL;
    char** nodes = NULL;
    char buf[1024] = { 0, };

    if (hostfile == NULL) {
        return -EINVAL;
    }

    /*
     * node names may be duplicated in hostfile, depending on the number of
     * available cores per node. for instance, nodeX will appear 16 times if
     * nodeX has 16 cores. so, get the correct node number first.
     */

    fp = fopen(hostfile, "r");
    if (!fp) {
        return -errno;
    }

    nodes = calloc(sizeof(char*), n_nodes);
    if (!nodes) {
        ret = -errno;
        goto out;
    }

    while (fgets(buf, 1024, fp) != NULL) {
        buf[strlen(buf) - 1] = '\0';    /* discard the newline */
        if (i > 0) {
            if (strcmp(buf, nodes[i - 1]) == 0) {
                continue;    // skip duplicate
            }
            nodes[i] = strdup(buf);
        } else {
            nodes[i] = strdup(buf);
        }
        i++;
    }

    resource->n_nodes = n_nodes;
    resource->nodes = nodes;

out:
    fclose(fp);

    return ret;
}

/**
 * @brief Get node list from $LSB_HOSTS or $LSB_MCPU_HOSTS.
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int lsf_read_resource(unifycr_resource_t* resource)
{
    size_t i, n_nodes;
    char* val;
    char* node;
    char* last_node;
    char* lsb_hosts;
    char* pos;
    char** nodes;
    int mcpu = 0;

    // LSB_HOSTS is space-separated list of host-slots with duplicates,
    // and includes launch node as first entry
    val = getenv("LSB_HOSTS");
    if (val == NULL) {
        // LSB_MCPU_HOSTS is space-separated list of host slot-count pairs,
        // and includes launch node as first entry
        val = getenv("LSB_MCPU_HOSTS");
        if (val == NULL) {
            return -EINVAL;
        } else {
            mcpu = 1;
        }
    }

    lsb_hosts = strdup(val);

    // replace spaces with zeroes
    for (pos = lsb_hosts; *pos; pos++)
        if (isspace(*pos)) {
            *pos = '\0';
        }

    // count nodes, skipping first
    pos = lsb_hosts + (strlen(lsb_hosts) + 1); // skip launch node
    if (!mcpu) {
        last_node = lsb_hosts;
    } else {
        pos += (strlen(pos) + 1);    // skip launch node slot count
    }
    for (n_nodes = 0; *pos;) {
        node = pos;
        if (!mcpu) {
            if (strcmp(last_node, node) != 0) {
                n_nodes++;
                last_node = node;
            }
            pos += (strlen(node) + 1); // skip node
        } else {
            n_nodes++;
            pos += (strlen(node) + 1); // skip node
            pos += (strlen(pos) + 1);  // skip count
        }
    }

    nodes = calloc(sizeof(char*), n_nodes);
    if (nodes == NULL) {
        return -ENOMEM;
    }

    // fill nodes array, skipping first
    pos = lsb_hosts + (strlen(lsb_hosts) + 1); // skip launch node
    if (!mcpu) {
        last_node = lsb_hosts;
    } else {
        pos += (strlen(pos) + 1);    // skip launch node slot count
    }
    for (i = 0; *pos && i < n_nodes;) {
        node = pos;
        if (!mcpu) {
            if (strcmp(last_node, node) != 0) {
                nodes[i++] = node;
                last_node = node;
            }
            pos += (strlen(node) + 1); // skip node
        } else {
            nodes[i++] = node;
            pos += (strlen(node) + 1); // skip node
            pos += (strlen(pos) + 1);  // skip count
        }
    }

    resource->n_nodes = n_nodes;
    resource->nodes = nodes;

    return 0;
}

/**
 * @brief Get list of nodes from $PBS_NODEFILE
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int pbs_read_resource(unifycr_resource_t* resource)
{
    size_t n_nodes = 0;
    char* num_nodes_str = NULL;
    char* nodefile = NULL;

    nodefile = getenv("PBS_NODEFILE");
    if (nodefile == NULL) {
        return -EINVAL;
    }

    num_nodes_str = getenv("PBS_NUM_NODES");
    if (num_nodes_str == NULL) {
        return -EINVAL;
    }
    n_nodes = (size_t) strtoul(num_nodes_str, NULL, 10);

    return parse_hostfile(resource, nodefile, n_nodes);
}

/**
 *
 * @brief Get list of nodes using SLURM scontrol
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int slurm_read_resource(unifycr_resource_t* resource)
{
    int ret;
    size_t len = 0;
    size_t n_nodes = 0;
    char* num_nodes_str = NULL;
    char* cmd = NULL;
    char* hostfile = NULL;
    char* tmpdir = NULL;
    const char* cmd_fmt = "scontrol show hostnames > %s";
    const char* hostfile_fmt = "%s/unifycr-hosts.%d";
    const char* tmp = "/tmp";

    // get num nodes
    num_nodes_str = getenv("SLURM_NNODES");
    if (num_nodes_str == NULL) {
        return -EINVAL;
    }
    n_nodes = (size_t) strtoul(num_nodes_str, NULL, 10);

    // get temporary hostfile
    tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = (char*)tmp;
    }
    len = strlen(tmpdir) + strlen(hostfile_fmt) + 16;
    hostfile = malloc(len);
    if (hostfile == NULL) {
        return -ENOMEM;
    }
    snprintf(hostfile, len, hostfile_fmt, tmpdir, (int)getpid());

    // write SLURM hostnames to temporary hostfile for parsing
    len = strlen(cmd_fmt) + strlen(hostfile) + 8;
    cmd = malloc(len);
    if (cmd == NULL) {
        return -ENOMEM;
    }
    snprintf(cmd, len, cmd_fmt, hostfile);
    ret = system(cmd);
    free(cmd);

    ret = parse_hostfile(resource, hostfile, n_nodes);
    free(hostfile);

    return ret;
}

/**
 * @brief Default server launch routine
 *
 * @param args         The command-line options
 * @param server_args  Server argument vector to be filled
 *
 * @return number of server arguments
 */
static size_t construct_server_argv(unifycr_args_t* args,
                                    char** server_argv)
{
    size_t argc;
    char number[16];

    if (server_argv != NULL) {
        if (args->server_path != NULL) {
            server_argv[0] = strdup(args->server_path);
        } else {
            server_argv[0] = strdup(BINDIR "/unifycrd");
        }
    }
    argc = 1;

    if (args->debug) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-d");
            server_argv[argc + 1] = strdup("-v");
            snprintf(number, sizeof(number), "%d", args->debug);
            server_argv[argc + 2] = strdup(number);
        }
        argc += 3;
    }

    if (args->cleanup) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-C");
        }
        argc++;
    }

    if (args->consistency != UNIFYCR_CM_LAMINATED) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-c");
            server_argv[argc + 1] =
                strdup(unifycr_cm_enum_str(args->consistency));
        }
        argc += 2;
    }

    if (args->mountpoint != NULL) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-m");
            server_argv[argc + 1] = strdup(args->mountpoint);
        }
        argc += 2;
    }

    return argc;
}

/**
 * @brief Default server launch routine
 *
 * @param resource  Not used
 * @param args      Not used
 *
 * @return -ENOSYS
 */
static int invalid_launch(unifycr_resource_t* resource,
                          unifycr_args_t* args)
{
    return -ENOSYS;
}

/**
 * @brief Default server terminate routine
 *
 * @param resource  Not used
 * @param args      Not used
 *
 * @return -ENOSYS
 */
static int invalid_terminate(unifycr_resource_t* resource,
                             unifycr_args_t* args)
{
    return -ENOSYS;
}

/**
 * @brief Launch servers using IBM jsrun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int jsrun_launch(unifycr_resource_t* resource,
                        unifycr_args_t* args)
{
    size_t argc, jsrun_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: jsrun <jsrun args> <server args>
    jsrun_argc = 9;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + jsrun_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("jsrun");
    argv[1] = strdup("--immediate");
    argv[2] = strdup("-e");
    argv[3] = strdup("individual");
    argv[4] = strdup("--nrs");
    argv[5] = strdup(n_nodes);
    argv[6] = strdup("-r1");
    argv[7] = strdup("-c1");
    argv[8] = strdup("-a1");
    construct_server_argv(args, argv + jsrun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() jsrun to launch unifycrd");
    return -errno;
}

/**
 * @brief Cleanup servers using IBM jsrun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int jsrun_terminate(unifycr_resource_t* resource,
                           unifycr_args_t* args)
{
    size_t argc, jsrun_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: jsrun <jsrun args> pkill unifycrd
    jsrun_argc = 11;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + jsrun_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("jsrun");
    argv[1] = strdup("--immediate");
    argv[2] = strdup("-e");
    argv[3] = strdup("individual");
    argv[4] = strdup("--nrs");
    argv[5] = strdup(n_nodes);
    argv[6] = strdup("-r1");
    argv[7] = strdup("-c1");
    argv[8] = strdup("-a1");
    argv[9] = strdup("pkill");
    argv[10] = strdup("unifycrd");

    execvp(argv[0], argv);
    perror("failed to execvp() jsrun to pkill unifycrd");
    return -errno;
}

/**
 * @brief Launch servers using mpirun (OpenMPI)
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int mpirun_launch(unifycr_resource_t* resource,
                         unifycr_args_t* args)
{
    size_t argc, mpirun_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: mpirun <mpirun args> <server args>

    mpirun_argc = 5;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + mpirun_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("mpirun");
    argv[1] = strdup("-np");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("--map-by");
    argv[4] = strdup("ppr:1:node");
    construct_server_argv(args, argv + mpirun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() mpirun to launch unifycrd");
    return -errno;
}

/**
 * @brief Terminate servers using mpirun (OpenMPI)
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int mpirun_terminate(unifycr_resource_t* resource,
                            unifycr_args_t* args)
{
    size_t argc, mpirun_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: mpirun <mpirun args> pkill unifycrd
    mpirun_argc = 7;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + mpirun_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("mpirun");
    argv[1] = strdup("-np");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("--map-by");
    argv[4] = strdup("ppr:1:node");
    argv[5] = strdup("pkill");
    argv[6] = strdup("unifycrd");

    execvp(argv[0], argv);
    perror("failed to execvp() mpirun to pkill unifycrd");
    return -errno;
}


/**
 * @brief Launch servers using SLURM srun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int srun_launch(unifycr_resource_t* resource,
                       unifycr_args_t* args)
{
    size_t argc, srun_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: srun <srun args> <server args>

    srun_argc = 5;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + srun_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("srun");
    argv[1] = strdup("-N");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("-n");
    argv[4] = strdup(n_nodes);
    construct_server_argv(args, argv + srun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() srun to launch unifycrd");
    return -errno;
}

/**
 * @brief Terminate servers using SLURM srun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int srun_terminate(unifycr_resource_t* resource,
                          unifycr_args_t* args)
{
    size_t argc, srun_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: srun <srun args> pkill unifycrd
    srun_argc = 7;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + srun_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("srun");
    argv[1] = strdup("-N");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("-n");
    argv[4] = strdup(n_nodes);
    argv[5] = strdup("pkill");
    argv[6] = strdup("unifycrd");

    execvp(argv[0], argv);
    perror("failed to execvp() srun to pkill unifycrd");
    return -errno;
}

/**
 * @brief Launch servers using custom script
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int script_launch(unifycr_resource_t* resource,
                         unifycr_args_t* args)
{
    size_t argc, script_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: <script> <#nodes> <server args>

    script_argc = 2;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + script_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup(args->script);
    argv[1] = strdup(n_nodes);
    construct_server_argv(args, argv + script_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() custom launch script");
    return -errno;
}

/**
 * @brief Terminate servers using custom script
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int script_terminate(unifycr_resource_t* resource,
                            unifycr_args_t* args)
{
    size_t argc, script_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: <script> <#nodes> pkill unifycrd
    script_argc = 4;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + script_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup(args->script);
    argv[1] = strdup(n_nodes);
    argv[2] = strdup("pkill");
    argv[3] = strdup("unifycrd");

    execvp(argv[0], argv);
    perror("failed to execvp() custom terminate script");
    return -errno;
}

/* The following is indexed by unifycr_rm_e, so the order must
 * match the definition in common/src/rm_enumerator.h
 */
static _ucr_resource_manager_t resource_managers[] = {
    { "none", &invalid_read_resource, &invalid_launch, &invalid_terminate },
    { "pbs", &pbs_read_resource, &mpirun_launch, &mpirun_terminate },
    { "slurm", &slurm_read_resource, &srun_launch, &srun_terminate },
    { "lsf", &lsf_read_resource, &mpirun_launch, &mpirun_terminate },
    { "lsfcsm", &lsf_read_resource, &jsrun_launch, &jsrun_terminate },
};

int unifycr_detect_resources(unifycr_resource_t* resource)
{
    if (getenv("PBS_JOBID") != NULL) {
        resource->rm = UNIFYCR_RM_PBS;
    } else if (getenv("SLURM_JOBID") != NULL) {
        resource->rm = UNIFYCR_RM_SLURM;
    } else if (getenv("LSB_JOBID") != NULL) {
        if (getenv("CSM_ALLOCATION_ID") != NULL) {
            resource->rm = UNIFYCR_RM_LSF_CSM;
        } else {
            resource->rm = UNIFYCR_RM_LSF;
        }
    } else {
        resource->rm = UNIFYCR_RM_INVALID;
    }

    return resource_managers[resource->rm].read_resource(resource);
}

int unifycr_start_servers(unifycr_resource_t* resource,
                          unifycr_args_t* args)
{

    if ((resource == NULL) || (args == NULL)) {
        return -EINVAL;
    }

    if (args->script != NULL) {
        return script_launch(resource, args);
    } else {
        return resource_managers[resource->rm].launch(resource, args);
    }
}

int unifycr_stop_servers(unifycr_resource_t* resource,
                         unifycr_args_t* args)
{

    if ((resource == NULL) || (args == NULL)) {
        return -EINVAL;
    }

    if (args->script != NULL) {
        return script_terminate(resource, args);
    } else {
        return resource_managers[resource->rm].terminate(resource, args);
    }
}
