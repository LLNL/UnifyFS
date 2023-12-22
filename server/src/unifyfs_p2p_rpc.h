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

#ifndef _UNIFYFS_P2P_RPC_H
#define _UNIFYFS_P2P_RPC_H

#include "unifyfs_global.h"
#include "extent_tree.h"
#include "margo_server.h"
#include "unifyfs_inode.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"


/* determine server responsible for maintaining target file's metadata */
int hash_gfid_to_server(int gfid);

/* server peer-to-peer (p2p) margo request structure */
typedef struct {
    margo_request request;
    hg_addr_t     peer;
    hg_handle_t   handle;
} p2p_request;

/* helper method to initialize peer request rpc handle */
int init_p2p_request_handle(hg_id_t request_hgid,
                            int peer_rank,
                            p2p_request* req);

/* helper method to forward peer rpc request */
int forward_p2p_request(void* input_ptr,
                        p2p_request* req);

/* helper method to wait for peer rpc request completion */
int wait_for_p2p_request(p2p_request* req);

/*** Point-to-point Server RPCs ***/

/**
 * @brief Request chunk reads from remote server
 *
 * @param dst_srvr_rank  remote server rank
 * @param rdreq          read request structure
 * @param remote_reads   server chunk reads
 *
 * @return success|failure
 */
int invoke_chunk_read_request_rpc(int dst_srvr_rank,
                                  server_read_req_t* rdreq,
                                  server_chunk_reads_t* remote_reads);
/**
 * @brief Respond to chunk read request
 *
 * @param scr  server chunk reads structure
 *
 * @return success|failure
 */
int invoke_chunk_read_response_rpc(server_chunk_reads_t* scr);

/**
 * @brief Add new extents to target file
 *
 * @param gfid         target file
 * @param num_extents  length of file extents array
 * @param extents      array of extents to add
 *
 * @return success|failure
 */
int unifyfs_invoke_add_extents_rpc(int gfid,
                                   unsigned int num_extents,
                                   extent_metadata* extents);

/**
 * @brief Find location of extents for target file
 *
 * @param gfid         target file
 * @param num_extents  length of file extents array
 * @param extents      array of extents to find
 *
 * @param[out] num_chunks  number of chunk locations
 * @param[out] chunks      array of chunk locations for requested extents
 *
 * @return success|failure
 */
int unifyfs_invoke_find_extents_rpc(int gfid,
                                    unsigned int num_extents,
                                    unifyfs_extent_t* extents,
                                    unsigned int* num_chunks,
                                    chunk_read_req_t** chunks);

/**
 * @brief Get file size for the target file
 *
 * @param gfid      target file
 * @param filesize  pointer to size variable
 *
 * @return success|failure
 */
int unifyfs_invoke_filesize_rpc(int gfid,
                                size_t* filesize);

/**
 * @brief Laminate the target file
 *
 * @param gfid  target file
 *
 * @return success|failure
 */
int unifyfs_invoke_laminate_rpc(int gfid);

/**
 * @brief Get metadata for target file
 *
 * @param gfid    target file
 * @param create  flag indicating if this is a newly created file
 * @param attr    file attributes to update
 *
 * @return success|failure
 */
int unifyfs_invoke_metaget_rpc(int gfid,
                               unifyfs_file_attr_t* attrs);

/**
 * @brief Update metadata for target file
 *
 * @param gfid     target file
 * @param attr_op  metadata operation that triggered update
 * @param attr     file attributes to update
 *
 * @return success|failure
 */
int unifyfs_invoke_metaset_rpc(int gfid, int attr_op,
                               unifyfs_file_attr_t* attrs);

/**
 * @brief Transfer target file
 *
 * @param client_app      requesting client app id
 * @param client_id       requesting client id
 * @param transfer_id     requesting client transfer id
 * @param gfid            target file
 * @param transfer_mode   transfer mode
 * @param dest_file       destination file
 *
 * @return success|failure
 */
int unifyfs_invoke_transfer_rpc(int client_app,
                                int client_id,
                                int transfer_id,
                                int gfid,
                                int transfer_mode,
                                const char* dest_file);

/**
 * @brief Truncate target file
 *
 * @param gfid      target file
 * @param filesize  truncated file size
 *
 * @return success|failure
 */
int unifyfs_invoke_truncate_rpc(int gfid, size_t filesize);

/**
 * @brief Report pid of local server to rank 0 server
 *
 * @return success|failure
 */
int unifyfs_invoke_server_pid_rpc(void);

#endif // UNIFYFS_P2P_RPC_H
