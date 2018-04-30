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
    hg_id_t unifycr_read_rpc_id;
    hg_id_t unifycr_mount_rpc_id;
    hg_id_t unifycr_metaget_rpc_id;
    hg_id_t unifycr_metaset_rpc_id;
 } unifycr_client_rpc_context_t;

static int unifycr_client_rpc_init(char* svr_addr_str,
                             unifycr_client_rpc_context_t** unifycr_rpc_context);


static uint32_t unifycr_client_mount_rpc_invoke(unifycr_client_rpc_context_t** unifycr_rpc_context);

/*
 int unifycr_client_metaset_rpc(uint32_t app_id,
                             uint32_t local_rank_idx,
                             uint32_t dbg_rank,
                             uint32_t num_procs_per_node,
                             uint32_t req_buf_sz,
                             uint32_t recv_buf_sz,
                             uint32_t superblock_sz,
                             uint32_t meta_offset,
                             uint32_t meta_size,
                             uint32_t fmeta_offset,
                             uint32_t fmeta_size,
                             uint32_t data_offset,
                             uint32_t data_size,
                             hg_const_string_t external_spill_dir,
                             unifycr_client_rpc_context_t** unifycr_client_context);

 int unifycr_client_read_rpc(uint64_t fid,
                            void* buf,
                            uint64_t offset,
                            size_t count,
                            unifycr_client_rpc_context_t* unifycr_client_context);

*/
#endif
