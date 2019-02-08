#ifndef __UNIFYCR_CLIENT_H
#define __UNIFYCR_CLIENT_H

/*******************************************************************************
 * unifycr_client.h
 *
 * Declarations for the unifycr client interface.
 *
 * ******************************************************************************/

#include "unifycr-internal.h"

#include <unistd.h>
#include <margo.h>

typedef struct ClientRpcContext {
    margo_instance_id mid;
    hg_context_t* hg_context;
    hg_class_t* hg_class;
    hg_addr_t svr_addr;
    hg_id_t unifycr_filesize_rpc_id;
    hg_id_t unifycr_read_rpc_id;
    hg_id_t unifycr_mount_rpc_id;
    hg_id_t unifycr_unmount_rpc_id;
    hg_id_t unifycr_metaget_rpc_id;
    hg_id_t unifycr_metaset_rpc_id;
    hg_id_t unifycr_fsync_rpc_id;
} unifycr_client_rpc_context_t;

/* global rpc context (probably should find a better spot for this) */
extern unifycr_client_rpc_context_t* unifycr_rpc_context;

/*
int unifycr_client_rpc_init(char* svr_addr_str,
                             unifycr_client_rpc_context_t**
                             unifycr_rpc_context);
*/

int32_t unifycr_client_mount_rpc_invoke(unifycr_client_rpc_context_t**
                                        unifycr_rpc_context);

int32_t unifycr_client_unmount_rpc_invoke(unifycr_client_rpc_context_t**
                                          unifycr_rpc_context);

int32_t unifycr_client_metaset_rpc_invoke(unifycr_client_rpc_context_t**
                                          unifycr_rpc_context,
                                          unifycr_file_attr_t* f_meta);

int32_t unifycr_client_metaget_rpc_invoke(
    unifycr_client_rpc_context_t** unifycr_rpc_context,
    int32_t gfid,
    unifycr_file_attr_t* f_meta);

int32_t unifycr_client_fsync_rpc_invoke(unifycr_client_rpc_context_t**
                                        unifycr_rpc_context,
                                        int32_t app_id,
                                        int32_t local_rank_idx,
                                        int32_t gfid);

uint32_t unifycr_client_filesize_rpc_invoke(unifycr_client_rpc_context_t**
                                            unifycr_rpc_context,
                                            int32_t app_id,
                                            int32_t local_rank_idx,
                                            int32_t gfid,
                                            hg_size_t* filesize);

int32_t unifycr_client_read_rpc_invoke(unifycr_client_rpc_context_t**
                                       unifycr_rpc_context,
                                       int32_t app_id,
                                       int32_t local_rank_idx,
                                       int32_t gfid,
                                       hg_size_t offset,
                                       hg_size_t length);

int32_t unifycr_client_mread_rpc_invoke(unifycr_client_rpc_context_t**
                                        unifycr_rpc_context,
                                        int32_t app_id,
                                        int32_t local_rank_idx,
                                        int32_t gfid,
                                        int32_t read_count,
                                        hg_size_t size,
                                        void* buffer);

#endif
