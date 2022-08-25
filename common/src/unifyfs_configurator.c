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

#ifdef __cplusplus
# include <cassert>
# include <cctype>
# include <cerrno>
# include <cstddef>
# include <cstdlib>
# include <cstring>
#else
# include <assert.h>
# include <ctype.h>
# include <errno.h>
# include <stddef.h>
# include <stdlib.h>
# include <string.h>
#endif

#include <getopt.h>   // getopt_long()
#include <sys/stat.h> // stat()
#include <unistd.h>

#include "ini.h"
#include "tinyexpr.h"
#include "unifyfs_configurator.h"

#define UNIFYFS_CFG_MAX_MSG 1024

#define stringify_indirect(x) #x
#define stringify(x) stringify_indirect(x)


// initialize configuration using all available methods
int unifyfs_config_init(unifyfs_cfg_t* cfg,
                        int argc, char** argv,
                        int nopt, unifyfs_cfg_option* options)
{
    int rc;
    char *syscfg = NULL;

    if (cfg == NULL)
        return EINVAL;

    memset((void*)cfg, 0, sizeof(unifyfs_cfg_t));

    // set default configuration
    rc = unifyfs_config_set_defaults(cfg);
    if (rc)
        return rc;

    // process system config file (if available)
    syscfg = cfg->unifyfs_configfile;
    rc = configurator_file_check(NULL, NULL, syscfg, NULL);
    if (rc == 0) {
        rc = unifyfs_config_process_ini_file(cfg, syscfg);
        if (rc)
            return rc;
    }
    if (syscfg != NULL)
        free(syscfg);
    cfg->unifyfs_configfile = NULL;

    // process environment (overrides defaults and system config)
    rc = unifyfs_config_process_environ(cfg);
    if (rc) {
        return rc;
    }

    // process options array (overrides all previous)
    rc = unifyfs_config_process_options(cfg, nopt, options);
    if (rc) {
        return rc;
    }

    // process command-line args (overrides all previous)
    rc = unifyfs_config_process_cli_args(cfg, argc, argv);
    if (rc) {
        return rc;
    }

    // read config file passed on command-line (does not override cli args)
    if (cfg->unifyfs_configfile != NULL) {
        rc = unifyfs_config_process_ini_file(cfg, cfg->unifyfs_configfile);
        if (rc) {
            return rc;
        }
    }

    // validate settings
    rc = unifyfs_config_validate(cfg);
    if (rc) {
        return rc;
    }

    return (int)UNIFYFS_SUCCESS;
}

