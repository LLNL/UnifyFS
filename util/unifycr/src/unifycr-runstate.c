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

    if (runstate->meta_db_name)
        fprintf(fp, "meta_db_name = %s\n", runstate->meta_db_name);
    if (runstate->meta_db_path)
        fprintf(fp, "meta_db_path = %s\n", runstate->meta_db_path);
    if (runstate->server_debug_log_path)
        fprintf(fp, "server_debug_log_path = %s\n",
                runstate->server_debug_log_path);

    if (runstate->meta_server_ratio > 0)
        fprintf(fp, "meta_server_ratio = %llu\n",
                    (unsigned long long) runstate->meta_server_ratio);
    if (runstate->chunk_mem > 0)
        fprintf(fp, "chunk_mem = %llu\n",
                    (unsigned long long) runstate->chunk_mem);

    if (runstate->transfer_in)
        fprintf(fp, "transfer_in = %s\n", runstate->transfer_in);
    if (runstate->transfer_out)
        fprintf(fp, "transfer_out = %s\n", runstate->transfer_out);

    fprintf(fp, "n_nodes = %d\n", runstate->n_nodes);
    fprintf(fp, "nodes = [ ");

    for (i = 0; i < runstate->n_nodes; i++) {
        fprintf(fp, "%s%s",
                    runstate->nodes[i],
                    i == runstate->n_nodes - 1 ? " ]\n" : ", ");
    }

    fclose(fp);

    return ret;
}

static int create_runstatedir(const char *path)
{
    int ret = 0;
    char *ch = NULL;
    char *buf = strdup(path);

    for (ch = strchr(path+1, '/'); ch; ch = strchr(ch+1, '/')) {
        *ch = '\0';
        ret = mkdir(path, 0755);
        if (ret < 0 && errno != EEXIST) {
            *ch = '/';
            goto out;
        }
        *ch = '/';
    }

out:
    free(buf);

    return ret;
}

int unifycr_write_runstate(unifycr_resource_t *resource,
                           unifycr_sysconf_t *sysconf,
                           unifycr_env_t *env,
                           unifycr_args_t *cmdargs,
                           unifycr_runstate_t *runstate)
{
    int ret = 0;
    char runstatefile[4096] = { 0, };

    /*
     * create the runstatedir, if not exists
     */
    ret = create_runstatedir(sysconf->runstatedir);
    if (ret)
        return ret;

    /*
     * apply options from the sysconf
     */
    runstate->consistency = sysconf->consistency;
    runstate->mountpoint = sysconf->mountpoint;
    runstate->unifycrd_path = sysconf->unifycrd_path;
    runstate->meta_db_name = sysconf->meta_db_name;
    runstate->meta_db_path = sysconf->meta_db_path;
    runstate->server_debug_log_path = sysconf->server_debug_log_path;
    runstate->meta_server_ratio = sysconf->meta_server_ratio;
    runstate->chunk_mem = sysconf->chunk_mem;

    /*
     * environmental variables
     */
    runstate->mountpoint = env->unifycr_mt;
    runstate->meta_db_name = env->unifycr_meta_db_name;
    runstate->meta_db_path = env->unifycr_meta_db_path;
    runstate->server_debug_log_path = env->unifycr_server_debug_log;
    runstate->meta_server_ratio = env->unifycr_meta_server_ratio;
    runstate->chunk_mem = env->unifycr_chunk_mem;

    /*
     * command-line arguments
     */
    runstate->cleanup = cmdargs->cleanup;
    runstate->consistency = cmdargs->consistency;
    runstate->mountpoint = cmdargs->mountpoint;
    runstate->transfer_in = cmdargs->transfer_in;
    runstate->transfer_out = cmdargs->transfer_out;

    /*
     * resources
     */
    runstate->n_nodes = resource->n_nodes;
    runstate->nodes = resource->nodes;

    sprintf(runstatefile, "%s/unifycr-runstate.conf", sysconf->runstatedir);

    return write_runstate_file(runstate, runstatefile);
}

