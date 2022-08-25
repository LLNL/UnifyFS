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

#include "unifyfs_fid.h"
#include "margo_client.h"

/* ---------------------------------------
 * fid stack management
 * --------------------------------------- */

/* allocate a file id slot for a new file
 * return the fid or -1 on error */
int unifyfs_fid_alloc(unifyfs_client* client)
{
    unifyfs_stack_lock(client);
    int fid = unifyfs_stack_pop(client->free_fid_stack);
    unifyfs_stack_unlock(client);
    LOGDBG("unifyfs_stack_pop() gave %d", fid);
    if (fid < 0) {
        /* need to create a new file, but we can't */
        LOGERR("unifyfs_stack_pop() failed (%d)", fid);
        return -EMFILE;
    }
    return fid;
}

/* return the file id back to the free pool */
int unifyfs_fid_free(unifyfs_client* client,
                     int fid)
{
    unifyfs_stack_lock(client);
    unifyfs_stack_push(client->free_fid_stack, fid);
    unifyfs_stack_unlock(client);
    return UNIFYFS_SUCCESS;
}

/* ---------------------------------------
 * fid metadata update operations
 * --------------------------------------- */

/* allocate and initialize data management resource for file */
static int fid_storage_alloc(unifyfs_client* client,
                             int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if ((meta != NULL) && (meta->fid == fid)) {
        /* Initialize our segment tree that will record our writes */
        int rc = seg_tree_init(&meta->extents_sync);
        if (rc != 0) {
            return UNIFYFS_FAILURE;
        }

        /* Initialize our segment tree to track extents for all writes
         * by this process, can be used to read back local data */
        if (client->use_local_extents) {
            rc = seg_tree_init(&meta->extents);
            if (rc != 0) {
                /* clean up extents_sync tree we initialized */
                seg_tree_destroy(&meta->extents_sync);
                return UNIFYFS_FAILURE;
            }
        }

        /* indicate that we're using LOGIO to store data for this file */
        meta->storage = FILE_STORAGE_LOGIO;

        return UNIFYFS_SUCCESS;
    } else {
        LOGERR("failed to get filemeta for fid=%d", fid);
    }

    return UNIFYFS_FAILURE;
}

/* free data management resource for file */
static int fid_storage_free(unifyfs_client* client,
                            int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if ((meta != NULL) && (fid == meta->fid)) {
        if (meta->storage == FILE_STORAGE_LOGIO) {
            /* client needs to release unsynced write extents, since server
             * does not know about them */
            seg_tree_rdlock(&meta->extents_sync);
            struct seg_tree_node* node = NULL;
            while ((node = seg_tree_iter(&meta->extents_sync, node))) {
                size_t nbytes = (size_t) (node->end - node->start + 1);
                off_t log_offset = (off_t) node->ptr;
                int rc = unifyfs_logio_free(client->state.logio_ctx,
                                            log_offset, nbytes);
                if (UNIFYFS_SUCCESS != rc) {
                    LOGERR("failed to free logio allocation for "
                           "client[%d:%d] log_offset=%zu nbytes=%zu",
                           client->state.app_id, client->state.client_id,
                           log_offset, nbytes);
                }
            }
            seg_tree_unlock(&meta->extents_sync);

            /* Free our write seg_tree */
            seg_tree_destroy(&meta->extents_sync);

            /* Free our extent seg_tree */
            if (client->use_local_extents) {
                seg_tree_destroy(&meta->extents);
            }
        }

        /* set storage type back to NULL */
        meta->storage = FILE_STORAGE_NULL;

        meta->fid = -1;

        return UNIFYFS_SUCCESS;
    }

    return UNIFYFS_FAILURE;
}


/* Update local metadata for file from global metadata */
int unifyfs_fid_update_file_meta(unifyfs_client* client,
                                 int fid,
                                 unifyfs_file_attr_t* gfattr)
{
    if (NULL == gfattr) {
        return EINVAL;
    }

    /* lookup local metadata for file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if (meta != NULL) {
        meta->attrs = *gfattr;
        return UNIFYFS_SUCCESS;
    }

    /* else, bad fid */
    return EINVAL;
}

/*
 * Set the global metadata values for a file using local file
 * attributes associated with the given local file id.
 *
 * fid:    The local file id on which to base global metadata values.
 *
 * op:     If set to FILE_ATTR_OP_CREATE, attempt to create the file first.
 *         If the file already exists, then update its metadata with the values
 *         from fid filemeta.  If not creating and the file does not exist,
 *         then the server will return an error.
 */
