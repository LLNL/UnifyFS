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

#include "unifyfs-fixed.h"
#include "unifyfs_log.h"

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
    if (prev_idx->fid != next_idx->fid) {
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

/* given an index, split it into multiple indices whose range is equal or
 * smaller than slice_range size
 * @param  cur_idx:     the index to split
 * @param  slice_range: the slice size of the key-value store
 * @return index_set:   the set of split indices
 * @param  maxcount:     number of entries in output array */
static int unifyfs_split_index(
    unifyfs_index_t* cur_idx,   /* write index to split (offset and length) */
    long slice_range,           /* number of bytes in each slice */
    unifyfs_index_t* index_set, /* output array to store new indexes in */
    off_t maxcount,             /* max number of items in output array */
    off_t* used_count)          /* number of entries we added in split */
{
    /* first byte offset this write will write to */
    long idx_start = cur_idx->file_pos;

    /* last byte offset this write will write to */
    long idx_end = cur_idx->file_pos + cur_idx->length - 1;

    /* starting byte offset of slice that first write offset falls in */
    long slice_start = (idx_start / slice_range) * slice_range;

    /* last byte offset of slice that first write offset falls in */
    long slice_end = slice_start + slice_range - 1;

    /* get pointer to first output index structure */
    unifyfs_index_t* set = index_set;

    /* initialize count of output index entries */
    off_t count = 0;

    /* no room to write index values */
    if (count >= maxcount) {
        /* no room to write more index values,
         * and we have at least one more,
         * record number we wrote and return with error */
        *used_count = 0;
        return UNIFYFS_FAILURE;
    }

    /* define new index entries in index_set by splitting write index
     * at slice boundaries */
    if (idx_end <= slice_end) {
        /* index falls fully within one slice
         *
         * slice_start           slice_end
         *      idx_start   idx_end
         */
        set[count] = *cur_idx;
        count++;
    } else {
        /* ending offset of index is beyond last offset in first slice,
         * so this index spans across multiple slices
         *
         * slice_start  slice_end  next_slice_start   next_slice_end
         *      idx_start                          idx_end
         */

        /* get starting position in log */
        long log_pos = cur_idx->log_pos;

        /* copy over all fields in current index */
        set[count] = *cur_idx;

        /* update length field to adjust for boundary of first slice */
        long length = slice_end - idx_start + 1;
        set[count].length = length;

        /* advance offset into log */
        log_pos += length;

        /* increment our output index count */
        count++;

        /* check that we have room to write more index values */
        if (count >= maxcount) {
            /* no room to write more index values,
             * and we have at least one more,
             * record number we wrote and return with error */
            *used_count = count;
            return UNIFYFS_FAILURE;
        }

        /* advance slice boundary offsets to next slice */
        slice_start = slice_end + 1;
        slice_end   = slice_start + slice_range - 1;

        /* loop until we find the slice that contains
         * ending offset of write */
        while (idx_end > slice_end) {
            /* ending offset of write is beyond end of this slice,
             * so write spans the full length of this slice */
            length = slice_range;

            /* define index for this slice */
            set[count].fid      = cur_idx->fid;
            set[count].file_pos = slice_start;
            set[count].length   = length;
            set[count].log_pos  = log_pos;

            /* advance offset into log */
            log_pos += length;

            /* increment our output index count */
            count++;

            /* check that we have room to write more index values */
            if (count >= maxcount) {
                /* no room to write more index values,
                 * and we have at least one more,
                 * record number we wrote and return with error */
                *used_count = count;
                return UNIFYFS_FAILURE;
            }

            /* advance slice boundary offsets to next slice */
            slice_start = slice_end + 1;
            slice_end   = slice_start + slice_range - 1;
        }

        /* this slice contains the remainder of write */
        length = idx_end - slice_start + 1;
        set[count].fid      = cur_idx->fid;
        set[count].file_pos = slice_start;
        set[count].length   = length;
        set[count].log_pos  = log_pos;
        count++;
    }

    /* record number of entires we added */
    *used_count = count;

    return UNIFYFS_SUCCESS;
}

/* read data from specified chunk id, chunk offset, and count into user buffer,
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

        /* write data into file at appropriate offset */
        //MAP_OR_FAIL(pwrite);
        ssize_t rc = __real_pwrite(unifyfs_spilloverblock, buf, count, spill_offset);
        if (rc < 0)  {
            LOGERR("pwrite failed: errno=%d (%s)", errno, strerror(errno));
        }

        /* record byte offset position within log, for spill over
         * locations we take the offset within the spill file
         * added to the total size of all shared memory data chunks */
        log_offset = spill_offset + unifyfs_max_chunks * (1 << unifyfs_chunk_bits);
    }

    /* TODO: pass in gfid for this file or call function to look it up? */
    /* find the corresponding file attr entry and update attr*/
    unifyfs_file_attr_t tmp_meta_entry;
    tmp_meta_entry.fid = fid;
    unifyfs_file_attr_t* ptr_meta_entry
        = (unifyfs_file_attr_t*)bsearch(&tmp_meta_entry,
                                        unifyfs_fattrs.meta_entry,
                                        *unifyfs_fattrs.ptr_num_entries,
                                        sizeof(unifyfs_file_attr_t),
                                        compare_fattr);

    /* define an new index entry for this write operation */
    unifyfs_index_t cur_idx;
    cur_idx.fid      = ptr_meta_entry->gfid;
    cur_idx.file_pos = pos;
    cur_idx.log_pos  = log_offset;
    cur_idx.length   = count;

    /* lookup number of existing index entries */
    off_t num_entries = *(unifyfs_indices.ptr_num_entries);

    /* remaining entries we can fit in the shared memory region */
    off_t remaining_entries = unifyfs_max_index_entries - num_entries;

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
    }

    /* split any remaining write index at boundaries of
     * unifyfs_key_slice_range */
    if (cur_idx.length > 0) {
        off_t used_entries = 0;
        int split_rc = unifyfs_split_index(&cur_idx, unifyfs_key_slice_range,
            &idxs[num_entries], remaining_entries, &used_entries);
        if (split_rc != UNIFYFS_SUCCESS) {
            /* in this case, we have copied data to the log,
             * but we failed to generate index entries,
             * we're returning with an error and leaving the data
             * in the log */
            LOGERR("exhausted space when splitting write index");
            return UNIFYFS_ERROR_IO;
        }

        /* account for entries we just added */
        num_entries += used_entries;
    }

    /* update number of entries in index array */
    (*unifyfs_indices.ptr_num_entries) = num_entries;

    /* assume read was successful if we get to here */
    return UNIFYFS_SUCCESS;
}

/* read data from specified chunk id, chunk offset, and count into user buffer,
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

    /* assume read was successful if we get to here */
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
