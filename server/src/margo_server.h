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

#ifndef _MARGO_SERVER_H
#define _MARGO_SERVER_H

/********************************************
 *
 * margo_server.h
 *
 * Declarations for the server's use of Margo
 *
 *********************************************/

#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include <margo.h>

typedef struct ServerRpcIds {
    /* server-server rpcs */
    hg_id_t bcast_progress_id;
    hg_id_t chunk_read_request_id;
    hg_id_t chunk_read_response_id;
    hg_id_t extent_add_id;
    hg_id_t extent_bcast_id;
    hg_id_t extent_lookup_id;
    hg_id_t filesize_id;
    hg_id_t laminate_id;
    hg_id_t laminate_bcast_id;
    hg_id_t metaget_id;
    hg_id_t metaset_id;
    hg_id_t fileattr_bcast_id;
    hg_id_t server_pid_id;
    hg_id_t transfer_id;
    hg_id_t transfer_bcast_id;
    hg_id_t truncate_id;
    hg_id_t truncate_bcast_id;
    hg_id_t unlink_bcast_id;
    hg_id_t node_local_extents_get_id;

    /* client-server rpcs */
    hg_id_t client_heartbeat_id;
    hg_id_t client_mread_data_id;
    hg_id_t client_mread_complete_id;
    hg_id_t client_transfer_complete_id;
    hg_id_t client_unlink_callback_id;
} server_rpcs_t;

typedef struct ServerRpcContext {
    margo_instance_id shm_mid;
    margo_instance_id svr_mid;
    server_rpcs_t rpcs;
} ServerRpcContext_t;

extern ServerRpcContext_t* unifyfsd_rpc_context;

extern bool margo_use_tcp;
extern bool margo_lazy_connect;
extern int  margo_client_server_pool_sz;
extern int  margo_server_server_pool_sz;
extern double margo_client_server_timeout_msec;
extern double margo_server_server_timeout_msec;

int margo_server_rpc_init(void);
int margo_server_rpc_finalize(void);

int margo_connect_server(int rank);
int margo_connect_servers(void);

hg_addr_t get_margo_server_address(int rank);



/* invokes the client heartbeat rpc function */
int invoke_client_heartbeat_rpc(int app_id, int client_id);

/* invokes the client mread request data response rpc function */
int invoke_client_mread_req_data_rpc(int app_id,
                                     int client_id,
                                     int mread_id,
                                     int read_index,
                                     size_t read_offset,
                                     size_t extent_size,
                                     void* extent_buffer);

/* invokes the client mread request completion rpc function */
int invoke_client_mread_req_complete_rpc(int app_id,
                                         int client_id,
                                         int mread_id,
                                         int read_index,
                                         int read_error);

/* invokes the client transfer request completion rpc function */
int invoke_client_transfer_complete_rpc(int app_id,
                                        int client_id,
                                        int transfer_id,
                                        size_t transfer_sz_bytes,
                                        int transfer_time_sec,
                                        int transfer_time_usec,
                                        int error_code);

/* invokes the client unlink callback rpc function */
int invoke_client_unlink_callback_rpc(int app_id,
                                      int client_id,
                                      int gfid);

#endif // MARGO_SERVER_H
