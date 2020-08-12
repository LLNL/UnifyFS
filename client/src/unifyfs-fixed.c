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

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file LICENSE.CRUISE
 */

#include "unifyfs-internal.h"
#include "unifyfs-fixed.h"
#include "unifyfs_log.h"
#include "margo_client.h"
#include "seg_tree.h"

/* ---------------------------------------
 * Operations on client write index
 * --------------------------------------- */

/*
 * Clear all entries in the log index.  This only clears the metadata,
 * not the data itself.
 */
static void clear_index(void)
{
    *unifyfs_indices.ptr_num_entries = 0;
}

/* Add the metadata for a single write to the index */
static int add_write_meta_to_index(unifyfs_filemeta_t* meta,
                                   off_t file_pos,
                                   off_t log_pos,
                                   size_t length)
{
    /* add write extent to our segment trees */
    if (unifyfs_local_extents) {
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
    if (count_before >= (unifyfs_max_index_entries - 2)) {
        /* this will flush our segments, sync them, and set the running
         * segment count back to 0 */
        unifyfs_sync(meta->fid);
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
 * After this function is done, 'unifyfs_indices' will have been totally
 * re-written. The writes in the index will be flattened, non-overlapping,
 * and sequential. The extents_sync segment tree will be cleared.
 *
 * This function is called when we sync our extents with the server.
 *
 * Returns maximum write log offset for synced extents.
 */
off_t unifyfs_rewrite_index_from_seg_tree(unifyfs_filemeta_t* meta)
{
    /* get pointer to index buffer */
    unifyfs_index_t* indexes = unifyfs_indices.index_entry;

    /* Erase the index before we re-write it */
    clear_index();

    /* count up number of entries we wrote to buffer */
    unsigned long idx = 0;

    /* record maximum write log offset */
    off_t max_log_offset = 0;

    int gfid = meta->gfid;

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
    *unifyfs_indices.ptr_num_entries = idx;

    return max_log_offset;
}

/*
 * Find any write extents that span or exceed truncation point and remove them.
 *
 * This function is called when we truncate a file and there are cached writes.
 */
int truncate_write_meta(unifyfs_filemeta_t* meta, off_t trunc_sz)
{
    if (0 == trunc_sz) {
        /* All writes should be removed. Clear extents_sync */
        seg_tree_clear(&meta->extents_sync);

        if (unifyfs_local_extents) {
            /* Clear the local extent cache too */
            seg_tree_clear(&meta->extents);
        }
        return UNIFYFS_SUCCESS;
    }

    unsigned long trunc_off = (unsigned long) trunc_sz;
    int rc = seg_tree_remove(&meta->extents_sync, trunc_off, ULONG_MAX);
    if (unifyfs_local_extents) {
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


/*
 * Sync all the write extents for the target file(s) to the server.
 * The target_fid identifies a specific file, or all files (-1).
 * Clears the metadata index afterwards.
 *
 * Returns 0 on success, nonzero otherwise.
 */
int unifyfs_sync(int target_fid)
{
    int ret = UNIFYFS_SUCCESS;
    off_t max_log_offset = 0;

    /* For each open file descriptor .. */
    for (int i = 0; i < UNIFYFS_MAX_FILEDESCS; i++) {
        /* get file id for each file descriptor */
        int fid = unifyfs_fds[i].fid;
        if (-1 == fid) {
            /* file descriptor is not currently in use */
            continue;
        }

        /* is this the target file? */
        if ((target_fid != -1) && (fid != target_fid)) {
            continue;
        }

        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
        if ((NULL == meta) || (meta->fid != fid)) {
            LOGERR("missing filemeta for fid=%d", fid);
            if (fid == target_fid) {
                return UNIFYFS_FAILURE;
            }
            continue;
        }

        if (meta->needs_sync) {
            /* write contents from segment tree to index buffer */
            off_t max_log_off = unifyfs_rewrite_index_from_seg_tree(meta);
            if (max_log_off > max_log_offset) {
                max_log_offset = max_log_off;
            }

            /* if there are no index entries, we've got nothing to sync */
            if (*unifyfs_indices.ptr_num_entries == 0) {
                if (fid == target_fid) {
                    return UNIFYFS_SUCCESS;
                }
            }

            /* tell the server to grab our new extents */
            ret = invoke_client_sync_rpc();
            if (ret != UNIFYFS_SUCCESS) {
                /* something went wrong when trying to flush extents */
                LOGERR("failed to flush write index to server for gfid=%d",
                       meta->gfid);
            }
            meta->needs_sync = 0;

            /* flushed, clear buffer and refresh number of entries
             * and number remaining */
            clear_index();
        }

        /* break out of loop when targeting a specific file */
        if (fid == target_fid) {
            break;
        }
    }

    /* ensure any data written to the spillover file is flushed */
    off_t logio_shmem_size;
    unifyfs_logio_get_sizes(logio_ctx, &logio_shmem_size, NULL);
    if (max_log_offset >= logio_shmem_size) {
        ret = unifyfs_logio_sync(logio_ctx);
        if (ret != UNIFYFS_SUCCESS) {
            LOGERR("failed to sync logio data");
        }
    }

    return ret;
}

/* ---------------------------------------
 * Operations on file storage
 * --------------------------------------- */

/**
 * Write data to file using log-based I/O
 *
 * @param fid       file id to write to
 * @param meta      metadata for file
 * @param pos       file position to start writing at
 * @param buf       user buffer holding data
 * @param count     number of bytes to write
 * @param nwritten  number of bytes written
 * @return UNIFYFS_SUCCESS, or error code
 */
int unifyfs_fid_logio_write(int fid,
                            unifyfs_filemeta_t* meta,
                            off_t pos,
                            const void* buf,
                            size_t count,
                            size_t* nwritten)
{
    /* assume we'll fail to write anything */
    *nwritten = 0;

    assert(meta != NULL);
    if (meta->storage != FILE_STORAGE_LOGIO) {
        LOGERR("file (fid=%d) storage mode != FILE_STORAGE_LOGIO", fid);
        return EINVAL;
    }

    /* allocate space in the log for this write */
    off_t log_off;
    int rc = unifyfs_logio_alloc(logio_ctx, count, &log_off);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("logio_alloc(%zu) failed", count);
        return rc;
    }

    /* do the write */
    rc = unifyfs_logio_write(logio_ctx, log_off, count, buf, nwritten);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("logio_write(%zu, %zu) failed", log_off, count);
        return rc;
    }

    if (*nwritten < count) {
        LOGWARN("partial logio_write() @ offset=%zu (%zu of %zu bytes)",
                (size_t)log_off, *nwritten, count);
    } else {
        LOGDBG("fid=%d pos=%zu - successful logio_write() "
               "@ log offset=%zu (%zu bytes)",
               fid, (size_t)pos, (size_t)log_off, count);
    }

    /* update our write metadata for this write */
    rc = add_write_meta_to_index(meta, pos, log_off, *nwritten);
    return rc;
}
