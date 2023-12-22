/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef __UNIFYFS_SERVER_RPCS_H
#define __UNIFYFS_SERVER_RPCS_H

/*
 * Declarations for server-server margo RPCs
 */

#include <time.h>
#include <abt.h>
#include <margo.h>
#include <mercury.h>
#include <mercury_proc_string.h>
#include <mercury_types.h>

#include "unifyfs_rpc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* enumerate the various server-to-server rpcs */
typedef enum {
    UNIFYFS_SERVER_RPC_INVALID = 0,
    UNIFYFS_SERVER_RPC_CHUNK_READ,
    UNIFYFS_SERVER_RPC_EXTENTS_ADD,
    UNIFYFS_SERVER_RPC_EXTENTS_FIND,
    UNIFYFS_SERVER_RPC_FILESIZE,
    UNIFYFS_SERVER_RPC_LAMINATE,
    UNIFYFS_SERVER_RPC_METAGET,
    UNIFYFS_SERVER_RPC_METASET,
    UNIFYFS_SERVER_RPC_TRANSFER,
    UNIFYFS_SERVER_RPC_TRUNCATE,
    UNIFYFS_SERVER_BCAST_RPC_BOOTSTRAP,
    UNIFYFS_SERVER_BCAST_RPC_EXTENTS,
    UNIFYFS_SERVER_BCAST_RPC_FILEATTR,
    UNIFYFS_SERVER_BCAST_RPC_LAMINATE,
    UNIFYFS_SERVER_BCAST_RPC_METAGET,
    UNIFYFS_SERVER_BCAST_RPC_TRANSFER,
    UNIFYFS_SERVER_BCAST_RPC_TRUNCATE,
    UNIFYFS_SERVER_BCAST_RPC_UNLINK,
    UNIFYFS_SERVER_PENDING_SYNC
} server_rpc_e;

/* structure to track server-to-server rpc request state */
typedef struct {
    server_rpc_e req_type;
    hg_handle_t handle;
    void* coll;
    void* input;
    void* bulk_buf;
    size_t bulk_sz;
} server_rpc_req_t;

/*---- Server Point-to-Point (p2p) RPCs ----*/

/* Report server pid to rank 0 */
MERCURY_GEN_PROC(server_pid_in_t,
                 ((int32_t)(rank))
                 ((int32_t)(pid)))
