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

#include "unifyfs_api_internal.h"
#include "unifyfs_client_rpcs.h"
#include <margo.h>

typedef struct ClientRpcIds {
    /* client-to-server */
    hg_id_t attach_id;
    hg_id_t mount_id;
    hg_id_t unmount_id;
    hg_id_t metaset_id;
    hg_id_t metaget_id;
    hg_id_t filesize_id;
    hg_id_t transfer_id;
    hg_id_t truncate_id;
    hg_id_t unlink_id;
    hg_id_t laminate_id;
    hg_id_t fsync_id;
    hg_id_t mread_id;
    hg_id_t node_local_extents_get_id;
    hg_id_t get_gfids_id;

    /* server-to-client */
    hg_id_t heartbeat_id;
    hg_id_t mread_req_data_id;
    hg_id_t mread_req_complete_id;
    hg_id_t transfer_complete_id;
    hg_id_t unlink_callback_id;
} client_rpcs_t;

typedef struct ClientRpcContext {
    margo_instance_id mid;
    char* client_addr_str;
    hg_addr_t client_addr;
    hg_addr_t svr_addr;
    client_rpcs_t rpcs;
    double timeout; /* timeout to wait on rpc, in millisecs */
} client_rpc_context_t;


int unifyfs_client_rpc_init(double timeout_msecs);

int unifyfs_client_rpc_finalize(void);

int invoke_client_attach_rpc(unifyfs_client* client);

int invoke_client_mount_rpc(unifyfs_client* client);

int invoke_client_unmount_rpc(unifyfs_client* client);

int invoke_client_metaset_rpc(unifyfs_client* client,
                              unifyfs_file_attr_op_e attr_op,
                              unifyfs_file_attr_t* f_meta);

int invoke_client_metaget_rpc(unifyfs_client* client,
                              int gfid,
                              unifyfs_file_attr_t* f_meta);

int invoke_client_filesize_rpc(unifyfs_client* client,
                               int gfid,
                               size_t* filesize);

int invoke_client_laminate_rpc(unifyfs_client* client,
                               int gfid);

int invoke_client_mread_rpc(unifyfs_client* client,
                            unsigned int reqid,
                            int read_count,
                            size_t extents_size,
                            void* extents_buffer);

int invoke_client_sync_rpc(unifyfs_client* client,
                           int gfid);

int invoke_client_transfer_rpc(unifyfs_client* client,
                               int transfer_id,
                               int gfid,
                               int parallel_transfer,
                               const char* dest_file);

int invoke_client_truncate_rpc(unifyfs_client* client,
                               int gfid,
                               size_t filesize);

int invoke_client_unlink_rpc(unifyfs_client* client,
                             int gfid);


int invoke_client_node_local_extents_get_rpc(unifyfs_client* client,
                                             int num_req,
                                             extents_list_t* read_req,
                                             size_t* extent_count,
                                             unifyfs_client_index_t** extents);

int invoke_client_get_gfids_rpc(unifyfs_client* client,
                                int* num_gfids,
                                int** gfid_list);


#endif // MARGO_CLIENT_H
