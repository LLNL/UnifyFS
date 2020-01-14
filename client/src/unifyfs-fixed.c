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

static inline
unifyfs_chunkmeta_t* filemeta_get_chunkmeta(const unifyfs_filemeta_t* meta,
                                            int cid)
{
    unifyfs_chunkmeta_t* chunkmeta = NULL;
    uint64_t limit = 0;

    if (unifyfs_use_memfs) {
        limit += unifyfs_max_chunks;
    }

    if (unifyfs_use_spillover) {
        limit += unifyfs_spillover_max_chunks;
    }

    if (meta && (cid >= 0 && cid < limit)) {
        chunkmeta = &unifyfs_chunkmetas[meta->chunkmeta_idx + cid];
    }

    return chunkmeta;
}

/* given a file id and logical chunk id, return pointer to meta data
 * for specified chunk, return NULL if not found */
static inline unifyfs_chunkmeta_t* unifyfs_get_chunkmeta(int fid, int cid)
{
    /* lookup file meta data for specified file id */
    unifyfs_filemeta_t* meta = unifyfs_get_meta_from_fid(fid);

    return filemeta_get_chunkmeta(meta, cid);
}

/* ---------------------------------------
 * Operations on file chunks
 * --------------------------------------- */

/* given a logical chunk id and an offset within that chunk, return the pointer
 * to the memory location corresponding to that location */
static inline void* unifyfs_compute_chunk_buf(const unifyfs_filemeta_t* meta,
                                              int cid, off_t offset)
{
    /* get pointer to chunk meta */
    const unifyfs_chunkmeta_t* chunk_meta = filemeta_get_chunkmeta(meta, cid);

    /* identify physical chunk id */
    int physical_id = chunk_meta->id;

    /* compute the start of the chunk */
    char* start = NULL;
    if (physical_id < unifyfs_max_chunks) {
        start = unifyfs_chunks + ((long)physical_id << unifyfs_chunk_bits);
    } else {
        /* chunk is in spill over */
        LOGERR("wrong chunk ID");
        return NULL;
    }

    /* now add offset */
    char* buf = start + offset;
    return (void*)buf;
}

/* given a chunk id and an offset within that chunk, return the offset
 * in the spillover file corresponding to that location */
static inline off_t unifyfs_compute_spill_offset(const unifyfs_filemeta_t* meta,
                                                 int cid, off_t offset)
{
    /* get pointer to chunk meta */
    const unifyfs_chunkmeta_t* chunk_meta = filemeta_get_chunkmeta(meta, cid);

    /* identify physical chunk id */
    int physical_id = chunk_meta->id;

    /* compute start of chunk in spill over device */
    off_t start = 0;
    if (physical_id < unifyfs_max_chunks) {
        LOGERR("wrong spill-chunk ID");
        return -1;
    } else {
        /* compute buffer loc within spillover device chunk */
        /* account for the unifyfs_max_chunks added to identify location when
         * grabbing this chunk */
        start = ((long)(physical_id - unifyfs_max_chunks) << unifyfs_chunk_bits);
    }

    off_t buf = start + offset;
    return buf;
}

