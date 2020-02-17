#ifndef __UNIFYFS_CLIENT_RPCS_H
#define __UNIFYFS_CLIENT_RPCS_H

/*
 * Declarations for client-server margo RPCs (shared-memory)
 */

#include <time.h>
#include <margo.h>
#include <mercury.h>
#include <mercury_proc_string.h>
#include <mercury_types.h>

#include "unifyfs_rpc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* unifyfs_attach_rpc (client => server)
 *
 * initialize server access to client's shared memory and file state */
MERCURY_GEN_PROC(unifyfs_attach_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((hg_size_t)(shmem_data_size))
                 ((hg_size_t)(shmem_super_size))
                 ((hg_size_t)(meta_offset))
                 ((hg_size_t)(meta_size))
                 ((hg_size_t)(logio_mem_size))
                 ((hg_size_t)(logio_spill_size))
                 ((hg_const_string_t)(logio_spill_dir)))
MERCURY_GEN_PROC(unifyfs_attach_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_attach_rpc)

/* unifyfs_mount_rpc (client => server)
 *
 * connect application client to the server */
MERCURY_GEN_PROC(unifyfs_mount_in_t,
                 ((int32_t)(dbg_rank))
                 ((hg_const_string_t)(mount_prefix))
                 ((hg_const_string_t)(client_addr_str)))
MERCURY_GEN_PROC(unifyfs_mount_out_t,
                 ((hg_size_t)(meta_slice_sz))
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_mount_rpc)

/* unifyfs_unmount_rpc (client => server)
 *
 * disconnect client from server */
MERCURY_GEN_PROC(unifyfs_unmount_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id)))
MERCURY_GEN_PROC(unifyfs_unmount_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_unmount_rpc)

/* unifyfs_metaset_rpc (client => server)
 *
 * given a global file id and a file name,
 * record key/value entry for this file */
MERCURY_GEN_PROC(unifyfs_metaset_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(create))
                 ((unifyfs_file_attr_t)(attr)))
MERCURY_GEN_PROC(unifyfs_metaset_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_metaset_rpc)

/* unifyfs_metaget_rpc (client => server)
 *
 * returns file metadata including size and name
 * given a global file id */
MERCURY_GEN_PROC(unifyfs_metaget_in_t,
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_metaget_out_t,
                 ((int32_t)(ret))
                 ((unifyfs_file_attr_t)(attr)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_metaget_rpc)

/* unifyfs_sync_rpc (client => server)
 *
 * given app_id and client_id as input, read all write extents
 * from client index in shared memory and insert corresponding
 * key/value pairs into our global metadata */
MERCURY_GEN_PROC(unifyfs_sync_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id)))
MERCURY_GEN_PROC(unifyfs_sync_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_sync_rpc)

/* unifyfs_filesize_rpc (client => server)
 *
 * given an app_id, client_id, global file id,
 * return filesize for given file */
MERCURY_GEN_PROC(unifyfs_filesize_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_filesize_out_t,
                 ((int32_t)(ret))
                 ((hg_size_t)(filesize)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_filesize_rpc)

/* unifyfs_truncate_rpc (client => server)
 *
 * given an app_id, client_id, global file id,
 * and a filesize, truncate file to that size */
MERCURY_GEN_PROC(unifyfs_truncate_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid))
                 ((hg_size_t)(filesize)))
MERCURY_GEN_PROC(unifyfs_truncate_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_truncate_rpc)

/* unifyfs_unlink_rpc (client => server)
 *
 * given an app_id, client_id, and global file id,
 * unlink the file */
MERCURY_GEN_PROC(unifyfs_unlink_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_unlink_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_unlink_rpc)

/* unifyfs_laminate_rpc (client => server)
 *
 * given an app_id, client_id, and global file id,
 * laminate the file */
MERCURY_GEN_PROC(unifyfs_laminate_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_laminate_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_laminate_rpc)

/* unifyfs_read_rpc (client => server)
 *
 * given an app_id, client_id, global file id, an offset, and a length,
 * initiate read request for data */
MERCURY_GEN_PROC(unifyfs_read_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid))
                 ((hg_size_t)(offset))
                 ((hg_size_t)(length)))
MERCURY_GEN_PROC(unifyfs_read_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_read_rpc)

/* unifyfs_mread_rpc (client => server)
 *
 * given an app_id, client_id, and count of read requests,
 * followed by list of (gfid, offset, length) tuples,
 * initiate read requests for data */
MERCURY_GEN_PROC(unifyfs_mread_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(read_count))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(unifyfs_mread_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_mread_rpc)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_CLIENT_RPCS_H
