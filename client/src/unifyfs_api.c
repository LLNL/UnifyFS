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
#include "unifyfs_fid.h"
#include "margo_client.h"

/*
 * Public Methods
 */

/* Initialize client's use of UnifyFS */
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

    // initialize configuration
    long l;
    bool b;
    unifyfs_cfg_t* client_cfg = &(client->cfg);
    int rc = unifyfs_config_init(client_cfg, 0, NULL, n_opts, options);
    if (rc) {
        LOGERR("failed to initialize client configuration");
        return rc;
    }
    client_cfg->ptype = UNIFYFS_CLIENT;
    client_cfg->unifyfs_mountpoint = strdup(mountpoint);
    client->state.mount_prefix = strdup(mountpoint);
    client->state.mount_prefixlen = strlen(mountpoint);

    /* set our current working directory if user provided one */
    char* cfgval = client_cfg->client_cwd;
    if (cfgval != NULL) {
        client->cwd = strdup(cfgval);

        /* check that cwd falls somewhere under the mount point */
        int cwd_within_mount = 0;
        if (0 == strncmp(client->cwd, client->state.mount_prefix,
                         client->state.mount_prefixlen)) {
            /* characters in target up through mount point match,
             * assume we match */
            cwd_within_mount = 1;

            /* if we have another character, it must be '/' */
            if ((strlen(client->cwd) > client->state.mount_prefixlen) &&
                (client->cwd[client->state.mount_prefixlen] != '/')) {
                cwd_within_mount = 0;
            }
        }
        if (!cwd_within_mount) {
            /* path given in CWD is outside of the UnifyFS mount point */
            LOGERR("UNIFYFS_CLIENT_CWD '%s' must be within the mount '%s'",
                   client->cwd, client->state.mount_prefix);

            /* ignore setting and set back to NULL */
            free(client->cwd);
            client->cwd = NULL;
        }
    } else {
        /* user did not specify a CWD, so initialize with the actual
         * current working dir */
        char* cwd = getcwd(NULL, 0);
        if (cwd != NULL) {
            client->cwd = cwd;
        } else {
            LOGERR("Failed getcwd (%s)", strerror(errno));
        }
    }

    /* set log level from config */
    cfgval = client_cfg->log_verbosity;
    if (cfgval != NULL) {
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            unifyfs_set_log_level((unifyfs_log_level_t)l);
        }
    }

    /* determine max number of files to store in file system */
    client->max_files = UNIFYFS_CLIENT_MAX_FILES;
    cfgval = client_cfg->client_max_files;
    if (cfgval != NULL) {
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            client->max_files = (int)l;
        }
    }

    /* Determine if we should track all write extents and use them
     * to service read requests if all data is local */
    client->use_local_extents = 0;
    cfgval = client_cfg->client_local_extents;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if (rc == 0) {
            client->use_local_extents = (bool)b;
        }
    }

    /* Determine if we should track all extents on the node-local clients
     * for laminated files only and use them to service read requests
     * if all data is local */
    client->use_node_local_extents = 0;
    cfgval = client_cfg->client_node_local_extents;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if (rc == 0) {
            client->use_node_local_extents = (bool)b;
        }
    }

    /* Create node-local private files, rather than globally shared files
     * when given O_EXCL during file open/create operations. */
    client->use_excl_private = true;
    cfgval = client_cfg->client_excl_private;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if (rc == 0) {
            client->use_excl_private = (bool)b;
        }
    }

    /* Determine whether we persist data to storage device on fsync().
     * Turning this setting off speeds up fsync() by only syncing the
     * extent metadata, but it violates POSIX semanatics. */
    client->use_fsync_persist = true;
    cfgval = client_cfg->client_fsync_persist;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if (rc == 0) {
            client->use_fsync_persist = (bool)b;
        }
    }

    /* Determine whether we automatically sync every write to server.
     * Turning this setting on slows write performance, but it can serve
     * as a workaround for apps that do not have all the necessary syncs. */
    client->use_write_sync = false;
    cfgval = client_cfg->client_write_sync;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if (rc == 0) {
            client->use_write_sync = (bool)b;
        }
    }

    /* Determine SUPER MAGIC value to return from statfs.
     * Use UNIFYFS_SUPER_MAGIC if true, TMPFS_SUPER_MAGIC otherwise. */
    client->use_unifyfs_magic = true;
    cfgval = client_cfg->client_super_magic;
    if (cfgval != NULL) {
        rc = configurator_bool_val(cfgval, &b);
        if (rc == 0) {
            client->use_unifyfs_magic = (bool)b;
        }
    }

    /* Define size of buffer used to cache key/value pairs for
     * data offsets before passing them to the server */
    client->write_index_size = UNIFYFS_CLIENT_WRITE_INDEX_SIZE;
    cfgval = client_cfg->client_write_index_size;
    if (cfgval != NULL) {
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            client->write_index_size = (size_t)l;
        }
    }
    client->max_write_index_entries =
        client->write_index_size / sizeof(unifyfs_index_t);

    /* Number of microsecs to sleep after calling client-to-server unlink rpc.
     * This is a work around (hack) to try to give the unlink operation time
     * to complete before the client returns from calling its unlink wrapper. */
    client->unlink_usecs = 0;
    cfgval = client_cfg->client_unlink_usecs;
    if (cfgval != NULL) {
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            client->unlink_usecs = (int)l;
        }
    }

    /* Timeout to wait on rpc calls to server, in milliseconds */
    double timeout_msecs = UNIFYFS_MARGO_CLIENT_SERVER_TIMEOUT_MSEC;
    cfgval = client_cfg->margo_client_timeout;
    if (cfgval != NULL) {
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            timeout_msecs = (double)l;
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
    rc = unifyfs_client_rpc_init(timeout_msecs);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to initialize client RPC");
        return rc;
    }

    /* Call client mount rpc function to get client id */
    int app_id = unifyfs_generate_gfid(mountpoint);
    client->state.app_id  = app_id;
    LOGDBG("calling mount rpc");
    rc = invoke_client_mount_rpc(client);
    if (rc != UNIFYFS_SUCCESS) {
        /* If we fail to connect to the server, bail with an error */
        LOGERR("failed to mount to server");
        return rc;
    }
    client->state.is_mounted = true;

    /* initialize our library using assigned client id, creates shared memory
     * regions (e.g., superblock and data recv) and inits log-based I/O */
    rc = unifyfs_client_init(client);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* Call client attach rpc function to register our newly created shared
     * memory and files with server */
    LOGDBG("calling attach rpc");
    rc = invoke_client_attach_rpc(client);
    if (rc != UNIFYFS_SUCCESS) {
        /* If we fail, bail with an error */
        LOGERR("failed to attach to server");
        unifyfs_client_fini(client);
        return rc;
    }

    /* add mount point as a new directory in the file list */
    if (unifyfs_fid_from_path(client, mountpoint) < 0) {
        /* no entry exists for mount point, so create one */
        rc = unifyfs_fid_create_directory(client, mountpoint);
        if (rc != UNIFYFS_SUCCESS) {
            /* if there was an error, return it */
            LOGERR("failed to create directory entry for mount point: `%s'",
                   mountpoint);
            unifyfs_client_fini(client);
            return rc;
        }
    }

    unifyfs_handle client_hdl = (unifyfs_handle) client;
    *fshdl = client_hdl;
    return UNIFYFS_SUCCESS;
}

