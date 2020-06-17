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

/* server_hello_rpc (server => server)
 *
 * say hello from one server to another */
MERCURY_GEN_PROC(server_hello_in_t,
                 ((int32_t)(src_rank))
                 ((hg_const_string_t)(message_str)))
MERCURY_GEN_PROC(server_hello_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(server_hello_rpc)

/* server_pid_rpc (server => server, n:1)
 *
 * notify readiness with pid to the master server (rank:0) */
MERCURY_GEN_PROC(server_pid_in_t,
                 ((int32_t)(rank))
                 ((int32_t)(pid)))
MERCURY_GEN_PROC(server_pid_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(server_pid_handle_rpc)

/* server_request_rpc (server => server)
 *
 * request from one server to another */
MERCURY_GEN_PROC(server_request_in_t,
                 ((int32_t)(src_rank))
                 ((int32_t)(req_id))
                 ((int32_t)(req_tag))
                 ((hg_size_t)(bulk_size))
                 ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(server_request_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(server_request_rpc)

/* chunk_read_request_rpc (server => server)
 *
 * request for chunk reads from another server */
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

/* chunk_read_response_rpc (server => server)
 *
 * response to remote chunk reads request */
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

/* server collective operations: */

/* extbcast_request_rpc (server => server)
 *
 * initiates extbcast request operation */
MERCURY_GEN_PROC(extbcast_request_in_t,
        ((int32_t)(root))
        ((int32_t)(gfid))
        ((int32_t)(num_extends))
        ((hg_bulk_t)(exttree)))
MERCURY_GEN_PROC(extbcast_request_out_t,
        ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(extbcast_request_rpc)

/*
 * filesize (server => server)
 */
MERCURY_GEN_PROC(filesize_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(filesize_out_t,
                 ((hg_size_t)(filesize))
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(filesize_rpc)

/*
 * truncate (server => server)
 */
MERCURY_GEN_PROC(truncate_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((hg_size_t)(filesize)))
MERCURY_GEN_PROC(truncate_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(truncate_rpc)

/*
 * metaset (server => server)
 */
MERCURY_GEN_PROC(metaset_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(create))
                 ((unifyfs_file_attr_t)(attr)))
MERCURY_GEN_PROC(metaset_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(metaset_rpc)

/*
 * unlink (server => server)
 */
MERCURY_GEN_PROC(unlink_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(unlink_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unlink_rpc)

/*
 * laminate (server => server)
 */
MERCURY_GEN_PROC(laminate_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid)))
MERCURY_GEN_PROC(laminate_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(laminate_rpc)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_SERVER_RPCS_H
