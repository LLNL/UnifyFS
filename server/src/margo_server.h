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
#include <mercury.h>
#include <abt.h>

#include "unifycr_client_rpcs.h"
#include "unifycr_server_rpcs.h"
#include "unifycr_rpc_util.h"

typedef struct ServerRpcContext {
    margo_instance_id shm_mid;
    margo_instance_id svr_mid;
    /* TODO: rpc id's executed on client go here */
} ServerRpcContext_t;

extern ServerRpcContext_t* unifycrd_rpc_context;

extern bool margo_use_tcp;

int margo_server_rpc_init(void);
int margo_server_rpc_finalize(void);

#endif // MARGO_SERVER_H
