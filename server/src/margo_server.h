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
    hg_id_t hello_id;
    hg_id_t server_pid_id;
    hg_id_t request_id;
    hg_id_t chunk_read_request_id;
    hg_id_t chunk_read_response_id;
    hg_id_t extbcast_request_id;
    hg_id_t extbcast_response_id;
    hg_id_t filesize_id;
    hg_id_t truncate_id;
    hg_id_t metaset_id;
    hg_id_t unlink_id;
} server_rpcs_t;

typedef struct ServerRpcContext {
    margo_instance_id shm_mid;
    margo_instance_id svr_mid;
    server_rpcs_t rpcs;
} ServerRpcContext_t;

extern ServerRpcContext_t* unifyfsd_rpc_context;

extern bool margo_use_tcp;
extern bool margo_lazy_connect;

int margo_server_rpc_init(void);
int margo_server_rpc_finalize(void);

int margo_connect_servers(void);

#endif // MARGO_SERVER_H
