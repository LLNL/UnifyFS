#ifndef _MARGO_CLIENT_H
#define _MARGO_CLIENT_H

/********************************************
 * margo_client.h - client-server margo RPCs
 ********************************************/

#include <margo.h>
#include "unifyfs_meta.h"
#include "unifyfs_client_rpcs.h"

typedef struct ClientRpcIds {
    hg_id_t attach_id;
    hg_id_t mount_id;
    hg_id_t unmount_id;
    hg_id_t metaset_id;
    hg_id_t metaget_id;
    hg_id_t filesize_id;
    hg_id_t truncate_id;
    hg_id_t unlink_id;
    hg_id_t laminate_id;
    hg_id_t sync_id;
    hg_id_t read_id;
    hg_id_t mread_id;
} client_rpcs_t;

typedef struct ClientRpcContext {
    margo_instance_id mid;
    char* client_addr_str;
    hg_addr_t client_addr;
    hg_addr_t svr_addr;
    client_rpcs_t rpcs;
} client_rpc_context_t;


int unifyfs_client_rpc_init(void);

int unifyfs_client_rpc_finalize(void);

void fill_client_attach_info(unifyfs_attach_in_t* in);
int invoke_client_attach_rpc(void);

void fill_client_mount_info(unifyfs_mount_in_t* in);
int invoke_client_mount_rpc(void);

int invoke_client_unmount_rpc(void);

int invoke_client_metaset_rpc(int create, unifyfs_file_attr_t* f_meta);

int invoke_client_metaget_rpc(int gfid, unifyfs_file_attr_t* f_meta);

int invoke_client_filesize_rpc(int gfid, size_t* filesize);

int invoke_client_truncate_rpc(int gfid, size_t filesize);

int invoke_client_unlink_rpc(int gfid);

int invoke_client_laminate_rpc(int gfid);

int invoke_client_sync_rpc(void);

int invoke_client_read_rpc(int gfid, size_t offset, size_t length);

/*
 * mread rpc function is non-blocking (using margo_iforward), and the response
 * from the server should be checked by the caller manually using
 * the unifyfs_mread_rpc_status_check function.
 */
struct unifyfs_mread_rpc_ctx {
    margo_request req;      /* margo request for track iforward result */
    hg_handle_t handle;     /* rpc handle */
    int rpc_ret;            /* rpc response from the server */
};

typedef struct unifyfs_mread_rpc_ctx unifyfs_mread_rpc_ctx_t;

/**
 * @brief track the progress of the submitted rpc. if the rpc is done, this
 * funtcion returns 1 with the server response being stored in @ctx->rpc_ret.
 *
 * @param ctx pointer to the rpc ctx
 *
 * @return 1 if rpc is done (received response from the server), 0 if still in
 * progress. -EINVAL if the @ctx is invalid.
 */
int unifyfs_mread_rpc_status_check(unifyfs_mread_rpc_ctx_t* ctx);


int invoke_client_mread_rpc(int read_count, size_t size, void* buffer,
                            unifyfs_mread_rpc_ctx_t* ctx);

#endif // MARGO_CLIENT_H
