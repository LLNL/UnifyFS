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
 * @brief supported resource managers
 */
typedef enum {
    UNIFYCR_RM_NONE     = 0,
    UNIFYCR_RM_PBS,
    UNIFYCR_RM_SLURM,
    UNIFYCR_RM_LSF,
} unifycr_rm_t;

/**
 * @brief supported consistency models
 */
typedef enum {
    UNIFYCR_CM_INVALID    = -1,
    UNIFYCR_CM_NONE       = 0,
    UNIFYCR_CM_LAMINATED,
    UNIFYCR_CM_POSIX,
    N_UNIFYCR_CM,
} unifycr_cm_t;

/**
 * @brief return consistency enum entry matching with the given string.
 *
 * @param str input string of the consistency model
 *
 * @return matching enum entry on success, UNIFYCR_CM_INVALID (-1) otherwise
 */
unifycr_cm_t unifycr_read_consistency(const char *str);

/**
 * @brief return the consistency string based on the given code.
 *
 * @param con the consistency code (integer)
 *
 * @return string that describes the consistency model
 */
const char *unifycr_write_consistency(unifycr_cm_t con);

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

    /* the directory where unifycr-runtime.conf will be written */
    char *runstatedir;
    char *unifycrd_path;

    /* file system configuration */
    unifycr_cm_t consistency;       /* consistency model */
    char *mountpoint;               /* unifycr mountpoint */

    /* server configuration */
    uint64_t meta_server_ratio;
    char *meta_db_name;
    char *meta_db_path;
    char *server_debug_log_path;

    /* client intercept library configuration */
    uint64_t chunk_mem;
    char *external_meta_dir;
    char *external_data_dir;
};

typedef struct _unifycr_sysconf unifycr_sysconf_t;

/**
 * @brief options read from environmental variables.
 *
 * The following env variables are supported currently:
 *
 * UNIFYCR_MT
 * UNIFYCR_META_SERVER_RATIO
 * UNIFYCR_META_DB_NAME
 * UNIFYCR_META_DB_PATH
 * UNIFYCR_CHUNK_MEM
 * UNIFYCR_SERVER_DEBUG_LOG
 *
 * TODO: do we need to duplicate some of these parameters to sysconf file for
 * setting some default values??
 */
struct _unifycr_env {
    char *unifycr_mt;
    char *unifycr_meta_db_name;
    char *unifycr_meta_db_path;
    char *unifycr_server_debug_log;

    char *unifycr_external_meta_dir;
    char *unifycr_external_data_dir;

    uint64_t unifycr_meta_server_ratio;
    uint64_t unifycr_chunk_mem;
};

typedef struct _unifycr_env unifycr_env_t;

/**
 * @brief options read from command line arguments
 */
struct _unifycr_args {
    int cleanup;                    /* cleanup on termination? (0 or 1) */
    unifycr_cm_t consistency;       /* consistency model */
    char *mountpoint;               /* mountpoint */
    char *transfer_in;              /* data path to stage-in */
    char *transfer_out;             /* data path to stage-out (drain) */
};

typedef struct _unifycr_args unifycr_args_t;

/**
 * @brief nodes allocated to the current job.
 */
struct _unifycr_resource {
    unifycr_rm_t rm;                /* resource manager */
    uint64_t n_nodes;               /* number of nodes in job allocation */
    char **nodes;                   /* allocated node names */
};

typedef struct _unifycr_resource unifycr_resource_t;

/**
 * @brief the final runtime configurations (/var/run/unifycr-run.conf).
 */
struct _unifycr_runstate {
    char *mountpoint;               /* mountpoint */
    char *unifycrd_path;            /* unifycrd path */
    char *transfer_in;              /* data path to stage-in */
    char *transfer_out;             /* data path to stage-out (drain) */
    int cleanup;                    /* cleanup on termination? */
    int consistency;                /* consistency model */

    char *meta_db_name;
    char *meta_db_path;
    char *server_debug_log_path;

    uint64_t meta_server_ratio;
    uint64_t chunk_mem;

    uint32_t n_nodes;               /* number of nodes in job allocation */
    char **nodes;                   /* allocdated node names */
};

typedef struct _unifycr_runstate unifycr_runstate_t;

/**
 * @brief detect a resource manager and find allocated nodes accordingly.
 *
 * @param resource the structure to be filled by this function
 *
 * @return 0 on success, negative errno otherwise
 */
int unifycr_read_resource(unifycr_resource_t *resource);

/**
 * @brief
 *
 * @param resource
 * @param state
 *
 * @return
 */
int unifycr_launch_daemon(unifycr_resource_t *resource,
                          unifycr_runstate_t *state);

/**
 * @brief reads unifycr environmental variables
 *
 * @param env the structure to be filled by this function
 *
 * @return 0 on success, negative errno otherwise
 */
int unifycr_read_env(unifycr_env_t *env);

/**
 * @brief reads system configuration file (PREFIX/etc/unifycr/unifycr.conf)
 *
 * @param sysconf the structure to be filled by this function
 *
 * @return 0 on success, negative errno otherwise
 */
int unifycr_read_sysconf(unifycr_sysconf_t *sysconf);

/**
 * @brief write runstate file (@runstatedir@/unifuycr/unifycr-runstate.conf)
 *
 * @param resource allocated node informantion read from resource manager
 * @param sysconf options from sysconf file
 * @param env options from environmental variables
 * @param cmdargs options from command line arguments
 * @param runstate [out] this structure is filled by this function
 *
 * @return 0 on success, negative errno otherwise
 */
int unifycr_write_runstate(unifycr_resource_t *resource,
                           unifycr_sysconf_t *sysconf,
                           unifycr_env_t *env,
                           unifycr_args_t *cmdargs,
                           unifycr_runstate_t *runstate);

#endif  /* __UNIFYCR_H */

