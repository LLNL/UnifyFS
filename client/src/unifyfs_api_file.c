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

    /* NOTE: the 'flags' parameter is not currently used. it is reserved
     * for future indication of file-specific behavior */

    /* the output parameters of unifyfs_fid_open() are not used here, but
     * must be provided */
    int fid = -1;
    off_t filepos = -1;

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int create_flags = (O_CREAT | O_EXCL);
    int rc = unifyfs_fid_open(filepath, create_flags, mode, &fid, &filepos);
    if (UNIFYFS_SUCCESS == rc) {
        *gfid = unifyfs_generate_gfid(filepath);
    }
    return (unifyfs_rc)rc;
}

/* Open an existing file in UnifyFS */
unifyfs_rc unifyfs_open(unifyfs_handle fshdl,
                        const char* filepath,
                        unifyfs_gfid* gfid)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl)
        || (NULL == filepath)
        || (NULL == gfid)) {
        return (unifyfs_rc)EINVAL;
    }
    *gfid = UNIFYFS_INVALID_GFID;

    /* the output parameters of unifyfs_fid_open() are not used here, but
     * must be provided */
    int fid = -1;
    off_t filepos = -1;

    mode_t mode = 0;
    int flags = O_RDWR;
    int rc = unifyfs_fid_open(filepath, flags, mode, &fid, &filepos);
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

    int fid = unifyfs_fid_from_gfid((int)gfid);
    if (-1 == fid) {
        return (unifyfs_rc)EINVAL;
    }

    int rc = unifyfs_fid_sync(fid);
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

    int fid = unifyfs_fid_from_gfid((int)gfid);
    if (-1 == fid) {
        return (unifyfs_rc)EINVAL;
    }

    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
    if (meta == NULL) {
        LOGERR("missing local file metadata for gfid=%d", (int)gfid);
        return UNIFYFS_FAILURE;
    }

    /* get global metadata to pick up current file size */
    unifyfs_file_attr_t attr = {0};
    int rc = unifyfs_get_global_file_meta((int)gfid, &attr);
    if (UNIFYFS_SUCCESS != rc) {
        LOGERR("missing global file metadata for gfid=%d", (int)gfid);
    } else {
        /* update local file metadata from global metadata */
        unifyfs_fid_update_file_meta(fid, &attr);
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

    int gfid = unifyfs_generate_gfid(filepath);
    int rc = invoke_client_laminate_rpc(gfid);
    if (UNIFYFS_SUCCESS == rc) {
        /* update the local state for this file (if any) */
        int fid = unifyfs_fid_from_gfid((int)gfid);
        if (-1 != fid) {
            /* get global metadata to pick up file size and laminated flag */
            unifyfs_file_attr_t attr = {0};
            rc = unifyfs_get_global_file_meta(gfid, &attr);
            if (UNIFYFS_SUCCESS != rc) {
                LOGERR("missing global metadata for %s (gfid:%d)",
                       filepath, gfid);
            } else {
                /* update local file metadata from global metadata */
                unifyfs_fid_update_file_meta(fid, &attr);
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

    unifyfs_rc ret = UNIFYFS_SUCCESS;

    /* invoke unlink rpc */
    int gfid = unifyfs_generate_gfid(filepath);
    int rc = invoke_client_unlink_rpc(gfid);
    if (rc != UNIFYFS_SUCCESS) {
        ret = rc;
    }

    /* clean up the local state for this file (if any) */
    int fid = unifyfs_fid_from_gfid(gfid);
    if (-1 != fid) {
        rc = unifyfs_fid_delete(fid);
        if (rc != UNIFYFS_SUCCESS) {
            /* released storage for file, but failed to release
             * structures tracking storage, again bail out to keep
             * its file id active */
            ret = rc;
        }
    }

    return ret;
}
