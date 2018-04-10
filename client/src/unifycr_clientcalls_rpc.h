#ifndef __UNIFYCR_CLIENTCALS_RPC_H
#define __UNIFYCR_CLIENTCALLS_RPC_H

/*******************************************************************************
 * unifycr_clientcalls_rpc.h
 * Declarations for the RPC shared-memory interfaces to the UCR server.
 ********************************************************************************/

#include <unistd.h>
#include <margo.h>
#include <mercury.h>
#include <mercury_proc_string.h>

    MERCURY_GEN_PROC(unifycr_mount_out_t, ((int32_t)(ret)))
    MERCURY_GEN_PROC(unifycr_mount_in_t,
        ((uint32_t)(app_id))\
        ((uint32_t)(local_rank_idx))\
        ((uint32_t)(dbg_rank))\
        ((uint32_t)(num_procs_per_node))\
        ((uint32_t)(req_buf_sz))\
        ((uint32_t)(recv_buf_sz))\
        ((uint64_t)(superblock_sz))\
        ((uint64_t)(meta_offset))\
        ((uint64_t)(meta_size))\
        ((uint64_t)(fmeta_offset))\
        ((uint64_t)(fmeta_size))\
        ((uint64_t)(data_offset))\
        ((uint64_t)(data_size))\
        ((hg_const_string_t)(external_spill_dir)))
    DECLARE_MARGO_RPC_HANDLER(unifycr_mount_rpc)

#endif
