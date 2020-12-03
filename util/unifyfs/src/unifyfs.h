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

#ifndef __UNIFYFS_H
#define __UNIFYFS_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#include "cm_enumerator.h"
#include "rm_enumerator.h"
#include "unifyfs_const.h"


/**
 * @brief The requested command-line options
 */
struct _unifyfs_args {
    int debug;                 /* enable debug output */
    int cleanup;               /* cleanup on termination? (0 or 1) */
    int timeout;               /* timeout of server initialization */
    unifyfs_cm_e consistency;  /* consistency model */
    char* mountpoint;          /* mountpoint */
    char* server_path;         /* full path to installed unifyfsd */
    char* share_dir;           /* full path to shared file system directory */
    char* share_hostfile;      /* full path to shared server hostfile */
    char* stage_in;            /* data path to stage-in */
    char* stage_out;           /* data path to stage-out (drain) */
    int stage_timeout;         /* timeout of (in or out) file staging*/
    char* script;              /* path to custom launch/terminate script */
};
typedef struct _unifyfs_args unifyfs_args_t;

/**
 * @brief The job resource record, including allocated nodes
 */
struct _unifyfs_resource {
    unifyfs_rm_e rm;           /* resource manager */
    size_t n_nodes;            /* number of nodes in job allocation */
    char** nodes;              /* allocated node names */
};
typedef struct _unifyfs_resource unifyfs_resource_t;

/**
 * @brief Detect a resource manager and query allocated nodes
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
int unifyfs_detect_resources(unifyfs_resource_t* resource);

/**
 * @brief Start the unifyfsd server on all allocated nodes
 *
 * @param resource  The job resource record
 * @param args      The command-line options
 *
 * @return -errno on failure (exec's on success)
 */
int unifyfs_start_servers(unifyfs_resource_t* resource,
                          unifyfs_args_t* args);

/**
 * @brief Stop the unifyfsd server on all allocated nodes
 *
 * @param resource  The job resource record
 * @param args      The command-line options
 *
 * @return -errno on failure (exec's on success)
 */
int unifyfs_stop_servers(unifyfs_resource_t* resource,
                         unifyfs_args_t* args);

#endif  /* __UNIFYFS_H */

