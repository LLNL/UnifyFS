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

typedef int (*unifycr_rm_read_resource_t)(unifycr_resource_t *resource);

typedef int (*unifycr_rm_launch_t)(unifycr_resource_t *resource,
                                   unifycr_args_t *args);

struct _ucr_resource_manager {
    const char *type;

    unifycr_rm_read_resource_t read_resource;
    unifycr_rm_launch_t launch;
};

typedef struct _ucr_resource_manager _ucr_resource_manager_t;

/*
 * TODO: currently, we cannot launch if no resource manager is detected
 * (UNIFYCR_RM_INVALID).
 */

/**
 * @brief
 *
 * @param resource
 *
 * @return
 */
static int none_read_resource(unifycr_resource_t *resource)
{
    return -ENOSYS;
}

static int none_launch_daemon(unifycr_resource_t *resource,
                              unifycr_args_t *args)
{
    return -ENOSYS;
}

/**
 * @brief parse the hostfile for lsf and SLURM
 *
 * @param resource the resource record which will be filled
 * @param hostfile which will be parsed
 * @param number of nodes allocated by resource manager
 *
 * @return 0 on success, negative errno otherwise
 */
static int parse_hostfile(unifycr_resource_t *resource,
                         char *hostfile,
                         uint64_t n_nodes)
{
    int ret = 0;
    int i = 0;
    FILE *fp = NULL;
    char **nodes = NULL;
    char buf[1024] = { 0, };

    if (hostfile == NULL)
        return -EINVAL;

    /*
     * node names may be duplicated in hostfile, depending on the number of
     * available cores per node. for instance, nodeX will appear 16 times if
     * nodeX has 16 cores. so, get the correct node number first.
     */

    fp = fopen(hostfile, "r");
    if (!fp)
        return -errno;

    nodes = calloc(sizeof(char *), n_nodes);
    if (!nodes) {
        ret = -errno;
        goto out;
    }

    while (fgets(buf, 1024, fp) != NULL) {
        buf[strlen(buf) - 1] = '\0';    /* discard the newline */
        if (i > 0) {
            if (strcmp(buf, nodes[i - 1]) == 0)
                continue;
            nodes[i] = strdup(buf);
        } else
            nodes[i] = strdup(buf);
        i++;
    }

    resource->n_nodes = n_nodes;
    resource->nodes = nodes;

out:
    fclose(fp);

    return ret;
}

/**
 * @brief get the node list from the $PBS_NODEFILE
 *
 * @param resource the resource record which will be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int pbs_read_resource(unifycr_resource_t *resource)
{
    int ret = 0;
    uint64_t n_nodes = 0;
    char *num_nodes_str = NULL;
    char *nodefile = getenv("PBS_NODEFILE");

    if (nodefile == NULL)
        return -EINVAL;

    num_nodes_str = getenv("PBS_NUM_NODES");
    if (num_nodes_str == NULL)
        return -EINVAL;

    n_nodes = (uint64_t) strtoul(num_nodes_str, NULL, 10);

    ret = parse_hostfile(resource, nodefile, n_nodes);

    return ret;
}

/**
 * @brief
 *
 * @param resource
 * @param args
 *
 * @return
 */
static int pbs_launch_daemon(unifycr_resource_t *resource,
                             unifycr_args_t *args)
{
    return 0;
}

/**
 *
 * @brief get the node list from writing hostfile to
 * /tmp/hostfile with scontrol
 *
 * @param resource
 *
 * @return 0 on success, negative errno otherwise
 */
static int slurm_read_resource(unifycr_resource_t *resource)
{
    int ret = 0;
    uint64_t n_nodes = 0;
    char *num_nodes_str = NULL;
    char *hostfile = "/tmp/hostfile";

    /* write SLURM hostnames to /tmp/hostfile for parsing */
    system("scontrol show hostnames > /tmp/hostfile");

    num_nodes_str = getenv("SLURM_NNODES");
    if (num_nodes_str == NULL)
        return -EINVAL;

    n_nodes = (uint64_t) strtoul(num_nodes_str, NULL, 10);

    ret = parse_hostfile(resource, hostfile, n_nodes);

    return ret;
}

/**
 * @brief
 *
 * @param resource
 * @param args
 *
 * @return
 */