int unifyfs_set_global_file_meta_from_fid(unifyfs_client* client,
                                          int fid,
                                          unifyfs_file_attr_op_e op)
{
    /* initialize an empty file attributes structure */
    unifyfs_file_attr_t fattr;
    unifyfs_file_attr_set_invalid(&fattr);

    /* lookup local metadata for file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    assert(meta != NULL);

    /* set global file id */
    fattr.gfid = meta->attrs.gfid;

    LOGDBG("setting global file metadata for fid:%d gfid:%d path:%s",
           fid, fattr.gfid, meta->attrs.filename);

    unifyfs_file_attr_update(op, &fattr, &(meta->attrs));

    LOGDBG("using following attributes");
    debug_print_file_attr(&fattr);

    /* submit file attributes to global key/value store */
    int ret = unifyfs_set_global_file_meta(client, fattr.gfid, op, &fattr);
    return ret;
}

/* add a new file and initialize metadata
 * returns the new fid, or negative value on error */
int unifyfs_fid_create_file(unifyfs_client* client,
                            const char* path,
                            int private)
{
    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return -ENAMETOOLONG;
    }

    pthread_mutex_lock(&(client->sync));

    /* allocate an id for this file */
    int fid = unifyfs_fid_alloc(client);
    if (fid < 0)  {
        pthread_mutex_unlock(&(client->sync));
        return fid;
    }

    /* mark this slot as in use */
    client->unifyfs_filelist[fid].in_use = 1;

    /* copy file name into slot */
    strlcpy((void*)&client->unifyfs_filelist[fid].filename, path,
            UNIFYFS_MAX_FILENAME);
    LOGDBG("Filename %s got unifyfs fid %d",
           client->unifyfs_filelist[fid].filename, fid);

    pthread_mutex_unlock(&(client->sync));

    /* get metadata for this file id */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    assert(meta != NULL);

    /* initialize file attributes */
    unifyfs_file_attr_set_invalid(&(meta->attrs));
    meta->attrs.gfid = unifyfs_generate_gfid(path);
    meta->attrs.size = 0;
    meta->attrs.mode = UNIFYFS_STAT_DEFAULT_FILE_MODE;
    meta->attrs.is_laminated = 0;
    meta->attrs.is_shared = !private;
    meta->attrs.filename = (char*)&(client->unifyfs_filelist[fid].filename);

    /* use client user/group */
    meta->attrs.uid = getuid();
    meta->attrs.gid = getgid();

    /* use current time for atime/mtime/ctime */
    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);
    meta->attrs.atime = tp;
    meta->attrs.mtime = tp;
    meta->attrs.ctime = tp;

    /* set UnifyFS client metadata */
    meta->fid            = fid;
    meta->storage        = FILE_STORAGE_NULL;
    meta->needs_sync     = 0;
    meta->pending_unlink = 0;

    return fid;
}

/* create directory state for given path. returns success|error */
int unifyfs_fid_create_directory(unifyfs_client* client,
                                 const char* path)
{
    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return ENAMETOOLONG;
    }

    /* get local and global file ids */
    int fid  = unifyfs_fid_from_path(client, path);
    int gfid = unifyfs_generate_gfid(path);

    /* test whether we have info for file in our local file list */
    int found_local = (fid != -1);

    /* test whether we have metadata for file in global key/value store */
    int found_global = 0;
    unifyfs_file_attr_t gfattr = { 0 };
    int rc = unifyfs_get_global_file_meta(client, gfid, &gfattr);
    if (UNIFYFS_SUCCESS == rc) {
        found_global = 1;
    }

    if (found_local && !found_global) {
        /* exists locally, but not globally
         *
         * FIXME: so, we have detected the cache inconsistency here.
         * we cannot simply unlink or remove the entry because then we also
         * need to check whether any subdirectories or files exist.
         *
         * this can happen when
         * - a process created a directory. this process (A) has opened it at
         *   least once.
         * - then, the directory has been deleted by another process (B). it
         *   deletes the global entry without checking any local used entries
         *   in other processes.
         *
         * we currently return EEXIST, and this needs to be addressed according
         * to a consistency model this fs intance assumes.
         */
        return EEXIST;
    }

    /* now, we need to create a new directory. we reuse the file creation
     * method and then update the mode to indicate it's a directory */
    if (!found_local) {
        /* create a new file */
        fid = unifyfs_fid_create_file(client, path, 0);
        if (fid < 0) {
            /* convert negative error code to positive */
            return -fid;
        }

        /* mark it as a directory */
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
        assert(meta != NULL);
        meta->attrs.mode = (meta->attrs.mode & ~S_IFREG) | S_IFDIR;

        if (!found_global) {
            /* insert global meta data for directory */
            unifyfs_file_attr_op_e op = UNIFYFS_FILE_ATTR_OP_CREATE;
            rc = unifyfs_set_global_file_meta_from_fid(client, fid, op);
            if (rc != UNIFYFS_SUCCESS) {
                if (rc != EEXIST) {
                    LOGERR("Failed to add global metadata for dir %s (rc=%d)",
                           path, rc);
                    return rc;
                } /* else, someone else created global metadata first */
            }
        }
    }

    return UNIFYFS_SUCCESS;
}

