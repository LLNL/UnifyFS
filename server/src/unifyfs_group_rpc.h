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
#include "unifyfs_tree.h"
#include "margo_server.h"

/* Collective Server RPCs */

/* server collective (coll) request state structure */
typedef struct coll_request {
    server_rpc_e   req_type;
    int            app_id;
    int            client_id;
    int            client_req_id;
    unifyfs_tree_t tree;
    hg_handle_t    progress_hdl;
    hg_handle_t    resp_hdl;
    size_t         output_sz;       /* size of output struct */
    void*          output;          /* output struct (type depends on rpc) */
    void*          input;
    void*          bulk_buf;        /* allocated buffer for bulk data */
    hg_bulk_t      bulk_in;
    hg_bulk_t      bulk_forward;
    margo_request  progress_req;
    margo_request* child_reqs;
    hg_handle_t*   child_hdls;

    int            auto_cleanup;    /* If set (non-zero), bcast_progress_rpc()
                                     * will call collective_cleanup(). This is
                                     * the default behavior. */
    ABT_cond       resp_valid_cond; /* bcast_progress_rpc() will signal this
                                     * condition variable when all the child
                                     * responses have been processed.
                                     * Provides a mechanism for the root
                                     * server for a bcast RPC to wait for all
                                     * the results to come back. */
    ABT_mutex      resp_valid_sync; /* mutex for above condition variable */
} coll_request;

/* set collective output return value to local result value */
void collective_set_local_retval(coll_request* coll_req, int val);

/* finish collective process by waiting for any child responses and
 * sending parent response (if applicable) */
int collective_finish(coll_request* coll_req);

/* release resources associated with collective */
void collective_cleanup(coll_request* coll_req);

/**
 * @brief Progress an ongoing broadcast tree operation
 *
 * @param coll_req    the broadcast collective
 *
 * @return success|failure
 */
int invoke_bcast_progress_rpc(coll_request* coll_req);


/**
 * @brief Broadcast that all servers have completed bootstrapping
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_bootstrap_complete(void);

/**
 * @brief Broadcast file extents metadata to all servers
 *
 * @param gfid     target file
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_extents(int gfid);

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
 * @brief Broadcast request to transfer file to all servers
 *
 * @param gfid      target file
 *
 * @return success|failure
 */
int unifyfs_invoke_broadcast_transfer(int client_app,
                                      int client_id,
                                      int transfer_id,
                                      int gfid,
                                      int transfer_mode,
                                      const char* dest_file);

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

/**
 * @brief Fetch metadata for all files on all servers
 *
 * @param file_attrs list of file metadata
 * @param num_file_attrs number of files in the list
 *
 * @return success|failure
 */
/* Upon success, file_attrs will hold data that has been allocated with
 * malloc() and must be freed by the caller with free().  In the event of an
 * error, the caller must *NOT* free the pointer. */
int unifyfs_invoke_broadcast_metaget_all(unifyfs_file_attr_t** file_attrs,
                                         int* num_file_attrs);

#endif // UNIFYFS_GROUP_RPC_H
