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

// common headers
#include "unifyfs_keyval.h"
#include "unifyfs_client_rpcs.h"
#include "unifyfs_server_rpcs.h"
#include "unifyfs_rpc_util.h"

// server headers
#include "unifyfs_global.h"
#include "margo_server.h"
#include "na_config.h" // from mercury include lib
#include "mercury_log.h"

// global variables
ServerRpcContext_t* unifyfsd_rpc_context;
bool margo_use_tcp = true;
bool margo_lazy_connect; // = false
int  margo_client_server_pool_sz = 4;
int  margo_server_server_pool_sz = 4;
int  margo_use_progress_thread = 1;

#if defined(NA_HAS_SM)
static const char* PROTOCOL_MARGO_SHM = "na+sm";
#else
#error Required Mercury NA shared memory plugin not found (please enable 'SM')
#endif

#if !defined(NA_HAS_BMI) && !defined(NA_HAS_OFI)
#error No supported Mercury NA plugin found (please use one of: 'BMI', 'OFI')
#endif

#if defined(NA_HAS_BMI)
static const char* PROTOCOL_MARGO_BMI_TCP = "bmi+tcp";
#else
static const char* PROTOCOL_MARGO_BMI_TCP;
#endif

#if defined(NA_HAS_OFI)
static const char* PROTOCOL_MARGO_OFI_SOCKETS = "ofi+sockets";
static const char* PROTOCOL_MARGO_OFI_TCP = "ofi+tcp";
static const char* PROTOCOL_MARGO_OFI_RMA = "ofi+verbs";
#else
static const char* PROTOCOL_MARGO_OFI_SOCKETS;
static const char* PROTOCOL_MARGO_OFI_TCP;
static const char* PROTOCOL_MARGO_OFI_RMA;
#endif