/* delete a file id, free its local storage resources and return
 * the file id to free stack */
int unifyfs_fid_delete(unifyfs_client* client,
                       int fid)
{
    pthread_mutex_lock(&(client->sync));

    /* set this file id as not in use */
    client->unifyfs_filelist[fid].in_use = 0;
    client->unifyfs_filelist[fid].filename[0] = 0;

    /* finalize the storage we're using for this file */
    int rc = fid_storage_free(client, fid);
    if (rc != UNIFYFS_SUCCESS) {
        /* failed to release structures tracking storage,
         * bail out to keep its file id active */
        pthread_mutex_unlock(&(client->sync));
        return rc;
    }

    /* add this id back to the free stack */
    rc = unifyfs_fid_free(client, fid);
    if (rc != UNIFYFS_SUCCESS) {
        /* storage for the file was released, but we hit
         * an error while freeing the file id */
        pthread_mutex_unlock(&(client->sync));
        return rc;
    }

    pthread_mutex_unlock(&(client->sync));

    return UNIFYFS_SUCCESS;
}

/* opens a new file id with specified path, access flags, and permissions,
 * fills outfid with file id and outpos with position for current file pointer,
 * returns UNIFYFS error code
 */
int unifyfs_fid_open(
    unifyfs_client* client,
    const char* path, /* path of file to be opened */
    int flags,        /* flags bits as from open(2) */
    mode_t mode,      /* mode bits as from open(2) */
    int* outfid,      /* allocated local file id if open is successful */
    off_t* outpos)    /* initial file position if open is successful */
{
    int rc;
    int ret = UNIFYFS_SUCCESS;

    /* set the pointer to the start of the file */
    off_t pos = 0;

    /* check that pathname is within bounds */
    size_t pathlen = strlen(path) + 1;
    if (pathlen > UNIFYFS_MAX_FILENAME) {
        return ENAMETOOLONG;
    }

    /*
     * TODO: The test of file existence involves both local and global checks.
     * However, the testing below does not seem to cover all cases. For
     * instance, a globally unlinked file might be still cached locally because
     * the broadcast for cache invalidation has not been implemented, yet.
     */

    /* look for local and global file ids */
    int fid  = unifyfs_fid_from_path(client, path);
    int gfid = unifyfs_generate_gfid(path);
    LOGDBG("unifyfs_fid_from_path() gave %d (gfid = %d)", fid, gfid);

    /* test whether we have info for file in our local file list */
    int found_local = (fid >= 0);

    /* determine whether any write flags are specified */
    int open_for_write = flags & (O_RDWR | O_WRONLY);

    int exclusive = flags & O_EXCL;

    /* struct to hold global metadata for file */
    unifyfs_file_attr_t gfattr = { 0, };

    /* if O_CREAT,
     *   if not local, allocate fid and storage
     *   create local fid meta
     *   attempt to create global inode
     *   if EEXIST and O_EXCL, error and release fid/storage
     *   lookup global meta
     *   check that local and global info are consistent
     *   if O_TRUNC and not laminated, truncate
     * else
     *   lookup global meta
     *     if not found, error
     *   check that local and global info are consistent
     * if O_APPEND, set pos to file size
     */

    /* flag indicating whether file should be truncated */
    int need_truncate = 0;

    /* determine whether we are creating a new file
     * or opening an existing one */
    if (flags & O_CREAT) {
        /* user wants to create a new file,
         * allocate a local file id structure if needed */
        if (!found_local) {
            /* initialize local metadata for this file */
            fid = unifyfs_fid_create_file(client, path, exclusive);
            if (fid < 0) {
                LOGERR("failed to create a new file %s", path);
                return -fid;
            }

            /* initialize local storage for this file */
            rc = fid_storage_alloc(client, fid);
            if (rc != UNIFYFS_SUCCESS) {
                LOGERR("failed to allocate storage space for file %s (fid=%d)",
                    path, fid);
                unifyfs_fid_delete(client, fid);
                return ENOMEM;
            }

            /* TODO: set meta->mode bits to mode variable */
        }

        /* create global file metadata */
        unifyfs_file_attr_op_e op = UNIFYFS_FILE_ATTR_OP_CREATE;
        ret = unifyfs_set_global_file_meta_from_fid(client, fid, op);
        if (ret == EEXIST) {
            LOGINFO("Attempt to create existing file %s (fid:%d)", path, fid);
            if (!exclusive) {
                /* File didn't exist before, but now it does.
                 * Another process beat us to the punch in creating it.
                 * Read its metadata to update our cache. */
                rc = unifyfs_get_global_file_meta(client, gfid, &gfattr);
                if (rc == UNIFYFS_SUCCESS) {
                    /* check for truncate if the file exists already */
                    if ((flags & O_TRUNC) && open_for_write) {
                        if (gfattr.is_laminated) {
                            ret = EROFS;
                        } else {
                            need_truncate = 1;
                        }
                    }

                    /* append writes are ok on existing files too */
                    if ((flags & O_APPEND) && open_for_write) {
                        ret = UNIFYFS_SUCCESS;
                    }

                    /* Successful in fetching metadata for existing file.
                     * Update our local cache using that metadata. */
                    unifyfs_fid_update_file_meta(client, fid, &gfattr);

                    if (!found_local) {
                        /* it's ok if another client created the shared file */
                        ret = UNIFYFS_SUCCESS;
                    }
                } else {
                    /* Failed to get metadata for a file that should exist.
                     * Perhaps it was since deleted.  We could try to create
                     * it again and loop through these steps, but for now
                     * consider this situation to be an error. */
                    LOGERR("Failed to get metadata on existing file %s", path);
                }
            } else {
                LOGERR("Failed create of existing private/exclusive file %s",
                       path);
            }
        }
        if (ret != UNIFYFS_SUCCESS) {
            if (!found_local) {
                /* free fid resources we just allocated above,
                 * but don't do that by calling fid_unlink */
                unifyfs_fid_delete(client, fid);
            }
            return ret;
        }
    } else {
        /* trying to open without creating, file must already exist,
         * lookup global metadata for file */
        ret = unifyfs_get_global_file_meta(client, gfid, &gfattr);
        if (ret != UNIFYFS_SUCCESS) {
            /* bail out if we failed to find global file */
            if (found_local && ret == ENOENT) {
                /* Have a local entry, but there is no global entry.
                 * Perhaps global file was unlinked?
                 * Invalidate our local entry. */
                LOGDBG("file found locally, but seems to be deleted globally. "
                       "invalidating the local cache.");
                unifyfs_fid_delete(client, fid);
            }

            return ret;
        }

        /* succeeded in global lookup for file,
         * allocate a local file id structure if needed */
        if (!found_local) {
            /* initialize local metadata for this file */
            fid = unifyfs_fid_create_file(client, path, 0);
            if (fid < 0) {
                LOGERR("failed to create a new file %s", path);
                return -fid;
            }

            /* initialize local storage for this file */
            ret = fid_storage_alloc(client, fid);
            if (ret != UNIFYFS_SUCCESS) {
                LOGERR("failed to allocate storage space for file %s (fid=%d)",
                       path, fid);
                /* free fid we just allocated above,
                 * but don't do that by calling fid_unlink */
                unifyfs_fid_delete(client, fid);
                return ret;
            }
        } else {
            /* TODO: already have a local entry for this path and found
             * a global entry, check that they are consistent */
        }

        /* Successful in fetching metadata for existing file.
         * Update our local cache using that metadata. */
        unifyfs_fid_update_file_meta(client, fid, &gfattr);

        /* check if we need to truncate the existing file */
        if ((flags & O_TRUNC) && open_for_write && !gfattr.is_laminated) {
            need_truncate = 1;
        }
    }

    /* if given O_DIRECTORY, the named file must be a directory */
    if ((flags & O_DIRECTORY) && !unifyfs_fid_is_dir(client, fid)) {
        if (!found_local) {
            /* free fid we just allocated above,
             * but don't do that by calling fid_unlink */
            unifyfs_fid_delete(client, fid);
        }
        return ENOTDIR;
    }

    /* TODO: does O_DIRECTORY really have to be given to open a directory? */
    if (!(flags & O_DIRECTORY) && unifyfs_fid_is_dir(client, fid)) {
        if (!found_local) {
            /* free fid we just allocated above,
             * but don't do that by calling fid_unlink */
            unifyfs_fid_delete(client, fid);
        }
        return EISDIR;
    }

    /*
     * Catch any case where we could potentially want to write to a laminated
     * file.
     */
    if (gfattr.is_laminated &&
        ((flags & (O_CREAT | O_TRUNC | O_APPEND | O_WRONLY)) ||
         ((mode & 0222) && (flags != O_RDONLY)))) {
            LOGDBG("Can't open laminated file %s with a writable flag.", path);
            /* TODO: free fid we just allocated above,
             * but don't do that by calling fid_unlink */
            if (!found_local) {
                /* free fid we just allocated above,
                 * but don't do that by calling fid_unlink */
                unifyfs_fid_delete(client, fid);
            }
            return EROFS;
    }

    /* truncate the file, if we have to */
    if (need_truncate) {
        ret = unifyfs_fid_truncate(client, fid, 0);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("Failed to truncate the file %s", path);
            return ret;
        }
    }

    /* for appends, update position to EOF */
    if ((flags & O_APPEND) && open_for_write) {
        pos = unifyfs_fid_logical_size(client, fid);
    }

    /* return local file id and starting file position */
    *outfid = fid;
    *outpos = pos;

    return UNIFYFS_SUCCESS;
}

