/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2019, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#include "margo_server.h"
#include "unifycr_global.h"
#include "unifycr_keyval.h"

// global variables
ServerRpcContext_t* unifycrd_rpc_context;
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

    mid = margo_init(margo_protocol, MARGO_SERVER_MODE, 1, 1);
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

static void register_server_server_rpcs(margo_instance_id mid)
{

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

    mid = margo_init(PROTOCOL_MARGO_SHM, MARGO_SERVER_MODE, 1, 1);
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

static void register_client_server_rpcs(margo_instance_id mid)
{
    /* register client-server RPCs */
    MARGO_REGISTER(mid, "unifycr_mount_rpc",
                   unifycr_mount_in_t, unifycr_mount_out_t,
                   unifycr_mount_rpc);

    MARGO_REGISTER(mid, "unifycr_unmount_rpc",
                   unifycr_unmount_in_t, unifycr_unmount_out_t,
                   unifycr_unmount_rpc);

    MARGO_REGISTER(mid, "unifycr_metaget_rpc",
                   unifycr_metaget_in_t, unifycr_metaget_out_t,
                   unifycr_metaget_rpc);

    MARGO_REGISTER(mid, "unifycr_metaset_rpc",
                   unifycr_metaset_in_t, unifycr_metaset_out_t,
                   unifycr_metaset_rpc);

    MARGO_REGISTER(mid, "unifycr_fsync_rpc",
                   unifycr_fsync_in_t, unifycr_fsync_out_t,
                   unifycr_fsync_rpc);

    MARGO_REGISTER(mid, "unifycr_filesize_rpc",
                   unifycr_filesize_in_t, unifycr_filesize_out_t,
                   unifycr_filesize_rpc);

    MARGO_REGISTER(mid, "unifycr_read_rpc",
                   unifycr_read_in_t, unifycr_read_out_t,
                   unifycr_read_rpc)

    MARGO_REGISTER(mid, "unifycr_mread_rpc",
                   unifycr_mread_in_t, unifycr_mread_out_t,
                   unifycr_mread_rpc);
}

/* margo_server_rpc_init
 *
 * Initialize the server's Margo RPC functionality, for
 * both intra-node (client-server shared memory) and
 * inter-node (server-server).
 */
int margo_server_rpc_init(void)
{
    int rc = UNIFYCR_SUCCESS;

    if (NULL == unifycrd_rpc_context) {
        /* create rpc server context */
        unifycrd_rpc_context = calloc(1, sizeof(ServerRpcContext_t));
        assert(unifycrd_rpc_context);
    }

    margo_instance_id mid;
    mid = setup_local_target();
    if (mid == MARGO_INSTANCE_NULL) {
        rc = UNIFYCR_FAILURE;
    } else {
        unifycrd_rpc_context->shm_mid = mid;
        register_client_server_rpcs(mid);
    }

    mid = setup_remote_target();
    if (mid == MARGO_INSTANCE_NULL) {
        rc = UNIFYCR_FAILURE;
    } else {
        unifycrd_rpc_context->svr_mid = mid;
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
    int rc = UNIFYCR_SUCCESS;

    if (NULL != unifycrd_rpc_context) {
        /* define a temporary to refer to context */
        ServerRpcContext_t* ctx = unifycrd_rpc_context;
        unifycrd_rpc_context = NULL;

        rpc_clean_local_server_addr();

        /* shut down margo */
        margo_finalize(ctx->shm_mid);
        /* NOTE: 2nd call to margo_finalize() hangs - Margo bug?
         * margo_finalize(ctx->svr_mid); */

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
    int ret = (int)UNIFYCR_SUCCESS;
    size_t i;
    hg_return_t hret;

    for (i = 0; i < glb_num_servers; i++) {
        int remote_mpi_rank = -1;
        char* mpi_rank_str = NULL;
        char* margo_addr_str = NULL;

        if (i == glb_svr_rank) {
            continue;
        }

        // NOTE: this really doesn't belong here, and will eventually go away
        rc = unifycr_keyval_lookup_remote(i, key_unifycrd_mpi_rank,
                                          &mpi_rank_str);
        if ((int)UNIFYCR_SUCCESS == rc) {
            remote_mpi_rank = atoi(mpi_rank_str);
            free(mpi_rank_str);
        } else {
            LOGERR("server index=%zu - MPI rank lookup failed", i);
            ret = (int)UNIFYCR_FAILURE;
        }
        glb_servers[i].mpi_rank = remote_mpi_rank;

        margo_addr_str = rpc_lookup_remote_server_addr(i);
        glb_servers[i].margo_svr_addr_str = margo_addr_str;
        if (NULL != margo_addr_str) {
            LOGDBG("server index=%zu, mpi_rank=%d, margo_addr=%s",
                i, remote_mpi_rank, margo_addr_str);
            if (!margo_lazy_connect) {
                glb_servers[i].margo_svr_addr = HG_ADDR_NULL;
                hret = margo_addr_lookup(unifycrd_rpc_context->svr_mid,
                                         glb_servers[i].margo_svr_addr_str,
                                         &(glb_servers[i].margo_svr_addr));
                if (hret != HG_SUCCESS) {
                    LOGERR("server index=%zu - margo_addr_lookup(%s) failed",
                           i, margo_addr_str);
                    ret = (int)UNIFYCR_FAILURE;
                }
            }
        } else {
            LOGERR("server index=%zu - margo addr string lookup failed", i);
            ret = (int)UNIFYCR_FAILURE;
        }
    }

    return ret;
}