/* allocate a new chunk for the specified file and logical chunk id */
static int unifyfs_chunk_alloc(int fid, unifyfs_filemeta_t* meta, int chunk_id)
{
    /* get pointer to chunk meta data */
    unifyfs_chunkmeta_t* chunk_meta = filemeta_get_chunkmeta(meta, chunk_id);

    /* allocate a chunk and record its location */
    if (unifyfs_use_memfs) {
        /* allocate a new chunk from memory */
        unifyfs_stack_lock();
        int id = unifyfs_stack_pop(free_chunk_stack);
        unifyfs_stack_unlock();

        /* if we got one return, otherwise try spill over */
        if (id >= 0) {
            /* got a chunk from memory */
            chunk_meta->location = CHUNK_LOCATION_MEMFS;
            chunk_meta->id = id;
        } else if (unifyfs_use_spillover) {
            /* shm segment out of space, grab a block from spill-over device */
            LOGDBG("getting blocks from spill-over device");

            /* TODO: missing lock calls? */
            /* add unifyfs_max_chunks to identify chunk location */
            unifyfs_stack_lock();
            id = unifyfs_stack_pop(free_spillchunk_stack) + unifyfs_max_chunks;
            unifyfs_stack_unlock();
            if (id < unifyfs_max_chunks) {
                LOGERR("spill-over device out of space (%d)", id);
                return UNIFYFS_ERROR_NOSPC;
            }

            /* got one from spill over */
            chunk_meta->location = CHUNK_LOCATION_SPILLOVER;
            chunk_meta->id = id;
        } else {
            /* spill over isn't available, so we're out of space */
            LOGERR("memfs out of space (%d)", id);
            return UNIFYFS_ERROR_NOSPC;
        }
    } else if (unifyfs_use_spillover) {
        /* memory file system is not enabled, but spill over is */

        /* shm segment out of space, grab a block from spill-over device */
        LOGDBG("getting blocks from spill-over device");

        /* TODO: missing lock calls? */
        /* add unifyfs_max_chunks to identify chunk location */
        unifyfs_stack_lock();
        int id = unifyfs_stack_pop(free_spillchunk_stack) + unifyfs_max_chunks;
        unifyfs_stack_unlock();
        if (id < unifyfs_max_chunks) {
            LOGERR("spill-over device out of space (%d)", id);
            return UNIFYFS_ERROR_NOSPC;
        }

        /* got one from spill over */
        chunk_meta->location = CHUNK_LOCATION_SPILLOVER;
        chunk_meta->id = id;
    } else {
        /* don't know how to allocate chunk */
        chunk_meta->location = CHUNK_LOCATION_NULL;
        return UNIFYFS_ERROR_IO;
    }

    return UNIFYFS_SUCCESS;
}

static int unifyfs_chunk_free(int fid, unifyfs_filemeta_t* meta, int chunk_id)
{
    /* get pointer to chunk meta data */
    unifyfs_chunkmeta_t* chunk_meta = filemeta_get_chunkmeta(meta, chunk_id);

    /* get physical id of chunk */
    int id = chunk_meta->id;
    LOGDBG("free chunk %d from location %d", id, chunk_meta->location);

    /* determine location of chunk */
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        unifyfs_stack_lock();
        unifyfs_stack_push(free_chunk_stack, id);
        unifyfs_stack_unlock();
    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* TODO: free spill over chunk */
    } else {
        /* unkwown chunk location */
        LOGERR("unknown chunk location %d", chunk_meta->location);
        return UNIFYFS_ERROR_IO;
    }

    /* update location of chunk */
    chunk_meta->location = CHUNK_LOCATION_NULL;

    return UNIFYFS_SUCCESS;
}

/* read data from specified chunk id, chunk offset, and count into user buffer,
 * count should fit within chunk starting from specified offset */
static int unifyfs_chunk_read(
    unifyfs_filemeta_t* meta, /* pointer to file meta data */
    int chunk_id,            /* logical chunk id to read data from */
    off_t chunk_offset,      /* logical offset within chunk to read from */
    void* buf,               /* buffer to store data to */
    size_t count)            /* number of bytes to read */
{
    /* get chunk meta data */
    unifyfs_chunkmeta_t* chunk_meta = filemeta_get_chunkmeta(meta, chunk_id);

    /* determine location of chunk */
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        /* just need a memcpy to read data */
        void* chunk_buf = unifyfs_compute_chunk_buf(
            meta, chunk_id, chunk_offset);
        memcpy(buf, chunk_buf, count);
    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* spill over to a file, so read from file descriptor */
        //MAP_OR_FAIL(pread);
        off_t spill_offset = unifyfs_compute_spill_offset(meta, chunk_id, chunk_offset);
        ssize_t rc = pread(unifyfs_spilloverblock, buf, count, spill_offset);
        if (rc < 0) {
            return unifyfs_errno_map_to_err(rc);
        }
    } else {
        /* unknown chunk type */
        LOGERR("unknown chunk type");
        return UNIFYFS_ERROR_IO;
    }

    /* assume read was successful if we get to here */
    return UNIFYFS_SUCCESS;
}

