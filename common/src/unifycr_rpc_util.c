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
#include "unifycr_rpc_util.h"

/* publishes server RPC address */
void rpc_publish_server_addr(const char* addr)
{
    /* TODO: support other publish modes like PMIX */

    /* write server address to /dev/shm/ for client on node to
     * read from */
    FILE* fp = fopen("/dev/shm/unifycrd_id", "w+");
    if (fp != NULL) {
        fprintf(fp, "%s", addr);
        fclose(fp);
    } else {
        LOGERR("Error writing server rpc addr to file `/dev/shm/unifycrd_id'");
    }
}

/* lookup address of server, returns NULL if server address is not found,
 *  * otherwise returns server address in newly allocated string that caller
 *  must free */
char* rpc_lookup_server_addr(void)
{
    /* returns NULL if we can't find server address */
    char* str = NULL;

    /* TODO: support other lookup methods here like PMIX */

    /* read server address string from well-known file name in ramdisk */
    FILE* fp = fopen("/dev/shm/unifycrd_id", "r");
    if (fp != NULL) {
        /* opened the file, now read the address string */
        char addr_string[256];
        int rc = fscanf(fp, "%255s", addr_string);
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
