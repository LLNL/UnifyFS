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

#ifndef UNIFYFS_CLIENT_RPCS_H
#define UNIFYFS_CLIENT_RPCS_H

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

typedef enum {
    UNIFYFS_CLIENT_RPC_INVALID = 0,
    UNIFYFS_CLIENT_RPC_ATTACH,
    UNIFYFS_CLIENT_RPC_FILESIZE,
    UNIFYFS_CLIENT_RPC_LAMINATE,
    UNIFYFS_CLIENT_RPC_METAGET,
    UNIFYFS_CLIENT_RPC_METASET,
    UNIFYFS_CLIENT_RPC_MOUNT,
    UNIFYFS_CLIENT_RPC_READ,
    UNIFYFS_CLIENT_RPC_SYNC,
    UNIFYFS_CLIENT_RPC_TRANSFER,
    UNIFYFS_CLIENT_RPC_TRUNCATE,
    UNIFYFS_CLIENT_RPC_UNLINK,
    UNIFYFS_CLIENT_RPC_UNMOUNT,
    UNIFYFS_CLIENT_RPC_NODE_LOCAL_EXTENTS_GET
} client_rpc_e;

typedef enum {
    UNIFYFS_CLIENT_CALLBACK_INVALID = 0,
    UNIFYFS_CLIENT_CALLBACK_LAMINATE,
    UNIFYFS_CLIENT_CALLBACK_TRUNCATE,
    UNIFYFS_CLIENT_CALLBACK_UNLINK
} client_callback_e;

/* unifyfs_attach_rpc (client => server)
 *
 * initialize server access to client's shared memory and file state */
MERCURY_GEN_PROC(unifyfs_attach_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
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
                 ((int32_t)(client_id))
                 ((int32_t)(attr_op))
                 ((unifyfs_file_attr_t)(attr)))
MERCURY_GEN_PROC(unifyfs_metaset_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_metaset_rpc)

/* unifyfs_metaget_rpc (client => server)
 *
 * returns file metadata including size and name
 * given a global file id */
MERCURY_GEN_PROC(unifyfs_metaget_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_metaget_out_t,
                 ((int32_t)(ret))
                 ((unifyfs_file_attr_t)(attr)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_metaget_rpc)

/* unifyfs_fsync_rpc (client => server)
 *
 * given a client identified by (app_id, client_id) as input, read the write
 * extents for one or more of the client's files from the shared memory index
 * and update the global metadata for the file(s) */
MERCURY_GEN_PROC(unifyfs_fsync_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_fsync_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_fsync_rpc)

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

/* unifyfs_transfer_rpc (client => server)
 *
 * given an app_id, client_id, transfer id, global file id, transfer mode,
 * and a destination file path, transfer data to that file */
MERCURY_GEN_PROC(unifyfs_transfer_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(transfer_id))
                 ((int32_t)(gfid))
                 ((int32_t)(mode))
                 ((hg_const_string_t)(dst_file)))
MERCURY_GEN_PROC(unifyfs_transfer_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_transfer_rpc)

/* unifyfs_transfer_complete_rpc (server => client)
 *
 * Transfer completion response for a request with specified transfer_id.
 *
 * A non-zero error_code indicates the server encountered an error during
 * processing of the request. */
MERCURY_GEN_PROC(unifyfs_transfer_complete_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(transfer_id))
                 ((hg_size_t)(transfer_size_bytes))
                 ((uint32_t)(transfer_time_sec))
                 ((uint32_t)(transfer_time_usec))
                 ((int32_t)(error_code)))
MERCURY_GEN_PROC(unifyfs_transfer_complete_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_transfer_complete_rpc)

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

/* unifyfs_unlink_callback_rpc (server => client)
 *
 * given an app_id, client_id, and global file id,
 * free the client metadata and data associated with the file */
MERCURY_GEN_PROC(unifyfs_unlink_callback_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unifyfs_unlink_callback_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_unlink_callback_rpc)

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

/* unifyfs_mread_rpc (client => server)
 *
 * given mread (mread_id, app_id, client_id) and count of read requests,
 * followed by a bulk data array of read extents (unifyfs_extent_t),
 * initiate read requests for data */
MERCURY_GEN_PROC(unifyfs_mread_in_t,
                 ((int32_t)(mread_id))
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(read_count))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_extents)))
MERCURY_GEN_PROC(unifyfs_mread_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_mread_rpc)

