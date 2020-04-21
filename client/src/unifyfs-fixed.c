/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
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

/* Merges write at next_idx into prev_idx if possible.
 * Updates both prev_idx and next_idx leaving any
 * portion that could not be merged in next_idx */
static int unifyfs_coalesce_index(
    unifyfs_index_t* prev_idx, /* existing index entry to coalesce into */
    unifyfs_index_t* next_idx, /* new index entry we'd like to add */
    long slice_size)           /* byte size of slice of key-value store */
{
    /* check whether last index and next index refer to same file */
    if (prev_idx->gfid != next_idx->gfid) {
        /* two index values are for different files, can't combine */
        return UNIFYFS_SUCCESS;
    }

    /* got same file,
     * check whether last index and next index refer to
     * contiguous bytes */
    off_t prev_offset = prev_idx->file_pos + prev_idx->length;
    off_t next_offset = next_idx->file_pos;
    if (prev_offset != next_offset) {
        /* index values are not contiguous, can't combine */
        return UNIFYFS_SUCCESS;
    }

    /* got contiguous bytes in the same file,
     * check whether both index values fall in the same slice */
    off_t prev_slice = prev_idx->file_pos / slice_size;
    off_t next_slice = next_idx->file_pos / slice_size;
    if (prev_slice != next_slice) {
        /* index values refer to different slices, can't combine */
        return UNIFYFS_SUCCESS;
    }

    /* check whether last index and next index refer to
     * contiguous bytes in the log */
    off_t prev_log = prev_idx->log_pos + prev_idx->length;
    off_t next_log = next_idx->log_pos;
    if (prev_log != next_log) {
        /* index values are not contiguous in log, can't combine */
        return UNIFYFS_SUCCESS;
    }

    /* if we get here, we can coalesce next index value into previous */

    /* get ending offset of write in next index,
     * and ending offset of current slice */
    off_t next_end  = next_idx->file_pos + next_idx->length;
    off_t slice_end = (next_slice * slice_size) + slice_size;

    /* determine number of bytes in next index that lie in current slice,
     * assume all bytes in the index will fit */
    long length = next_idx->length;
    if (next_end > slice_end) {
        /* current index writes beyond end of slice,
         * so we can only coalesce bytes that fall within slice */
        length = slice_end - next_offset;
    }

    /* extend length of last index to include beginning portion
     * of current index */
    prev_idx->length += length;

    /* adjust current index to subtract off those bytes */
    next_idx->file_pos += length;
    next_idx->log_pos  += length;
    next_idx->length   -= length;

    return UNIFYFS_SUCCESS;
}

/*
 * Clear all entries in the log index.  This only clears the metadata,
 * not the data itself.
 */
static void clear_index(void)
{
    *unifyfs_indices.ptr_num_entries = 0;
}

static void add_index_entry_to_seg_tree(unifyfs_filemeta_t* meta,
                                        unifyfs_index_t* index)
{
    /* add index to our local log */
    if (unifyfs_local_extents) {
        seg_tree_add(&meta->extents, index->file_pos,
            index->file_pos + index->length - 1,
            index->log_pos);
    }

    if (!unifyfs_flatten_writes) {
        /* We're not flattening writes.  Nothing to do */
        return;
    }

    /* to update the global running segment count, we need to capture
     * the count in this tree before adding and the count after to
     * add the difference */
    unsigned long count_before = seg_tree_count(&meta->extents_sync);

    /*
     * Store the write in our segment tree.  We will later use this for
     * flattening writes.
     */
    seg_tree_add(&meta->extents_sync, index->file_pos,
        index->file_pos + index->length - 1,
        index->log_pos);

    /*
     * We want to make sure the next write following this wont overflow the
     * max number of index entries (if it were synced).  A write can at most
     * create two new nodes in the seg_tree.  If we're close to potentially
     * filling up the index, sync it out.
     */
    if (unifyfs_segment_count >= (unifyfs_max_index_entries - 2)) {
        /* this will flush our segments, sync them, and set the running
         * segment count back to 0 */
        unifyfs_sync(meta->gfid);
    } else {
        /* increase the running global segment count by the number of
         * new entries we added to this tree */
        unsigned long count_after = seg_tree_count(&meta->extents_sync);
        unifyfs_segment_count += (count_after - count_before);
    }
}