/* merges next_idx into prev_idx if possible,
 * updates both prev_idx and next_idx leaving any
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
static void unifyfs_clear_index(void)
{
    *unifyfs_indices.ptr_num_entries = 0;
}

/*
 * Sync all the extents to the server.  Clears the metadata index afterwards.
 *
 * Returns 0 on success, nonzero otherwise.
 */
int unifyfs_sync(int gfid)
{
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
    /* if using spill over, fsync spillover data to disk */
    if (unifyfs_use_spillover) {
        int ret = __real_fsync(unifyfs_spilloverblock);
        if (ret != 0) {
            /* error, need to set errno appropriately,
             * we called the real fsync which should
             * have already set errno to something reasonable */
            LOGERR("failed to flush data to spill over file");
            return UNIFYFS_ERROR_IO;
        }
    }

    /* tell the server to grab our new extents */
    int ret = invoke_client_fsync_rpc(gfid);
    if (ret != UNIFYFS_SUCCESS) {
        /* something went wrong when trying to flush key/values */
        LOGERR("failed to flush key/value index to server");
        return UNIFYFS_ERROR_IO;
    }

    /* flushed, clear buffer and refresh number of entries
     * and number remaining */
    unifyfs_clear_index();

    return UNIFYFS_SUCCESS;
}

void unifyfs_add_index_entry_to_seg_tree(
    unifyfs_filemeta_t* meta,
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
static int unifyfs_logio_add_write_meta_to_index(unifyfs_filemeta_t* meta,
    off_t file_pos, off_t log_pos, size_t length)
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
            unifyfs_add_index_entry_to_seg_tree(meta, prev_idx);
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
                return UNIFYFS_ERROR_IO;
            }
        }

        /* copy entry into index buffer */
        idxs[num_entries] = cur_idx;

        /* Add index entry to our seg_tree */
        unifyfs_add_index_entry_to_seg_tree(meta, &idxs[num_entries]);

        /* account for entries we just added */
        num_entries += 1;

        /* update number of entries in index array */
        (*unifyfs_indices.ptr_num_entries) = num_entries;
    }

    return UNIFYFS_SUCCESS;
}

/* write count bytes from user buffer into specified chunk id at chunk offset,
 * count should fit within chunk starting from specified offset */
static int unifyfs_logio_chunk_write(
    int fid,                  /* local file id */
    long pos,                 /* write offset inside the file */
    unifyfs_filemeta_t* meta, /* pointer to file meta data */
    int chunk_id,             /* logical chunk id to write to */
    off_t chunk_offset,       /* logical offset within chunk to write to */
    const void* buf,          /* buffer holding data to be written */
    size_t count)             /* number of bytes to write */
{
    /* get chunk meta data */
    unifyfs_chunkmeta_t* chunk_meta = filemeta_get_chunkmeta(meta, chunk_id);

    /* sanity check on the chunk type */
    if (chunk_meta->location != CHUNK_LOCATION_MEMFS &&
        chunk_meta->location != CHUNK_LOCATION_SPILLOVER) {
        /* unknown chunk type */
        LOGERR("unknown chunk type");
        return UNIFYFS_ERROR_IO;
    }

    /* copy data into chunk and record its starting offset within the log */
    off_t log_offset = 0;
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        /* store data in shared memory chunk,
         * compute address to copy data to */
        char* chunk_buf = unifyfs_compute_chunk_buf(
            meta, chunk_id, chunk_offset);

        /* just need a memcpy to record data */
        memcpy(chunk_buf, buf, count);

        /* record byte offset position within log, which is the number
         * of bytes between the memory location we write to and the
         * starting memory location of the shared memory data chunk region */
        log_offset = chunk_buf - unifyfs_chunks;
    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* spill over to a file, so write to file descriptor,
         * compute offset within spill over file */
        off_t spill_offset = unifyfs_compute_spill_offset(meta, chunk_id, chunk_offset);

        /* write data into file at appropriate offset,
         * loop to keep trying short writes */
        //MAP_OR_FAIL(pwrite);
        size_t nwritten = 0;
        while (nwritten < count) {
            /* attempt to write */
            void* bufpos = (void*)((char*)buf + nwritten);
            size_t remaining = count - nwritten;
            off_t filepos = spill_offset + (off_t)nwritten;
            errno = 0;
            ssize_t rc = __real_pwrite(unifyfs_spilloverblock,
                bufpos, remaining, filepos);
            if (rc < 0)  {
                LOGERR("pwrite failed: errno=%d (%s)", errno, strerror(errno));
                return UNIFYFS_ERROR_IO;
            }

            /* wrote without an error, total up bytes written so far */
            nwritten += (size_t)rc;
        }

        /* record byte offset position within log, for spill over
         * locations we take the offset within the spill file
         * added to the total size of all shared memory data chunks */
        log_offset = spill_offset + unifyfs_max_chunks * (1 << unifyfs_chunk_bits);
    }

    /* get global file id for this file */
    int gfid = unifyfs_gfid_from_fid(fid);

    /* Update our write metadata with the new write */
    int rc = unifyfs_logio_add_write_meta_to_index(meta, pos, log_offset,
        count);
    return rc;
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
    unifyfs_clear_index();

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

