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

#include "unifyfs_api_internal.h"
#include "unifyfs_fid.h"
#include "margo_client.h"


/*
 * Internal Methods
 */

bool is_unifyfs_path(unifyfs_client* client,
                     const char* filepath)
{
    /* the library API expects absolute paths without relative components,
     * so we don't do any path normalization here */

    /* if the path starts with our mount point, intercept it */
    bool intercept = false;
    char* mount = client->state.mount_prefix;
    size_t len = client->state.mount_prefixlen;
    if (strncmp(filepath, mount, len) == 0) {
        /* characters in target up through mount point match,
         * so assume we match */
        intercept = true;

        /* if we have another character, it must be '/' */
        if ((strlen(filepath) > len) &&
            (filepath[len] != '/')) {
            intercept = 0;
        }
    }
    return intercept;
}

/*
 * Public Methods
 */

/* Create and open a new file in UnifyFS */
unifyfs_rc unifyfs_create(unifyfs_handle fshdl,
                          const int flags,
                          const char* filepath,
                          unifyfs_gfid* gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)
        || (NULL == gfid)) {
        return (unifyfs_rc)EINVAL;
    }
    *gfid = UNIFYFS_INVALID_GFID;

    unifyfs_client* client = fshdl;

    /* make sure requested file is within client namespace */
    if (!is_unifyfs_path(client, filepath)) {
        return (unifyfs_rc)EINVAL;
    }

    /* NOTE: the 'flags' parameter is not currently used. it is reserved
     * for future indication of file-specific behavior */

    /* the output parameters of unifyfs_fid_open() are not used here, but
     * must be provided */
    int fid = -1;
    off_t filepos = -1;

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int create_flags = O_CREAT;
    int rc = unifyfs_fid_open(client, filepath, create_flags, mode,
                              &fid, &filepos);
    if (UNIFYFS_SUCCESS == rc) {
        *gfid = unifyfs_generate_gfid(filepath);
    }
    return (unifyfs_rc)rc;
}

/* Open an existing file in UnifyFS */
unifyfs_rc unifyfs_open(unifyfs_handle fshdl,
                        const int flags,
                        const char* filepath,
                        unifyfs_gfid* gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)
        || (NULL == gfid)) {
        return (unifyfs_rc)EINVAL;
    }
    *gfid = UNIFYFS_INVALID_GFID;

    unifyfs_client* client = fshdl;

    /* make sure requested file is within client namespace */
    if (!is_unifyfs_path(client, filepath)) {
        return (unifyfs_rc)EINVAL;
    }

    /* the output parameters of unifyfs_fid_open() are not used here, but
     * must be provided */
    int fid = -1;
    off_t filepos = -1;

    mode_t mode = 0;
    int rc = unifyfs_fid_open(client, filepath, flags, mode, &fid, &filepos);
    if (UNIFYFS_SUCCESS == rc) {
        *gfid = unifyfs_generate_gfid(filepath);
    }
    return (unifyfs_rc)rc;
}

/* Synchronize client writes with server */
unifyfs_rc unifyfs_sync(unifyfs_handle fshdl,
                        const unifyfs_gfid gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (UNIFYFS_INVALID_GFID == gfid)) {
        return (unifyfs_rc)EINVAL;
    }

    unifyfs_client* client = fshdl;

    int fid = unifyfs_fid_from_gfid(client, (int)gfid);
    if (-1 == fid) {
        return (unifyfs_rc)EINVAL;
    }

    int rc = unifyfs_fid_sync_extents(client, fid);
    return (unifyfs_rc)rc;
}

/* Get global file status */
unifyfs_rc unifyfs_stat(unifyfs_handle fshdl,
                        const unifyfs_gfid gfid,
                        unifyfs_status* st)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (UNIFYFS_INVALID_GFID == gfid)
        || (NULL == st)) {
        return (unifyfs_rc)EINVAL;
    }

    unifyfs_client* client = fshdl;

    int fid = unifyfs_fid_from_gfid(client, (int)gfid);
    if (-1 == fid) {
        return (unifyfs_rc)EINVAL;
    }

    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if (meta == NULL) {
        LOGERR("missing local file metadata for gfid=%d", (int)gfid);
        return UNIFYFS_FAILURE;
    }

    /* get global metadata to pick up current file size */
    unifyfs_file_attr_t attr = {0};
    int rc = unifyfs_get_global_file_meta(client, (int)gfid, &attr);
    if (UNIFYFS_SUCCESS != rc) {
        LOGERR("missing global file metadata for gfid=%d", (int)gfid);
    } else {
        /* update local file metadata from global metadata */
        unifyfs_fid_update_file_meta(client, fid, &attr);
    }

    st->global_file_size = meta->attrs.size;
    st->laminated = meta->attrs.is_laminated;
    st->mode = (int) meta->attrs.mode;

    /* TODO - need new metadata fields to track these */
    st->local_file_size = meta->attrs.size;
    st->local_write_nbytes = 0;
    return UNIFYFS_SUCCESS;
}

/* Global lamination - no further writes to file are permitted */
unifyfs_rc unifyfs_laminate(unifyfs_handle fshdl,
                            const char* filepath)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)) {
        return (unifyfs_rc)EINVAL;
    }

    unifyfs_client* client = fshdl;

    /* make sure requested file is within client namespace */
    if (!is_unifyfs_path(client, filepath)) {
        return (unifyfs_rc)EINVAL;
    }

    int gfid = unifyfs_generate_gfid(filepath);
    int rc = invoke_client_laminate_rpc(client, gfid);
    if (UNIFYFS_SUCCESS == rc) {
        /* update the local state for this file (if any) */
        int fid = unifyfs_fid_from_gfid(client, (int)gfid);
        if (-1 != fid) {
            /* get global metadata to pick up file size and laminated flag */
            unifyfs_file_attr_t attr = {0};
            rc = unifyfs_get_global_file_meta(client, gfid, &attr);
            if (UNIFYFS_SUCCESS != rc) {
                LOGERR("missing global metadata for %s (gfid:%d)",
                       filepath, gfid);
            } else {
                /* update local file metadata from global metadata */
                unifyfs_fid_update_file_meta(client, fid, &attr);
            }
        }
    }

    return (unifyfs_rc)rc;
}

/* Remove an existing file from UnifyFS */
unifyfs_rc unifyfs_remove(unifyfs_handle fshdl,
                          const char* filepath)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)) {
        return (unifyfs_rc)EINVAL;
    }

    unifyfs_client* client = fshdl;

    /* make sure requested file is within client namespace */
    if (!is_unifyfs_path(client, filepath)) {
        return (unifyfs_rc)EINVAL;
    }

    unifyfs_rc ret = UNIFYFS_SUCCESS;

    /* invoke unlink rpc */
    int gfid = unifyfs_generate_gfid(filepath);
    int rc = invoke_client_unlink_rpc(client, gfid);
    if (rc != UNIFYFS_SUCCESS) {
        ret = rc;
    }

    /* clean up the local state for this file (if any) */
    int fid = unifyfs_fid_from_gfid(client, gfid);
    if (-1 != fid) {
        rc = unifyfs_fid_delete(client, fid);
        if (rc != UNIFYFS_SUCCESS) {
            /* released storage for file, but failed to release
             * structures tracking storage, again bail out to keep
             * its file id active */
            ret = rc;
        }
    }

    return ret;
}
