/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2019, UT-Battelle, LLC.
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

// global variables
ServerRpcContext_t* unifyfsd_rpc_context;
bool margo_use_tcp = true;
bool margo_lazy_connect; // = false

static const char* PROTOCOL_MARGO_SHM   = "na+sm://";
static const char* PROTOCOL_MARGO_VERBS = "ofi+verbs://";
static const char* PROTOCOL_MARGO_TCP   = "bmi+tcp://";

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
        margo_protocol = PROTOCOL_MARGO_VERBS;
    }

    mid = margo_init(margo_protocol, MARGO_SERVER_MODE, 1, 4);
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
    unifyfsd_rpc_context->rpcs.hello_id =
        MARGO_REGISTER(mid, "server_hello_rpc",
                       server_hello_in_t, server_hello_out_t,
                       server_hello_rpc);

    unifyfsd_rpc_context->rpcs.server_pid_id =
        MARGO_REGISTER(mid, "server_pid_rpc",
                       server_pid_in_t, server_pid_out_t,
                       server_pid_handle_rpc);

    unifyfsd_rpc_context->rpcs.request_id =
        MARGO_REGISTER(mid, "server_request_rpc",
                       server_request_in_t, server_request_out_t,
                       server_request_rpc);

    unifyfsd_rpc_context->rpcs.chunk_read_request_id =
        MARGO_REGISTER(mid, "chunk_read_request_rpc",
                       chunk_read_request_in_t, chunk_read_request_out_t,
                       chunk_read_request_rpc);

    unifyfsd_rpc_context->rpcs.chunk_read_response_id =
        MARGO_REGISTER(mid, "chunk_read_response_rpc",
                       chunk_read_response_in_t, chunk_read_response_out_t,
                       chunk_read_response_rpc);

    unifyfsd_rpc_context->rpcs.filesize_request_id =
        MARGO_REGISTER(mid, "filesize_request_rpc",
                       filesize_request_in_t, filesize_request_out_t,
                       filesize_request_rpc);

    unifyfsd_rpc_context->rpcs.filesize_response_id =
        MARGO_REGISTER(mid, "filesize_response_rpc",
                       filesize_response_in_t, filesize_response_out_t,
                       filesize_response_rpc);

    unifyfsd_rpc_context->rpcs.truncate_request_id =
        MARGO_REGISTER(mid, "truncate_request_rpc",
                       truncate_request_in_t, truncate_request_out_t,
                       truncate_request_rpc);

    unifyfsd_rpc_context->rpcs.truncate_response_id =
        MARGO_REGISTER(mid, "truncate_response_rpc",
                       truncate_response_in_t, truncate_response_out_t,
                       truncate_response_rpc);

    unifyfsd_rpc_context->rpcs.metaset_request_id =
        MARGO_REGISTER(mid, "metaset_request_rpc",
                       metaset_request_in_t, metaset_request_out_t,
                       metaset_request_rpc);

    unifyfsd_rpc_context->rpcs.metaset_response_id =
        MARGO_REGISTER(mid, "metaset_response_rpc",
                       metaset_response_in_t, metaset_response_out_t,
                       metaset_response_rpc);

    unifyfsd_rpc_context->rpcs.unlink_request_id =
        MARGO_REGISTER(mid, "unlink_request_rpc",
                       unlink_request_in_t, unlink_request_out_t,
                       unlink_request_rpc);

    unifyfsd_rpc_context->rpcs.unlink_response_id =
        MARGO_REGISTER(mid, "unlink_response_rpc",
                       unlink_response_in_t, unlink_response_out_t,
                       unlink_response_rpc);

    unifyfsd_rpc_context->rpcs.extbcast_request_id =
        MARGO_REGISTER(mid, "extbcast_request_rpc",
                       extbcast_request_in_t, extbcast_request_out_t,
                       extbcast_request_rpc);
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

    mid = margo_init(PROTOCOL_MARGO_SHM, MARGO_SERVER_MODE, 1, -1);
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

    MARGO_REGISTER(mid, "unifyfs_sync_rpc",
                   unifyfs_sync_in_t, unifyfs_sync_out_t,
                   unifyfs_sync_rpc);

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
        assert(unifyfsd_rpc_context);
    }

    margo_instance_id mid;
    mid = setup_local_target();
    if (mid == MARGO_INSTANCE_NULL) {
        rc = UNIFYFS_FAILURE;
    } else {
        unifyfsd_rpc_context->shm_mid = mid;
        register_client_server_rpcs(mid);
    }

    mid = setup_remote_target();
    if (mid == MARGO_INSTANCE_NULL) {
        rc = UNIFYFS_FAILURE;
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
