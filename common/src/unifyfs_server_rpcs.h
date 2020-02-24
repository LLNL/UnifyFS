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

/* filesize_request_rpc (server => server)
 *
 * initiates filesize request operation */
MERCURY_GEN_PROC(filesize_request_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(tag)))
MERCURY_GEN_PROC(filesize_request_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(filesize_request_rpc)

/* filesize_response_rpc (server => server)
 *
 * response to filesize request */
MERCURY_GEN_PROC(filesize_response_in_t,
                 ((int32_t)(tag))
                 ((hg_size_t)(filesize))
                 ((int32_t)(err)))
MERCURY_GEN_PROC(filesize_response_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(filesize_response_rpc)

/* truncate_request_rpc (server => server)
 *
 * initiates truncate request operation */
MERCURY_GEN_PROC(truncate_request_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(tag))
                 ((hg_size_t)(length)))
MERCURY_GEN_PROC(truncate_request_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(truncate_request_rpc)

/* truncate_response_rpc (server => server)
 *
 * response to truncate request */
MERCURY_GEN_PROC(truncate_response_in_t,
                 ((int32_t)(tag))
                 ((int32_t)(err)))
MERCURY_GEN_PROC(truncate_response_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(truncate_response_rpc)

/* metaset_request_rpc (server => server)
 *
 * initiates metaset request operation */
MERCURY_GEN_PROC(metaset_request_in_t,
                 ((int32_t)(root))
                 ((int32_t)(tag))
                 ((int32_t)(create))
                 ((unifyfs_file_attr_t)(attr)))
MERCURY_GEN_PROC(metaset_request_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(metaset_request_rpc)

/* metaset_response_rpc (server => server)
 *
 * response to metaset request */
MERCURY_GEN_PROC(metaset_response_in_t,
                 ((int32_t)(tag))
                 ((int32_t)(err)))
MERCURY_GEN_PROC(metaset_response_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(metaset_response_rpc)

/* unlink_request_rpc (server => server)
 *
 * initiates unlink request operation */
MERCURY_GEN_PROC(unlink_request_in_t,
                 ((int32_t)(root))
                 ((int32_t)(gfid))
                 ((int32_t)(tag)))
MERCURY_GEN_PROC(unlink_request_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unlink_request_rpc)

/* unlink_response_rpc (server => server)
 *
 * response to unlink request */
MERCURY_GEN_PROC(unlink_response_in_t,
                 ((int32_t)(tag))
                 ((int32_t)(err)))
MERCURY_GEN_PROC(unlink_response_out_t,
                 ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(unlink_response_rpc)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIFYFS_SERVER_RPCS_H
