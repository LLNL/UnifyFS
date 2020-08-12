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
#include <margo.h>
#include <mercury.h>
#include <mercury_proc_string.h>
#include <mercury_types.h>

#include "unifyfs_rpc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* Truncate file at owner */
MERCURY_GEN_PROC(truncate_in_t,
                 ((int32_t)(gfid))
                 ((hg_size_t)(filesize)))
MERCURY_GEN_PROC(truncate_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(truncate_rpc)

/*---- Collective RPCs ----*/

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


#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_SERVER_RPCS_H
