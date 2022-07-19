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

/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

// system headers
#include <fcntl.h>
#include <sys/mman.h>

// server components
#include "unifyfs_global.h"
#include "unifyfs_metadata_mdhim.h"
#include "unifyfs_request_manager.h"

// margo rpcs
#include "margo_server.h"
#include "unifyfs_client_rpcs.h"
#include "unifyfs_rpc_util.h"
#include "unifyfs_misc.h"


static void create_mountpoint_dir(int app_id,
                                  int client_id,
                                  const char* mountpoint)
{
    /* initialize an empty file attributes structure */
    unifyfs_file_attr_t fattr;
    unifyfs_file_attr_set_invalid(&fattr);

    /* set global file id and path */
    fattr.gfid = app_id;
    fattr.filename = strdup(mountpoint);

    /* set initial directory state */
    fattr.mode = UNIFYFS_STAT_DEFAULT_DIR_MODE;
    fattr.is_shared = 1;
    fattr.is_laminated = 0;
    fattr.size = 0;

    /* use current time for atime/mtime/ctime */
    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);
    fattr.atime = tp;
    fattr.mtime = tp;
    fattr.ctime = tp;

    /* capture current uid and gid */
    fattr.uid = getuid();
    fattr.gid = getgid();

    LOGDBG("creating global file metadata for mountpoint:");
    debug_print_file_attr(&fattr);

    /* create metaset request */
    client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
    unifyfs_metaset_in_t* in = malloc(sizeof(*in));
    if ((NULL == req) || (NULL == in)) {
        LOGERR("memory allocation failed");
        return;
    }

    req->req_type = UNIFYFS_CLIENT_RPC_METASET;
    req->handle   = HG_HANDLE_NULL;
    req->input    = (void*) in;
    req->bulk_buf = NULL;
    req->bulk_sz  = 0;

    in->app_id    = app_id;
    in->client_id = client_id;
    in->attr      = fattr;
    in->attr_op   = UNIFYFS_FILE_ATTR_OP_CREATE;

    unifyfs_fops_ctx_t ctx = {
        .app_id    = app_id,
        .client_id = client_id,
    };

    int ret = rm_submit_client_rpc_request(&ctx, req);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to submit metaset request for mountpoint");
    }
}

/* BEGIN MARGO CLIENT-SERVER RPC HANDLER FUNCTIONS */

/* called by client to register with the server, client provides a
 * structure of values on input, some of which specify global
 * values across all clients in the app_id, and some of which are
 * specific to the client process,
 *
 * server creates a structure for the given app_id (if needed),
 * and then fills in a set of values for the particular client,
 *
 * server attaches to client shared memory regions, opens files
 * holding spill over data, and launchers request manager for
 * client */
static void unifyfs_mount_rpc(hg_handle_t handle)
{
    int ret = (int)UNIFYFS_SUCCESS;
    int app_id = -1;
    int client_id = -1;
    int create_mountpoint = 0;

    /* get input params */
    unifyfs_mount_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* read app_id and client_id from input */
        app_id = unifyfs_generate_gfid(in.mount_prefix);

        /* lookup app_config for given app_id */
        app_config* app_cfg = get_application(app_id);
        if (app_cfg == NULL) {
            /* insert new app_config into our app_configs array */
            LOGDBG("creating new application for app_id=%d", app_id);
            app_cfg = new_application(app_id, &create_mountpoint);
            if (NULL == app_cfg) {
                ret = UNIFYFS_FAILURE;
            }
        } else {
            LOGDBG("using existing app_config for app_id=%d", app_id);
        }

        if (NULL != app_cfg) {
            LOGDBG("creating new app client for %s", in.client_addr_str);
            app_client* client = new_app_client(app_cfg,
                                                in.client_addr_str,
                                                in.dbg_rank);
            if (NULL == client) {
                LOGERR("failed to create new client for app_id=%d dbg_rank=%d",
                       app_id, (int)in.dbg_rank);
                ret = (int)UNIFYFS_FAILURE;
            } else {
                client_id = client->state.client_id;
                LOGDBG("created new application client %d:%d",
                       app_id, client_id);
                if (create_mountpoint) {
                    create_mountpoint_dir(app_id, client_id, in.mount_prefix);
                }
            }
        }

        margo_free_input(handle, &in);
    }

    /* build output structure to return to caller */
    unifyfs_mount_out_t out;
    out.app_id = (int32_t) app_id;
    out.client_id = (int32_t) client_id;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_mount_rpc)

/* server attaches to client's shared memory region, opens file
 * holding spillover data */
static void unifyfs_attach_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_attach_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_ATTACH;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_attach_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_attach_rpc)

static void unifyfs_unmount_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;

    /* get input params */
    unifyfs_unmount_in_t in;
    hg_return_t hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_get_input() failed");
        ret = UNIFYFS_ERROR_MARGO;
    } else {
        /* read app_id and client_id from input */
        int app_id    = in.app_id;
        int client_id = in.client_id;

        /* disconnect app client */
        app_client* clnt = get_app_client(app_id, client_id);
        if (NULL != clnt) {
            ret = disconnect_app_client(clnt);
        } else {
            LOGERR("application client not found");
            ret = EINVAL;
        }

        margo_free_input(handle, &in);
    }

    /* build output structure to return to caller */
    unifyfs_unmount_out_t out;
    out.ret = ret;

    /* send output back to caller */
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("margo_respond() failed");
    }

    /* free margo resources */
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_unmount_rpc)

