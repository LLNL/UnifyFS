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

typedef struct ServerRpcContext {
    margo_instance_id mid;
    hg_context_t* hg_context;
    hg_class_t* hg_class;
    /* TODO: rpc id's executed on client go here */
} ServerRpcContext_t;
extern ServerRpcContext_t* unifycrd_rpc_context;

static const char* SMSVR_ADDR_STR   = "na+sm://";
static const char* VERBSVR_ADDR_STR = "cci+verbs";
static const char* TCPSVR_ADDR_STR  = "cci+tcp";

extern bool usetcp;

int unifycr_server_rpc_init(void);

#endif // MARGO_SERVER_H
