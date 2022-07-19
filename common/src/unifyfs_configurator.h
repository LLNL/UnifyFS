/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

/*  Copyright (c) 2018 - Michael J. Brim
 *
 *  Configurator is part of https://github.com/MichaelBrim/tedium
 *
 *  MIT License - See LICENSE.tedium
 */

#ifndef _UNIFYFS_CONFIGURATOR_H_
#define _UNIFYFS_CONFIGURATOR_H_

/* Configurator unifies config files, environment variables, and command-line
 * arguments into a set of simple preprocessor definitions that capture the
 * necessary info.
 *
 * See README.md for instructions on usage.
 */

// need bool, NULL, FILE*
#ifdef __cplusplus
# include <climits>
# include <cstddef>
# include <cstdio>
#else
# include <limits.h>
# include <stdbool.h>
# include <stddef.h>
# include <stdio.h>
#endif

#include "unifyfs_const.h"

#ifndef TMPDIR
#define TMPDIR /tmp
#endif

#ifndef RUNDIR
#define RUNDIR /var/tmp // NOTE: typically user-writable, /var/run is not
#endif

#ifndef SYSCONFDIR
#define SYSCONFDIR /etc
#endif

#ifndef LOGDIR
#define LOGDIR TMPDIR
#endif

// NOTE: NULLSTRING is a sentinel token meaning "no default string value"

/* UNIFYFS_CONFIGS is the list of configuration settings, and should contain
   one macro definition per setting */
#define UNIFYFS_CONFIGS \
    UNIFYFS_CFG_CLI(unifyfs, cleanup, BOOL, off, "cleanup storage on server exit", NULL, 'C', "on|off") \
    UNIFYFS_CFG_CLI(unifyfs, configfile, STRING, /etc/unifyfs.conf, "path to configuration file", configurator_file_check, 'f', "specify full path to config file") \
    UNIFYFS_CFG_CLI(unifyfs, consistency, STRING, LAMINATED, "consistency model", NULL, 'c', "specify consistency model (NONE | LAMINATED | POSIX)") \
    UNIFYFS_CFG_CLI(unifyfs, daemonize, BOOL, off, "enable server daemonization", NULL, 'D', "on|off") \
    UNIFYFS_CFG_CLI(unifyfs, mountpoint, STRING, /unifyfs, "mountpoint directory", NULL, 'm', "specify full path to desired mountpoint") \
    UNIFYFS_CFG(client, cwd, STRING, NULLSTRING, "current working directory", NULL) \
    UNIFYFS_CFG(client, fsync_persist, BOOL, on, "persist written data to storage on fsync()", NULL) \
    UNIFYFS_CFG(client, local_extents, BOOL, off, "use client-cached extents to service local reads without consulting local server", NULL) \
    UNIFYFS_CFG(client, node_local_extents, BOOL, off, \
        "use node-local extents to service node-local reads", NULL) \
    UNIFYFS_CFG(client, max_files, INT, UNIFYFS_CLIENT_MAX_FILES, "client max file count", NULL) \
    UNIFYFS_CFG(client, write_index_size, INT, UNIFYFS_CLIENT_WRITE_INDEX_SIZE, "write metadata index buffer size", NULL) \
    UNIFYFS_CFG(client, write_sync, BOOL, off, "sync every write to server", NULL) \
    UNIFYFS_CFG(client, super_magic, BOOL, on, "return UnifyFS super magic from statfs, TMPFS otherwise", NULL) \
    UNIFYFS_CFG_CLI(log, verbosity, INT, 0, "log verbosity level", NULL, 'v', "specify logging verbosity level") \
    UNIFYFS_CFG_CLI(log, file, STRING, unifyfsd.log, "log file name", NULL, 'l', "specify log file name") \
    UNIFYFS_CFG_CLI(log, dir, STRING, LOGDIR, "log file directory", configurator_directory_check, 'L', "specify full path to directory to contain log file") \
    UNIFYFS_CFG(log, on_error, BOOL, off, "turn on verbose logging when an error is encountered", NULL) \
    UNIFYFS_CFG(logio, chunk_size, INT, UNIFYFS_LOGIO_CHUNK_SIZE, "log-based I/O data chunk size", NULL) \
    UNIFYFS_CFG(logio, shmem_size, INT, UNIFYFS_LOGIO_SHMEM_SIZE, "log-based I/O shared memory region size", NULL) \
    UNIFYFS_CFG(logio, spill_size, INT, UNIFYFS_LOGIO_SPILL_SIZE, "log-based I/O spillover file size", NULL) \
    UNIFYFS_CFG(logio, spill_dir, STRING, NULLSTRING, "spillover directory", configurator_directory_check) \
    UNIFYFS_CFG(margo, client_pool_size, INT, UNIFYFS_MARGO_POOL_SZ, "size of server's ULT pool for client-server RPCs", NULL) \
    UNIFYFS_CFG(margo, lazy_connect, BOOL, on, "wait until first communication with server to resolve its connection address", NULL) \
    UNIFYFS_CFG(margo, server_pool_size, INT, UNIFYFS_MARGO_POOL_SZ, "size of server's ULT pool for server-server RPCs", NULL) \
    UNIFYFS_CFG(margo, tcp, BOOL, on, "use TCP for server-to-server margo RPCs", NULL) \
    UNIFYFS_CFG(meta, range_size, INT, UNIFYFS_META_DEFAULT_SLICE_SZ, "metadata range size", NULL) \
    UNIFYFS_CFG_CLI(runstate, dir, STRING, RUNDIR, "runstate file directory", configurator_directory_check, 'R', "specify full path to directory to contain server-local state") \
    UNIFYFS_CFG_CLI(server, hostfile, STRING, NULLSTRING, "server hostfile name", NULL, 'H', "specify full path to server hostfile") \
    UNIFYFS_CFG_CLI(server, init_timeout, INT, UNIFYFS_DEFAULT_INIT_TIMEOUT, "timeout of waiting for server initialization", NULL, 't', "timeout in seconds to wait for servers to be ready for clients") \
    UNIFYFS_CFG(server, local_extents, BOOL, off, "use server-cached extents to service local reads without consulting file owner", NULL) \
    UNIFYFS_CFG(server, max_app_clients, INT, UNIFYFS_SERVER_MAX_APP_CLIENTS, "maximum number of clients per application", NULL) \
    UNIFYFS_CFG_CLI(sharedfs, dir, STRING, NULLSTRING, "shared file system directory", configurator_directory_check, 'S', "specify full path to directory to contain server shared files") \

