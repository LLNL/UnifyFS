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
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "toml.h"
#include "unifycr.h"

static int
write_runstate_file(unifycr_runstate_t *runstate, const char *file)
{
    int ret = 0;
    uint32_t i = 0;
    FILE *fp = NULL;

    fp = fopen(file, "w");
    if (!fp)
        return -errno;

    fprintf(fp, "mountpoint = %s\n", runstate->mountpoint);
    fprintf(fp, "cleanup = %d\n", runstate->cleanup);
    fprintf(fp, "consistency = %s\n",
                unifycr_write_consistency(runstate->consistency));

    if (runstate->transfer_in)
        fprintf("transfer_in = %s\n", runstate->transfer_in);
    if (runstate->transfer_out)
        fprintf("transfer_out = %s\n", runstate->transfer_out);

    fprintf(fp, "n_nodes = %d\n", runstate->n_nodes);
    fprintf(fp, "nodes = [ ");

    for (i = 0; i < runstate->n_nodes; i++) {
        fprintf(fp, "%s%s", runstate->nodes[i]
                          , i == runstate->n_nodes - 1 ? " ]\n" : ", ");
    }

    fclose(fp);

    return ret;
}

int unifycr_write_runstate(unifycr_resource_t *resource,
                           unifycr_sysconf_t *sysconf,
                           unifycr_env_t *env,
                           unifycr_args_t *cmdargs)
{
    char runstatefile[4096] = { 0, };
    unifycr_runstate_t runstate = { 0, };

    /*
     * apply options from the sysconf
     */
    runstate.consistency = sysconf->consistency;
    runstate.mountpoint = sysconf->mountpoint;

    /*
     * environmental variables
     */
    runstate.mountpoint = env->unifycr_mt;

    /*
     * command-line arguments
     */
    runstate.cleanup = cmdargs->cleanup;
    runstate.consistency = cmdargs->consistency;
    runstate.mountpoint = cmdargs->mountpoint;
    runstate.transfer_in = cmdargs->transfer_in;
    runstate.transfer_out = cmdargs->transfer_out;

    /*
     * resources
     */
    runstate.n_nodes = resource->n_nodes;
    runstate.nodes = resource->nodes;

    sprintf(runstatefile, "%s/unifycr-runstate.conf", sysconf->runstatedir);

    return write_runstate_file(&runstate, runstatefile);
}

