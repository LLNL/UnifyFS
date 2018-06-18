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
#include "unifycr_configurator.h"

#define UNIFYCR_CFG_MAX_MSG 1024

#define stringify_indirect(x) #x
#define stringify(x) stringify_indirect(x)


// initialize configuration using all available methods
int unifycr_config_init(unifycr_cfg_t *cfg,
                        int argc,
                        char **argv)
{
    int rc;
    char *syscfg = NULL;

    if (cfg == NULL)
        return -1;

    memset((void *)cfg, 0, sizeof(unifycr_cfg_t));

    // set default configuration
    rc = unifycr_config_set_defaults(cfg);
    if (rc)
        return rc;

    // process system config file (if available)
    syscfg = cfg->unifycr_configfile;
    rc = configurator_file_check(NULL, NULL, syscfg, NULL);
    if (rc == 0) {
        rc = unifycr_config_process_ini_file(cfg, syscfg);
        if (rc)
            return rc;
    }
    if (syscfg != NULL)
        free(syscfg);
    cfg->unifycr_configfile = NULL;

    // process environment (overrides defaults and system config)
    rc = unifycr_config_process_environ(cfg);
    if (rc)
        return rc;

    // process command-line args (overrides all previous)
    rc = unifycr_config_process_cli_args(cfg, argc, argv);
    if (rc)
        return rc;

    // read config file passed on command-line (does not override cli args)
    if (cfg->unifycr_configfile != NULL) {
        rc = unifycr_config_process_ini_file(cfg, cfg->unifycr_configfile);
        if (rc)
            return rc;
    }

    // validate settings
    rc = unifycr_config_validate(cfg);
    if (rc)
        return rc;

    return 0;
}