int unifyfs_fid_close(unifyfs_client* client,
                      int fid)
{
    /* TODO: clear any held locks */

    /* nothing to do here, just a place holder */
    return UNIFYFS_SUCCESS;
}

/* unlink file and then delete its associated state */
int unifyfs_fid_unlink(unifyfs_client* client,
                       int fid)
{
    int rc;

    /* invoke unlink rpc */
    int gfid = unifyfs_gfid_from_fid(client, fid);

    /* finalize the storage we're using for this file */
    rc = unifyfs_fid_delete(client, fid);
    if (rc != UNIFYFS_SUCCESS) {
        /* released storage for file, but failed to release
         * structures tracking storage, again bail out to keep
         * its file id active */
        return rc;
    }

    rc = invoke_client_unlink_rpc(client, gfid);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    return UNIFYFS_SUCCESS;
}


/* ---------------------------------------
 * fid metadata query operations
 * --------------------------------------- */

/* given a file id, return a pointer to the meta data,
 * otherwise return NULL */
unifyfs_filemeta_t* unifyfs_get_meta_from_fid(unifyfs_client* client,
                                              int fid)
{
    /* check that the file id is within range of our array */
    if (fid >= 0 && fid < client->max_files) {
        /* get a pointer to the file meta data structure */
        pthread_mutex_lock(&(client->sync));
        unifyfs_filemeta_t* meta = &(client->unifyfs_filemetas[fid]);
        pthread_mutex_unlock(&(client->sync));

        if (fid == meta->fid) {
            /* before returning metadata, process any pending callbacks */
            if (meta->pending_unlink) {
                LOGDBG("processing pending global unlink");
                meta->pending_unlink = 0;
                int rc = unifyfs_fid_delete(client, fid);
                if (UNIFYFS_SUCCESS != rc) {
                    LOGERR("fid delete failed");
                }
                return NULL; /* we just deleted it */
            }
        }
        return meta;
    }
    return NULL;
}

