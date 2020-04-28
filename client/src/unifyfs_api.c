/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include "unifyfs_api_internal.h"

/*
 * Public Methods
 */

/* Initialize client's use of UnifyFS */
// TODO: replace unifyfs_mount()
unifyfs_rc unifyfs_initialize(const char* mountpoint,
                              unifyfs_cfg_option* options, int n_opts,
                              unifyfs_handle* fshdl)
{
    if ((NULL == mountpoint) || (NULL == fshdl)) {
        return EINVAL;
    }
    *fshdl = UNIFYFS_INVALID_HANDLE;

    // print log messages to stderr
    unifyfs_log_open(NULL);

    unifyfs_client* client;
    client = (unifyfs_client*) calloc(1, sizeof(unifyfs_client));
    if (NULL == client) {
        LOGERR("failed to allocate client handle");
        return ENOMEM;
    }
    unifyfs_app_id = unifyfs_generate_gfid(mountpoint);
    client->app_id = unifyfs_app_id;

    // initialize configuration
    unifyfs_cfg_t* client_cfg = &(client->cfg);
    int rc = unifyfs_config_init(client_cfg, 0, NULL, n_opts, options);
    if (rc) {
        LOGERR("failed to initialize client configuration");
        return rc;
    }
    client_cfg->ptype = UNIFYFS_CLIENT;
    client_cfg->unifyfs_mountpoint = strdup(mountpoint);
    unifyfs_mount_prefix = client_cfg->unifyfs_mountpoint;
    unifyfs_mount_prefixlen = strlen(unifyfs_mount_prefix);

    // set log level from config
    char* cfgval = client_cfg->log_verbosity;
    if (cfgval != NULL) {
        long l;
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            unifyfs_set_log_level((unifyfs_log_level_t)l);
        }
    }

    // initialize k-v store access
    int kv_rank = 0;
    int kv_nranks = 1;
    rc = unifyfs_keyval_init(client_cfg, &kv_rank, &kv_nranks);
    if (rc) {
        LOGERR("failed to initialize kvstore");
        return UNIFYFS_FAILURE;
    }

    /* open rpc connection to server */
    rc = unifyfs_client_rpc_init();
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to initialize client RPC");
        return rc;
    }

    /* Call client mount rpc function to get client id */
    LOGDBG("calling mount rpc");
    rc = invoke_client_mount_rpc(client_cfg);
    if (rc != UNIFYFS_SUCCESS) {
        /* If we fail to connect to the server, bail with an error */
        LOGERR("failed to mount to server");
        return rc;
    }
    unifyfs_mounted = unifyfs_app_id;
    client->is_mounted = true;
    client->client_id = unifyfs_client_id;

    /* initialize our library using assigned client id, creates shared memory
     * regions (e.g., superblock and data recv) and inits log-based I/O */
    rc = unifyfs_init(client_cfg);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* Call client attach rpc function to register our newly created shared
     * memory and files with server */
    LOGDBG("calling attach rpc");
    rc = invoke_client_attach_rpc(client_cfg);
    if (rc != UNIFYFS_SUCCESS) {
        /* If we fail, bail with an error */
        LOGERR("failed to attach to server");
        unifyfs_fini();
        return rc;
    }

    /* add mount point as a new directory in the file list */
    if (unifyfs_get_fid_from_path(mountpoint) < 0) {
        /* no entry exists for mount point, so create one */
        int fid = unifyfs_fid_create_directory(mountpoint);
        if (fid < 0) {
            /* if there was an error, return it */
            LOGERR("failed to create directory entry for mount point: `%s'",
                   mountpoint);
            unifyfs_fini();
            return UNIFYFS_FAILURE;
        }
    }

    unifyfs_handle client_hdl = (unifyfs_handle) client;
    *fshdl = client_hdl;
    return UNIFYFS_SUCCESS;
}

/* Finalize client's use of UnifyFS */
// TODO: replace unifyfs_unmount()
unifyfs_rc unifyfs_finalize(unifyfs_handle fshdl)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }
    unifyfs_client* client = fshdl;

    int ret = UNIFYFS_SUCCESS;

    if (client->is_mounted) {
        /* sync any outstanding writes */
        LOGDBG("syncing data");
        int rc = unifyfs_sync_extents(-1);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("client sync failed");
            ret = rc;
        }

        /* invoke unmount rpc to tell server we're disconnecting */
        LOGDBG("calling unmount");
        rc = invoke_client_unmount_rpc();
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("client unmount rpc failed");
            ret = rc;
        }

        unifyfs_mounted = -1;
    }

    /* free resources allocated in client_rpc_init */
    unifyfs_client_rpc_finalize();

    /************************
     * free our mount point, and detach from structures
     * storing data
     ************************/

    /* free resources allocated in unifyfs_init */
    unifyfs_fini();

    /* free memory tracking our mount prefix string */
    if (unifyfs_mount_prefix != NULL) {
        free(unifyfs_mount_prefix);
        unifyfs_mount_prefix = NULL;
        unifyfs_mount_prefixlen = 0;
        client->cfg.unifyfs_mountpoint = NULL;
    }

    /************************
     * free configuration values
     ************************/

    /* free global holding current working directory */
    if (unifyfs_cwd != NULL) {
        free(unifyfs_cwd);
        unifyfs_cwd = NULL;
    }

    /* clean up configuration */
    int rc = unifyfs_config_fini(&(client->cfg));
    if (rc != 0) {
        LOGERR("unifyfs_config_fini() failed");
        ret = rc;
    }

    /* shut down our logging */
    unifyfs_log_close();

    /* free client structure */
    free(client);

    return ret;
}
