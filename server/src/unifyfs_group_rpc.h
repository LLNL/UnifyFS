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

#ifndef _UNIFYFS_GROUP_RPC_H
#define _UNIFYFS_GROUP_RPC_H

#include "unifyfs_global.h"
#include "unifyfs_inode.h"
#include "unifyfs_service_manager.h"
#include "unifyfs_tree.h"
#include "margo_server.h"

/* Collective Server RPCs */

/* server collective (coll) request state structure */
typedef struct {
    server_rpc_e   req_type;
    unifyfs_tree_t tree;
    hg_handle_t    resp_hdl;
    size_t         output_sz;    /* size of output struct */
    void*          output;       /* output struct (type is dependent on rpc) */
    void*          input;
    void*          bulk_buf;     /* allocated buffer for bulk data */
    hg_bulk_t      bulk_in;
    hg_bulk_t      bulk_forward;
    margo_request* child_reqs;
    hg_handle_t*   child_hdls;
} coll_request;

/* set collective output return value to local result value */
void collective_set_local_retval(coll_request* coll_req, int val);

/**
 * @brief Progress an ongoing broadcast tree operation
 *
 * @param coll_req    the broadcast collective
 *
 * @return success|failure
 */
int invoke_bcast_progress_rpc(coll_request* coll_req);

/**
 * @brief Broadcast file extents metadata to all servers
 *
 * @param gfid     target file
 * @param len      length of file extents array
 * @param extents  array of extents to broadcast
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_extents(int gfid,
                                     unsigned int len,
                                     struct extent_tree_node* extents);

/**
 * @brief Broadcast file attributes metadata to all servers
 *
 * @param gfid      target file
 * @param fileop    file operation that triggered metadata update
 * @param attr      file attributes
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_fileattr(int gfid,
                                      int fileop,
                                      unifyfs_file_attr_t* attr);

/**
 * @brief Broadcast file attributes and extent metadata to all servers
 *
 * @param gfid      target file
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_laminate(int gfid);

/**
 * @brief Truncate target file at all servers
 *
 * @param gfid      target file
 * @param filesize  truncated file size
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_truncate(int gfid,
                                      size_t filesize);

/**
 * @brief Unlink file at all servers
 *
 * @param gfid  target file
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_unlink(int gfid);


#endif // UNIFYFS_GROUP_RPC_H