/* setup_remote_target - Initializes the server-server margo target */
static margo_instance_id setup_remote_target(void)
{
    /* initialize margo */
    hg_return_t hret;
    hg_addr_t addr_self;
    char self_string[128];
    hg_size_t self_string_sz = sizeof(self_string);
    margo_instance_id mid;
    const char* margo_protocol;

    /* by default we try to use ofi */
    margo_protocol = margo_use_tcp ?
                     PROTOCOL_MARGO_OFI_TCP : PROTOCOL_MARGO_OFI_RMA;

    /* when ofi is not available, fallback to using bmi */
    if (!margo_protocol) {
        LOGWARN("OFI is not available, using BMI for margo rpc");
        margo_protocol = PROTOCOL_MARGO_BMI_TCP;
    }

    mid = margo_init(margo_protocol, MARGO_SERVER_MODE,
                     margo_use_progress_thread,
                     margo_server_server_pool_sz);
    if (mid == MARGO_INSTANCE_NULL) {
        LOGERR("margo_init(%s, SERVER_MODE, %d, %d) failed",
               margo_protocol, margo_use_progress_thread,
               margo_server_server_pool_sz);
        if (margo_protocol == PROTOCOL_MARGO_OFI_TCP) {
            /* try "ofi+sockets" instead */
            margo_protocol = PROTOCOL_MARGO_OFI_SOCKETS;
            mid = margo_init(margo_protocol, MARGO_SERVER_MODE,
                             margo_use_progress_thread,
                             margo_server_server_pool_sz);
            if (mid == MARGO_INSTANCE_NULL) {
                LOGERR("margo_init(%s, SERVER_MODE, %d, %d) failed",
                       margo_protocol, margo_use_progress_thread,
                       margo_server_server_pool_sz);
                return mid;
            }
        }
    }

    /* figure out what address this server is listening on */
    hret = margo_addr_self(mid, &addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_self() failed");
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    hret = margo_addr_to_string(mid,
                                self_string, &self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_to_string() failed");
        margo_addr_free(mid, addr_self);
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    LOGINFO("margo RPC server: %s", self_string);
    margo_addr_free(mid, addr_self);

    /* publish rpc address of server for remote servers */
    rpc_publish_remote_server_addr(self_string);

    return mid;
}

/* register server-server RPCs */
static void register_server_server_rpcs(margo_instance_id mid)
{
    unifyfsd_rpc_context->rpcs.bcast_progress_id =
        MARGO_REGISTER(mid, "bcast_progress_rpc",
                       bcast_progress_in_t, bcast_progress_out_t,
                       bcast_progress_rpc);

    unifyfsd_rpc_context->rpcs.chunk_read_request_id =
        MARGO_REGISTER(mid, "chunk_read_request_rpc",
                       chunk_read_request_in_t, chunk_read_request_out_t,
                       chunk_read_request_rpc);

    unifyfsd_rpc_context->rpcs.chunk_read_response_id =
        MARGO_REGISTER(mid, "chunk_read_response_rpc",
                       chunk_read_response_in_t, chunk_read_response_out_t,
                       chunk_read_response_rpc);

    unifyfsd_rpc_context->rpcs.extent_add_id =
        MARGO_REGISTER(mid, "add_extents_rpc",
                       add_extents_in_t, add_extents_out_t,
                       add_extents_rpc);

    unifyfsd_rpc_context->rpcs.extent_bcast_id =
        MARGO_REGISTER(mid, "extent_bcast_rpc",
                       extent_bcast_in_t, extent_bcast_out_t,
                       extent_bcast_rpc);

    unifyfsd_rpc_context->rpcs.extent_lookup_id =
        MARGO_REGISTER(mid, "find_extents_rpc",
                       find_extents_in_t, find_extents_out_t,
                       find_extents_rpc);

    unifyfsd_rpc_context->rpcs.fileattr_bcast_id =
        MARGO_REGISTER(mid, "fileattr_bcast_rpc",
                       fileattr_bcast_in_t, fileattr_bcast_out_t,
                       fileattr_bcast_rpc);

    unifyfsd_rpc_context->rpcs.filesize_id =
        MARGO_REGISTER(mid, "filesize_rpc",
                       filesize_in_t, filesize_out_t,
                       filesize_rpc);

    unifyfsd_rpc_context->rpcs.laminate_id =
        MARGO_REGISTER(mid, "laminate_rpc",
                       laminate_in_t, laminate_out_t,
                       laminate_rpc);

    unifyfsd_rpc_context->rpcs.laminate_bcast_id =
        MARGO_REGISTER(mid, "laminate_bcast_rpc",
                       laminate_bcast_in_t, laminate_bcast_out_t,
                       laminate_bcast_rpc);

    unifyfsd_rpc_context->rpcs.metaget_id =
        MARGO_REGISTER(mid, "metaget_rpc",
                       metaget_in_t, metaget_out_t,
                       metaget_rpc);

    unifyfsd_rpc_context->rpcs.metaset_id =
        MARGO_REGISTER(mid, "metaset_rpc",
                       metaset_in_t, metaset_out_t,
                       metaset_rpc);

    unifyfsd_rpc_context->rpcs.server_pid_id =
        MARGO_REGISTER(mid, "server_pid_rpc",
                       server_pid_in_t, server_pid_out_t,
                       server_pid_rpc);

    unifyfsd_rpc_context->rpcs.truncate_id =
        MARGO_REGISTER(mid, "truncate_rpc",
                       truncate_in_t, truncate_out_t,
                       truncate_rpc);

    unifyfsd_rpc_context->rpcs.truncate_bcast_id =
        MARGO_REGISTER(mid, "truncate_bcast_rpc",
                       truncate_bcast_in_t, truncate_bcast_out_t,
                       truncate_bcast_rpc);

    unifyfsd_rpc_context->rpcs.unlink_bcast_id =
        MARGO_REGISTER(mid, "unlink_bcast_rpc",
                       unlink_bcast_in_t, unlink_bcast_out_t,
                       unlink_bcast_rpc);
}

/* setup_local_target - Initializes the client-server margo target */
static margo_instance_id setup_local_target(void)
{
    /* initialize margo */
    const char* margo_protocol = PROTOCOL_MARGO_SHM;
    hg_return_t hret;
    hg_addr_t addr_self;
    char self_string[128];
    hg_size_t self_string_sz = sizeof(self_string);
    margo_instance_id mid;
    mid = margo_init(margo_protocol, MARGO_SERVER_MODE,
                     margo_use_progress_thread, margo_client_server_pool_sz);
    if (mid == MARGO_INSTANCE_NULL) {
        LOGERR("margo_init(%s, SERVER_MODE, %d, %d) failed", margo_protocol,
               margo_use_progress_thread, margo_client_server_pool_sz);
        return mid;
    }

    /* figure out what address this server is listening on */
    hret = margo_addr_self(mid, &addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_self() failed");
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    hret = margo_addr_to_string(mid,
                                self_string, &self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_to_string() failed");
        margo_addr_free(mid, addr_self);
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    LOGINFO("shared-memory margo RPC server: %s", self_string);
    margo_addr_free(mid, addr_self);

    /* publish rpc address of server for local clients */
    rpc_publish_local_server_addr(self_string);

    return mid;
}

/* register client-server RPCs */
static void register_client_server_rpcs(margo_instance_id mid)
{
    /* register the RPC handler functions */
    MARGO_REGISTER(mid, "unifyfs_attach_rpc",
                   unifyfs_attach_in_t, unifyfs_attach_out_t,
                   unifyfs_attach_rpc);

    MARGO_REGISTER(mid, "unifyfs_mount_rpc",
                   unifyfs_mount_in_t, unifyfs_mount_out_t,
                   unifyfs_mount_rpc);

    MARGO_REGISTER(mid, "unifyfs_unmount_rpc",
                   unifyfs_unmount_in_t, unifyfs_unmount_out_t,
                   unifyfs_unmount_rpc);

    MARGO_REGISTER(mid, "unifyfs_metaget_rpc",
                   unifyfs_metaget_in_t, unifyfs_metaget_out_t,
                   unifyfs_metaget_rpc);

    MARGO_REGISTER(mid, "unifyfs_metaset_rpc",
                   unifyfs_metaset_in_t, unifyfs_metaset_out_t,
                   unifyfs_metaset_rpc);

    MARGO_REGISTER(mid, "unifyfs_fsync_rpc",
                   unifyfs_fsync_in_t, unifyfs_fsync_out_t,
                   unifyfs_fsync_rpc);

    MARGO_REGISTER(mid, "unifyfs_filesize_rpc",
                   unifyfs_filesize_in_t, unifyfs_filesize_out_t,
                   unifyfs_filesize_rpc);

    MARGO_REGISTER(mid, "unifyfs_truncate_rpc",
                   unifyfs_truncate_in_t, unifyfs_truncate_out_t,
                   unifyfs_truncate_rpc);

    MARGO_REGISTER(mid, "unifyfs_unlink_rpc",
                   unifyfs_unlink_in_t, unifyfs_unlink_out_t,
                   unifyfs_unlink_rpc);

    MARGO_REGISTER(mid, "unifyfs_laminate_rpc",
                   unifyfs_laminate_in_t, unifyfs_laminate_out_t,
                   unifyfs_laminate_rpc);

    MARGO_REGISTER(mid, "unifyfs_mread_rpc",
                   unifyfs_mread_in_t, unifyfs_mread_out_t,
                   unifyfs_mread_rpc);

    /* register the RPCs we call (and capture assigned hg_id_t) */
    unifyfsd_rpc_context->rpcs.client_mread_data_id =
        MARGO_REGISTER(mid, "unifyfs_mread_req_data_rpc",
                       unifyfs_mread_req_data_in_t,
                       unifyfs_mread_req_data_out_t,
                       NULL);

    unifyfsd_rpc_context->rpcs.client_mread_complete_id =
        MARGO_REGISTER(mid, "unifyfs_mread_req_complete_rpc",
                       unifyfs_mread_req_complete_in_t,
                       unifyfs_mread_req_complete_out_t,
                       NULL);

    unifyfsd_rpc_context->rpcs.client_heartbeat_id =
            MARGO_REGISTER(mid, "unifyfs_heartbeat_rpc",
                           unifyfs_heartbeat_in_t,
                           unifyfs_heartbeat_out_t,
                           NULL);

}

/* margo_server_rpc_init
 *
 * Initialize the server's Margo RPC functionality, for
 * both intra-node (client-server shared memory) and
 * inter-node (server-server).
 */
int margo_server_rpc_init(void)
{
    int rc = UNIFYFS_SUCCESS;

    if (NULL == unifyfsd_rpc_context) {
        /* create rpc server context */
        unifyfsd_rpc_context = calloc(1, sizeof(ServerRpcContext_t));
        if (NULL == unifyfsd_rpc_context) {
            return ENOMEM;
        }
    }

#if defined(HG_VERSION_MAJOR) && (HG_VERSION_MAJOR > 1)
    /* redirect mercury log to ours, using current log level */
    const char* mercury_log_level = NULL;
    switch (unifyfs_log_level) {
    case LOG_DBG:
        mercury_log_level = "debug";
        break;
    case LOG_ERR:
        mercury_log_level = "error";
        break;
    case LOG_WARN:
        mercury_log_level = "warning";
        break;
    default:
        break;
    }
    if (NULL != mercury_log_level) {
        HG_Set_log_level(mercury_log_level);
        NA_Set_log_level(mercury_log_level);
    }
    if (NULL != unifyfs_log_stream) {
        hg_log_set_stream_debug(unifyfs_log_stream);
        hg_log_set_stream_error(unifyfs_log_stream);
        hg_log_set_stream_warning(unifyfs_log_stream);
    }
#endif

    margo_instance_id mid;
    mid = setup_local_target();
    if (mid == MARGO_INSTANCE_NULL) {
        rc = UNIFYFS_ERROR_MARGO;
    } else {
        unifyfsd_rpc_context->shm_mid = mid;
        register_client_server_rpcs(mid);
    }

    mid = setup_remote_target();
    if (mid == MARGO_INSTANCE_NULL) {
        rc = UNIFYFS_ERROR_MARGO;
    } else {
        unifyfsd_rpc_context->svr_mid = mid;
        register_server_server_rpcs(mid);
    }

    return rc;
}

/* margo_server_rpc_finalize
 *
 * Finalize the server's Margo RPC functionality, for
 * both intra-node (client-server shared memory) and
 * inter-node (server-server).
 */
int margo_server_rpc_finalize(void)
{
    int rc = UNIFYFS_SUCCESS;

    if (NULL != unifyfsd_rpc_context) {
        /* define a temporary to refer to context */
        ServerRpcContext_t* ctx = unifyfsd_rpc_context;
        unifyfsd_rpc_context = NULL;

        rpc_clean_local_server_addr();

        /* free global server addresses */
        for (int i = 0; i < glb_num_servers; i++) {
            if (glb_servers[i].margo_svr_addr != HG_ADDR_NULL) {
                margo_addr_free(ctx->svr_mid, glb_servers[i].margo_svr_addr);
                glb_servers[i].margo_svr_addr = HG_ADDR_NULL;
            }
            if (NULL != glb_servers[i].margo_svr_addr_str) {
                free(glb_servers[i].margo_svr_addr_str);
                glb_servers[i].margo_svr_addr_str = NULL;
            }
        }

        /* shut down margo */
        LOGDBG("finalizing server-server margo");
        margo_finalize(ctx->svr_mid);
        /* NOTE: 2nd call to margo_finalize() sometimes crashes - Margo bug? */
        LOGDBG("finalizing client-server margo");
        margo_finalize(ctx->shm_mid);

        /* free memory allocated for context structure */
        free(ctx);
    }

    return rc;
}

int margo_connect_server(int rank)
{
    assert(rank < glb_num_servers);

    int ret = UNIFYFS_SUCCESS;
    char* margo_addr_str = rpc_lookup_remote_server_addr(rank);
    if (NULL == margo_addr_str) {
        LOGERR("server index=%d - margo server lookup failed", rank);
        ret = UNIFYFS_ERROR_KEYVAL;
        return ret;
    }
    glb_servers[rank].margo_svr_addr_str = margo_addr_str;
    LOGDBG("server rank=%d, margo_addr=%s", rank, margo_addr_str);

    hg_return_t hret = margo_addr_lookup(unifyfsd_rpc_context->svr_mid,
                                         glb_servers[rank].margo_svr_addr_str,
                                         &(glb_servers[rank].margo_svr_addr));
    if (hret != HG_SUCCESS) {
        LOGERR("server index=%zu - margo_addr_lookup(%s) failed",
               rank, margo_addr_str);
        ret = UNIFYFS_ERROR_MARGO;
    }

    return ret;
}

/* margo_connect_servers
 *
 * Using address strings found in glb_servers, resolve
 * each peer server's margo address.
 */
int margo_connect_servers(void)
{
    int rc;
    int ret = UNIFYFS_SUCCESS;
    int i;

    // block until a margo_svr key pair published by all servers
    rc = unifyfs_keyval_fence_remote();
    if ((int)UNIFYFS_SUCCESS != rc) {
        LOGERR("keyval fence on margo_svr key failed");
        ret = UNIFYFS_ERROR_KEYVAL;
        return ret;
    }

    for (i = 0; i < (int)glb_num_servers; i++) {
        glb_servers[i].pmi_rank = i;
        glb_servers[i].margo_svr_addr = HG_ADDR_NULL;
        glb_servers[i].margo_svr_addr_str = NULL;
        if (!margo_lazy_connect) {
            rc = margo_connect_server(i);
            if (rc != UNIFYFS_SUCCESS) {
                ret = rc;
            }
        }
    }

    return ret;
}

hg_addr_t get_margo_server_address(int rank)
{
    assert(rank < glb_num_servers);
    hg_addr_t addr = glb_servers[rank].margo_svr_addr;
    if ((HG_ADDR_NULL == addr) && margo_lazy_connect) {
        int rc = margo_connect_server(rank);
        if (rc == UNIFYFS_SUCCESS) {
            addr = glb_servers[rank].margo_svr_addr;
        }
    }
    return addr;
}

/* Use passed bulk handle to pull data into a newly allocated buffer.
 * If local_bulk is not NULL, will set to local bulk handle on success.
 * Returns bulk buffer, or NULL on failure. */
void* pull_margo_bulk_buffer(hg_handle_t rpc_hdl,
                             hg_bulk_t bulk_remote,
                             hg_size_t bulk_sz,
                             hg_bulk_t* local_bulk)
{
    if (0 == bulk_sz) {
        return NULL;
    }

    size_t sz = (size_t) bulk_sz;
    void* buffer = malloc(sz);
    if (NULL == buffer) {
        LOGERR("failed to allocate buffer(sz=%zu) for bulk transfer", sz);
        return NULL;
    }

    /* get mercury info to set up bulk transfer */
    const struct hg_info* hgi = margo_get_info(rpc_hdl);
    assert(hgi);
    margo_instance_id mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* register local target buffer for bulk access */
    hg_bulk_t bulk_local;
    hg_return_t hret = margo_bulk_create(mid, 1, &buffer, &bulk_sz,
                                         HG_BULK_READWRITE, &bulk_local);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_bulk_create() failed");
        free(buffer);
        return NULL;
    }

    /* execute the transfer to pull data from remote side
     * into our local buffer.
     *
     * NOTE: mercury/margo bulk transfer does not check the maximum
     * transfer size that the underlying transport supports, and a
     * large bulk transfer may result in failure. */
    int i = 0;
    hg_size_t remain = bulk_sz;
    do {
        hg_size_t offset = i * MAX_BULK_TX_SIZE;
        hg_size_t len = remain < MAX_BULK_TX_SIZE ? remain : MAX_BULK_TX_SIZE;
        hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr,
                                   bulk_remote, offset,
                                   bulk_local, offset, len);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_bulk_transfer(buf_offset=%zu, len=%zu) failed",
                   (size_t)offset, (size_t)len);
            break;
        }
        remain -= len;
        i++;
    } while (remain > 0);

    if (hret == HG_SUCCESS) {
        LOGDBG("successful bulk transfer (%zu bytes)", bulk_sz);
        if (local_bulk != NULL) {
            *local_bulk = bulk_local;
        } else {
            /* deregister our bulk transfer buffer */
            margo_bulk_free(bulk_local);
        }
        return buffer;
    } else {
        LOGERR("failed bulk transfer - transferred %zu of %zu bytes",
               (bulk_sz - remain), bulk_sz);
        free(buffer);
        return NULL;
    }
}