/* given a file id, return 1 if file is laminated, 0 otherwise */
int unifyfs_fid_is_laminated(unifyfs_client* client,
                             int fid)
{
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if ((meta != NULL) && (meta->fid == fid)) {
        return meta->attrs.is_laminated;
    }
    return 0;
}

/* checks to see if fid is a directory
 * returns 1 for yes
 * returns 0 for no */
int unifyfs_fid_is_dir(unifyfs_client* client,
                       int fid)
{
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if ((meta != NULL) && (meta->attrs.mode & S_IFDIR)) {
        return 1;
    }
    return 0;
}

/* checks to see if a directory is empty
 * assumes that check for is_dir has already been made
 * only checks for full path matches, does not check relative paths,
 * e.g. ../dirname will not work
 * returns 1 for yes it is empty
 * returns 0 for no */
int unifyfs_fid_is_dir_empty(unifyfs_client* client,
                             const char* path)
{
    pthread_mutex_lock(&(client->sync));
    for (int i = 0; i < client->max_files; i++) {
        /* only check this element if it's active */
        if (client->unifyfs_filelist[i].in_use) {
            /* if the file starts with the path, it is inside of that directory
             * also check that it's not the directory entry itself */
            char* strptr = strstr(path, client->unifyfs_filelist[i].filename);
            if (strptr == client->unifyfs_filelist[i].filename &&
                strcmp(path, client->unifyfs_filelist[i].filename) != 0) {
                /* found a child item in path */
                LOGDBG("File found: unifyfs_filelist[%d].filename = %s",
                       i, (char*)&client->unifyfs_filelist[i].filename);
                pthread_mutex_unlock(&(client->sync));
                return 0;
            }
        }
    }
    pthread_mutex_unlock(&(client->sync));

    /* couldn't find any files with this prefix, dir must be empty */
    return 1;
}

int unifyfs_gfid_from_fid(unifyfs_client* client,
                          int fid)
{
    /* check that local file id is in range */
    if (fid < 0 || fid >= client->max_files) {
        return -1;
    }

    /* return global file id, cached in file meta struct */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if (meta != NULL) {
        return meta->attrs.gfid;
    } else {
        return -1;
    }
}

/* scan list of files and return fid corresponding to target gfid,
 * returns -1 if not found */
