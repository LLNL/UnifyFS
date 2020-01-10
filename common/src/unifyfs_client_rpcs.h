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

#ifdef __cplusplus
extern "C" {
#endif

/* unifyfs_mount_rpc (client => server)
 *
 * connect application client to the server, and
 * initialize shared memory state */
MERCURY_GEN_PROC(unifyfs_mount_in_t,
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
                 ((hg_size_t)(data_offset))
                 ((hg_size_t)(data_size))
                 ((hg_const_string_t)(external_spill_dir)))
MERCURY_GEN_PROC(unifyfs_mount_out_t,
                 ((hg_size_t)(max_recs_per_slice))
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_mount_rpc)

/* unifyfs_unmount_rpc (client => server)
 *
 * disconnect client from server */
MERCURY_GEN_PROC(unifyfs_unmount_in_t,
    ((int32_t)(app_id))
    ((int32_t)(local_rank_idx)))
MERCURY_GEN_PROC(unifyfs_unmount_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_unmount_rpc)

/* need to transfer timespec structs */
typedef struct timespec sys_timespec_t;
MERCURY_GEN_STRUCT_PROC(sys_timespec_t,
                        ((uint64_t)(tv_sec))
                        ((uint64_t)(tv_nsec)))

/* unifyfs_metaset_rpc (client => server)
 *
 * given a global file id and a file name,
 * record key/value entry for this file */
MERCURY_GEN_PROC(unifyfs_metaset_in_t,
                 ((int32_t)(create))
                 ((hg_const_string_t)(filename))
                 ((int32_t)(gfid))
                 ((uint32_t)(mode))
                 ((uint32_t)(uid))
                 ((uint32_t)(gid))
                 ((uint64_t)(size))
                 ((sys_timespec_t)(atime))
                 ((sys_timespec_t)(mtime))
                 ((sys_timespec_t)(ctime))
                 ((uint32_t)(is_laminated)))
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
                 ((hg_const_string_t)(filename))
                 ((int32_t)(gfid))
                 ((uint32_t)(mode))
                 ((uint32_t)(uid))
                 ((uint32_t)(gid))
                 ((uint64_t)(size))
                 ((sys_timespec_t)(atime))
                 ((sys_timespec_t)(mtime))
                 ((sys_timespec_t)(ctime))
                 ((uint32_t)(is_laminated)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_metaget_rpc)

/* unifyfs_fsync_rpc (client => server)
 *
 * given app_id, client_id, and a global file id as input,
 * read extent location metadata from client shared memory
 * and insert corresponding key/value pairs into global index */
MERCURY_GEN_PROC(unifyfs_fsync_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_fsync_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_fsync_rpc)

/* unifyfs_filesize_rpc (client => server)
 *
 * given an app_id, client_id, global file id,
 * return filesize for given file */
MERCURY_GEN_PROC(unifyfs_filesize_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
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
                 ((int32_t)(local_rank_idx))
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
                 ((int32_t)(local_rank_idx))
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
                 ((int32_t)(local_rank_idx))
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
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(gfid))
                 ((hg_size_t)(offset))
                 ((hg_size_t)(length)))
MERCURY_GEN_PROC(unifyfs_read_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_read_rpc)

/* unifyfs_mread_rpc (client => server)
 *
 * given an app_id, client_id, global file id, and a count
 * of read requests, followed by list of offset/length tuples
 * initiate read requests for data */
MERCURY_GEN_PROC(unifyfs_mread_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(local_rank_idx))
                 ((int32_t)(read_count))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(unifyfs_mread_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_mread_rpc)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_CLIENT_RPCS_H
