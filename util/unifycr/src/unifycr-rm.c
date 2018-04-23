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
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "unifycr.h"

typedef int (*unifycr_rm_read_resource_t)(unifycr_resource_t *resource);

typedef int (*unifycr_rm_launch_t)(unifycr_resource_t *resource,
                                   unifycr_runstate_t *state);

struct _ucr_resource_manager {
    const char *type;

    unifycr_rm_read_resource_t read_resource;
    unifycr_rm_launch_t launch;
};

typedef struct _ucr_resource_manager _ucr_resource_manager_t;

/*
 * TODO: currently, we cannot launch if no resource manager is detected
 * (UNIFYCR_RM_NONE).
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
                              unifycr_runstate_t *state)
{
    return -ENOSYS;
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
    int i = 0;
    uint64_t n_nodes = 0;
    FILE *fp = NULL;
    char *num_nodes_str = NULL;
    char **nodes = NULL;
    char buf[1024] = { 0, };
    char *nodefile = getenv("PBS_NODEFILE");

    if (!nodefile)
        return -EINVAL;

    /*
     * node names are duplicated in PBS_NODEFILE, depending on the number of
     * available cores per node. for instance, nodeX will appear 16 times if
     * nodeX has 16 cores. so, get the correct node number first.
     */

    num_nodes_str = getenv("PBS_NUM_NODES");
    if (!num_nodes_str)
        return -EINVAL;

    n_nodes = (uint64_t) strtoul(num_nodes_str, NULL, 10);

    fp = fopen(nodefile, "r");
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
            if (!strcmp(buf, nodes[i-1]))
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
 * @brief
 *
 * @param resource
 * @param state
 *
 * @return
 */
static int pbs_launch_daemon(unifycr_resource_t *resource,
                             unifycr_runstate_t *state)
{
    return 0;
}

/**
 * TODO: not implemented yet
 *
 * @brief get the node list from the $SLURM_JOB_NODELIST, which requires
 * parsing.
 *
 * @param resource
 *
 * @return
 */
static int slurm_read_resource(unifycr_resource_t *resource)
{
    return -ENOSYS;
}

/**
 * @brief
 *
 * @param resource
 * @param state
 *
 * @return
 */
static int slurm_launch_daemon(unifycr_resource_t *resource,
                               unifycr_runstate_t *state)
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
 * @brief get the node list from the $LSB_HOSTS.
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
    if (!node)
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
 * @param state
 *
 * @return
 */
static int lsf_launch_daemon(unifycr_resource_t *resource,
                             unifycr_runstate_t *state)
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
    char *pbs_jobid = NULL;
    char *slurm_jobid = NULL;
    char *lsb_jobid = NULL;

    pbs_jobid = getenv("PBS_JOBID");
    slurm_jobid = getenv("SLURM_JOBID");
    lsb_jobid = getenv("LSB_JOBID");

    if (pbs_jobid)
        resource->rm = UNIFYCR_RM_PBS;
    else if (slurm_jobid)
        resource->rm = UNIFYCR_RM_SLURM;
    else if (lsb_jobid)
        resource->rm = UNIFYCR_RM_LSF;
    else
        resource->rm = UNIFYCR_RM_NONE;

    return resource_managers[resource->rm].read_resource(resource);
}

/*
 * TODO: currently it directly uses mpirun command in any cases.  The command
 * should be:
 * $ mpirun -n NUMNODES -np NUMNODES unifycrd
 */
static char *mpirun_newargv[] = {
    "mpirun", "-n", NULL, "-ppn", "1", "-hosts", NULL, NULL, (char *) NULL
};

int unifycr_launch_daemon(unifycr_resource_t *resource,
                          unifycr_runstate_t *state)
{
    char nbuf[16] = { 0, };
    char *hostlist = NULL;
    char *ptr = NULL;
    size_t i = 0;
    size_t len = 1;

    sprintf(nbuf, "%llu", (unsigned long long) resource->n_nodes);

    for (i = 0; i < resource->n_nodes; i++)
        len += strlen(resource->nodes[i]) + 1;

    hostlist = calloc(len, 1);
    if (!hostlist)
        return -errno;
    ptr = hostlist;

    for (i = 0; i < resource->n_nodes; i++)
        ptr += sprintf(ptr, "%s,", resource->nodes[i]);

    ptr = strrchr(hostlist, ',');
    *ptr = '\0';

    mpirun_newargv[2] = nbuf;
    mpirun_newargv[6] = hostlist;
    mpirun_newargv[7] = state->unifycrd_path ?
                        state->unifycrd_path : BINDIR "/unifycrd";

    execvp(mpirun_newargv[0], mpirun_newargv);
    perror("failed to launch unifycrd (execvp)");

    return -1;
}