/* unifyfs_mread_req_data_rpc (server => client)
 *
 * Bulk data response for a single read request located at the specified
 * read_index in the array of requests associated with the given mread_id.
 *
 * read_offset is the offset to be added to the start offset of the request,
 * and is used to transfer data for very large extents in multiple chunks. */
MERCURY_GEN_PROC(unifyfs_mread_req_data_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(mread_id))
                 ((int32_t)(read_index))
                 ((hg_size_t)(read_offset))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_data)))
MERCURY_GEN_PROC(unifyfs_mread_req_data_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_mread_req_data_rpc)

/* unifyfs_mread_req_complete_rpc (server => client)
 *
 * Request completion response for a single read request located at the
 * specified read_index in the array of requests associated with the given
 * mread_id.
 *
 * A non-zero read_error indicates the server encountered an error during
 * processing of the request. */
MERCURY_GEN_PROC(unifyfs_mread_req_complete_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id))
                 ((int32_t)(mread_id))
                 ((int32_t)(read_index))
                 ((int32_t)(read_error)))
MERCURY_GEN_PROC(unifyfs_mread_req_complete_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_mread_req_complete_rpc)

/* unifyfs_heartbeat_rpc (server => client)
 *
 * Used to detect when client unexpectedly goes away */
MERCURY_GEN_PROC(unifyfs_heartbeat_in_t,
                 ((int32_t)(app_id))
                 ((int32_t)(client_id)))
MERCURY_GEN_PROC(unifyfs_heartbeat_out_t, ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_heartbeat_rpc)

/* unifyfs_node_local_extents_get_rpc (client => server)
 *
 * returns node local extents for a file
 * given a global file id */
MERCURY_GEN_STRUCT_PROC(unifyfs_index_t,
                 ((uint32_t)(file_pos))
                 ((uint32_t)(log_pos))
                 ((uint32_t)(length))
                 ((int32_t)(gfid)))
/* List of extents used in the output of RPC call*/
typedef struct extents_list {
    unifyfs_index_t value;
    struct extents_list* next;
} *extents_list_t;
/* Custom serialization of the structure extents_list */
static inline hg_return_t hg_proc_extents_list_t(hg_proc_t proc, void* data)
{
    hg_return_t ret = HG_SUCCESS;
    extents_list_t* list = (extents_list_t*)data;

    hg_size_t length = 0;
    extents_list_t tmp   = NULL;
    extents_list_t prev  = NULL;

    switch (hg_proc_get_op(proc)) {

        case HG_ENCODE: {
            tmp = *list;
            // find out the length of the list
            while (tmp != NULL) {
                tmp = tmp->next;
                length += 1;
            }
            // write the length
            ret = hg_proc_hg_size_t(proc, &length);
            if (ret != HG_SUCCESS) {
                break;
            }
            // write the list
            tmp = *list;
            while (tmp != NULL) {
                ret = hg_proc_unifyfs_index_t(proc, &tmp->value);
                if (ret != HG_SUCCESS) {
                    break;
                }
                tmp = tmp->next;
            }
            break;
        }
        case HG_DECODE: {
            // find out the length of the list
            ret = hg_proc_hg_size_t(proc, &length);
            if (ret != HG_SUCCESS) {
                break;
            }
            // loop and create list elements
            *list = NULL;
            while (length > 0) {
                tmp = (extents_list_t)calloc(1, sizeof(*tmp));
                if (*list == NULL) {
                    *list = tmp;
                }
                if (prev != NULL) {
                    prev->next = tmp;
                }
                ret = hg_proc_unifyfs_index_t(proc, &tmp->value);
                if (ret != HG_SUCCESS) {
                    break;
                }
                prev = tmp;
                length -= 1;
            }
            break;
        }
        case HG_FREE: {
            tmp = *list;
            while (tmp != NULL) {
                prev = tmp;
                tmp  = prev->next;
                free(prev);
            }
            ret = HG_SUCCESS;
        }
    }
    return ret;
}

MERCURY_GEN_PROC(unifyfs_node_local_extents_get_in_t,
                 ((int32_t)(app_id))
                         ((int32_t)(client_id))
                         ((int32_t)(num_req))
                         ((extents_list_t)(read_req)))

MERCURY_GEN_PROC(unifyfs_node_local_extents_get_out_t,
                 ((int32_t)(ret))
                         ((int32_t)(extent_count))
                         ((extents_list_t)(bulk_data)))
DECLARE_MARGO_RPC_HANDLER(unifyfs_node_local_extents_get_rpc)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_CLIENT_RPCS_H
