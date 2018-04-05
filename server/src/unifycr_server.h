#ifndef __UNIFYCR_SERVER_H
#define __UNIFYCR_SERVER_H

/*******************************************************************************
 *
 * unifycr_server.h
 *
 * Declarations for the UCR global server.
 *
 *******************************************************************************/

#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include <abt.h>
#include <abt-snoozer.h>
#include <margo.h>
#include <abt-io.h>

struct ServerRpcContext
{
    margo_instance_id mid;
    hg_context_t* hg_context = NULL;
    hg_class_t* hg_class = NULL;
    hg_id_t read_rpc_id;
    //hg_id_t write_rpc_id;
    //hg_id_t chkdir_rpc_id;
    //hg_id_t addfile_rpc_id;
    //hg_id_t open_rpc_id;
    //hg_id_t close_rpc_id;
    //hg_id_t getfilestat_rpc_id;
    //hg_id_t getdircontents_rpc_id;
    //hg_id_t readtransfer_rpc_id;
};

static const char* SMSVR_ADDR_STR   = "cci+sm";
static const char* VERBSVR_ADDR_STR = "cci+verbs";
static const char* TCPSVR_ADDR_STR  = "cci+tcp";

extern bool usetcp;

extern uint16_t total_rank;
extern uint16_t my_rank;

extern abt_io_instance_id aid;

struct ServerAddress
{
    char* string_address;
    hg_addr_t svr_addr;
};

extern char** server_addresses;

extern ServerRpcContext* unifycr_rpc_context;

margo_instance_id unifycr_server_rpc_init();

void unifycr_server_addresses_init();

int unifycr_inter_server_client_init();

#endif