/* returns file meta data including file size and file name
 * given a global file id */
static void unifyfs_metaget_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_metaget_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_METAGET;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_metaget_out_t out;
        out.ret = (int32_t) ret;
        memset(&(out.attr), 0, sizeof(out.attr));
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_metaget_rpc)

/* given a global file id and a file name,
 * record key/value entry for this file */
static void unifyfs_metaset_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_metaset_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_METASET;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_metaset_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_metaset_rpc)

/* given a global file id and client identified by (app_id, client_id) as
 * input, read the write extents for the file from the shared memory index
 * and update its global metadata */
static void unifyfs_fsync_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_fsync_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_SYNC;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_fsync_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_fsync_rpc)

/* given an app_id, client_id, global file id,
 * return current file size */
static void unifyfs_filesize_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_filesize_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_FILESIZE;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_filesize_out_t out;
        out.ret      = (int32_t) ret;
        out.filesize = (hg_size_t) 0;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_filesize_rpc)

/* given an app_id, client_id, global file id, transfer mode
 * and destination file, transfer data to that file */
static void unifyfs_transfer_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_transfer_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_TRANSFER;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_transfer_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_transfer_rpc)

/* given an app_id, client_id, global file id,
 * and file size, truncate file to that size */
static void unifyfs_truncate_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_truncate_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_TRUNCATE;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_truncate_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_truncate_rpc)

/* given an app_id, client_id, and global file id,
 * remove file from system */
static void unifyfs_unlink_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_unlink_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_UNLINK;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_unlink_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }

}
DEFINE_MARGO_RPC_HANDLER(unifyfs_unlink_rpc)

/* given an app_id, client_id, and global file id,
 * laminate file */
static void unifyfs_laminate_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_laminate_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                    .app_id = in->app_id,
                    .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_LAMINATE;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_laminate_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }

}
DEFINE_MARGO_RPC_HANDLER(unifyfs_laminate_rpc)


/* given (mread_id, app_id, client_id) and count of read requests,
 * followed by a bulk data array of read extents (unifyfs_extent_t),
 * initiate read requests for data. */
static void unifyfs_mread_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_mread_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            /* allocate buffer to hold array of read requests */
            hg_size_t size = in->bulk_size;
            void* buffer = pull_margo_bulk_buffer(handle, in->bulk_extents,
                                                 size, NULL);
            if (NULL == buffer) {
                ret = UNIFYFS_ERROR_MARGO;
            } else {
                client_rpc_req_t* req = malloc(sizeof(*req));
                if (NULL == req) {
                    ret = ENOMEM;
                } else {
                    unifyfs_fops_ctx_t ctx = {
                        .app_id = in->app_id,
                        .client_id = in->client_id
                    };
                    req->req_type = UNIFYFS_CLIENT_RPC_READ;
                    req->handle = handle;
                    req->input = (void*) in;
                    req->bulk_buf = buffer;
                    req->bulk_sz = size;
                    ret = rm_submit_client_rpc_request(&ctx, req);
                }
                if (ret != UNIFYFS_SUCCESS) {
                    free(buffer);
                    if (NULL != req) {
                        free(req);
                    }
                    margo_free_input(handle, in);
                }
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }

        /* return to caller */
        unifyfs_mread_out_t out;
        out.ret = (int32_t) ret;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_mread_rpc)

/* returns file extents from node local server
 * given a global file id */
static void unifyfs_node_local_extents_get_rpc(hg_handle_t handle)
{
    int ret = UNIFYFS_SUCCESS;
    hg_return_t hret;

    /* get input params */
    unifyfs_node_local_extents_get_in_t* in = malloc(sizeof(*in));
    if (NULL == in) {
        ret = ENOMEM;
    } else {
        hret = margo_get_input(handle, in);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_get_input() failed");
            ret = UNIFYFS_ERROR_MARGO;
        } else {
            client_rpc_req_t* req = malloc(sizeof(client_rpc_req_t));
            if (NULL == req) {
                ret = ENOMEM;
            } else {
                unifyfs_fops_ctx_t ctx = {
                        .app_id = in->app_id,
                        .client_id = in->client_id,
                };
                req->req_type = UNIFYFS_CLIENT_RPC_NODE_LOCAL_EXTENTS_GET;
                req->handle = handle;
                req->input = (void*) in;
                req->bulk_buf = NULL;
                req->bulk_sz = 0;
                ret = rm_submit_client_rpc_request(&ctx, req);
            }

            if (ret != UNIFYFS_SUCCESS) {
                if (NULL != req) {
                    free(req);
                }
                margo_free_input(handle, in);
            }
        }
    }

    /* if we hit an error during request submission, respond with the error */
    if (ret != UNIFYFS_SUCCESS) {
        if (NULL != in) {
            free(in);
        }
        /* return to caller */
        unifyfs_node_local_extents_get_out_t out;
        out.ret = (int32_t) ret;
        out.extent_count = 0;
        hret = margo_respond(handle, &out);
        if (hret != HG_SUCCESS) {
            LOGERR("margo_respond() failed");
        }

        /* free margo resources */
        margo_destroy(handle);
    }
}
DEFINE_MARGO_RPC_HANDLER(unifyfs_node_local_extents_get_rpc)
