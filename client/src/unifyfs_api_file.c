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

bool is_unifyfs_path(unifyfs_handle fshdl,
                     const char* filepath)
{
    if ((UNIFYFS_INVALID_HANDLE == fshdl) || (NULL == filepath)) {
        return false;
    }
    unifyfs_client* client = fshdl;

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

    int exclusive = 1;
    int private = ((flags & O_EXCL) && client->use_excl_private);
    int truncate = 0;
    mode_t mode = UNIFYFS_STAT_DEFAULT_FILE_MODE;
    int ret = unifyfs_gfid_create(client, filepath,
        exclusive, private, truncate, mode);
    if (UNIFYFS_SUCCESS != ret) {
        return (unifyfs_rc)ret;
    }

    return unifyfs_open(fshdl, flags, filepath, gfid);
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

    /* Ensure that only read/write access flags are set.
     * In particular, do not allow flag like:
     * O_CREAT, O_EXCL, O_TRUNC, O_DIRECTORY */
    int allowed = O_RDONLY | O_RDWR | O_WRONLY;
    int remaining = flags & ~(allowed);
    if (remaining) {
        LOGERR("Valid flags limited to {O_RDONLY, O_RDWR, O_WRONLY} %s",
               filepath);
        return (unifyfs_rc)EINVAL;
    }

    /* File should exist at this point,
     * update our cache with its metadata. */
    int rc = unifyfs_fid_fetch(client, filepath);
    if (UNIFYFS_SUCCESS == rc) {
        *gfid = unifyfs_generate_gfid(filepath);
    } else {
        LOGERR("Failed to get metadata on file %s",
               filepath);
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
                        unifyfs_file_status* st)
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
    int gfid = unifyfs_generate_gfid(filepath);
    int rc;

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

    /* invoke unlink rpc */
    rc = invoke_client_unlink_rpc(client, gfid);
    if (rc != UNIFYFS_SUCCESS) {
        ret = rc;
    }

    return ret;
}


/* Return a list of all the gfids the server knows about */
unifyfs_rc unifyfs_get_gfid_list(unifyfs_handle fshdl,
                                 int* num_gfids,
                                 unifyfs_gfid** gfid_list)
{
    unifyfs_client* client = fshdl;
    // TODO: internally, we seem to be using ints for gfids.  The fact that
    // they're uint32_t at the API layer is worrying...
    return invoke_client_get_gfids_rpc(client, num_gfids, (int**)gfid_list);
}

/* Get metadata for a specific gfid from the server */
/* Note: This function differs from unifyfs_stat() above in that this function
 * goes directly to the server and doesn't bother checking for anything that
 * the client side knows about the file.
 */
unifyfs_rc unifyfs_get_server_file_meta(unifyfs_handle fshdl,
                                        unifyfs_gfid gfid,
                                        unifyfs_server_file_meta* fmeta)
{
    unifyfs_client* client = fshdl;
    unifyfs_file_attr_t gfattr;

    int ret = unifyfs_get_global_file_meta(client, gfid, &gfattr);
    if (UNIFYFS_SUCCESS == ret) {
        // Copy the fields from gfattr over to fmeta
        // This is basically a 1-to-1 copy becaue unifyfs_server_file_meta
        // is just a public-facing version of unifyfs_file_attr_t.  We don't
        // want to memcpy(), though, in case the structs ever do diverge.
        fmeta->filename     = gfattr.filename;
        fmeta->gfid         = gfattr.gfid;
        fmeta->is_laminated = gfattr.is_laminated;
        fmeta->is_shared    = gfattr.is_shared;
        fmeta->mode         = gfattr.mode;
        fmeta->uid          = gfattr.uid;
        fmeta->gid          = gfattr.gid;
        fmeta->size         = gfattr.size;
        fmeta->atime        = gfattr.atime;
        fmeta->mtime        = gfattr.mtime;
        fmeta->ctime        = gfattr.ctime;
    }
    return ret;
}
