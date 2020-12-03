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

// global variables
ServerRpcContext_t* unifyfsd_rpc_context;
bool margo_use_tcp = true;
bool margo_lazy_connect; // = false
int  margo_client_server_pool_sz = 4;
int  margo_server_server_pool_sz = 4;
int  margo_use_progress_thread = 1;

#if defined(NA_HAS_SM)
static const char* PROTOCOL_MARGO_SHM   = "na+sm://";
#else
#error Required Mercury NA shared memory plugin not found (please enable 'SM')
#endif

#if defined(NA_HAS_BMI)
static const char* PROTOCOL_MARGO_TCP = "bmi+tcp://";
static const char* PROTOCOL_MARGO_RMA = "bmi+tcp://";
#elif defined(NA_HAS_OFI)
static const char* PROTOCOL_MARGO_TCP = "ofi+tcp://";
static const char* PROTOCOL_MARGO_RMA = "ofi+verbs://";
#else
#error No supported Mercury NA plugin found (please use one of: 'BMI', 'OFI')
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

    if (margo_use_tcp) {
        margo_protocol = PROTOCOL_MARGO_TCP;
    } else {
        margo_protocol = PROTOCOL_MARGO_RMA;
    }

    mid = margo_init(margo_protocol, MARGO_SERVER_MODE,
                     margo_use_progress_thread, margo_server_server_pool_sz);
    if (mid == MARGO_INSTANCE_NULL) {
        LOGERR("margo_init(%s)", margo_protocol);
        return mid;
    }

    /* figure out what address this server is listening on */
    hret = margo_addr_self(mid, &addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_self()");
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    hret = margo_addr_to_string(mid,
                                self_string, &self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_to_string()");
        margo_addr_free(mid, addr_self);
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    LOGDBG("margo RPC server: %s", self_string);
    margo_addr_free(mid, addr_self);

    /* publish rpc address of server for remote servers */
    rpc_publish_remote_server_addr(self_string);

    return mid;
}

/* register server-server RPCs */
static void register_server_server_rpcs(margo_instance_id mid)
{
    unifyfsd_rpc_context->rpcs.server_pid_id =
        MARGO_REGISTER(mid, "server_pid_rpc",
                       server_pid_in_t, server_pid_out_t,
                       server_pid_rpc);

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
    hg_return_t hret;
    hg_addr_t addr_self;
    char self_string[128];
    hg_size_t self_string_sz = sizeof(self_string);
    margo_instance_id mid;

    mid = margo_init(PROTOCOL_MARGO_SHM, MARGO_SERVER_MODE,
                     margo_use_progress_thread, margo_client_server_pool_sz);
    if (mid == MARGO_INSTANCE_NULL) {
        LOGERR("margo_init(%s)", PROTOCOL_MARGO_SHM);
        return mid;
    }

    /* figure out what address this server is listening on */
    hret = margo_addr_self(mid, &addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_self()");
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    hret = margo_addr_to_string(mid,
                                self_string, &self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_addr_to_string()");
        margo_addr_free(mid, addr_self);
        margo_finalize(mid);
        return MARGO_INSTANCE_NULL;
    }
    LOGDBG("shared-memory margo RPC server: %s", self_string);
    margo_addr_free(mid, addr_self);

    /* publish rpc address of server for local clients */
    rpc_publish_local_server_addr(self_string);

    return mid;
}

/* register client-server RPCs */
static void register_client_server_rpcs(margo_instance_id mid)
{
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

    MARGO_REGISTER(mid, "unifyfs_read_rpc",
                   unifyfs_read_in_t, unifyfs_read_out_t,
                   unifyfs_read_rpc)

    MARGO_REGISTER(mid, "unifyfs_mread_rpc",
                   unifyfs_mread_in_t, unifyfs_mread_out_t,
                   unifyfs_mread_rpc);
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
        margo_finalize(ctx->svr_mid);
        /* NOTE: 2nd call to margo_finalize() sometimes crashes - Margo bug? */
        margo_finalize(ctx->shm_mid);

        /* free memory allocated for context structure */
        free(ctx);
    }

    return rc;
}

/* margo_connect_servers
 *
 * Using address strings found in glb_servers, resolve
 * each peer server's margo address.
 */
int margo_connect_servers(void)
{
    int rc;
    int ret = (int)UNIFYFS_SUCCESS;
    size_t i;
    hg_return_t hret;

    // block until a margo_svr key pair published by all servers
    rc = unifyfs_keyval_fence_remote();
    if ((int)UNIFYFS_SUCCESS != rc) {
        LOGERR("keyval fence on margo_svr key failed");
        ret = (int)UNIFYFS_FAILURE;
        return ret;
    }

    for (i = 0; i < glb_num_servers; i++) {
        int remote_pmi_rank = -1;
        char* pmi_rank_str = NULL;
        char* margo_addr_str = NULL;

        rc = unifyfs_keyval_lookup_remote(i, key_unifyfsd_pmi_rank,
                                          &pmi_rank_str);
        if ((int)UNIFYFS_SUCCESS != rc) {
            LOGERR("server index=%zu - pmi rank lookup failed", i);
            ret = (int)UNIFYFS_FAILURE;
            return ret;
        }
        if (NULL != pmi_rank_str) {
            remote_pmi_rank = atoi(pmi_rank_str);
            free(pmi_rank_str);
        }
        glb_servers[i].pmi_rank = remote_pmi_rank;

        margo_addr_str = rpc_lookup_remote_server_addr(i);
        if (NULL == margo_addr_str) {
            LOGERR("server index=%zu - margo server lookup failed", i);
            ret = (int)UNIFYFS_FAILURE;
            return ret;
        }
        glb_servers[i].margo_svr_addr = HG_ADDR_NULL;
        glb_servers[i].margo_svr_addr_str = margo_addr_str;
        LOGDBG("server index=%zu, pmi_rank=%d, margo_addr=%s",
               i, remote_pmi_rank, margo_addr_str);
        if (!margo_lazy_connect) {
            hret = margo_addr_lookup(unifyfsd_rpc_context->svr_mid,
                                     glb_servers[i].margo_svr_addr_str,
                                     &(glb_servers[i].margo_svr_addr));
            if (hret != HG_SUCCESS) {
                LOGERR("server index=%zu - margo_addr_lookup(%s) failed",
                       i, margo_addr_str);
                ret = (int)UNIFYFS_FAILURE;
            }
        }
    }

    return ret;
}
