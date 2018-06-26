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

/*  Copyright (c) 2018 - Michael J. Brim
 *
 *  Configurator is part of https://github.com/MichaelBrim/tedium
 *
 *  MIT License - See LICENSE.tedium
 */

#ifndef _UNIFYCR_CONFIGURATOR_H_
#define _UNIFYCR_CONFIGURATOR_H_

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

#include "unifycr_const.h"

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

/* UNIFYCR_CONFIGS is the list of configuration settings, and should contain
   one macro definition per setting */
#define UNIFYCR_CONFIGS \
    UNIFYCR_CFG_CLI(unifycr, configfile, STRING, SYSCONFDIR/unifycr/unifycr.conf, "path to configuration file", configurator_file_check, 'C', "specify full path to config file") \
    UNIFYCR_CFG_CLI(unifycr, consistency, STRING, LAMINATED, "consistency model", NULL, 'c', "specify consistency model (NONE | LAMINATED | POSIX)") \
    UNIFYCR_CFG_CLI(unifycr, debug, BOOL, off, "enable debug output", NULL, 'd', "on|off") \
    UNIFYCR_CFG_CLI(unifycr, mountpoint, STRING, /unifycr, "mountpoint directory", NULL, 'm', "specify full path to desired mountpoint") \
    UNIFYCR_CFG(client, max_files, INT, UNIFYCR_MAX_FILES, "client max file count", NULL) \
    UNIFYCR_CFG_CLI(log, verbosity, INT, 0, "log verbosity level", NULL, 'v', "specify logging verbosity level") \
    UNIFYCR_CFG_CLI(log, file, STRING, unifycrd.log, "log file name", NULL, 'l', "specify log file name") \
    UNIFYCR_CFG_CLI(log, dir, STRING, LOGDIR, "log file directory", configurator_directory_check, 'L', "specify full path to directory to contain log file") \
    UNIFYCR_CFG(logfs, index_buf_size, INT, UNIFYCR_INDEX_BUF_SIZE, "log file system index buffer size", NULL) \
    UNIFYCR_CFG(logfs, attr_buf_size, INT, UNIFYCR_FATTR_BUF_SIZE, "log file system file attributes buffer size", NULL) \
    UNIFYCR_CFG(meta, db_name, STRING, META_DEFAULT_DB_NAME, "metadata database name", NULL) \
    UNIFYCR_CFG(meta, db_path, STRING, META_DEFAULT_DB_PATH, "metadata database path", NULL) \
    UNIFYCR_CFG(meta, server_ratio, INT, META_DEFAULT_SERVER_RATIO, "metadata server ratio", NULL) \
    UNIFYCR_CFG(meta, range_size, INT, META_DEFAULT_RANGE_SZ, "metadata range size", NULL) \
    UNIFYCR_CFG_CLI(runstate, dir, STRING, RUNDIR/unifycr, "runstate file directory", configurator_directory_check, 'R', "specify full path to directory to contain runstate file") \
    UNIFYCR_CFG(shmem, chunk_bits, INT, UNIFYCR_CHUNK_BITS, "shared memory data chunk size in bits (i.e., size=2^bits)", NULL) \
    UNIFYCR_CFG(shmem, chunk_mem, INT, UNIFYCR_CHUNK_MEM, "shared memory segment size for data chunks", NULL) \
    UNIFYCR_CFG(shmem, recv_size, INT, UNIFYCR_SHMEM_RECV_SIZE, "shared memory segment size in bytes for receiving data from delegators", NULL) \
    UNIFYCR_CFG(shmem, req_size, INT, UNIFYCR_SHMEM_REQ_SIZE, "shared memory segment size in bytes for sending requests to delegators", NULL) \
    UNIFYCR_CFG(shmem, single, BOOL, off, "use single shared memory region for all clients", NULL) \
    UNIFYCR_CFG(spillover, enabled, BOOL, on, "use local device for data chunk spillover", NULL) \
    UNIFYCR_CFG(spillover, data_dir, STRING, NULLSTRING, "spillover data directory", configurator_directory_check) \
    UNIFYCR_CFG(spillover, meta_dir, STRING, NULLSTRING, "spillover metadata directory", configurator_directory_check) \
    UNIFYCR_CFG(spillover, size, INT, UNIFYCR_SPILLOVER_SIZE, "spillover max data size in bytes", NULL) \

#ifdef __cplusplus
extern "C" {
#endif

/* unifycr_cfg_t struct */
typedef struct {
#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn) \
    char *sec##_##key;

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    char *sec##_##key;

#define UNIFYCR_CFG_MULTI(sec, key, typ, dv, desc, vfn, me) \
    char *sec##_##key[me]; \
    unsigned n_##sec##_##key;

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, dv, desc, vfn, me, opt, use) \
    char *sec##_##key[me]; \
    unsigned n_##sec##_##key;

    UNIFYCR_CONFIGS

#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI
} unifycr_cfg_t;

/* initialization and cleanup */

int unifycr_config_init(unifycr_cfg_t *cfg,
                        int argc,
                        char **argv);

int unifycr_config_fini(unifycr_cfg_t *cfg);


/* print configuration to specified file (or stderr if fp==NULL) */
void unifycr_config_print(unifycr_cfg_t *cfg,
                          FILE *fp);

/* print configuration in .INI format to specified file (or stderr) */
void unifycr_config_print_ini(unifycr_cfg_t *cfg,
                              FILE *inifp);

/* used internally, but may be useful externally */

int unifycr_config_set_defaults(unifycr_cfg_t *cfg);

void unifycr_config_cli_usage(char *arg0);
void unifycr_config_cli_usage_error(char *arg0,
                                    char *err_msg);

int unifycr_config_process_cli_args(unifycr_cfg_t *cfg,
                                    int argc,
                                    char **argv);

int unifycr_config_process_environ(unifycr_cfg_t *cfg);

int unifycr_config_process_ini_file(unifycr_cfg_t *cfg,
                                    const char *file);


int unifycr_config_validate(unifycr_cfg_t *cfg);

/* validate function prototype
   -  Returns: 0 for valid input, non-zero otherwise.
   -  out_val: set this output parameter to specify an alternate value */
typedef int (*configurator_validate_fn)(const char *section,
                                        const char *key,
                                        const char *val,
                                        char **out_val);

/* predefined validation functions */
int configurator_bool_val(const char *val,
                          bool *b);
int configurator_bool_check(const char *section,
                            const char *key,
                            const char *val,
                            char **oval);

int configurator_float_val(const char *val,
                           double *d);
int configurator_float_check(const char *section,
                             const char *key,
                             const char *val,
                             char **oval);

int configurator_int_val(const char *val,
                         long *l);
int configurator_int_check(const char *section,
                           const char *key,
                           const char *val,
                           char **oval);

int configurator_file_check(const char *section,
                            const char *key,
                            const char *val,
                            char **oval);

int configurator_directory_check(const char *section,
                                 const char *key,
                                 const char *val,
                                 char **oval);


#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYCR_CONFIGURATOR_H */
