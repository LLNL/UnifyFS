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
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "toml.h"
#include "unifycr.h"

static const char *sysconf_file = CONFDIR "/unifycr.conf";

enum {
    CONF_ENTRY_STRING = 0,
    CONF_ENTRY_INT,
};

struct _conf_entry {
    char *name;
    char *val;
    int type;
};

typedef struct _conf_entry conf_entry_t;

struct _conf_section {
    char *title;
    conf_entry_t *entries;
    int (*read)(toml_table_t *tab, unifycr_sysconf_t *sysconf);
};

typedef struct _conf_section conf_section_t;

static conf_entry_t global_entries[] = {
    { "runstatedir", 0, CONF_ENTRY_STRING },
    { 0, 0, 0 },
};

static int global_read(toml_table_t *tab, unifycr_sysconf_t *sysconf)
{
    int ret = 0;
    const char *val = NULL;
    char *str = NULL;

    /* runstatedir */
    val = toml_raw_in(tab, "runstatedir");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->runstatedir = str;
    }

    /* unifycrd path */
    val = toml_raw_in(tab, "unifycrd_path");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->unifycrd_path = str;
    }

    return 0;
}

static conf_entry_t filesystem_entries[] = {
    { "mountpoint", 0, CONF_ENTRY_STRING },
    { "consistency", 0, CONF_ENTRY_STRING },
    { 0, 0, 0 },
};

static int filesystem_read(toml_table_t *tab, unifycr_sysconf_t *sysconf)
{
    int ret = 0;
    const char *val = NULL;
    char *str = NULL;

    /* mountpoint */
    val = toml_raw_in(tab, "mountpoint");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->mountpoint = str;
    }

    /* consistency */
    val = toml_raw_in(tab, "consistency");
    if (val) {
        unifycr_cm_t consistency = UNIFYCR_CM_INVALID;

        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        consistency = unifycr_read_consistency(str);

        if (consistency == UNIFYCR_CM_INVALID)
            return -EINVAL;

        sysconf->consistency = consistency;
    }

    return 0;
}

static conf_entry_t server_entries[] = {
    { "meta_server_ratio", 0, CONF_ENTRY_INT },
    { "meta_db_name", 0, CONF_ENTRY_STRING },
    { "meta_db_path", 0, CONF_ENTRY_STRING },
    { "server_debug_log_path", 0, CONF_ENTRY_STRING },
    { 0, 0, 0 },
};

static int server_read(toml_table_t *tab, unifycr_sysconf_t *sysconf)
{
    int ret = 0;
    int64_t intval = 0;
    const char *val = NULL;
    char *str = NULL;

    /* meta_server_ratio */
    val = toml_raw_in(tab, "meta_server_ratio");
    if (val) {
        ret = toml_rtoi(val, &intval);
        if (ret)
            return -errno;

        sysconf->meta_server_ratio = intval;
    }

    /* meta_db_name */
    val = toml_raw_in(tab, "meta_db_name");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->meta_db_name = str;
    }

    /* meta_db_path */
    val = toml_raw_in(tab, "meta_db_path");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->meta_db_path = str;
    }

    /* server_debug_log_path */
    val = toml_raw_in(tab, "server_debug_log_path");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->server_debug_log_path = str;
    }

    return 0;
}

static conf_entry_t client_entries[] = {
    { "chunk_mem", 0, CONF_ENTRY_INT },
    { "external_meta_dir", 0, CONF_ENTRY_STRING },
    { "external_string_dir", 0, CONF_ENTRY_STRING },
    { 0, 0, 0 },
};

static int client_read(toml_table_t *tab, unifycr_sysconf_t *sysconf)
{
    int ret = 0;
    int64_t intval = 0;
    const char *val = NULL;
    char *str = NULL;

    /* chunk_mem */
    val = toml_raw_in(tab, "chunk_mem");
    if (val) {
        ret = toml_rtoi(val, &intval);
        if (ret)
            return -errno;

        sysconf->chunk_mem = intval;
    }

    /* external_meta_dir */
    val = toml_raw_in(tab, "external_meta_dir");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->external_meta_dir = str;
    }

    /* external_data_dir */
    val = toml_raw_in(tab, "external_data_dir");
    if (val) {
        ret = toml_rtos(val, &str);
        if (ret)
            return -errno;

        sysconf->external_data_dir = str;
    }

    return 0;
}

static conf_section_t conf_sections[] = {
    { "global", global_entries, &global_read },
    { "filesystem", filesystem_entries, &filesystem_read },
    { "server", server_entries, &server_read },
    { "client", client_entries, &client_read },
    { 0, 0, 0 },
};

static int readconf(toml_table_t *curtab, unifycr_sysconf_t *sysconf)
{
    int ret = 0;
    toml_table_t *tab = NULL;
    conf_section_t *section = NULL;

    for (section = conf_sections; section->title; section++) {
        tab = toml_table_in(curtab, section->title);
        if (!tab)
            continue;

        ret = section->read(tab, sysconf);
        if (ret)
            return ret;
    }

    return 0;
}

int unifycr_read_sysconf(unifycr_sysconf_t *sysconf)
{
    int ret = 0;
    FILE *fp = NULL;
    char errbuf[1024] = { 0, };
    toml_table_t *tab = NULL;

    if (!sysconf)
        return -EINVAL;

    fp = fopen(sysconf_file, "r");
    if (!fp)
        return -errno;

    tab = toml_parse_file(fp, errbuf, sizeof(errbuf));
    if (!tab) {
        fprintf(stderr, "%s\n", errbuf);
        goto out_close;
    }

    ret = readconf(tab, sysconf);

    toml_free(tab);

out_close:
    fclose(fp);

    return ret;
}