/* write count bytes from user buffer into specified chunk id at chunk offset,
 * count should fit within chunk starting from specified offset */
static int unifyfs_chunk_write(
    unifyfs_filemeta_t* meta, /* pointer to file meta data */
    int chunk_id,            /* logical chunk id to write to */
    off_t chunk_offset,      /* logical offset within chunk to write to */
    const void* buf,         /* buffer holding data to be written */
    size_t count)            /* number of bytes to write */
{
    /* get chunk meta data */
    unifyfs_chunkmeta_t* chunk_meta = filemeta_get_chunkmeta(meta, chunk_id);

    /* determine location of chunk */
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        /* just need a memcpy to write data */
        void* chunk_buf = unifyfs_compute_chunk_buf(
            meta, chunk_id, chunk_offset);
        memcpy(chunk_buf, buf, count);
//        _intel_fast_memcpy(chunk_buf, buf, count);
//        unifyfs_memcpy(chunk_buf, buf, count);
    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* spill over to a file, so write to file descriptor */
        //MAP_OR_FAIL(pwrite);
        off_t spill_offset = unifyfs_compute_spill_offset(meta, chunk_id, chunk_offset);
        ssize_t rc = pwrite(unifyfs_spilloverblock, buf, count, spill_offset);
        if (rc < 0)  {
            LOGERR("pwrite failed: errno=%d (%s)", errno, strerror(errno));
        }

        /* TODO: check return code for errors */
    } else {
        /* unknown chunk type */
        LOGERR("unknown chunk type");
        return UNIFYFS_ERROR_IO;
    }

    /* assume write was successful if we get to here */
    return UNIFYFS_SUCCESS;
}

/* ---------------------------------------
 * Operations on file storage
 * --------------------------------------- */

/* if length is greater than reserved space, reserve space up to length */
int unifyfs_fid_store_fixed_extend(int fid, unifyfs_filemeta_t* meta,
                                   off_t length)
{
    /* determine whether we need to allocate more chunks */
    off_t maxsize = meta->chunks << unifyfs_chunk_bits;
    if (length > maxsize) {
        /* compute number of additional bytes we need */
        off_t additional = length - maxsize;
        while (additional > 0) {
            /* allocate a new chunk */
            int rc = unifyfs_chunk_alloc(fid, meta, meta->chunks);
            if (rc != UNIFYFS_SUCCESS) {
                /* ran out of space to store data */
                LOGERR("failed to allocate chunk");
                return UNIFYFS_ERROR_NOSPC;
            }

            /* increase chunk count and subtract bytes from the number we need */
            meta->chunks++;
            additional -= unifyfs_chunk_size;
        }
    }

    return UNIFYFS_SUCCESS;
}

