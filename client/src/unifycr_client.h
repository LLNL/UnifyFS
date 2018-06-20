#ifndef __UNIFYCR_CLIENT_H
#define __UNIFYCR_CLIENT_H

/*******************************************************************************
 * unifycr_client.h
 *
 * Declarations for the unifycr client interface.
 *
 * ******************************************************************************/

#include <unistd.h>
#include <margo.h>
#include "unifycr-internal.h"

 typedef struct ClientRpcContext
 {
    margo_instance_id mid;
    hg_context_t* hg_context;
    hg_class_t* hg_class;
    hg_addr_t svr_addr;
    hg_id_t unifycr_read_rpc_id;
    hg_id_t unifycr_mount_rpc_id;
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

uint32_t unifycr_client_mount_rpc_invoke(unifycr_client_rpc_context_t**
                                                unifycr_rpc_context);

uint32_t unifycr_client_metaset_rpc_invoke(unifycr_client_rpc_context_t**
                                                  unifycr_rpc_context,
                                                  unifycr_fattr_t* f_meta);

uint32_t unifycr_client_metaget_rpc_invoke(unifycr_client_rpc_context_t**
                                                  unifycr_rpc_context,
                                                  unifycr_fattr_t** file_meta, int fid, int gfid);

uint32_t unifycr_client_fsync_rpc_invoke(unifycr_client_rpc_context_t**
                                                  unifycr_rpc_context,
                                                  uint32_t app_id,
                                                  uint32_t local_rank_idx,
                                                  uint32_t gfid);
#endif
