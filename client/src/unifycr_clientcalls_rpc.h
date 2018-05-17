#ifndef __UNIFYCR_CLIENTCALS_RPC_H
#define __UNIFYCR_CLIENTCALLS_RPC_H

/*******************************************************************************
 * unifycr_clientcalls_rpc.h
 * Declarations for the RPC shared-memory interfaces to the UCR server.
 ********************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
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


    MERCURY_GEN_PROC(unifycr_metaset_out_t, ((uint32_t)(ret)))
    MERCURY_GEN_PROC(unifycr_metaset_in_t,
        ((int32_t)(fid))\
        ((int32_t)(gid))\
        ((hg_const_string_t)(filename)))
    DECLARE_MARGO_RPC_HANDLER(unifycr_metaset_rpc)

    /*MERCURY_GEN_PROC(unifycr_metaget_out_t,
                     ((unifycr_file_attr_t)(attr_val)) ((uint32_t)(ret)))*/
    MERCURY_GEN_PROC(unifycr_metaget_out_t,
                     ((int64_t)(st_size))\
                     ((uint32_t)(ret))\
        		   	 ((hg_const_string_t)(filename)))
    MERCURY_GEN_PROC(unifycr_metaget_in_t,
        ((int32_t)(gid)))
    DECLARE_MARGO_RPC_HANDLER(unifycr_metaget_rpc)


    MERCURY_GEN_PROC(unifycr_fsync_out_t, ((int32_t)(ret)))
    MERCURY_GEN_PROC(unifycr_fsync_in_t,
        ((uint32_t)(app_id))\
        ((uint32_t)(local_rank_idx))\
        ((int32_t)(gid)))
    DECLARE_MARGO_RPC_HANDLER(unifycr_fsync_rpc)

    MERCURY_GEN_PROC(unifycr_read_out_t, ((int32_t)(ret)))
    MERCURY_GEN_PROC(unifycr_read_in_t,
        ((uint32_t)(app_id))\
        ((uint32_t)(local_rank_idx))\
        ((int32_t)(gid))\
        ((int32_t)(read_count)))
    DECLARE_MARGO_RPC_HANDLER(unifycr_read_rpc)


    MERCURY_GEN_PROC(unifycr_unmount_out_t, ((int32_t)(ret)))
    MERCURY_GEN_PROC(unifycr_unmount_in_t,
        ((hg_const_string_t)(external_spill_dir)))
    DECLARE_MARGO_RPC_HANDLER(unifycr_unmount_rpc)

#endif