// cleanup allocated state
int unifycr_config_fini(unifycr_cfg_t *cfg)
{
    if (cfg == NULL)
        return -1;

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)       \
    if (cfg->sec##_##key != NULL) {                     \
        free(cfg->sec##_##key);                         \
        cfg->sec##_##key = NULL;                        \
    }

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    if (cfg->sec##_##key != NULL) {                             \
        free(cfg->sec##_##key);                                 \
        cfg->sec##_##key = NULL;                                \
    }

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me) \
    for (u = 0; u < me; u++) {                          \
        if (cfg->sec##_##key[u] != NULL) {              \
            free(cfg->sec##_##key[u]);                  \
            cfg->sec##_##key[u] = NULL;                 \
        }                                               \
    }                                                   \
    cfg->n_##sec##_##key = 0;

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    for (u = 0; u < me; u++) {                                          \
        if (cfg->sec##_##key[u] != NULL) {                              \
            free(cfg->sec##_##key[u]);                                  \
            cfg->sec##_##key[u] = NULL;                                 \
        }                                                               \
    }                                                                   \
    cfg->n_##sec##_##key = 0;

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    return 0;
}

// print configuration to specified file (or stderr)
void unifycr_config_print(unifycr_cfg_t *cfg,
                          FILE *fp)
{
    char msg[UNIFYCR_CFG_MAX_MSG];

    if (fp == NULL)
        fp = stderr;

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)                       \
    if (cfg->sec##_##key != NULL) {                                     \
        snprintf(msg, sizeof(msg), "UNIFYCR CONFIG: %s.%s = %s",        \
                 #sec, #key, cfg->sec##_##key);                         \
        fprintf(fp, "%s\n", msg);                                       \
    }

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    if (cfg->sec##_##key != NULL) {                                     \
        snprintf(msg, sizeof(msg), "UNIFYCR CONFIG: %s.%s = %s",        \
                 #sec, #key, cfg->sec##_##key);                         \
        fprintf(fp, "%s\n", msg);                                       \
    }

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)                 \
    for (u = 0; u < me; u++) {                                          \
        if (cfg->sec##_##key[u] != NULL) {                              \
            snprintf(msg, sizeof(msg), "UNIFYCR CONFIG: %s.%s[%u] = %s", \
                     #sec, #key, u+1, cfg->sec##_##key[u]);             \
            fprintf(fp, "%s\n", msg);                                   \
        }                                                               \
    }

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    for (u = 0; u < me; u++) {                                          \
        if (cfg->sec##_##key[u] != NULL) {                              \
            snprintf(msg, sizeof(msg), "UNIFYCR CONFIG: %s.%s[%u] = %s", \
                     #sec, #key, u+1, cfg->sec##_##key[u]);             \
            fprintf(fp, "%s\n", msg);                                   \
        }                                                               \
    }

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    fflush(fp);
}

// print configuration in .ini format to specified file (or stderr)
void unifycr_config_print_ini(unifycr_cfg_t *cfg,
                              FILE *inifp)
{
    const char *curr_sec = NULL;
    const char *last_sec = NULL;

    if (inifp == NULL)
        inifp = stderr;

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)                       \
    if (cfg->sec##_##key != NULL) {                                     \
        curr_sec = #sec;                                                \
        if ((last_sec == NULL) || (strcmp(curr_sec, last_sec) != 0))    \
            fprintf(inifp, "\n[%s]\n", curr_sec);                       \
        fprintf(inifp, "%s = %s\n", #key, cfg->sec##_##key);            \
        last_sec = curr_sec;                                            \
    }

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    if (cfg->sec##_##key != NULL) {                                     \
        curr_sec = #sec;                                                \
        if ((last_sec == NULL) || (strcmp(curr_sec, last_sec) != 0))    \
            fprintf(inifp, "\n[%s]\n", curr_sec);                       \
        fprintf(inifp, "%s = %s\n", #key, cfg->sec##_##key);            \
        last_sec = curr_sec;                                            \
    }

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)                 \
    for (u = 0; u < me; u++) {                                          \
        if (cfg->sec##_##key[u] != NULL) {                              \
            curr_sec = #sec;                                            \
            if ((last_sec == NULL) || (strcmp(curr_sec, last_sec) != 0)) \
                fprintf(inifp, "\n[%s]\n", curr_sec);                   \
            fprintf(inifp, "%s = %s ; (instance %u)\n",                 \
                    #key, cfg->sec##_##key[u], u+1);                    \
            last_sec = curr_sec;                                        \
        }                                                               \
    }

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    for (u = 0; u < me; u++) {                                          \
        if (cfg->sec##_##key[u] != NULL) {                              \
            curr_sec = #sec;                                            \
            if ((last_sec == NULL) || (strcmp(curr_sec, last_sec) != 0)) \
                fprintf(inifp, "\n[%s]\n", curr_sec);                   \
            fprintf(inifp, "%s = %s ; (instance %u)\n",                 \
                    #key, cfg->sec##_##key[u], u+1);                    \
            last_sec = curr_sec;                                        \
        }                                                               \
    }

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    fflush(inifp);
}

// set default values given in UNIFYCR_CONFIGS
int unifycr_config_set_defaults(unifycr_cfg_t *cfg)
{
    char *val;

    if (cfg == NULL)
        return -1;

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)       \
    val = stringify(dv);                                \
    if (0 != strcmp(val, "NULLSTRING"))                 \
        cfg->sec##_##key = strdup(val);

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    val = stringify(dv);                                        \
    if (0 != strcmp(val, "NULLSTRING"))                         \
        cfg->sec##_##key = strdup(val);

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)                 \
    cfg->n_##sec##_##key = 0;                                           \
    memset((void *)cfg->sec##_##key, 0, sizeof(cfg->sec##_##key));

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    cfg->n_##sec##_##key = 0;                                           \
    memset((void *)cfg->sec##_##key, 0, sizeof(cfg->sec##_##key));

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    return 0;
}


// utility routine to print CLI usage (and optional usage error message)
void unifycr_config_cli_usage(char *arg0)
{
    fprintf(stderr, "USAGE: %s [options]\n", arg0);

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    fprintf(stderr, "    -%c,--%s-%s <%s>\t%s (default value: %s)\n",   \
            opt, #sec, #key, #typ, use, stringify(dv));

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    fprintf(stderr, "    -%c,--%s-%s <%s>\t%s (multiple values supported - max %u entries)\n", \
            opt, #sec, #key, #typ, use, me);

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    fflush(stderr);
}

// print usage error message
void unifycr_config_cli_usage_error(char *arg0,
                                    char *err_msg)
{
    if (err_msg != NULL)
        fprintf(stderr, "USAGE ERROR: %s : %s\n\n", arg0, err_msg);

    unifycr_config_cli_usage(arg0);
}


static struct option cli_options[] = {
#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)
#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    { #sec "-" #key, required_argument, NULL, opt },
#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)
#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    { #sec "-" #key, required_argument, NULL, opt },
    UNIFYCR_CONFIGS
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI
    { NULL, 0, NULL, 0 }
};

// update config struct based on command line args
int unifycr_config_process_cli_args(unifycr_cfg_t *cfg,
                                    int argc,
                                    char **argv)
{
    int rc, c;
    int usage_err = 0;
    int ondx = 0;
    int sndx = 0;
    char errmsg[UNIFYCR_CFG_MAX_MSG];
    char short_opts[256];
    extern char *optarg;
    extern int optind, optopt;

    if (cfg == NULL)
        return -1;

    // setup short_opts and cli_options
    memset((void *)short_opts, 0, sizeof(short_opts));
    short_opts[sndx++] = ':'; // report missing args

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
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
#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    short_opts[sndx++] = opt;                                           \
    if (strcmp(#typ, "BOOL") == 0) {                                    \
        short_opts[sndx++] = ':';                                       \
        short_opts[sndx++] = ':';                                       \
        cli_options[ondx++].has_arg = optional_argument;                \
    }                                                                   \
    else {                                                              \
        short_opts[sndx++] = ':';                                       \
        cli_options[ondx++].has_arg = required_argument;                \
    }

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    //fprintf(stderr, "UNIFYCR CONFIG DEBUG: short-opts '%s'\n", short_opts);

    // process argv
    while ((c = getopt_long(argc, argv, short_opts, cli_options, NULL)) != -1) {
        switch (c) {

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
        case opt: {                                             \
            if (optarg)                                         \
                cfg->sec##_##key = strdup(optarg);              \
            else if (strcmp(#typ, "BOOL") == 0)                 \
                cfg->sec##_##key = strdup("on");                \
            break;                                              \
        }

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
        case opt: {                                                     \
            cfg->sec##_##key[cfg->n_##sec##_##key++] = strdup(optarg);  \
            break;                                                      \
        }

        UNIFYCR_CONFIGS
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

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
            fprintf(stderr, "UNIFYCR CONFIG DEBUG: unhandled option '%s'\n", optarg);
            break;
        }
        if (usage_err)
            break;
    }

    if (!usage_err)
        rc = 0;
    else {
        rc = -1;
        unifycr_config_cli_usage_error(argv[0], errmsg);
    }

    return rc;
}

// helper to check environment variable
char *getenv_helper(const char *section,
                    const char *key,
                    unsigned mentry)
{
    static char envname[256];
    unsigned u;
    size_t len;
    size_t ndx = 0;

    memset((void *)envname, 0, sizeof(envname));


    ndx += sprintf(envname, "UNIFYCR_");

    if (strcmp(section, "unifycr") != 0) {
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

    //fprintf(stderr, "UNIFYCR CONFIG DEBUG: checking env var %s\n", envname);
    return getenv(envname);
}


// update config struct based on environment variables
int unifycr_config_process_environ(unifycr_cfg_t *cfg)
{
    char *envval;

    if (cfg == NULL)
        return -1;


#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)       \
    envval = getenv_helper(#sec, #key, 0);              \
    if (envval != NULL)                                 \
        cfg->sec##_##key = strdup(envval);

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use) \
    envval = getenv_helper(#sec, #key, 0);                      \
    if (envval != NULL)                                         \
        cfg->sec##_##key = strdup(envval);

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me) \
    for (u = 0; u < me; u++) {                          \
        envval = getenv_helper(#sec, #key, u+1);        \
        if (envval != NULL) {                           \
            cfg->sec##_##key[u] = strdup(envval);       \
            cfg->n_##sec##_##key++;                     \
        }                                               \
    }

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    for (u = 0; u < me; u++) {                                          \
        envval = getenv_helper(#sec, #key, u+1);                        \
        if (envval != NULL) {                                           \
            cfg->sec##_##key[u] = strdup(envval);                       \
            cfg->n_##sec##_##key++;                                     \
        }                                                               \
    }

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    return 0;
}

// inih callback handler
int inih_config_handler(void *user,
                        const char *section,
                        const char *kee,
                        const char *val)
{
    char *curval;
    char *defval;
    unifycr_cfg_t *cfg = (unifycr_cfg_t *) user;
    assert(cfg != NULL);

    // if not already set by CLI args, set cfg cfgs
    if (0)
        ;

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)                       \
    else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
        curval = cfg->sec##_##key;                                      \
        defval = stringify(dv);                                         \
        if ((curval == NULL) || (strcmp(defval, curval) == 0))          \
            cfg->sec##_##key = strdup(val);                             \
    }

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
        curval = cfg->sec##_##key;                                      \
        defval = stringify(dv);                                         \
        if ((curval == NULL) || (strcmp(defval, curval) == 0))          \
            cfg->sec##_##key = strdup(val);                             \
    }

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)                 \
    else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
        cfg->sec##_##key[cfg->n_##sec##_##key++] = strdup(val);         \
    }

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    else if ((strcmp(section, #sec) == 0) && (strcmp(kee, #key) == 0)) { \
        cfg->sec##_##key[cfg->n_##sec##_##key++] = strdup(val);         \
    }

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    return 1;
}

// update config struct based on config file, using inih
int unifycr_config_process_ini_file(unifycr_cfg_t *cfg,
                                    const char *file)
{
    int rc, inih_rc;
    char errmsg[UNIFYCR_CFG_MAX_MSG];

    if (cfg == NULL)
        return EINVAL;

    if (file == NULL)
        return EINVAL;

    inih_rc = ini_parse(file, inih_config_handler, cfg);
    switch (inih_rc) {
    case 0:
        rc = 0;
        break;
    case -1:
        snprintf(errmsg, sizeof(errmsg),
                 "failed to open config file %s",
                 file);
        fprintf(stderr, "UNIFYCR CONFIG ERROR: %s\n", errmsg);
        rc = ENOENT;
        break;
    case -2:
        snprintf(errmsg, sizeof(errmsg),
                 "failed to parse config file %s",
                 file);
        fprintf(stderr, "UNIFYCR CONFIG ERROR: %s\n", errmsg);
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
        rc = EINVAL;
        fprintf(stderr, "UNIFYCR CONFIG ERROR: %s\n", errmsg);
        break;
    }

    return rc;
}


/* predefined validation functions */

// utility routine to validate a single value given function
int validate_value(const char *section,
                   const char *key,
                   const char *val,
                   const char *typ,
                   configurator_validate_fn vfn,
                   char **new_val)
{
    if (vfn != NULL)
        return vfn(section, key, val, new_val);
    else if (strcmp(typ, "BOOL") == 0)
        return configurator_bool_check(section, key, val, NULL);
    else if (strcmp(typ, "INT") == 0)
        return configurator_int_check(section, key, val, new_val);
    else if (strcmp(typ, "FLOAT") == 0)
        return configurator_float_check(section, key, val, new_val);

    return 0;
}


// validate configuration
int unifycr_config_validate(unifycr_cfg_t *cfg)
{
    int rc = 0;
    int vrc;
    char *new_val = NULL;

    if (cfg == NULL)
        return EINVAL;

#define UNIFYCR_CFG(sec, key, typ, dv, desc, vfn)                       \
    vrc = validate_value(#sec, #key, cfg->sec##_##key, #typ, vfn, &new_val); \
    if (vrc) {                                                          \
        rc = vrc;                                                       \
        fprintf(stderr, "UNIFYCR CONFIG ERROR: value '%s' for %s.%s is INVALID %s\n", \
                cfg->sec##_##key, #sec, #key, #typ);                    \
    } else if (new_val != NULL) {                                       \
        if (cfg->sec##_##key != NULL)                                   \
            free(cfg->sec##_##key);                                     \
        cfg->sec##_##key = new_val;                                     \
        new_val = NULL;                                                 \
    }

#define UNIFYCR_CFG_CLI(sec, key, typ, dv, desc, vfn, opt, use)         \
    vrc = validate_value(#sec, #key, cfg->sec##_##key, #typ, vfn, &new_val); \
    if (vrc) {                                                          \
        rc = vrc;                                                       \
        fprintf(stderr, "UNIFYCR CONFIG ERROR: value '%s' for %s.%s is INVALID %s\n", \
                cfg->sec##_##key, #sec, #key, #typ);                    \
    } else if (new_val != NULL) {                                       \
        if (cfg->sec##_##key != NULL)                                   \
            free(cfg->sec##_##key);                                     \
        cfg->sec##_##key = new_val;                                     \
        new_val = NULL;                                                 \
    }

#define UNIFYCR_CFG_MULTI(sec, key, typ, desc, vfn, me)                 \
    for (u = 0; u < me; u++) {                                          \
        vrc = validate_value(#sec, #key, cfg->sec##_##key[u], #typ, vfn, &new_val); \
        if (vrc) {                                                      \
            rc = vrc;                                                   \
            fprintf(stderr, "UNIFYCR CONFIG ERROR: value[%u] '%s' for %s.%s is INVALID %s\n", \
                    u+1, cfg->sec##_##key[u], #sec, #key, #typ);        \
        } else if (new_val != NULL) {                                   \
            if (cfg->sec##_##key[u] != NULL)                            \
                free(cfg->sec##_##key[u]);                              \
            cfg->sec##_##key[u] = new_val;                              \
            new_val = NULL;                                             \
        }                                                               \
    }

#define UNIFYCR_CFG_MULTI_CLI(sec, key, typ, desc, vfn, me, opt, use)   \
    for (u = 0; u < me; u++) {                                          \
        vrc = validate_value(#sec, #key, cfg->sec##_##key[u], #typ, vfn, &new_val); \
        if (vrc) {                                                      \
            rc = vrc;                                                   \
            fprintf(stderr, "UNIFYCR CONFIG ERROR: value[%u] '%s' for %s.%s is INVALID %s\n", \
                    u+1, cfg->sec##_##key[u], #sec, #key, #typ);        \
        } else if (new_val != NULL) {                                   \
            if (cfg->sec##_##key[u] != NULL)                            \
                free(cfg->sec##_##key[u]);                              \
            cfg->sec##_##key[u] = new_val;                              \
            new_val = NULL;                                             \
        }                                                               \
    }

    UNIFYCR_CONFIGS;
#undef UNIFYCR_CFG
#undef UNIFYCR_CFG_CLI
#undef UNIFYCR_CFG_MULTI
#undef UNIFYCR_CFG_MULTI_CLI

    return rc;
}

int contains_expression(const char *val)
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

int configurator_bool_val(const char *val,
                          bool *b)
{
    if ((val == NULL) || (b == NULL))
        return EINVAL;

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

int configurator_bool_check(const char *s,
                            const char *k,
                            const char *val,
                            char **o)
{
    bool b;

    if (val == NULL) // unset is OK
        return 0;

    return configurator_bool_val(val, &b);
}

int configurator_float_val(const char *val,
                           double *d)
{
    int err;
    double check, teval;
    char *end = NULL;

    if ((val == NULL) || (d == NULL))
        return EINVAL;

    if (contains_expression(val)) {
        err = 0;
        teval = te_interp(val, &err);
        if (err == 0)
            check = teval;
        else
            return EINVAL;
    }
    else {
        errno = 0;
        check = strtod(val, &end);
        err = errno;
        if ((err == ERANGE) || (end == val))
            return EINVAL;
        else if (*end != 0) {
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

int configurator_float_check(const char *s,
                             const char *k,
                             const char *val,
                             char **o)
{
    int rc;
    size_t len;
    double d;
    char *newval = NULL;

    if (val == NULL) // unset is OK
        return 0;

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

int configurator_int_val(const char *val,
                         long *l)
{
    long check;
    double teval;
    int err;
    char *end = NULL;

    if ((val == NULL) || (l == NULL))
        return EINVAL;

    if (contains_expression(val)) {
        err = 0;
        teval = te_interp(val, &err);
        if(err == 0)
            check = (long)teval;
        else
            return EINVAL;
    }
    else {
        errno = 0;
        check = strtol(val, &end, 0);
        err = errno;
        if ((err == ERANGE) || (end == val))
            return EINVAL;
        else if (*end != 0) {
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

int configurator_int_check(const char *s,
                           const char *k,
                           const char *val,
                           char **o)
{
    int rc;
    size_t len;
    long l;
    char *newval = NULL;

    if( NULL == val ) // unset is OK
        return 0;

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

int configurator_file_check(const char *s,
                            const char *k,
                            const char *val,
                            char **o)
{
    int rc;
    struct stat st;

    if (val == NULL)
        return 0;

    rc = stat(val, &st);
    if (rc == 0) {
        if (st.st_mode & S_IFREG)
            return 0;
        else
            return ENOENT;
    }
    return errno; // invalid
}

int configurator_directory_check(const char *s,
                                 const char *k,
                                 const char *val,
                                 char **o)
{
    int rc;
    struct stat st;

    if (val == NULL)
        return 0;

    rc = stat(val, &st);
    if (rc == 0) {
        if (st.st_mode & S_IFDIR)
            return 0;
        else
            return ENOTDIR;
    }
    return errno; // invalid
}
