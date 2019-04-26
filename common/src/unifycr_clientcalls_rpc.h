#ifndef __UNIFYCR_CLIENTCALLS_RPC_H
#define __UNIFYCR_CLIENTCALLS_RPC_H

/*
 * Declarations for client-server margo RPCs (shared-memory)
 */

#include <margo.h>
#include <mercury.h>
#include <mercury_proc_string.h>
#include <mercury_types.h>

/* unifycr_mount_rpc (client => server)
 *
 * connect application client to the server, and
 * initialize shared memory state */
MERCURY_GEN_PROC(unifycr_mount_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(dbg_rank))
                 ((int32_t)(num_procs_per_node))
                 ((hg_const_string_t)(client_addr_str))
                 ((hg_size_t)(req_buf_sz))
                 ((hg_size_t)(recv_buf_sz))
                 ((hg_size_t)(superblock_sz))
                 ((hg_size_t)(meta_offset))
                 ((hg_size_t)(meta_size))
                 ((hg_size_t)(fmeta_offset))
                 ((hg_size_t)(fmeta_size))
                 ((hg_size_t)(data_offset))
                 ((hg_size_t)(data_size))
                 ((hg_const_string_t)(external_spill_dir)))
MERCURY_GEN_PROC(unifycr_mount_out_t,
                 ((hg_size_t)(max_recs_per_slice))
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifycr_mount_rpc)

/* unifycr_unmount_rpc (client => server)
 *
 * disconnect client from server */
MERCURY_GEN_PROC(unifycr_unmount_in_t,
    ((int32_t)(app_id))
    ((int32_t)(local_rank_idx)))
MERCURY_GEN_PROC(unifycr_unmount_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifycr_unmount_rpc)

/* unifycr_metaset_rpc (client => server)
 *
 * given a global file id and a file name,
 * record key/value entry for this file */
MERCURY_GEN_PROC(unifycr_metaset_in_t,
                 ((int32_t)(fid))
                 ((int32_t)(gfid))
                 ((hg_const_string_t)(filename)))
MERCURY_GEN_PROC(unifycr_metaset_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifycr_metaset_rpc)

/* unifycr_metaget_rpc (client => server)
 *
 * returns file metadata including size and name
 * given a global file id */
MERCURY_GEN_PROC(unifycr_metaget_in_t,
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifycr_metaget_out_t,
                 ((hg_size_t)(st_size))
                 ((int32_t)(ret))
                 ((hg_const_string_t)(filename)))
DECLARE_MARGO_RPC_HANDLER(unifycr_metaget_rpc)

/* unifycr_fsync_rpc (client => server)
 *
 * given app_id, client_id, and a global file id as input,
 * read extent location metadata from client shared memory
 * and insert corresponding key/value pairs into global index */
MERCURY_GEN_PROC(unifycr_fsync_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifycr_fsync_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifycr_fsync_rpc)

/* unifycr_filesize_rpc (client => server)
 *
 * given an app_id, client_id, global file id,
 * return filesize for given file */
MERCURY_GEN_PROC(unifycr_filesize_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifycr_filesize_out_t,
                 ((int32_t)(ret))
                 ((hg_size_t)(filesize)))
DECLARE_MARGO_RPC_HANDLER(unifycr_filesize_rpc)

/* unifycr_read_rpc (client => server)
 *
 * given an app_id, client_id, global file id, an offset, and a length,
 * initiate read request for data */
MERCURY_GEN_PROC(unifycr_read_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(gfid))
                 ((hg_size_t)(offset))
                 ((hg_size_t)(length)))
MERCURY_GEN_PROC(unifycr_read_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifycr_read_rpc)

/* unifycr_mread_rpc (client => server)
 *
 * given an app_id, client_id, global file id, and a count
 * of read requests, followed by list of offset/length tuples
 * initiate read requests for data */
MERCURY_GEN_PROC(unifycr_mread_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(gfid))
                 ((int32_t)(read_count))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(unifycr_mread_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifycr_mread_rpc)

#endif
