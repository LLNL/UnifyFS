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

 typedef struct ClientRpcContext
 {
    margo_instance_id mid;
    hg_context_t* hg_context;
    hg_class_t* hg_class;
    hg_addr_t svr_addr;
    hg_id_t read_rpc_id;
 } unifycr_client_rpc_context_t;

 int unifycr_client_rpc_init(char* svr_addr_str, unifycr_client_rpc_context_t** unifycr_client_context);
 int unifycr_client_read_rpc(uint64_t fid, void* buf,
                             uint64_t offset, size_t count,
                             unifycr_client_rpc_context_t* unifycr_client_context);

#endif