int unifyfs_fid_from_gfid(unifyfs_client* client,
                          int gfid)
{
    pthread_mutex_lock(&(client->sync));
    for (int i = 0; i < client->max_files; i++) {
        if (client->unifyfs_filelist[i].in_use &&
            client->unifyfs_filemetas[i].attrs.gfid == gfid) {
            /* found a file id that's in use and it matches
             * the target fid, this is the one */
            pthread_mutex_unlock(&(client->sync));
            return i;
        }
    }
    pthread_mutex_unlock(&(client->sync));
    return -1;
}

/* Given a fid, return the path.  */
const char* unifyfs_path_from_fid(unifyfs_client* client,
                                  int fid)
{
    pthread_mutex_lock(&(client->sync));
    unifyfs_filename_t* fname = &client->unifyfs_filelist[fid];
    if (fname->in_use) {
        pthread_mutex_unlock(&(client->sync));
        return fname->filename;
    }
    pthread_mutex_unlock(&(client->sync));
    return NULL;
}

/* Given a path, return the local file id, or -1 if not found */
int unifyfs_fid_from_path(unifyfs_client* client,
                          const char* path)
{
    /* scan through active entries in filelist array looking
     * for a match of path */
    pthread_mutex_lock(&(client->sync));
    for (int i = 0; i < client->max_files; i++) {
        if (client->unifyfs_filelist[i].in_use) {
            const char* filename = client->unifyfs_filelist[i].filename;
            if (0 == strcmp(filename, path)) {
                LOGDBG("File found: unifyfs_filelist[%d].filename = %s",
                       i, (char*)filename);
                pthread_mutex_unlock(&(client->sync));
                return i;
            }
        }
    }
    pthread_mutex_unlock(&(client->sync));

    /* couldn't find specified path */
    return -1;
}

/* Return the global (laminated) size of the file */
off_t unifyfs_fid_global_size(unifyfs_client* client,
                              int fid)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if (meta != NULL) {
        return meta->attrs.size;
    }
    return (off_t)-1;
}

/*
 * Return the size of the file.  If the file is laminated, return the
 * laminated size.  If the file is not laminated, return the local
 * size.
 */
off_t unifyfs_fid_logical_size(unifyfs_client* client,
                               int fid)
{
    /* get meta data for this file */
    if (unifyfs_fid_is_laminated(client, fid)) {
        off_t size = unifyfs_fid_global_size(client, fid);
        return size;
    } else {
        /* invoke an rpc to ask the server what the file size is */

        /* sync any writes to disk before requesting file size */
        unifyfs_fid_sync_extents(client, fid);

        /* get file size for this file */
        size_t filesize;
        int gfid = unifyfs_gfid_from_fid(client, fid);
        int ret = invoke_client_filesize_rpc(client, gfid, &filesize);
        if (ret != UNIFYFS_SUCCESS) {
            /* failed to get file size */
            return (off_t)-1;
        }
        return (off_t)filesize;
    }
}


/* =======================================
 * I/O operations on fids
 * ======================================= */

/* Find write extents that span or exceed truncation offset and remove them */
static int fid_truncate_write_meta(unifyfs_client* client,
                                   unifyfs_filemeta_t* meta,
                                   off_t trunc_sz)
{
    if (0 == trunc_sz) {
        /* All writes should be removed. Clear extents_sync */
        seg_tree_clear(&meta->extents_sync);

        if (client->use_local_extents) {
            /* Clear the local extent cache too */
            seg_tree_clear(&meta->extents);
        }
        return UNIFYFS_SUCCESS;
    }

    unsigned long trunc_off = (unsigned long) trunc_sz;
    int rc = seg_tree_remove(&meta->extents_sync, trunc_off, ULONG_MAX);
    if (client->use_local_extents) {
        rc = seg_tree_remove(&meta->extents, trunc_off, ULONG_MAX);
    }
    if (rc) {
        LOGERR("removal of write extents due to truncation failed");
        rc = UNIFYFS_FAILURE;
    } else {
        rc = UNIFYFS_SUCCESS;
    }
    return rc;
}

/* truncate file id to given length, frees resources if length is
 * less than size and allocates and zero-fills new bytes if length
 * is more than size */
int unifyfs_fid_truncate(unifyfs_client* client,
                         int fid,
                         off_t length)
{
    /* get meta data for this file */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    assert(meta != NULL);

    /* truncate is not valid for directories */
    if (S_ISDIR(meta->attrs.mode)) {
        return EISDIR;
    }

    if (meta->attrs.is_laminated) {
        /* Can't truncate a laminated file */
        return EINVAL;
    }

    if (meta->storage != FILE_STORAGE_LOGIO) {
        /* unknown storage type */
        return EIO;
    }

    /* remove/update writes past truncation size for this file id */
    int rc = fid_truncate_write_meta(client, meta, length);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* truncate is a sync point */
    rc = unifyfs_fid_sync_extents(client, fid);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    /* update global size in filemeta to reflect truncated size.
     * note that log size is not affected */
    meta->attrs.size = length;

    /* invoke truncate rpc */
    int gfid = unifyfs_gfid_from_fid(client, fid);
    rc = invoke_client_truncate_rpc(client, gfid, length);
    if (rc != UNIFYFS_SUCCESS) {
        return rc;
    }

    return UNIFYFS_SUCCESS;
}

