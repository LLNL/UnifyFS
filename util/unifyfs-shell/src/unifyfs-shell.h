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
#ifndef __UNIFYFS_SHELL_H
#define __UNIFYFS_SHELL_H

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>

struct _unifyfs_shell_env {
    int rank;
    int total_ranks;
    int appid;
    char* mountpoint;
    char cwd[PATH_MAX];
    FILE* output;
};

typedef struct _unifyfs_shell_env unifyfs_shell_env_t;

typedef int (*unifyfs_shell_cmd_func_t)(int, char **, unifyfs_shell_env_t *);

struct _unifyfs_shell_cmd {
    const char* name;
    unifyfs_shell_cmd_func_t func;
};

typedef struct _unifyfs_shell_cmd unifyfs_shell_cmd_t;

struct _unifyfs_shell_exec {
    int cmd;
    int status;
    unifyfs_shell_env_t* env;
    char cmdline[LINE_MAX];
};

typedef struct _unifyfs_shell_exec unifyfs_shell_exec_t;

extern char* unifyfs_shell_program;

/*
 * Available commands, defined in separted c files.
 */
extern unifyfs_shell_cmd_t unifyfs_shell_cmd_status;
extern unifyfs_shell_cmd_t unifyfs_shell_cmd_ls;
extern unifyfs_shell_cmd_t unifyfs_shell_cmd_mkdir;
extern unifyfs_shell_cmd_t unifyfs_shell_cmd_cd;
extern unifyfs_shell_cmd_t unifyfs_shell_cmd_pwd;
extern unifyfs_shell_cmd_t unifyfs_shell_cmd_stat;

/* unifyfs-shell.c */
int unifyfs_shell(unifyfs_shell_env_t* env);

/* unifyfs-util.c */
int unifyfs_shell_get_absolute_path(unifyfs_shell_env_t* env,
                                    const char* path, char* outpath);

/* common headers for accessing the client context */
#include "unifyfs_configurator.h"
#include "unifyfs_const.h"
#include "unifyfs_keyval.h"
#include "unifyfs_log.h"
#include "unifyfs_meta.h"
#include "unifyfs_shm.h"

/* global variables from unifyfs clients */
extern unifyfs_cfg_t client_cfg;
extern char* unifyfs_mount_prefix;

#endif /* __UNIFYFS_SHELL_H */