/* if length is shorter than reserved space, give back space down to length */
int unifyfs_fid_store_fixed_shrink(int fid, unifyfs_filemeta_t* meta,
                                   off_t length)
{
    /* determine the number of chunks to leave after truncating */
    off_t num_chunks = 0;
    if (length > 0) {
        num_chunks = (length >> unifyfs_chunk_bits) + 1;
    }

    /* clear off any extra chunks */
    while (meta->chunks > num_chunks) {
        meta->chunks--;
        unifyfs_chunk_free(fid, meta, meta->chunks);
    }

    return UNIFYFS_SUCCESS;
}

/* read data from file stored as fixed-size chunks */
int unifyfs_fid_store_fixed_read(int fid, unifyfs_filemeta_t* meta, off_t pos,
                                 void* buf, size_t count)
{
    int rc;

    /* get pointer to position within first chunk */
    int chunk_id = pos >> unifyfs_chunk_bits;
    off_t chunk_offset = pos & unifyfs_chunk_mask;

    /* determine how many bytes remain in the current chunk */
    size_t remaining = unifyfs_chunk_size - chunk_offset;
    if (count <= remaining) {
        /* all bytes for this read fit within the current chunk */
        rc = unifyfs_chunk_read(meta, chunk_id, chunk_offset, buf, count);
    } else {
        /* read what's left of current chunk */
        char* ptr = (char*) buf;
        rc = unifyfs_chunk_read(meta, chunk_id,
            chunk_offset, (void*)ptr, remaining);
        ptr += remaining;

        /* read from the next chunk */
        size_t processed = remaining;
        while (processed < count && rc == UNIFYFS_SUCCESS) {
            /* get pointer to start of next chunk */
            chunk_id++;

            /* compute size to read from this chunk */
            size_t num = count - processed;
            if (num > unifyfs_chunk_size) {
                num = unifyfs_chunk_size;
            }

            /* read data */
            rc = unifyfs_chunk_read(meta, chunk_id, 0, (void*)ptr, num);
            ptr += num;

            /* update number of bytes written */
            processed += num;
        }
    }

    return rc;
}

/* write data to file stored as fixed-size chunks */
int unifyfs_fid_store_fixed_write(int fid, unifyfs_filemeta_t* meta, off_t pos,
                                  const void* buf, size_t count)
{
    int rc;

    /* get pointer to position within first chunk */
    int chunk_id;
    off_t chunk_offset;

    if (meta->storage == FILE_STORAGE_LOGIO) {
        chunk_id = meta->log_size >> unifyfs_chunk_bits;
        chunk_offset = meta->log_size & unifyfs_chunk_mask;
    } else {
        return UNIFYFS_ERROR_IO;
    }

    /* determine how many bytes remain in the current chunk */
    size_t remaining = unifyfs_chunk_size - chunk_offset;
    if (count <= remaining) {
        /* all bytes for this write fit within the current chunk */
        if (meta->storage == FILE_STORAGE_LOGIO) {
            rc = unifyfs_logio_chunk_write(fid, pos, meta, chunk_id, chunk_offset,
                                           buf, count);
        } else {
            return UNIFYFS_ERROR_IO;
        }
    } else {
        /* otherwise, fill up the remainder of the current chunk */
        char* ptr = (char*) buf;
        if (meta->storage == FILE_STORAGE_LOGIO) {
            rc = unifyfs_logio_chunk_write(fid, pos, meta, chunk_id,
                chunk_offset, (void*)ptr, remaining);
        } else {
            return UNIFYFS_ERROR_IO;
        }

        ptr += remaining;
        pos += remaining;

        /* then write the rest of the bytes starting from beginning
         * of chunks */
        size_t processed = remaining;
        while (processed < count && rc == UNIFYFS_SUCCESS) {
            /* get pointer to start of next chunk */
            chunk_id++;

            /* compute size to write to this chunk */
            size_t num = count - processed;
            if (num > unifyfs_chunk_size) {
                num = unifyfs_chunk_size;
            }

            /* write data */
            if (meta->storage == FILE_STORAGE_LOGIO) {
                rc = unifyfs_logio_chunk_write(fid, pos, meta, chunk_id, 0,
                                               (void*)ptr, num);
            } else {
                return UNIFYFS_ERROR_IO;
            }
            ptr += num;
            pos += num;

            /* update number of bytes processed */
            processed += num;
        }
    }

    return rc;
}