// cleanup allocated state
int unifyfs_config_fini(unifyfs_cfg_t* cfg)
{
    if (cfg == NULL) {
        return EINVAL;
    }

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)       \
    if (cfg->sec##_##key != NULL) {                     \
        free(cfg->sec##_##key);                         \
        cfg->sec##_##key = NULL;                        \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    if (cfg->sec##_##key != NULL) {                             \
        free(cfg->sec##_##key);                                 \
        cfg->sec##_##key = NULL;                                \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    return (int)UNIFYFS_SUCCESS;
}

// print configuration to specified file (or stderr)
void unifyfs_config_print(unifyfs_cfg_t* cfg,
                          FILE* fp)
{
    char msg[UNIFYFS_CFG_MAX_MSG];

    if (fp == NULL) {
        fp = stderr;
    }

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)                       \
    if (cfg->sec##_##key != NULL) {                                     \
        snprintf(msg, sizeof(msg), "UNIFYFS CONFIG: %s.%s = %s",        \
                 #sec, #key, cfg->sec##_##key);                         \
        fprintf(fp, "%s\n", msg);                                       \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    if (cfg->sec##_##key != NULL) {                                     \
        snprintf(msg, sizeof(msg), "UNIFYFS CONFIG: %s.%s = %s",        \
                 #sec, #key, cfg->sec##_##key);                         \
        fprintf(fp, "%s\n", msg);                                       \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    fflush(fp);
}

// print configuration in .ini format to specified file (or stderr)
void unifyfs_config_print_ini(unifyfs_cfg_t* cfg,
                              FILE* inifp)
{
    const char* curr_sec = NULL;
    const char* last_sec = NULL;

    if (inifp == NULL)
        inifp = stderr;

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)                       \
    if (cfg->sec##_##key != NULL) {                                     \
        curr_sec = #sec;                                                \
        if ((last_sec == NULL) || (strcmp(curr_sec, last_sec) != 0))    \
            fprintf(inifp, "\n[%s]\n", curr_sec);                       \
        fprintf(inifp, "%s = %s\n", #key, cfg->sec##_##key);            \
        last_sec = curr_sec;                                            \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    if (cfg->sec##_##key != NULL) {                                     \
        curr_sec = #sec;                                                \
        if ((last_sec == NULL) || (strcmp(curr_sec, last_sec) != 0))    \
            fprintf(inifp, "\n[%s]\n", curr_sec);                       \
        fprintf(inifp, "%s = %s\n", #key, cfg->sec##_##key);            \
        last_sec = curr_sec;                                            \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    fflush(inifp);
}

// set default values given in UNIFYFS_CONFIGS
int unifyfs_config_set_defaults(unifyfs_cfg_t* cfg)
{
    char* val;

    if (cfg == NULL) {
        return EINVAL;
    }

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)       \
    val = stringify(dv);                                \
    if (0 != strcmp(val, "NULLSTRING"))                 \
        cfg->sec##_##key = strdup(val);

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    val = stringify(dv);                                        \
    if (0 != strcmp(val, "NULLSTRING"))                         \
        cfg->sec##_##key = strdup(val);

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    return (int)UNIFYFS_SUCCESS;
}


// utility routine to print CLI usage (and optional usage error message)
void unifyfs_config_cli_usage(char* arg0)
{
    fprintf(stderr, "USAGE: %s [options]\n", arg0);

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    fprintf(stderr, "    -%c,--%s-%s <%s>\t%s (default value: %s)\n",   \
            opt, #sec, #key, #typ, use, stringify(dv));

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    fflush(stderr);
}

// print usage error message
void unifyfs_config_cli_usage_error(char* arg0,
                                    char* err_msg)
{
    if (err_msg != NULL) {
        fprintf(stderr, "USAGE ERROR: %s : %s\n\n", arg0, err_msg);
    }

    unifyfs_config_cli_usage(arg0);
}


static struct option cli_options[] = {
#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)
#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    { #sec "-" #key, required_argument, NULL, opt },

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    { NULL, 0, NULL, 0 }
};

// update config struct based on command line args
int unifyfs_config_process_cli_args(unifyfs_cfg_t* cfg,
                                    int argc,
                                    char** argv)
{
    int rc, c;
    int usage_err = 0;
    int ondx = 0;
    int sndx = 0;
    char errmsg[UNIFYFS_CFG_MAX_MSG];
    char short_opts[256];
    extern char* optarg;
    extern int optind, optopt;

    if (cfg == NULL) {
        return EINVAL;
    }

    // setup short_opts and cli_options
    memset((void*)short_opts, 0, sizeof(short_opts));
    short_opts[sndx++] = ':'; // report missing args

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    short_opts[sndx++] = opt;                                   \
    if (strcmp(#typ, "BOOL") == 0) {                            \
        short_opts[sndx++] = ':';                               \
        short_opts[sndx++] = ':';                               \
        cli_options[ondx++].has_arg = optional_argument;        \
    }                                                           \
    else {                                                      \
        short_opts[sndx++] = ':';                               \
        cli_options[ondx++].has_arg = required_argument;        \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    //fprintf(stderr, "UNIFYFS CONFIG DEBUG: short-opts '%s'\n", short_opts);

    // process argv
    while ((c = getopt_long(argc, argv, short_opts, cli_options, NULL)) != -1) {
        switch (c) {

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
        case opt: {                                             \
            if (optarg) {                                       \
                if (cfg->sec##_##key != NULL)                   \
                    free(cfg->sec##_##key);                     \
                cfg->sec##_##key = strdup(optarg);              \
            }                                                   \
            else if (strcmp(#typ, "BOOL") == 0) {               \
                if (cfg->sec##_##key != NULL)                   \
                    free(cfg->sec##_##key);                     \
                cfg->sec##_##key = strdup("on");                \
            }                                                   \
            break;                                              \
        }

        UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI


        case ':':
            usage_err = 1;
            snprintf(errmsg, sizeof(errmsg),
                     "CLI option -%c requires operand", optopt);
            break;
        case '?':
            usage_err = 1;
            snprintf(errmsg, sizeof(errmsg),
                     "unknown CLI option -%c", optopt);
            break;
        default:
            // should not reach here
            fprintf(stderr, "UNIFYFS CONFIG DEBUG: unhandled option '%s'\n", optarg);
            break;
        }
        if (usage_err)
            break;
    }

    if (!usage_err)
        rc = (int)UNIFYFS_SUCCESS;
    else {
        rc = (int)UNIFYFS_FAILURE;
        unifyfs_config_cli_usage_error(argv[0], errmsg);
    }

    return rc;
}

// helper to check environment variable
char* getenv_helper(const char* section,
                    const char* key,
                    unsigned mentry)
{
    static char envname[256];
    unsigned u;
    size_t len;
    size_t ndx = 0;

    memset((void*)envname, 0, sizeof(envname));


    ndx += sprintf(envname, "UNIFYFS_");

    if (strcmp(section, "unifyfs") != 0) {
        len = strlen(section);
        for (u = 0; u < len; u++)
            envname[ndx + u] = toupper(section[u]);
        ndx += len;
        envname[ndx++] = '_';
    }

    len = strlen(key);
    for (u = 0; u < len; u++)
        envname[ndx + u] = toupper(key[u]);
    ndx += len;

    if (mentry)
        ndx += sprintf(envname + ndx, "_%u", mentry);

    //fprintf(stderr, "UNIFYFS CONFIG DEBUG: checking env var %s\n", envname);
    char* val = getenv(envname);
    return val;
}


// update config struct based on environment variables
int unifyfs_config_process_environ(unifyfs_cfg_t* cfg)
{
    char* envval;

    if (cfg == NULL) {
        return EINVAL;
    }


#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)       \
    envval = getenv_helper(#sec, #key, 0);              \
    if (envval != NULL) {                               \
        if (cfg->sec##_##key != NULL)                   \
            free(cfg->sec##_##key);                     \
        cfg->sec##_##key = strdup(envval);              \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    envval = getenv_helper(#sec, #key, 0);                      \
    if (envval != NULL) {                                       \
        if (cfg->sec##_##key != NULL)                           \
            free(cfg->sec##_##key);                             \
        cfg->sec##_##key = strdup(envval);                      \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    return (int)UNIFYFS_SUCCESS;
}

// inih callback handler
int inih_config_handler(void* user,
                        const char* section,
                        const char* kee,
                        const char* val)
{
    char* curval;
    char* defval;
    unifyfs_cfg_t* cfg = (unifyfs_cfg_t*) user;
    assert(cfg != NULL);

    // if not already set by CLI args, set cfg cfgs
    if (0) {
        ;
    }

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)                       \
    else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
        curval = cfg->sec##_##key;                                      \
        defval = stringify(dv);                                         \
        if (curval == NULL)                                             \
            cfg->sec##_##key = strdup(val);                             \
        else if (strcmp(defval, curval) == 0) {                         \
            free(cfg->sec##_##key);                                     \
            cfg->sec##_##key = strdup(val);                             \
        }                                                               \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
        curval = cfg->sec##_##key;                                      \
        defval = stringify(dv);                                         \
        if (curval == NULL)                                             \
            cfg->sec##_##key = strdup(val);                             \
        else if (strcmp(defval, curval) == 0) {                         \
            free(cfg->sec##_##key);                                     \
            cfg->sec##_##key = strdup(val);                             \
        }                                                               \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI


    return 1;
}

// update config struct based on config file, using inih
int unifyfs_config_process_ini_file(unifyfs_cfg_t* cfg,
                                    const char* file)
{
    int rc, inih_rc;
    char errmsg[UNIFYFS_CFG_MAX_MSG];

    if (cfg == NULL) {
        return EINVAL;
    }

    if (file == NULL) {
        return EINVAL;
    }

    inih_rc = ini_parse(file, inih_config_handler, cfg);
    switch (inih_rc) {
    case 0:
        rc = (int)UNIFYFS_SUCCESS;
        break;
    case -1:
        snprintf(errmsg, sizeof(errmsg),
                 "failed to open config file %s",
                 file);
        fprintf(stderr, "UNIFYFS CONFIG ERROR: %s\n", errmsg);
        rc = ENOENT;
        break;
    case -2:
        snprintf(errmsg, sizeof(errmsg),
                 "failed to parse config file %s",
                 file);
        fprintf(stderr, "UNIFYFS CONFIG ERROR: %s\n", errmsg);
        rc = ENOMEM;
        break;
    default:
        /* > 0  indicates parse error at line */
        if (inih_rc > 0)
            snprintf(errmsg, sizeof(errmsg),
                     "parse error at line %d of config file %s",
                     inih_rc, file);
        else
            snprintf(errmsg, sizeof(errmsg),
                     "failed to parse config file %s",
                     file);
        rc = (int)UNIFYFS_ERROR_BADCONFIG;
        fprintf(stderr, "UNIFYFS CONFIG ERROR: %s\n", errmsg);
        break;
    }

    return rc;
}

// update config struct based on option key-value pair
int unifyfs_config_process_option(unifyfs_cfg_t* cfg,
                                  const char* opt_name,
                                  const char* opt_val)
{
    if ((NULL == cfg) || (NULL == opt_name) || (NULL == opt_val)) {
        return EINVAL;
    }

    int rc = UNIFYFS_SUCCESS;
    char errmsg[UNIFYFS_CFG_MAX_MSG];
    char* curval;

    // split option name into section and key
    char* section = NULL;
    char* kee = NULL;
    char* name_copy = strdup(opt_name);
    char* period = strchr(name_copy, '.');
    if (NULL == period) {
        rc = EINVAL;
    } else {
        *period = '\0';
        section = name_copy;
        kee = period + 1;
        if ((0 == strlen(section)) ||
            (0 == strlen(kee))) {
            rc = EINVAL;
        }
    }
    if (rc != UNIFYFS_SUCCESS) {
        snprintf(errmsg, sizeof(errmsg),
                 "option %s has invalid format - expected '<section>.<key>'",
                 opt_name);
        fprintf(stderr, "UNIFYFS CONFIG ERROR: %s\n", errmsg);
    } else {
        // set config for given option (overwrites existing values)
        if (0) {
            ;
        }

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)                            \
        else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
            curval = cfg->sec##_##key;                                       \
            if (curval == NULL)                                              \
                cfg->sec##_##key = strdup(opt_val);                          \
            else {                                                           \
                free(cfg->sec##_##key);                                      \
                cfg->sec##_##key = strdup(opt_val);                          \
            }                                                                \
        }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)              \
        else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
            curval = cfg->sec##_##key;                                       \
            if (curval == NULL)                                              \
                cfg->sec##_##key = strdup(opt_val);                          \
            else {                                                           \
                free(cfg->sec##_##key);                                      \
                cfg->sec##_##key = strdup(opt_val);                          \
            }                                                                \
        }

        UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    }

    free(name_copy);
    return rc;
}

int unifyfs_config_process_options(unifyfs_cfg_t* cfg,
                                   int nopt,
                                   unifyfs_cfg_option* options)
{
    if (nopt > 0) {
        if ((NULL == cfg) || (NULL == options)) {
            return EINVAL;
        }
        for (int i = 0; i < nopt; i++) {
            unifyfs_cfg_option* opt = options + i;
            int rc = unifyfs_config_process_option(cfg,
                                                   opt->opt_name,
                                                   opt->opt_value);
            if (rc) {
                return rc;
            }
        }
    }
    return UNIFYFS_SUCCESS;
}

int unifyfs_config_get_options(unifyfs_cfg_t* cfg,
                               int* nopt,
                               unifyfs_cfg_option** options)
{
    if ((NULL == cfg) || (NULL == nopt) || (NULL == options)) {
        return EINVAL;
    }

    *nopt = 0;
    *options = NULL;

    /* first, count the non-NULL settings */
    int num_set = 0;

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)                       \
    if (cfg->sec##_##key != NULL) {                                     \
        num_set++;                                                      \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    if (cfg->sec##_##key != NULL) {                                     \
        num_set++;                                                      \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    /* now, allocate and fill the options array */
    unifyfs_cfg_option* opts = calloc(num_set, sizeof(unifyfs_cfg_option));
    if (NULL == opts) {
        return ENOMEM;
    }

    int opt_ndx = 0;
    unifyfs_cfg_option* curr_opt;
    char kee[256];

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)                       \
    if (cfg->sec##_##key != NULL) {                                     \
        curr_opt = opts + opt_ndx;                                      \
        opt_ndx++;                                                      \
        snprintf(kee, sizeof(kee), "%s.%s", #sec, #key);                \
        curr_opt->opt_name = strdup(kee);                               \
        curr_opt->opt_value = strdup(cfg->sec##_##key);                 \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    if (cfg->sec##_##key != NULL) {                                     \
        curr_opt = opts + opt_ndx;                                      \
        opt_ndx++;                                                      \
        snprintf(kee, sizeof(kee), "%s.%s", #sec, #key);                \
        curr_opt->opt_name = strdup(kee);                               \
        curr_opt->opt_value = strdup(cfg->sec##_##key);                 \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI

    *nopt = num_set;
    *options = opts;
    return UNIFYFS_SUCCESS;
}

/* predefined validation functions */

// utility routine to validate a single value given function
int validate_value(const char* section,
                   const char* key,
                   const char* val,
                   const char* typ,
                   configurator_validate_fn vfn,
                   char** new_val)
{
    if (vfn != NULL) {
        return vfn(section, key, val, new_val);
    } else if (strcmp(typ, "BOOL") == 0) {
        return configurator_bool_check(section, key, val, NULL);
    } else if (strcmp(typ, "INT") == 0) {
        return configurator_int_check(section, key, val, new_val);
    } else if (strcmp(typ, "FLOAT") == 0) {
        return configurator_float_check(section, key, val, new_val);
    }
    return 0;
}


// validate configuration
int unifyfs_config_validate(unifyfs_cfg_t* cfg)
{
    int rc = (int)UNIFYFS_SUCCESS;
    int vrc;
    char* new_val = NULL;

    if (cfg == NULL) {
        return EINVAL;
    }

#define UNIFYFS_CFG(sec, key, typ, dv, desc, vfn)                       \
    vrc = validate_value(#sec, #key, cfg->sec##_##key, #typ, vfn, &new_val); \
    if (vrc) {                                                          \
        rc = vrc;                                                       \
        fprintf(stderr, "UNIFYFS CONFIG ERROR: value '%s' for %s.%s is INVALID %s\n", \
                cfg->sec##_##key, #sec, #key, #typ);                    \
    }                                                                   \
    else if (new_val != NULL) {                                         \
        if (cfg->sec##_##key != NULL)                                   \
            free(cfg->sec##_##key);                                     \
        cfg->sec##_##key = new_val;                                     \
        new_val = NULL;                                                 \
    }

#define UNIFYFS_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    vrc = validate_value(#sec, #key, cfg->sec##_##key, #typ, vfn, &new_val); \
    if (vrc) {                                                          \
        rc = vrc;                                                       \
        fprintf(stderr, "UNIFYFS CONFIG ERROR: value '%s' for %s.%s is INVALID %s\n", \
                cfg->sec##_##key, #sec, #key, #typ);                    \
    }                                                                   \
    else if (new_val != NULL) {                                         \
        if (cfg->sec##_##key != NULL)                                   \
            free(cfg->sec##_##key);                                     \
        cfg->sec##_##key = new_val;                                     \
        new_val = NULL;                                                 \
    }

#define UNIFYFS_CFG_MULTI(sec, key, typ, desc, vfn, me)                 \
    for (u = 0; u < me; u++) {                                          \
        vrc = validate_value(#sec, #key, cfg->sec##_##key[u], #typ, vfn, &new_val); \
        if (vrc) {                                                      \
            rc = vrc;                                                   \
            fprintf(stderr, "UNIFYFS CONFIG ERROR: value[%u] '%s' for %s.%s is INVALID %s\n", \
                    u+1, cfg->sec##_##key[u], #sec, #key, #typ);        \
        }                                                               \
        else if (new_val != NULL) {                                     \
            if (cfg->sec##_##key[u] != NULL)                            \
                free(cfg->sec##_##key[u]);                              \
            cfg->sec##_##key[u] = new_val;                              \
            new_val = NULL;                                             \
        }                                                               \
    }

#define UNIFYFS_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    for (u = 0; u < me; u++) {                                          \
        vrc = validate_value(#sec, #key, cfg->sec##_##key[u], #typ, vfn, &new_val); \
        if (vrc) {                                                      \
            rc = vrc;                                                   \
            fprintf(stderr, "UNIFYFS CONFIG ERROR: value[%u] '%s' for %s.%s is INVALID %s\n", \
                    u+1, cfg->sec##_##key[u], #sec, #key, #typ);        \
        }                                                               \
        else if (new_val != NULL) {                                     \
            if (cfg->sec##_##key[u] != NULL)                            \
                free(cfg->sec##_##key[u]);                              \
            cfg->sec##_##key[u] = new_val;                              \
            new_val = NULL;                                             \
        }                                                               \
    }

    UNIFYFS_CONFIGS
#undef UNIFYFS_CFG
#undef UNIFYFS_CFG_CLI
#undef UNIFYFS_CFG_MULTI
#undef UNIFYFS_CFG_MULTI_CLI

    return rc;
}

int contains_expression(const char* val)
{
    static char expr_chars[8] = {'(', ')', '+', '-', '*', '/', '%', '^'};
    size_t s;
    char c;
    char *match;
    for (s=0; s < sizeof(expr_chars); s++) {
        c = expr_chars[s];
        match = strchr(val, c);
        if (match != NULL) {
            if (((c == '+') || (c == '-')) && (match > val)) {
                // check for exponent notation
                c = *(--match);
                if ((c == 'e') || (c == 'E'))
                   return 0; // tinyexpr can't handle exponent notation
            }
            return 1;
        }
    }
    return 0;
}

int configurator_bool_val(const char* val,
                          bool* b)
{
    if ((val == NULL) || (b == NULL)) {
        return EINVAL;
    }

    if (1 == strlen(val)) {
        switch (val[0]) {
        case '0':
        case 'f':
        case 'n':
        case 'F':
        case 'N':
            *b = false;
            return 0;
        case '1':
        case 't':
        case 'y':
        case 'T':
        case 'Y':
            *b = true;
            return 0;
        default:
            return 1;
        }
    } else if ((strcmp(val, "no") == 0)
               || (strcmp(val, "off") == 0)
               || (strcmp(val, "false") == 0)) {
        *b = false;
        return 0;
    } else if ((strcmp(val, "yes") == 0)
               || (strcmp(val, "on") == 0)
               || (strcmp(val, "true") == 0)) {
        *b = true;
        return 0;
    }
    return EINVAL;
}

int configurator_bool_check(const char* s,
                            const char* k,
                            const char* val,
                            char** o)
{
    bool b;

    if (val == NULL) {
        // unset is OK
        return 0;
    }

    return configurator_bool_val(val, &b);
}

int configurator_float_val(const char* val,
                           double* d)
{
    int err;
    double check, teval;
    char* end = NULL;

    if ((val == NULL) || (d == NULL)) {
        return EINVAL;
    }

    if (contains_expression(val)) {
        err = 0;
        teval = te_interp(val, &err);
        if (err == 0)
            check = teval;
        else
            return EINVAL;
    } else {
        errno = 0;
        check = strtod(val, &end);
        err = errno;
        if ((err == ERANGE) || (end == val)) {
            return EINVAL;
        } else if (*end != 0) {
            switch (*end) {
            case 'f':
            case 'l':
            case 'F':
            case 'L':
                break;
            default:
                return EINVAL;
            }
        }
    }

    *d = check;
    return 0;
}

int configurator_float_check(const char* s,
                             const char* k,
                             const char* val,
                             char** o)
{
    int rc;
    size_t len;
    double d;
    char* newval = NULL;

    if (val == NULL) {
        // unset is OK
        return 0;
    }

    rc = configurator_float_val(val, &d);
    if ((o != NULL) && (rc == 0) && contains_expression(val)) {
        // update config setting to evaluated value
        len = strlen(val) + 1; // evaluated value should be shorter
        newval = (char*) calloc(len, sizeof(char));
        if (newval != NULL) {
            snprintf(newval, len, "%.6le", d);
            *o = newval;
        }
    }
    return rc;
}

int configurator_int_val(const char* val,
                         long* l)
{
    long check;
    double teval;
    int err;
    char* end = NULL;

    if ((val == NULL) || (l == NULL)) {
        return EINVAL;
    }

    if (contains_expression(val)) {
        err = 0;
        teval = te_interp(val, &err);
        if(err == 0)
            check = (long)teval;
        else
            return EINVAL;
    } else {
        errno = 0;
        check = strtol(val, &end, 0);
        err = errno;
        if ((err == ERANGE) || (end == val)) {
            return EINVAL;
        } else if (*end != 0) {
            switch (*end) {
            case 'l':
            case 'u':
            case 'L':
            case 'U':
                break;
            default:
                return EINVAL;
            }
        }
    }

    *l = check;
    return 0;
}

int configurator_int_check(const char* s,
                           const char* k,
                           const char* val,
                           char** o)
{
    int rc;
    size_t len;
    long l;
    char* newval = NULL;

    if (val == NULL) {
        // unset is OK
        return 0;
    }

    rc = configurator_int_val(val, &l);
    if ((o != NULL) && (rc == 0) && contains_expression(val)) {
        // update config setting to evaluated value
        len = strlen(val) + 1; // evaluated value should be shorter
        newval = (char*) calloc(len, sizeof(char));
        if (newval != NULL) {
            snprintf(newval, len, "%ld", l);
            *o = newval;
        }
    }
    return rc;
}

int configurator_file_check(const char* s,
                            const char* k,
                            const char* val,
                            char** o)
{
    int rc;
    struct stat st;

    if (val == NULL) {
        return 0;
    }

    rc = stat(val, &st);
    if (rc == 0) {
        if (st.st_mode & S_IFREG) {
            return 0;
        } else {
            return ENOENT;
        }
    }
    return errno; // invalid
}

int configurator_directory_check(const char* s,
                                 const char* k,
                                 const char* val,
                                 char** o)
{
    int mode, rc;
    struct stat st;

    if (val == NULL) {
        return 0;
    }

    // check dir exists
    rc = stat(val, &st);
    if (rc == 0) {
        if (st.st_mode & S_IFDIR) {
            return 0;
        } else {
            return ENOTDIR;
        }
    } else { // try to create it
        mode = 0770; // S_IRWXU | S_IRWXG
        rc = mkdir(val, mode);
        if (rc == 0) {
            return 0;
        } else {
            return errno; // invalid
        }
    }
}