/* Add the metadata for a single write to the index */
static int add_write_meta_to_index(unifyfs_filemeta_t* meta,
                                   off_t file_pos,
                                   off_t log_pos,
                                   size_t length)
{
    /* global file id for this entry */
    int gfid = meta->gfid;

    /* define an new index entry for this write operation */
    unifyfs_index_t cur_idx;
    cur_idx.gfid     = gfid;
    cur_idx.file_pos = file_pos;
    cur_idx.log_pos  = log_pos;
    cur_idx.length   = length;

    /* lookup number of existing index entries */
    off_t num_entries = *(unifyfs_indices.ptr_num_entries);

    /* get pointer to index array */
    unifyfs_index_t* idxs = unifyfs_indices.index_entry;

    /* attempt to coalesce contiguous index entries if we
     * have an existing index in the buffer */
    if (num_entries > 0) {
        /* get pointer to last element in index array */
        unifyfs_index_t* prev_idx = &idxs[num_entries - 1];

        /* attempt to coalesce current index with last index,
         * updates fields in last index and current index
         * accordingly */
        unifyfs_coalesce_index(prev_idx, &cur_idx,
            unifyfs_key_slice_range);
        if (cur_idx.length == 0) {
            /* We were able to coalesce this write into prev_idx */
            add_index_entry_to_seg_tree(meta, prev_idx);
        }
    }

    /* add new index entry if needed */
    if (cur_idx.length > 0) {
        /* remaining entries we can fit in the shared memory region */
        off_t remaining_entries = unifyfs_max_index_entries - num_entries;

        /* if we have filled the key/value buffer, flush it to server */
        if (0 == remaining_entries) {
            /* index buffer is full, flush it */
            int ret = unifyfs_sync(cur_idx.gfid);
            if (ret != UNIFYFS_SUCCESS) {
                /* something went wrong when trying to flush key/values */
                LOGERR("failed to flush key/value index to server");
                return EIO;
            }
        }

        /* copy entry into index buffer */
        idxs[num_entries] = cur_idx;

        /* Add index entry to our seg_tree */
        add_index_entry_to_seg_tree(meta, &idxs[num_entries]);

        /* account for entries we just added */
        num_entries += 1;

        /* update number of entries in index array */
        (*unifyfs_indices.ptr_num_entries) = num_entries;
    }

    return UNIFYFS_SUCCESS;
}

/*
 * Remove all entries in the current index and re-write it using the write
 * metadata stored in all the file's seg_trees.  This only re-writes the
 * metadata in the index.  All the actual data is still kept in the log and
 * will be referenced correctly by the new metadata.
 *
 * After this function is done 'unifyfs_indices' will have been totally
 * re-written.  The writes in the index will be flattened, non-overlapping,
 * and sequential, for each file, one after another.  All seg_trees will be
 * cleared.
 *
 * This function is called when we sync our extents.
 */
void unifyfs_rewrite_index_from_seg_tree(void)
{
    /* get pointer to index buffer */
    unifyfs_index_t* indexes = unifyfs_indices.index_entry;

    /* Erase the index before we re-write it */
    clear_index();

    /* count up number of entries we wrote to buffer */
    unsigned long idx = 0;

    /* For each fid .. */
    for (int i = 0; i < UNIFYFS_MAX_FILEDESCS; i++) {
        /* get file id for each file descriptor */
        int fid = unifyfs_fds[i].fid;
        unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);
        if (!meta) {
            continue;
        }

        int gfid = unifyfs_gfid_from_fid(fid);

        seg_tree_rdlock(&meta->extents_sync);

        /* For each write in this file's seg_tree ... */
        struct seg_tree_node* node = NULL;
        while ((node = seg_tree_iter(&meta->extents_sync, node))) {
            indexes[idx].file_pos = node->start;
            indexes[idx].log_pos  = node->ptr;
            indexes[idx].length   = node->end - node->start + 1;
            indexes[idx].gfid     = gfid;
            idx++;
        }

        seg_tree_unlock(&meta->extents_sync);

        /* All done processing this files writes.  Clear its seg_tree */
        seg_tree_clear(&meta->extents_sync);
    }

    /* reset our segment count since we just dumped them all */
    unifyfs_segment_count = 0;

    /* record total number of entries in index buffer */
    *unifyfs_indices.ptr_num_entries = idx;
}

/*
 * Sync all the extents to the server.  Clears the metadata index afterwards.
 *
 * Returns 0 on success, nonzero otherwise.
 */
int unifyfs_sync(int gfid)
{
    /* NOTE: we currently ignore gfid and sync extents for all files in
     * the index. If we ever switch to storing extents in a per-file index,
     * we can support per-file sync. */

    /* write contents from segment tree to index buffer
     * if we're using that optimization */
    if (unifyfs_flatten_writes) {
        unifyfs_rewrite_index_from_seg_tree();
    }

    /* if there are no index entries, we've got nothing to sync */
    if (*unifyfs_indices.ptr_num_entries == 0) {
        return UNIFYFS_SUCCESS;
    }

    /* ensure any data written to the spill over file is flushed */
    int ret = unifyfs_logio_sync(logio_ctx);
    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to sync logio data");
        return EIO;
    }

    /* tell the server to grab our new extents */
    ret = invoke_client_sync_rpc();
    if (ret != UNIFYFS_SUCCESS) {
        /* something went wrong when trying to flush key/values */
        LOGERR("failed to flush key/value index to server");
        return EIO;
    }

    /* flushed, clear buffer and refresh number of entries
     * and number remaining */
    clear_index();

    return UNIFYFS_SUCCESS;
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
        LOGDBG("successful logio_write() @ log offset=%zu (%zu bytes)",
               (size_t)log_off, count);
    }

    /* update our write metadata for this write */
    rc = add_write_meta_to_index(meta, pos, log_off, *nwritten);
    return rc;
}