/* MARGO CLIENT-SERVER RPC INVOCATION FUNCTIONS */

/* create and return a margo handle for given rpc id and app-client */
static hg_handle_t create_client_handle(hg_id_t id,
                                        int app_id,
                                        int client_id)
{
    hg_handle_t handle = HG_HANDLE_NULL;

    /* lookup application client */
    app_client* client = get_app_client(app_id, client_id);
    if (NULL != client) {
        hg_addr_t client_addr = client->margo_addr;

        /* create handle for specified rpc */
        hg_return_t hret = margo_create(unifyfsd_rpc_context->shm_mid,
                                        client_addr, id, &handle);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_create() failed");
        }
    } else {
        LOGERR("invalid app-client [%d:%d]", app_id, client_id);
    }
    return handle;
}

/* invokes the client mread request data response rpc function */
int invoke_client_mread_req_data_rpc(int app_id,
                                     int client_id,
                                     int mread_id,
                                     int read_index,
                                     size_t read_offset,
                                     size_t extent_size,
                                     void* extent_buffer)
{
    hg_return_t hret;

    /* check that we have initialized margo */
    if (NULL == unifyfsd_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* fill input struct */
    unifyfs_mread_req_data_in_t in;
    in.mread_id      = (int32_t) mread_id;
    in.read_index    = (int32_t) read_index;
    in.read_offset   = (hg_size_t) read_offset;
    in.bulk_size     = (hg_size_t) extent_size;

    if (extent_size > 0) {
        /* initialize bulk handle for extents */
        hret = margo_bulk_create(unifyfsd_rpc_context->shm_mid,
                                 1, &extent_buffer, &extent_size,
                                 HG_BULK_READ_ONLY, &in.bulk_data);
        if (hret != HG_SUCCESS) {
            return UNIFYFS_ERROR_MARGO;
        }
    }

    /* get handle to rpc function */
    hg_id_t rpc_id = unifyfsd_rpc_context->rpcs.client_mread_data_id;
    hg_handle_t handle = create_client_handle(rpc_id, app_id, client_id);

    /* call rpc function */
    LOGDBG("invoking the mread req data rpc function in client");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_mread_req_data_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    if (extent_size > 0) {
        margo_bulk_free(in.bulk_data);
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the client mread request completion rpc function */
int invoke_client_mread_req_complete_rpc(int app_id,
                                         int client_id,
                                         int mread_id,
                                         int read_index,
                                         int read_error)
{
    hg_return_t hret;

    /* check that we have initialized margo */
    if (NULL == unifyfsd_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* fill input struct */
    unifyfs_mread_req_complete_in_t in;
    in.mread_id      = (int32_t) mread_id;
    in.read_index    = (int32_t) read_index;
    in.read_error    = (int32_t) read_error;

    /* get handle to rpc function */
    hg_id_t rpc_id = unifyfsd_rpc_context->rpcs.client_mread_complete_id;
    hg_handle_t handle = create_client_handle(rpc_id, app_id, client_id);

    /* call rpc function */
    LOGDBG("invoking the mread[%d] complete rpc function in client",
            mread_id);
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_mread_req_complete_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}

/* invokes the heartbeat rpc function */
int invoke_heartbeat_rpc(int app_id, int client_id)
{
    hg_return_t hret;

    /* check that we have initialized margo */
    if (NULL == unifyfsd_rpc_context) {
        return UNIFYFS_FAILURE;
    }

    /* fill input struct */
    unifyfs_heartbeat_in_t in;

    /* get handle to rpc function */
    hg_id_t rpc_id = unifyfsd_rpc_context->rpcs.client_heartbeat_id;
    hg_handle_t handle = create_client_handle(rpc_id, app_id, client_id);

    /* call rpc function */
    LOGDBG("invoking the heartbeat rpc function in client");
    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_forward() failed");
        margo_destroy(handle);
        return UNIFYFS_ERROR_MARGO;
    }

    /* decode response */
    int ret;
    unifyfs_heartbeat_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret == HG_SUCCESS) {
        LOGDBG("Got response ret=%" PRIi32, out.ret);
        ret = (int) out.ret;
        margo_free_output(handle, &out);
    } else {
        LOGERR("margo_get_output() failed");
        ret = UNIFYFS_ERROR_MARGO;
    }

    /* free resources */
    margo_destroy(handle);

    return ret;
}
