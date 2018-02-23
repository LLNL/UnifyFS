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

#ifndef __UNIFYCR_H
#define __UNIFYCR_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief resource managers
 */
typedef enum {
    UNIFYCR_RM_NONE     = 0,
    UNIFYCR_RM_PBS,
    UNIFYCR_RM_SLURM,
    UNIFYCR_RM_LSF,
} unifycr_rm_t;

/**
 * @brief consistency models
 */
typedef enum {
    UNIFYCR_CM_INVALID    = -1,
    UNIFYCR_CM_NONE       = 0,
    UNIFYCR_CM_LAMINATED,
    UNIFYCR_CM_POSIX,
} unifycr_cm_t;

/**
 * @brief return consistency enum entry matching with the given string.
 *
 * @param str input string of the consistency model
 *
 * @return matching enum entry on success, UNIFYCR_CM_INVALID (-1) otherwise
 */
static inline unifycr_cm_t unifycr_read_consistency(const char *str)
{
    int i = 0;
    unifycr_cm_t cons = UNIFYCR_CM_INVALID;
    static char *constr[] = { "none", "laminated", "posix" };

    for (i = 0; i < sizeof(constr)/sizeof(char *); i++)
        if (0 == strncmp(str, constr[i], strlen(constr[i])))
            cons = i;

    return cons;
}

/*
 * Runtime configurations are read from:
 *
 * 1. sysconfdir, $PREFIX/etc/unifycr/unifycr.conf. This file contains default
 *    runtime options and cluster-wide global settings.
 *
 * 2. Environment variables, which can be defined by system or user.
 *
 * 3. Command line argument to this unifycr program.
 *
 * If a same configuration option is defined multiple times by above three
 * cases, 3 overrides 2, which overrides 1.
 *
 * The final configuration is written to a temporary configuration file in
 * runstatedir ($PREFIX/var/run/unifycr/unifycr-run.conf).
 */

/**
 * @brief global options set by sysconf dir (unifycr.conf).
 */
struct _unifycr_sysconf {
    /* global configuration */
    char *runstatedir;

    /* file system configuration */
    unifycr_cm_t consistency;
    char *mountpoint;
};

typedef struct _unifycr_sysconf unifycr_sysconf_t;

/**
 * @brief options read from environmental variables
 */
struct _unifycr_env {
    char *unifycr_mt;
};

typedef struct _unifycr_env unifycr_env_t;

/**
 * @brief options read from command line arguments
 */
struct _unifycr_args {
    int cleanup;
    unifycr_cm_t consistency;
    char *mountpoint;
    char *transfer_in;
    char *transfer_out;
};

typedef struct _unifycr_args unifycr_args_t;

struct _unifycr_resource {
    uint64_t n_nodes;
    char **nodes;
};

typedef struct _unifycr_resource unifycr_resource_t;

/**
 * @brief the final runtime configurations (/var/run/unifycr-run.conf).
 */
struct _unifycr_runtime {
    char *mountpoint;
    char *transfer_in;
    char *transfer_out;
    int cleanup;
    int consistency;

    uint32_t n_nodes;
    char *nodes[0];
};

typedef struct _unifycr_runtime unifycr_runtime_t;

/**
 * @brief
 *
 * @param resource
 *
 * @return
 */
int unifycr_read_resource(unifycr_resource_t *resource);

/**
 * @brief
 *
 * @param env
 *
 * @return
 */
int unifycr_read_env(unifycr_env_t *env);

/**
 * @brief
 *
 * @param sysconf
 *
 * @return
 */
int unifycr_read_sysconf(unifycr_sysconf_t *sysconf);

/**
 * @brief
 *
 * @param resource
 * @param sysconf
 * @param env
 * @param cmdargs
 *
 * @return
 */
int unifycr_write_runstate(unifycr_resource_t *resource,
                           unifycr_sysconf_t *sysconf,
                           unifycr_env_t *env,
                           unifycr_args_t *cmdargs);

#endif  /* __UNIFYCR_H */