/*
 * Clear all entries in the write log index.  This only clears the metadata,
 * not the data itself.
 */
static void clear_index(unifyfs_client* client)
{
    *(client->state.write_index.ptr_num_entries) = 0;
}

/* Add the metadata for a single write to the index */
static int add_write_meta_to_index(unifyfs_client* client,
                                   unifyfs_filemeta_t* meta,
                                   off_t file_pos,
                                   off_t log_pos,
                                   size_t length)
{
    /* add write extent to our segment trees */
    if (client->use_local_extents) {
        /* record write extent in our local cache */
        seg_tree_add(&meta->extents,
                     file_pos,
                     file_pos + length - 1,
                     log_pos);
    }

    /*
     * We want to make sure this write will not overflow the maximum
     * number of index entries we can sync with server. A write can at most
     * create two new nodes in the seg_tree. If we're close to potentially
     * filling up the index, sync it out.
     */
    unsigned long count_before = seg_tree_count(&meta->extents_sync);
    if (count_before >= (client->max_write_index_entries - 2)) {
        /* this will flush our segments, sync them, and set the running
         * segment count back to 0 */
        unifyfs_fid_sync_extents(client, meta->fid);
    }

    /* store the write in our segment tree used for syncing with server. */
    seg_tree_add(&meta->extents_sync,
                 file_pos,
                 file_pos + length - 1,
                 log_pos);

    return UNIFYFS_SUCCESS;
}

/*
 * Remove all entries in the current index and re-write it using the write
 * metadata stored in the target file's extents_sync segment tree. This only
 * re-writes the metadata in the index. All the actual data is still kept
 * in the write log and will be referenced correctly by the new metadata.
 *
 * After this function is done, 'state.write_index' will have been totally
 * re-written. The writes in the index will be flattened, non-overlapping,
 * and sequential. The extents_sync segment tree will be cleared.
 *
 * This function is called when we sync our extents with the server.
 *
 * Returns maximum write log offset for synced extents.
 */
static off_t rewrite_index_from_seg_tree(unifyfs_client* client,
                                         unifyfs_filemeta_t* meta)
{
    /* get pointer to index buffer */
    unifyfs_index_t* indexes = client->state.write_index.index_entries;

    /* Erase the index before we re-write it */
    clear_index(client);

    /* count up number of entries we wrote to buffer */
    unsigned long idx = 0;

    /* record maximum write log offset */
    off_t max_log_offset = 0;

    int gfid = meta->attrs.gfid;

    seg_tree_rdlock(&meta->extents_sync);
    /* For each write in this file's seg_tree ... */
    struct seg_tree_node* node = NULL;
    while ((node = seg_tree_iter(&meta->extents_sync, node))) {
        indexes[idx].file_pos = node->start;
        indexes[idx].log_pos  = node->ptr;
        indexes[idx].length   = node->end - node->start + 1;
        indexes[idx].gfid     = gfid;
        idx++;
        if ((off_t)(node->end) > max_log_offset) {
            max_log_offset = (off_t) node->end;
        }
    }
    seg_tree_unlock(&meta->extents_sync);
    /* All done processing this files writes.  Clear its seg_tree */
    seg_tree_clear(&meta->extents_sync);

    /* record total number of entries in index buffer */
    *(client->state.write_index.ptr_num_entries) = idx;

    return max_log_offset;
}

/* Sync extent data for file to storage */
int unifyfs_fid_sync_data(unifyfs_client* client,
                          int fid)
{
    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if ((NULL == meta) || (meta->fid != fid)) {
        /* bail out with an error if we fail to find it */
        LOGERR("missing filemeta for fid=%d", fid);
        return UNIFYFS_FAILURE;
    }

    /* sync file data to storage.
     * NOTE: this syncs all client data, not just the target file's */
    int rc = unifyfs_logio_sync(client->state.logio_ctx);
    if (UNIFYFS_SUCCESS != rc) {
        /* something went wrong when trying to flush extents */
        LOGERR("failed to flush data to storage for client[%d:%d]",
               client->state.app_id, client->state.client_id);
        ret = rc;
    }

    return ret;
}


