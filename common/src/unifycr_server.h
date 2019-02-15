#ifndef __UNIFYCR_SERVER_H
#define __UNIFYCR_SERVER_H

/********************************************
 *
 * unifycr_server.h
 *
 * Declarations for the UCR global server.
 *
 *********************************************/

#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include <mercury.h>
#include <abt.h>
#include <margo.h>

typedef struct ServerRpcContext {
    margo_instance_id mid;
    hg_context_t* hg_context;
    hg_class_t* hg_class;
    /* TODO: rpc id's executed on client go here */
} ServerRpcContext_t;

static const char* SMSVR_ADDR_STR   = "na+sm://";
static const char* VERBSVR_ADDR_STR = "cci+verbs";
static const char* TCPSVR_ADDR_STR  = "cci+tcp";

extern bool usetcp;

extern uint16_t total_rank;
extern uint16_t my_rank;

extern ServerRpcContext_t* unifycr_server_rpc_context;

margo_instance_id unifycr_server_rpc_init(void);

#endif
