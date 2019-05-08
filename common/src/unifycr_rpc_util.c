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

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include "unifycr_log.h"
#include "unifycr_pmix.h"
#include "unifycr_rpc_util.h"

#define LOCAL_RPC_ADDR_FILE "/tmp/unifycrd.margo-shm"

/* publishes client-server RPC address */
void rpc_publish_local_server_addr(const char* addr)
{
    LOGDBG("publishing client-server rpc address '%s'", addr);

#ifdef HAVE_PMIX_H
    // publish client-server margo address
    unifycr_pmix_publish(pmix_key_unifycrd_margo_shm, addr);
#endif

    /* write server address to local file for client to read */
    FILE* fp = fopen(LOCAL_RPC_ADDR_FILE, "w+");
    if (fp != NULL) {
        fprintf(fp, "%s", addr);
        fclose(fp);
    } else {
        LOGERR("Error writing server rpc addr file " LOCAL_RPC_ADDR_FILE);
    }
}

/* publishes server-server RPC address */
void rpc_publish_remote_server_addr(const char* addr)
{
    LOGDBG("publishing server-server rpc address '%s'", addr);

#ifdef HAVE_PMIX_H
    // publish client-server margo address
    unifycr_pmix_publish(pmix_key_unifycrd_margo_svr, addr);
#endif
}

/* lookup address of server, returns NULL if server address is not found,
 * otherwise returns server address in newly allocated string that caller
 * must free */
char* rpc_lookup_local_server_addr(void)
{
    /* returns NULL if we can't find server address */
    char* addr = NULL;

#ifdef HAVE_PMIX_H
    char* valstr = NULL;

    // lookup client-server margo address
    if (0 == unifycr_pmix_lookup(pmix_key_unifycrd_margo_shm, 0, &valstr)) {
        addr = strdup(valstr);
        free(valstr);
    }
#endif

    if (NULL == addr) {
        /* read server address from local file */
        FILE* fp = fopen(LOCAL_RPC_ADDR_FILE, "r");
        if (fp != NULL) {
            char addr_string[256];
            memset(addr_string, 0, sizeof(addr_string));
            if (1 == fscanf(fp, "%255s", addr_string)) {
                addr = strdup(addr_string);
            }
            fclose(fp);
        }
    }

    /* print server address (debugging) */
    if (NULL != addr) {
        LOGDBG("found local server rpc address '%s'", addr);
    }
    return addr;
}

/* lookup address of server, returns NULL if server address is not found,
 * otherwise returns server address in newly allocated string that caller
 * must free */
char* rpc_lookup_remote_server_addr(const char* hostname)
{
    /* returns NULL if we can't find server address */
    char* addr = NULL;

#ifdef HAVE_PMIX_H
    char* valstr = NULL;

    // lookup server-server margo address
    if (0 == unifycr_pmix_lookup_remote(hostname,
                                        pmix_key_unifycrd_margo_svr,
                                        0, &valstr)) {
        addr = strdup(valstr);
        free(valstr);
    }
#endif

    /* print sserver address (debugging) */
    if (NULL != addr) {
        LOGDBG("found server rpc address '%s' for %s", addr, hostname);
    }
    return addr;
}

/* remove local server RPC address file */
void rpc_clean_local_server_addr(void)
{
    int rc = unlink(LOCAL_RPC_ADDR_FILE);
    if (rc != 0) {
        int err = errno;
        if (err != ENOENT) {
            LOGERR("Error (%s) removing local server rpc addr file "
                   LOCAL_RPC_ADDR_FILE, strerror(err));
        }
    }
}