/* Sync data for file to server if needed */
int unifyfs_fid_sync_extents(unifyfs_client* client,
                             int fid)
{
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    if (NULL == meta) {
        LOGDBG("no filemeta for fid=%d", fid);
        return UNIFYFS_SUCCESS;
    } else if(meta->fid != fid) {
        /* bail out with an error if we fail to find it */
        LOGERR("missing filemeta for fid=%d", fid);
        return UNIFYFS_FAILURE;
    }

    /* assume we'll succeed */
    int ret = UNIFYFS_SUCCESS;

    /* sync with server if we need to */
    if (meta->needs_sync) {
        int rc;

        /* write contents from segment tree to index buffer */
        rewrite_index_from_seg_tree(client, meta);

        /* if there are no index entries, we've got nothing to sync */
        if (*(client->state.write_index.ptr_num_entries) == 0) {
            /* consider that we've sync'd successfully */
            meta->needs_sync = 0;
            return UNIFYFS_SUCCESS;
        }

        /* tell the server to grab our new extents */
        rc = invoke_client_sync_rpc(client, meta->attrs.gfid);
        if (UNIFYFS_SUCCESS != rc) {
            /* something went wrong when trying to flush extents */
            LOGERR("failed to flush write index to server for gfid=%d",
                   meta->attrs.gfid);
            ret = rc;
        }

        /* we've sync'd, so mark this file as being up-to-date */
        meta->needs_sync = 0;

        /* flushed, clear buffer and refresh number of entries
         * and number remaining */
        clear_index(client);
    }

    return ret;
}


/* Write data to file using log-based I/O.
 * Return UNIFYFS_SUCCESS, or error code */
static int fid_logio_write(
    unifyfs_client* client,
    unifyfs_filemeta_t* meta, /* meta data for file */
    off_t pos,                /* file position to start writing at */
    const void* buf,          /* user buffer holding data */
    size_t count,             /* number of bytes to write */
    size_t* nwritten)         /* returns number of bytes written */
{
    /* assume we'll fail to write anything */
    *nwritten = 0;

    assert(meta != NULL);
    int fid = meta->fid;
    int gfid = meta->attrs.gfid;
    if (meta->storage != FILE_STORAGE_LOGIO) {
        LOGERR("file (fid=%d) storage mode != FILE_STORAGE_LOGIO", fid);
        return EINVAL;
    }

    /* allocate space in the log for this write */
    off_t log_off;
    int rc = unifyfs_logio_alloc(client->state.logio_ctx, count, &log_off);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("logio_alloc(%zu) failed", count);
        return rc;
    }

    /* do the write */
    rc = unifyfs_logio_write(client->state.logio_ctx, log_off, count,
                             buf, nwritten);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("fid=%d gfid=%d logio_write(off=%zu, cnt=%zu) failed",
               fid, gfid, log_off, count);
        return rc;
    }

    if (*nwritten < count) {
        LOGWARN("partial logio_write() @ offset=%zu (%zu of %zu bytes)",
                (size_t)log_off, *nwritten, count);
    } else {
        LOGDBG("fid=%d gfid=%d pos=%zu - successful logio_write() "
               "@ log offset=%zu (%zu bytes)",
               fid, gfid, (size_t)pos, (size_t)log_off, count);
    }

    /* update our write metadata for this write */
    rc = add_write_meta_to_index(client, meta, pos, log_off, *nwritten);
    return rc;
}

/* Write count bytes from buf into file starting at offset pos.
 *
 * Returns UNIFYFS_SUCCESS, or an error code
 */
int unifyfs_fid_write(
    unifyfs_client* client,
    int fid,          /* local file id to write to */
    off_t pos,        /* starting position in file */
    const void* buf,  /* buffer to be written */
    size_t count,     /* number of bytes to write */
    size_t* nwritten) /* returns number of bytes written */
{
    int rc;

    /* assume we won't write anything */
    *nwritten = 0;

    /* short-circuit a 0-byte write */
    if (count == 0) {
        return UNIFYFS_SUCCESS;
    }

    /* get meta for this file id */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(client, fid);
    assert(meta != NULL);

    if (meta->attrs.is_laminated) {
        /* attempt to write to laminated file, return read-only filesystem */
        return EROFS;
    }

    /* determine storage type to write file data */
    if (meta->storage == FILE_STORAGE_LOGIO) {
        /* file stored in logged i/o */
        rc = fid_logio_write(client, meta, pos, buf, count, nwritten);
        if (rc == UNIFYFS_SUCCESS) {
            /* write succeeded, remember that we have new data
             * that needs to be synced with the server */
            meta->needs_sync = 1;

            /* optionally sync after every write */
            if (client->use_write_sync) {
                int ret = unifyfs_fid_sync_extents(client, fid);
                if (ret != UNIFYFS_SUCCESS) {
                    LOGERR("client sync after write failed");
                    rc = ret;
                }
            }
        }
    } else {
        /* unknown storage type */
        LOGERR("unknown storage type for fid=%d", fid);
        rc = EIO;
    }

    return rc;
}

