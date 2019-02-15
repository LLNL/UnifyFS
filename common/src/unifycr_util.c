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
#include "unifycr_log.h"
#include "unifycr_util.h"

/* publishes server RPC address */
void addr_publish_server(const char* addr)
{
    /* TODO: support other publish modes like PMIX */

    /* write server address to /dev/shm/ for client on node to
     * read from */
    FILE* fp = fopen("/dev/shm/svr_id", "w+");
    if (fp != NULL) {
        fprintf(fp, "%s", addr);
        fclose(fp);
    } else {
        LOGERR("Error writing server rpc addr to file `/dev/shm/svr_id'");
    }
}

/* publishes client RPC address to a place where server can find it */
void addr_publish_client(const char* addr,
                                    int local_rank_idx,
                                    int app_id)
{
    /* TODO: support other publish modes like PMIX */

    /* write client address to /dev/shm/ for server to read from */
    char fname[36];
    sprintf(fname, "/dev/shm/client-%d.%d", app_id, local_rank_idx);
    FILE* fp = fopen(fname, "w+");
    if (fp != NULL) {
        fprintf(fp, "%s", addr);
        fclose(fp);
    } else {
        LOGERR("Error writing client rpc addr to file");
    }
}

/* returns server rpc address in newly allocated string
 * to be freed by caller, returns NULL if not found */
char* addr_lookup_server(void)
{
    /* returns NULL if we can't find server address */
    char* str = NULL;

    /* TODO: support other lookup methods here like PMIX */

    /* read server address string from well-known file name in ramdisk */
    FILE* fp = fopen("/dev/shm/svr_id", "r");
    if (fp != NULL) {
        /* opened the file, now read the address string */
        char addr_string[50];
        int rc = fscanf(fp, "%s", addr_string);
        if (rc == 1) {
            /* read the server address, dup a copy of it */
            str = strdup(addr_string);
        }
        fclose(fp);
    }

    /* print server address (debugging) */
    if (str != NULL) {
        LOGDBG("rpc address: %s", str);
    }

    return str;
}

/* returns client rpc address in newly allocated string
 * to be freed by caller, returns NULL if not found */
char* addr_lookup_client(int app_id, int client_id)
{
    /* returns NULL if we can't find client  address */
    char* str = NULL;

    /* TODO: support other lookup methods here like PMIX */

    /* read client address string from well-known file name in ramdisk */
    char fname[36];
    sprintf(fname, "/dev/shm/client-%d.%d", app_id, client_id);
    FILE* fp = fopen(fname, "r");
    if (fp != NULL) {
        /* opened the file, now read the address string */
        char addr_string[50];
        int rc = fscanf(fp, "%s", addr_string);
        if (rc == 1) {
            /* read the client address, dup a copy of it */
            str = strdup(addr_string);
        }
        fclose(fp);
    }

    /* print client address (debugging) */
    if (str != NULL) {
        LOGDBG("rpc address: %s", str);
    }

    return str;
}
