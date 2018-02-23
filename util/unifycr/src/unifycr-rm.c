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

#include "unifycr.h"

/**
 * @brief get the node list from the $PBS_NODEFILE
 *
 * @param resource
 *
 * @return
 */
static int read_resource_pbs(unifycr_resource_t *resource)
{
    int ret = 0;
    int i = 0;
    int n_nodes = 0;
    FILE *fp = NULL;
    char **nodes = NULL;
    char buf[1024] = { 0, };
    char *nodefile = getenv("PBS_NODEFILE");

    if (!nodefile)
        return -EINVAL;

    fp = fopen(nodefile, "r");
    if (!fp)
        return -errno;

    while (fgets(buf, 1023, fp))
        n_nodes++;

    if (ferror(fp)) {
        ret = -errno;
        goto out;
    }

    nodes = calloc(sizeof(char *), n_nodes);
    if (!nodes) {
        ret = -errno;
        goto out;
    }

    rewind(fp);

    while (fgets(buf, 1024, fp) != NULL) {
        buf[strlen(buf) - 1] = '\0';    /* discard the newline */
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
 * TODO: not implemented yet
 *
 * @brief get the node list from the $SLURM_JOB_NODELIST, which requires
 * parsing.
 *
 * @param resource
 *
 * @return
 */
static int read_resource_slurm(unifycr_resource_t *resource)
{
    return -ENOSYS;
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
static int read_resource_lsf(unifycr_resource_t *resource)
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

int unifycr_read_resource(unifycr_resource_t *resource)
{
    int ret = -1;
    char *str = NULL;

    str = getenv("PBS_JOBID");
    if (str) {
        ret = read_resource_pbs(resource);
        goto out;
    }

    str = getenv("SLURM_JOBID");
    if (str) {
        ret = read_resource_slurm(resource);
        goto out;
    }

    str = getenv("LSB_JOBID");
    if (str) {
        ret = read_resource_lsf(resource);
        goto out;
    }

out:
    return ret;
}