/* Finalize client's use of UnifyFS */
unifyfs_rc unifyfs_finalize(unifyfs_handle fshdl)
{
    if (UNIFYFS_INVALID_HANDLE == fshdl) {
        return EINVAL;
    }
    unifyfs_client* client = fshdl;

    int ret = UNIFYFS_SUCCESS;

    if (client->state.is_mounted) {
        /* sync any outstanding writes */
        LOGDBG("syncing data");
        int rc = unifyfs_sync_files(client);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("client sync failed");
            ret = rc;
        }

        /* invoke unmount rpc to tell server we're disconnecting */
        LOGDBG("calling unmount");
        rc = invoke_client_unmount_rpc(client);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("client unmount rpc failed");
            ret = rc;
        }
    }

    /* free resources allocated in client_rpc_init */
    unifyfs_client_rpc_finalize();

    /* free resources allocated in unifyfs_client_init */
    unifyfs_client_fini(client);

    /* free memory tracking our mount prefix string */
    if (client->state.mount_prefix != NULL) {
        free(client->state.mount_prefix);
        client->state.mount_prefix = NULL;
        client->state.mount_prefixlen = 0;
    }

    /* free global holding current working directory */
    if (client->cwd != NULL) {
        free(client->cwd);
        client->cwd = NULL;
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

/* Retrieve client's UnifyFS configuration for the given handle. */
unifyfs_rc unifyfs_get_config(unifyfs_handle fshdl,
                              int* n_opts,
                              unifyfs_cfg_option** options)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl) ||
        (NULL == n_opts) ||
        (NULL == options)) {
        return EINVAL;
    }
    unifyfs_client* client = fshdl;

    int num_options;
    unifyfs_cfg_option* options_array;
    int ret = unifyfs_config_get_options(&(client->cfg),
                                         &num_options,
                                         &options_array);
    if (UNIFYFS_SUCCESS == ret) {
        *n_opts = num_options;
        *options = options_array;
    } else {
        *n_opts = 0;
        *options = NULL;
    }
    return ret;
}