MERCURY_GEN_PROC(server_pid_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(server_pid_rpc)

/* Chunk read request */
MERCURY_GEN_PROC(chunk_read_request_in_t,
                 ((int32_t)(src_rank))
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(req_id))
                 ((int32_t)(num_chks))
                 ((hg_size_t)(total_data_size))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(chunk_read_request_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(chunk_read_request_rpc)

/* Chunk read response */
MERCURY_GEN_PROC(chunk_read_response_in_t,
                 ((int32_t)(src_rank))
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(req_id))
                 ((int32_t)(num_chks))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(chunk_read_response_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(chunk_read_response_rpc)

/* Add file extents at owner */
MERCURY_GEN_PROC(add_extents_in_t,
                 ((int32_t)(src_rank))
                 ((int32_t)(gfid))
                 ((int32_t)(num_extents))
                 ((hg_bulk_t)(extents)))
MERCURY_GEN_PROC(add_extents_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(add_extents_rpc)

/* Find file extent locations by querying owner */
MERCURY_GEN_PROC(find_extents_in_t,
                 ((int32_t)(src_rank))
                 ((int32_t)(gfid))
                 ((int32_t)(num_extents))
                 ((hg_bulk_t)(extents)))
MERCURY_GEN_PROC(find_extents_out_t,
                 ((int32_t)(num_locations))
                 ((hg_bulk_t)(locations))
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(find_extents_rpc)

/* Get file size from owner */
MERCURY_GEN_PROC(filesize_in_t,
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(filesize_out_t,
                 ((hg_size_t)(filesize))
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(filesize_rpc)

/* Laminate file at owner */
MERCURY_GEN_PROC(laminate_in_t,
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(laminate_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(laminate_rpc)

/* Get file metadata from owner */
MERCURY_GEN_PROC(metaget_in_t,
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(metaget_out_t,
                 ((unifyfs_file_attr_t)(attr))
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(metaget_rpc)

/* Set file metadata at owner */
MERCURY_GEN_PROC(metaset_in_t,
                 ((int32_t)(gfid))
                 ((int32_t)(fileop))
                 ((unifyfs_file_attr_t)(attr)))
MERCURY_GEN_PROC(metaset_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(metaset_rpc)

/* Transfer file */
MERCURY_GEN_PROC(transfer_in_t,
                 ((int32_t)(src_rank))
                 ((int32_t)(client_app))
                 ((int32_t)(client_id))
                 ((int32_t)(transfer_id))
                 ((int32_t)(gfid))
                 ((int32_t)(mode))
                 ((hg_const_string_t)(dst_file)))
MERCURY_GEN_PROC(transfer_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(transfer_rpc)

/* Truncate file at owner */
MERCURY_GEN_PROC(truncate_in_t,
                 ((int32_t)(gfid))
                 ((hg_size_t)(filesize)))
MERCURY_GEN_PROC(truncate_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(truncate_rpc)

/*---- Server Collective RPCs ----*/

/* Finish an ongoing broadcast rpc */
MERCURY_GEN_PROC(bcast_progress_in_t,
                 ((hg_ptr_t)(coll_req)))
MERCURY_GEN_PROC(bcast_progress_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bcast_progress_rpc)

/* Broadcast 'bootstrap complete' to all servers */
MERCURY_GEN_PROC(bootstrap_complete_bcast_in_t,
                 ((int32_t)(root)))
MERCURY_GEN_PROC(bootstrap_complete_bcast_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bootstrap_complete_bcast_rpc)

/* Broadcast file extents to all servers */
MERCURY_GEN_PROC(extent_bcast_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(num_extents))
                 ((hg_bulk_t)(extents)))
MERCURY_GEN_PROC(extent_bcast_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(extent_bcast_rpc)

/* Broadcast file metadata to all servers */
MERCURY_GEN_PROC(fileattr_bcast_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(attrop))
                 ((unifyfs_file_attr_t)(attr)))
MERCURY_GEN_PROC(fileattr_bcast_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(fileattr_bcast_rpc)

/* Broadcast laminated file metadata to all servers */
MERCURY_GEN_PROC(laminate_bcast_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(num_extents))
                 ((unifyfs_file_attr_t)(attr))
                 ((hg_bulk_t)(extents)))
MERCURY_GEN_PROC(laminate_bcast_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(laminate_bcast_rpc)

/* Broadcast transfer request to all servers */
MERCURY_GEN_PROC(transfer_bcast_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(mode))
                 ((hg_const_string_t)(dst_file)))
MERCURY_GEN_PROC(transfer_bcast_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(transfer_bcast_rpc)

/* Broadcast truncation point to all servers */
MERCURY_GEN_PROC(truncate_bcast_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((hg_size_t)(filesize)))
MERCURY_GEN_PROC(truncate_bcast_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(truncate_bcast_rpc)

/* Broadcast unlink to all servers */
MERCURY_GEN_PROC(unlink_bcast_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unlink_bcast_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unlink_bcast_rpc)

/* Broadcast request for metadata to all servers */
/* Sends a request to all servers to reply with a the metadata for
 * all files that they own. */
MERCURY_GEN_PROC(metaget_all_bcast_in_t,
                 ((int32_t)(root)))
MERCURY_GEN_PROC(metaget_all_bcast_out_t,
                 ((int32_t)(num_files))
                 ((hg_bulk_t)(file_meta))
                 ((hg_string_t)(filenames))
                 ((int32_t)(ret)))
/* file_meta will be an array of unifyfs_file_attr_t structs.  Since
 * these structs store the filename in separately allocated memory, we'll
 * have to send all the filenames separately from the array of structs.
 * That's what filenames is for: we'll concatenate all the filenames into
 * a single hg_string_t, send that and then recreate correct
 * unifyfs_file_attr_t structs at the receiving end. */
DECLARE_MARGO_RPC_HANDLER(metaget_all_bcast_rpc)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_SERVER_RPCS_H
