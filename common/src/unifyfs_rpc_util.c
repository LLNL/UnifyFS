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

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <margo.h>
#include <assert.h>
#include "unifyfs_log.h"
#include "unifyfs_keyval.h"
#include "unifyfs_rpc_util.h"

#define LOCAL_RPC_ADDR_FILE "/tmp/unifyfsd.margo-shm"

/* publishes client-server RPC address */
void rpc_publish_local_server_addr(const char* addr)
{
    LOGDBG("publishing client-server rpc address '%s'", addr);

    // publish client-server margo address
    unifyfs_keyval_publish_local(key_unifyfsd_margo_shm, addr);

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

    // publish client-server margo address
    unifyfs_keyval_publish_remote(key_unifyfsd_margo_svr, addr);
}

/* lookup address of server, returns NULL if server address is not found,
 * otherwise returns server address in newly allocated string that caller
 * must free */
char* rpc_lookup_local_server_addr(void)
{
    /* returns NULL if we can't find server address */
    char* addr = NULL;
    char* valstr = NULL;

    // lookup client-server margo address
    if (0 == unifyfs_keyval_lookup_local(key_unifyfsd_margo_shm, &valstr)) {
        addr = strdup(valstr);
        free(valstr);
    }

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
char* rpc_lookup_remote_server_addr(int srv_rank)
{
    /* returns NULL if we can't find server address */
    char* addr = NULL;
    char* valstr = NULL;

    // lookup server-server margo address
    if (0 == unifyfs_keyval_lookup_remote(srv_rank, key_unifyfsd_margo_svr,
                                          &valstr)) {
        addr = strdup(valstr);
        free(valstr);
    }

    /* print sserver address (debugging) */
    if (NULL != addr) {
        LOGDBG("found server %d rpc address '%s'", srv_rank, addr);
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

/* Use passed bulk handle to pull data into a newly allocated buffer.
 * If local_bulk is not NULL, will set to local bulk handle on success.
 * Returns bulk buffer, or NULL on failure. */
void* pull_margo_bulk_buffer(hg_handle_t rpc_hdl,
                             hg_bulk_t bulk_remote,
                             hg_size_t bulk_sz,
                             hg_bulk_t* local_bulk)
{
    if (0 == bulk_sz) {
        return NULL;
    }

    size_t sz = (size_t) bulk_sz;
    void* buffer = malloc(sz);
    if (NULL == buffer) {
        LOGERR("failed to allocate buffer(sz=%zu) for bulk transfer", sz);
        return NULL;
    }

    /* get mercury info to set up bulk transfer */
    const struct hg_info* hgi = margo_get_info(rpc_hdl);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* register local target buffer for bulk access */
    hg_bulk_t bulk_local;
    hg_return_t hret = margo_bulk_create(mid, 1, &buffer, &bulk_sz,
                                         HG_BULK_READWRITE, &bulk_local);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        free(buffer);
        return NULL;
    }

    /* execute the transfer to pull data from remote side
     * into our local buffer.
     *
     * NOTE: mercury/margo bulk transfer does not check the maximum
     * transfer size that the underlying transport supports, and a
     * large bulk transfer may result in failure. */
    int i = 0;
    hg_size_t max_bulk = UNIFYFS_SERVER_MAX_BULK_TX_SIZE;
    hg_size_t remain = bulk_sz;
    do {
        hg_size_t offset = i * max_bulk;
        hg_size_t len = (remain < max_bulk) ? remain : max_bulk;
        hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                                   bulk_remote, offset,
                                   bulk_local, offset, len);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_bulk_transfer(buf_offset=%zu, len=%zu) failed",
                   (size_t)offset, (size_t)len);
            break;
        }
        remain -= len;
        i++;
    } while (remain > 0);

    if (hret == HG_SUCCESS) {
        LOGDBG("successful bulk transfer (%zu bytes)", bulk_sz);
        if (local_bulk != NULL) {
            *local_bulk = bulk_local;
        } else {
            /* deregister our bulk transfer buffer */
            margo_bulk_free(bulk_local);
        }
        return buffer;
    } else {
        LOGERR("failed bulk transfer - transferred %zu of %zu bytes",
               (bulk_sz - remain), bulk_sz);
        free(buffer);
        return NULL;
    }
}