static int slurm_launch_daemon(unifycr_resource_t *resource,
                               unifycr_args_t *args)
{
    return 0;
}

/**
 * From the documentation: The list of hosts selected by LSF Batch to run the
 * batch job. If the job is run on a single processor, the value of LSB_HOSTS
 * is the name of the execution host. For parallel jobs, the names of all
 * execution hosts are listed separated by spaces. The batch job file is run on
 * the first host in the list.
 *
 * @brief get the nod list from the $LSB_HOSTS.
 * n hosts are listed separated by spaces. The batch job file is run on
 *
 *
 * @param resource
 *
 * @return
 */
static int lsf_read_resource(unifycr_resource_t *resource)
{
    int i = 0;
    char *node = NULL;
    char *pos = NULL;
    int n_nodes = 0;
    char **nodes = NULL;
    char *lsb_hosts = NULL;

    node = getenv("LSB_HOSTS");
    if (node == NULL)
        return -EINVAL;

    lsb_hosts = strdup(node);
    pos = lsb_hosts;

    for (pos = lsb_hosts; *pos; pos++)
        if (isspace(*pos))
            i++;

    n_nodes = i + 1;
    nodes = calloc(sizeof(char *), n_nodes);

    for (i = 0, node = pos = lsb_hosts; *pos; pos++) {
        if (isspace(*pos)) {
            *pos = '\0';
            nodes[i++] = node;
            node = &pos[1];
        }
    }

    nodes[i] = node;    /* the last one, not caught by isspace() */

    resource->n_nodes = n_nodes;
    resource->nodes = nodes;

    return 0;
}

/**
 * @brief
 *
 * @param resource
 * @param args
 *
 * @return
 */
static int lsf_launch_daemon(unifycr_resource_t *resource,
                             unifycr_args_t *args)
{
    return 0;
}

static _ucr_resource_manager_t resource_managers[] = {
    { "none", &none_read_resource, &none_launch_daemon },
    { "pbs", &pbs_read_resource, &pbs_launch_daemon },
    { "slurm", &slurm_read_resource, &slurm_launch_daemon },
    { "lsf", &lsf_read_resource, &lsf_launch_daemon },
};

int unifycr_read_resource(unifycr_resource_t *resource)
{
    if (getenv("PBS_JOBID") != NULL)
        resource->rm = UNIFYCR_RM_PBS;
    else if (getenv("SLURM_JOBID") != NULL)
        resource->rm = UNIFYCR_RM_SLURM;
    else if (getenv("LSB_JOBID") != NULL) {
        if (getenv("CSM_ALLOCATION_ID") != NULL)
            resource->rm = UNIFYCR_RM_LSF_CSM;
        else
            resource->rm = UNIFYCR_RM_LSF;
    } else
        resource->rm = UNIFYCR_RM_INVALID;

    return resource_managers[resource->rm].read_resource(resource);
}

/*
 * TODO: currently it directly uses mpirun command in any cases.  The command
 * should be:
 * $ mpirun -n NUMNODES -np NUMNODES unifycrd
 */
static char *mpirun_newargv[] = {
    "mpirun", "-n", NULL, "-np", NULL, NULL, (char *) NULL
};

static char *srun_newargv[] = {
    "srun", "-N", NULL, "-n", NULL, NULL, (char *) NULL
};

int unifycr_launch_daemon(unifycr_resource_t *resource,
                          unifycr_args_t *args)
{
    char nbuf[16] = { 0, };

    sprintf(nbuf, "%llu", (unsigned long long) resource->n_nodes);

    if (resource->rm == UNIFYCR_RM_SLURM) {
        srun_newargv[2] = nbuf;
        srun_newargv[4] = nbuf;
        srun_newargv[5] = args->server_path ?
                        args->server_path : BINDIR "/unifycrd";

        execvp(srun_newargv[0], srun_newargv);
        perror("failed to launch unifycrd (execvp)");
    } else {
        mpirun_newargv[2] = nbuf;
        mpirun_newargv[4] = nbuf;
        mpirun_newargv[5] = args->server_path ?
                        args->server_path : BINDIR "/unifycrd";

        execvp(mpirun_newargv[0], mpirun_newargv);
        perror("failed to launch unifycrd (execvp)");
    }
    return -1;
}
