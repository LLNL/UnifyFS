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

#ifndef _MARGO_CLIENT_H
#define _MARGO_CLIENT_H

/********************************************
 * margo_client.h - client-server margo RPCs
 ********************************************/

#include <margo.h>
#include "unifyfs_meta.h"
#include "unifyfs_client_rpcs.h"

typedef struct ClientRpcIds {
    hg_id_t attach_id;
    hg_id_t mount_id;
    hg_id_t unmount_id;
    hg_id_t metaset_id;
    hg_id_t metaget_id;
    hg_id_t filesize_id;
    hg_id_t truncate_id;
    hg_id_t unlink_id;
    hg_id_t laminate_id;
    hg_id_t fsync_id;
    hg_id_t mread_id;
    hg_id_t mread_req_data_id;
    hg_id_t mread_req_complete_id;
    hg_id_t heartbeat_id;
} client_rpcs_t;

typedef struct ClientRpcContext {
    margo_instance_id mid;
    char* client_addr_str;
    hg_addr_t client_addr;
    hg_addr_t svr_addr;
    client_rpcs_t rpcs;
} client_rpc_context_t;


int unifyfs_client_rpc_init(void);

int unifyfs_client_rpc_finalize(void);

void fill_client_attach_info(unifyfs_cfg_t* clnt_cfg,
                             unifyfs_attach_in_t* in);
int invoke_client_attach_rpc(unifyfs_cfg_t* clnt_cfg);

void fill_client_mount_info(unifyfs_cfg_t* clnt_cfg,
                            unifyfs_mount_in_t* in);
int invoke_client_mount_rpc(unifyfs_cfg_t* clnt_cfg);

int invoke_client_unmount_rpc(void);

int invoke_client_metaset_rpc(unifyfs_file_attr_op_e attr_op,
                              unifyfs_file_attr_t* f_meta);

int invoke_client_metaget_rpc(int gfid, unifyfs_file_attr_t* f_meta);

int invoke_client_filesize_rpc(int gfid, size_t* filesize);

int invoke_client_truncate_rpc(int gfid, size_t filesize);

int invoke_client_unlink_rpc(int gfid);

int invoke_client_laminate_rpc(int gfid);

int invoke_client_sync_rpc(int gfid);

int invoke_client_mread_rpc(unsigned int reqid, int read_count,
                            size_t extents_size, void* extents_buffer);

#endif // MARGO_CLIENT_H