#ifdef __cplusplus
extern "C" {
#endif

/* UnifyFS config option struct (key-value pair) */
typedef struct unifyfs_config_option {
    const char* opt_name;
    const char* opt_value;
} unifyfs_cfg_option;

typedef enum {
    INVALID_PROCESS_TYPE = 0,
    UNIFYFS_CLIENT = 1,
    UNIFYFS_SERVER = 2
} unifyfs_proc_type_e;

/* unifyfs_cfg_t struct */
typedef struct {
    unifyfs_proc_type_e ptype;

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn) \
    char *sec##_##key;

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    char *sec##_##key;

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

} unifyfs_cfg_t;

/* initialization and cleanup */

int unifyfs_config_init(unifyfs_cfg_t* cfg,
                        int argc, char** argv,
                        int nopt, unifyfs_cfg_option* options);

int unifyfs_config_fini(unifyfs_cfg_t *cfg);


/* print configuration to specified file (or stderr if fp==NULL) */
void unifyfs_config_print(unifyfs_cfg_t* cfg,
                          FILE* fp);

/* print configuration in .INI format to specified file (or stderr) */
void unifyfs_config_print_ini(unifyfs_cfg_t* cfg,
                              FILE* inifp);

/* used internally, but may be useful externally */

int unifyfs_config_set_defaults(unifyfs_cfg_t* cfg);

void unifyfs_config_cli_usage(char* arg0);
void unifyfs_config_cli_usage_error(char* arg0,
                                    char* err_msg);

int unifyfs_config_process_cli_args(unifyfs_cfg_t* cfg,
                                    int argc,
                                    char** argv);

int unifyfs_config_process_environ(unifyfs_cfg_t* cfg);

int unifyfs_config_process_ini_file(unifyfs_cfg_t* cfg,
                                    const char* file);

int unifyfs_config_process_option(unifyfs_cfg_t* cfg,
                                  const char* opt_name,
                                  const char* opt_val);

int unifyfs_config_process_options(unifyfs_cfg_t* cfg,
                                   int nopt,
                                   unifyfs_cfg_option* options);

int unifyfs_config_get_options(unifyfs_cfg_t* cfg,
                               int* nopt,
                               unifyfs_cfg_option** options);

int unifyfs_config_validate(unifyfs_cfg_t* cfg);

/* validate function prototype
   -  Returns: 0 for valid input, non-zero otherwise.
   -  out_val: set this output parameter to specify an alternate value */
typedef int (*configurator_validate_fn)(const char* section,
                                        const char* key,
                                        const char* val,
                                        char** out_val);

/* predefined validation functions */
int configurator_bool_val(const char* val,
                          bool* b);
int configurator_bool_check(const char* section,
                            const char* key,
                            const char* val,
                            char** oval);

int configurator_float_val(const char* val,
                           double* d);
int configurator_float_check(const char* section,
                             const char* key,
                             const char* val,
                             char** oval);

int configurator_int_val(const char* val,
                         long* l);
int configurator_int_check(const char* section,
                           const char* key,
                           const char* val,
                           char** oval);

int configurator_file_check(const char* section,
                            const char* key,
                            const char* val,
                            char** oval);

int configurator_directory_check(const char* section,
                                 const char* key,
                                 const char* val,
                                 char** oval);


#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYFS_CONFIGURATOR_H */
