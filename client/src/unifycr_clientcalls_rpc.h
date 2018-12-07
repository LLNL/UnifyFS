#ifndef __UNIFYCR_CLIENTCALLS_RPC_H
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

/* called by client to register with the server, client provides a
 * structure of values on input, some of which specify global
 * values across all clients in the app_id, and some of which are
 * specific to the client process,
 *
 * server creates a structure for the given app_id (if needed),
 * and then fills in a set of values for the particular client,
 *
 * server attaches to client shared memory regions, opens files
 * holding spill over data, and launchers request manager for
 * client */
MERCURY_GEN_PROC(unifycr_mount_out_t,
                 ((int64_t)(max_recs_per_slice))
                 ((int32_t)(ret)))
MERCURY_GEN_PROC(unifycr_mount_in_t,
                 ((uint32_t)(app_id))
                 ((uint32_t)(local_rank_idx))
                 ((uint32_t)(dbg_rank))
                 ((uint32_t)(num_procs_per_node))
                 ((uint32_t)(req_buf_sz))
                 ((uint32_t)(recv_buf_sz))
                 ((uint64_t)(superblock_sz))
                 ((uint64_t)(meta_offset))
                 ((uint64_t)(meta_size))
                 ((uint64_t)(fmeta_offset))
                 ((uint64_t)(fmeta_size))
                 ((uint64_t)(data_offset))
                 ((uint64_t)(data_size))
                 ((hg_const_string_t)(external_spill_dir)))
DECLARE_MARGO_RPC_HANDLER(unifycr_mount_rpc)

MERCURY_GEN_PROC(unifycr_unmount_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(unifycr_unmount_in_t,
    ((uint32_t)(app_id))
    ((uint32_t)(local_rank_idx)))
DECLARE_MARGO_RPC_HANDLER(unifycr_unmount_rpc)

/* given a global file id and a file name,
 * record key/value entry for this file */
MERCURY_GEN_PROC(unifycr_metaset_out_t, ((uint32_t)(ret)))
MERCURY_GEN_PROC(unifycr_metaset_in_t,
                 ((int32_t)(fid))
                 ((int32_t)(gfid))
                 ((hg_const_string_t)(filename)))
DECLARE_MARGO_RPC_HANDLER(unifycr_metaset_rpc)

/* returns file meta data including file size and file name
 * given a global file id */
MERCURY_GEN_PROC(unifycr_metaget_out_t,
                 ((int64_t)(st_size))
                 ((uint32_t)(ret))
                 ((hg_const_string_t)(filename)))
MERCURY_GEN_PROC(unifycr_metaget_in_t,
                 ((int32_t)(gfid)))
DECLARE_MARGO_RPC_HANDLER(unifycr_metaget_rpc)

/* given app_id, client_id, and a global file id as input,
 * read extent location metadata from client shared memory
 * and insert corresponding key/value pairs into global index */
MERCURY_GEN_PROC(unifycr_fsync_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(unifycr_fsync_in_t,
                 ((uint32_t)(app_id))
                 ((uint32_t)(local_rank_idx))
                 ((int32_t)(gfid)))
DECLARE_MARGO_RPC_HANDLER(unifycr_fsync_rpc)

/* given an app_id, client_id, global file id, an offset, and a length,
 * initiate read operation to lookup and return data,
 * client synchronizes with server again later when data is available
 * to be copied into user buffers */
MERCURY_GEN_PROC(unifycr_read_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(unifycr_read_in_t,
                 ((uint32_t)(app_id))
                 ((uint32_t)(local_rank_idx))
                 ((int32_t)(gfid))
                 ((int64_t)(offset))
                 ((int64_t)(length)))
DECLARE_MARGO_RPC_HANDLER(unifycr_read_rpc)

/* given an app_id, client_id, global file id, and a count
 * of read requests, follow by list of offset/length tuples
 * initiate read requests for data,
 * client synchronizes with server again later when data is available
 * to be copied into user buffers */
MERCURY_GEN_PROC(unifycr_mread_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(unifycr_mread_in_t,
                 ((uint32_t)(app_id))
                 ((uint32_t)(local_rank_idx))
                 ((int32_t)(gfid))
                 ((int32_t)(read_count))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_handle)))
DECLARE_MARGO_RPC_HANDLER(unifycr_mread_rpc)

#endif
