/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
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

#include "unifycr-runtime-config.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <search.h>
#include <assert.h>
#include <libgen.h>
#include <limits.h>
#define __USE_GNU
#include <pthread.h>

#include "unifycr-internal.h"
#include "unifycr_meta.h"

extern int dbgrank;
extern unifycr_index_buf_t unifycr_indices;
extern unifycr_fattr_buf_t unifycr_fattrs;
extern void *unifycr_superblock;
extern unsigned long unifycr_max_index_entries;
extern int unifycr_spillover_max_chunks;

/* given a file id and logical chunk id, return pointer to meta data
 * for specified chunk, return NULL if not found */
unifycr_chunkmeta_t *unifycr_get_chunkmeta(int fid, int cid)
{
    /* lookup file meta data for specified file id */
    unifycr_filemeta_t *meta = unifycr_get_meta_from_fid(fid);
    if (meta != NULL) {
        /* now lookup chunk meta data for specified chunk id */
        if (cid >= 0 && cid < unifycr_max_chunks) {
            unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[cid]);
            return chunk_meta;
        }
    }

    /* failed to find file or chunk id is out of range */
    return (unifycr_chunkmeta_t *)NULL;
}

/* ---------------------------------------
 * Operations on file chunks
 * --------------------------------------- */

/* given a logical chunk id and an offset within that chunk, return the pointer
 * to the memory location corresponding to that location */
static inline void *unifycr_compute_chunk_buf(
    const unifycr_filemeta_t *meta,
    int logical_id,
    off_t logical_offset)
{
    /* get pointer to chunk meta */
    const unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[logical_id]);

    /* identify physical chunk id */
    int physical_id = chunk_meta->id;

    /* compute the start of the chunk */
    char *start = NULL;
    if (physical_id < unifycr_max_chunks) {
        start = unifycr_chunks + ((long)physical_id << unifycr_chunk_bits);
    } else {
        /* chunk is in spill over */
        DEBUG("wrong chunk ID\n");
        return NULL;
    }

    /* now add offset */
    char *buf = start + logical_offset;
    return (void *)buf;
}

/* given a chunk id and an offset within that chunk, return the offset
 * in the spillover file corresponding to that location */
static inline off_t unifycr_compute_spill_offset(
    const unifycr_filemeta_t *meta,
    int logical_id,
    off_t logical_offset)
{
    /* get pointer to chunk meta */
    const unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[logical_id]);

    /* identify physical chunk id */
    int physical_id = chunk_meta->id;
    /* compute start of chunk in spill over device */
    off_t start = 0;
    if (physical_id < unifycr_max_chunks) {
        DEBUG("wrong spill-chunk ID\n");
        return -1;
    } else {
        /* compute buffer loc within spillover device chunk */
        /* account for the unifycr_max_chunks added to identify location when
         * grabbing this chunk */
        start = ((long)(physical_id - unifycr_max_chunks) << unifycr_chunk_bits);
    }
    off_t buf = start + logical_offset;
    return buf;
}

/* allocate a new chunk for the specified file and logical chunk id */
static int unifycr_chunk_alloc(int fid, unifycr_filemeta_t *meta, int chunk_id)
{
    /* get pointer to chunk meta data */
    unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[chunk_id]);
    /* allocate a chunk and record its location */
    if (unifycr_use_memfs) {
        /* allocate a new chunk from memory */
        unifycr_stack_lock();
        int id = unifycr_stack_pop(free_chunk_stack);
        unifycr_stack_unlock();

        /* if we got one return, otherwise try spill over */
        if (id >= 0) {
            /* got a chunk from memory */
            chunk_meta->location = CHUNK_LOCATION_MEMFS;
            chunk_meta->id = id;
        } else if (unifycr_use_spillover) {
            /* shm segment out of space, grab a block from spill-over device */

            DEBUG("getting blocks from spill-over device\n");
            /* TODO: missing lock calls? */
            /* add unifycr_max_chunks to identify chunk location */
            unifycr_stack_lock();
            id = unifycr_stack_pop(free_spillchunk_stack) + unifycr_max_chunks;
            unifycr_stack_unlock();
            if (id < unifycr_max_chunks) {
                DEBUG("spill-over device out of space (%d)\n", id);
                return UNIFYCR_ERROR_NOSPC;
            }

            /* got one from spill over */
            chunk_meta->location = CHUNK_LOCATION_SPILLOVER;
            chunk_meta->id = id;
        } else {
            /* spill over isn't available, so we're out of space */
            DEBUG("memfs out of space (%d)\n", id);
            return UNIFYCR_ERROR_NOSPC;
        }
    } else if (unifycr_use_spillover) {
        /* memory file system is not enabled, but spill over is */

        /* shm segment out of space, grab a block from spill-over device */
        DEBUG("getting blocks from spill-over device\n");

        /* TODO: missing lock calls? */
        /* add unifycr_max_chunks to identify chunk location */
        unifycr_stack_lock();
        int id = unifycr_stack_pop(free_spillchunk_stack) + unifycr_max_chunks;
        unifycr_stack_unlock();
        if (id < unifycr_max_chunks) {
            DEBUG("spill-over device out of space (%d)\n", id);
            return UNIFYCR_ERROR_NOSPC;
        }

        /* got one from spill over */
        chunk_meta->location = CHUNK_LOCATION_SPILLOVER;
        chunk_meta->id = id;
    } else {
        /* don't know how to allocate chunk */
        chunk_meta->location = CHUNK_LOCATION_NULL;
        return UNIFYCR_ERROR_IO;
    }

    return UNIFYCR_SUCCESS;
}

static int unifycr_chunk_free(int fid, unifycr_filemeta_t *meta, int chunk_id)
{
    /* get pointer to chunk meta data */
    unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[chunk_id]);

    /* get physical id of chunk */
    int id = chunk_meta->id;
    DEBUG("free chunk %d from location %d\n", id, chunk_meta->location);

    /* determine location of chunk */
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        unifycr_stack_lock();
        unifycr_stack_push(free_chunk_stack, id);
        unifycr_stack_unlock();
    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* TODO: free spill over chunk */
    } else {
        /* unkwown chunk location */
        DEBUG("unknown chunk location %d\n", chunk_meta->location);
        return UNIFYCR_ERROR_IO;
    }

    /* update location of chunk */
    chunk_meta->location = CHUNK_LOCATION_NULL;

    return UNIFYCR_SUCCESS;
}

/* read data from specified chunk id, chunk offset, and count into user buffer,
 * count should fit within chunk starting from specified offset */
static int unifycr_chunk_read(
    unifycr_filemeta_t *meta, /* pointer to file meta data */
    int chunk_id,            /* logical chunk id to read data from */
    off_t chunk_offset,      /* logical offset within chunk to read from */
    void *buf,               /* buffer to store data to */
    size_t count)            /* number of bytes to read */
{
    /* get chunk meta data */
    unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[chunk_id]);

    /* determine location of chunk */
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        /* just need a memcpy to read data */
        void *chunk_buf = unifycr_compute_chunk_buf(meta, chunk_id, chunk_offset);
        memcpy(buf, chunk_buf, count);
    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* spill over to a file, so read from file descriptor */
        //MAP_OR_FAIL(pread);
        off_t spill_offset = unifycr_compute_spill_offset(meta, chunk_id, chunk_offset);
        ssize_t rc = pread(unifycr_spilloverblock, buf, count, spill_offset);
        if (rc < 0)
            return unifycr_errno_map_to_err(rc);
    } else {
        /* unknown chunk type */
        DEBUG("unknown chunk type in read\n");
        return UNIFYCR_ERROR_IO;
    }

    /* assume read was successful if we get to here */
    return UNIFYCR_SUCCESS;
}

/*
 * given an index, split it into multiple indices whose range is equal or smaller
 * than slice_range size
 * @param cur_idx: the index to split
 * @param slice_range: the slice size of the key-value store
 * @return index_set: the set of split indices
 * */
int unifycr_split_index(unifycr_index_t *cur_idx, index_set_t *index_set,
                        long slice_range)
{

    long cur_idx_start = cur_idx->file_pos;
    long cur_idx_end = cur_idx->file_pos + cur_idx->length - 1;

    long cur_slice_start = cur_idx->file_pos / slice_range * slice_range;
    long cur_slice_end = cur_slice_start + slice_range - 1;


    index_set->count = 0;

    long cur_mem_pos = cur_idx->mem_pos;
    if (cur_idx_end <= cur_slice_end) {
        /*
        cur_slice_start                                  cur_slice_end
                         cur_idx_start      cur_idx_end

        */
        index_set->idxes[index_set->count] = *cur_idx;
        index_set->count++;

    } else {
        /*
        cur_slice_start                     cur_slice_endnext_slice_start                   next_slice_end
                         cur_idx_start                                      cur_idx_end

        */
        index_set->idxes[index_set->count] = *cur_idx;
        index_set->idxes[index_set->count].length =
            cur_slice_end - cur_idx_start + 1;

        cur_mem_pos += index_set->idxes[index_set->count].length;

        cur_slice_start = cur_slice_end + 1;
        cur_slice_end = cur_slice_start + slice_range - 1;
        index_set->count++;

        while (1) {
            if (cur_idx_end <= cur_slice_end) {
                break;
            }

            index_set->idxes[index_set->count].fid = cur_idx->fid;
            index_set->idxes[index_set->count].file_pos = cur_slice_start;
            index_set->idxes[index_set->count].length = slice_range;
            index_set->idxes[index_set->count].mem_pos = cur_mem_pos;
            cur_mem_pos += index_set->idxes[index_set->count].length;

            cur_slice_start = cur_slice_end + 1;
            cur_slice_end = cur_slice_start + slice_range - 1;
            index_set->count++;

        }

        index_set->idxes[index_set->count].fid = cur_idx->fid;
        index_set->idxes[index_set->count].file_pos = cur_slice_start;
        index_set->idxes[index_set->count].length = cur_idx_end - cur_slice_start + 1;
        index_set->idxes[index_set->count].mem_pos = cur_mem_pos;
        index_set->count++;
    }

    return 0;
}

/* read data from specified chunk id, chunk offset, and count into user buffer,
 * count should fit within chunk starting from specified offset */
static int unifycr_logio_chunk_write(
    int fid,
    long pos,                /* write offset inside the file */
    unifycr_filemeta_t *meta, /* pointer to file meta data */
    int chunk_id,            /* logical chunk id to write to */
    off_t chunk_offset,      /* logical offset within chunk to write to */
    const void *buf,         /* buffer holding data to be written */
    size_t count)            /* number of bytes to write */
{
    /* get chunk meta data */
    unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[chunk_id]);
    /* determine location of chunk */
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        /* just need a memcpy to write data */
        char *chunk_buf = unifycr_compute_chunk_buf(meta, chunk_id, chunk_offset);
        memcpy(chunk_buf, buf, count);
        /* Synchronize metadata*/

        unifycr_index_t cur_idx;
        cur_idx.file_pos = pos;
        cur_idx.mem_pos = chunk_buf - unifycr_chunks;
        cur_idx.length = count;

        /* find the corresponding file attr entry and update attr*/
        unifycr_file_attr_t tmp_meta_entry;
        tmp_meta_entry.fid = fid;
        unifycr_file_attr_t *ptr_meta_entry
            = (unifycr_file_attr_t *)bsearch(&tmp_meta_entry,
                                         unifycr_fattrs.meta_entry,
                                         *unifycr_fattrs.ptr_num_entries,
                                         sizeof(unifycr_file_attr_t),
					 compare_fattr);
        if (ptr_meta_entry !=  NULL) {
            ptr_meta_entry->file_attr.st_size = pos + count;
        }
        cur_idx.fid = ptr_meta_entry->gfid;

        /*split the write requests larger than unifycr_key_slice_range into
         * the ones smaller than unifycr_key_slice_range
         * */
        unifycr_split_index(&cur_idx, &tmp_index_set,
                            unifycr_key_slice_range);

        int i = 0;
        if (*(unifycr_indices.ptr_num_entries) + tmp_index_set.count
            < unifycr_max_index_entries) {
            /*coalesce contiguous indices*/

            if (*unifycr_indices.ptr_num_entries >= 1) {
                unifycr_index_t *ptr_last_idx =
                    &unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries - 1];
                if (ptr_last_idx->fid == tmp_index_set.idxes[0].fid &&
                    ptr_last_idx->file_pos + ptr_last_idx->length
                    == tmp_index_set.idxes[0].file_pos) {
                    if (ptr_last_idx->file_pos / unifycr_key_slice_range
                        == tmp_index_set.idxes[0].file_pos / unifycr_key_slice_range) {
                        ptr_last_idx->length  += tmp_index_set.idxes[0].length;
                        i++;
                    }
                }
            }

            for (; i < tmp_index_set.count; i++) {
                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].file_pos =
                    tmp_index_set.idxes[i].file_pos;
                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].mem_pos =
                    tmp_index_set.idxes[i].mem_pos;
                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].length =
                    tmp_index_set.idxes[i].length;


                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].fid
                    = tmp_index_set.idxes[i].fid;
                (*unifycr_indices.ptr_num_entries)++;
            }



        } else {
            /*TOdO:swap out existing metadata buffer to disk*/
        }




    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* spill over to a file, so write to file descriptor */
        //MAP_OR_FAIL(pwrite);
        off_t spill_offset = unifycr_compute_spill_offset(meta, chunk_id, chunk_offset);
        /*  printf("spill_offset is %ld, count:%ld, chunk_offset is %ld\n",
                  spill_offset, count, chunk_offset);
          fflush(stdout); */
        ssize_t rc = __real_pwrite(unifycr_spilloverblock, buf, count, spill_offset);
        if (rc < 0)  {
            perror("pwrite failed");
        }


        unifycr_index_t cur_idx;
        cur_idx.file_pos = pos;

        cur_idx.mem_pos = spill_offset + unifycr_max_chunks * (1 << unifycr_chunk_bits);
        cur_idx.length = count;

        /* find the corresponding file attr entry and update attr*/
        unifycr_file_attr_t tmp_meta_entry;
        tmp_meta_entry.fid = fid;
        unifycr_file_attr_t *ptr_meta_entry
            = (unifycr_file_attr_t *)bsearch(&tmp_meta_entry,
                                         unifycr_fattrs.meta_entry,
					 *unifycr_fattrs.ptr_num_entries,
                                         sizeof(unifycr_file_attr_t),
					 compare_fattr);
        if (ptr_meta_entry !=  NULL) {
            ptr_meta_entry->file_attr.st_size = pos + count;
        }
        cur_idx.fid = ptr_meta_entry->gfid;

        /*split the write requests larger than unifycr_key_slice_range into
         * the ones smaller than unifycr_key_slice_range
         * */
        unifycr_split_index(&cur_idx, &tmp_index_set,
                            unifycr_key_slice_range);
        int i = 0;
        if (*(unifycr_indices.ptr_num_entries) + tmp_index_set.count
            < unifycr_max_index_entries) {
            /*coalesce contiguous indices*/

            if (*unifycr_indices.ptr_num_entries >= 1) {
                unifycr_index_t *ptr_last_idx =
                    &unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries - 1];
                if (ptr_last_idx->fid == tmp_index_set.idxes[0].fid &&
                    ptr_last_idx->file_pos + ptr_last_idx->length
                    == tmp_index_set.idxes[0].file_pos) {
                    if (ptr_last_idx->file_pos / unifycr_key_slice_range
                        == tmp_index_set.idxes[0].file_pos / unifycr_key_slice_range) {
                        ptr_last_idx->length  += tmp_index_set.idxes[0].length;
                        i++;
                    }
                }
            }

            for (; i < tmp_index_set.count; i++) {
                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].file_pos =
                    tmp_index_set.idxes[i].file_pos;
                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].mem_pos =
                    tmp_index_set.idxes[i].mem_pos;
                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].length =
                    tmp_index_set.idxes[i].length;

                unifycr_indices.index_entry[*unifycr_indices.ptr_num_entries].fid
                    = tmp_index_set.idxes[i].fid;
                (*unifycr_indices.ptr_num_entries)++;
            }

        } else {
            /*Todo:page out existing metadata buffer to disk*/
        }

        /* TOdo: check return code for errors */
    } else {
        /* unknown chunk type */
        DEBUG("unknown chunk type in read\n");
        return UNIFYCR_ERROR_IO;
    }

    /* assume read was successful if we get to here */
    return UNIFYCR_SUCCESS;
}

/* read data from specified chunk id, chunk offset, and count into user buffer,
 * count should fit within chunk starting from specified offset */
static int unifycr_chunk_write(
    unifycr_filemeta_t *meta, /* pointer to file meta data */
    int chunk_id,            /* logical chunk id to write to */
    off_t chunk_offset,      /* logical offset within chunk to write to */
    const void *buf,         /* buffer holding data to be written */
    size_t count)            /* number of bytes to write */
{
    /* get chunk meta data */
    unifycr_chunkmeta_t *chunk_meta = &(meta->chunk_meta[chunk_id]);

    /* determine location of chunk */
    if (chunk_meta->location == CHUNK_LOCATION_MEMFS) {
        /* just need a memcpy to write data */
        void *chunk_buf = unifycr_compute_chunk_buf(meta, chunk_id, chunk_offset);
        memcpy(chunk_buf, buf, count);
//        _intel_fast_memcpy(chunk_buf, buf, count);
//        unifycr_memcpy(chunk_buf, buf, count);
    } else if (chunk_meta->location == CHUNK_LOCATION_SPILLOVER) {
        /* spill over to a file, so write to file descriptor */
        //MAP_OR_FAIL(pwrite);
        off_t spill_offset = unifycr_compute_spill_offset(meta, chunk_id, chunk_offset);
        ssize_t rc = pwrite(unifycr_spilloverblock, buf, count, spill_offset);
        if (rc < 0)  {
            perror("pwrite failed");
        }

        /* TODO: check return code for errors */
    } else {
        /* unknown chunk type */
        DEBUG("unknown chunk type in read\n");
        return UNIFYCR_ERROR_IO;
    }

    /* assume read was successful if we get to here */
    return UNIFYCR_SUCCESS;
}

/* ---------------------------------------
 * Operations on file storage
 * --------------------------------------- */

/* if length is greater than reserved space, reserve space up to length */
int unifycr_fid_store_fixed_extend(int fid, unifycr_filemeta_t *meta,
                                   off_t length)
{
    /* determine whether we need to allocate more chunks */
    off_t maxsize = meta->chunks << unifycr_chunk_bits;
//      printf("rank %d,meta->chunks is %d, length is %ld, maxsize is %ld\n", dbgrank, meta->chunks, length, maxsize);
    if (length > maxsize) {
        /* compute number of additional bytes we need */
        off_t additional = length - maxsize;
        while (additional > 0) {
            /* check that we don't overrun max number of chunks for file */
            if (meta->chunks == unifycr_max_chunks + unifycr_spillover_max_chunks) {
                return UNIFYCR_ERROR_NOSPC;
            }
            /* allocate a new chunk */
            int rc = unifycr_chunk_alloc(fid, meta, meta->chunks);
            if (rc != UNIFYCR_SUCCESS) {
                DEBUG("failed to allocate chunk\n");
                return UNIFYCR_ERROR_NOSPC;
            }

            /* increase chunk count and subtract bytes from the number we need */
            meta->chunks++;
            additional -= unifycr_chunk_size;
        }
    }

    return UNIFYCR_SUCCESS;
}

/* if length is shorter than reserved space, give back space down to length */
int unifycr_fid_store_fixed_shrink(int fid, unifycr_filemeta_t *meta,
                                   off_t length)
{
    /* determine the number of chunks to leave after truncating */
    off_t num_chunks = 0;
    if (length > 0) {
        num_chunks = (length >> unifycr_chunk_bits) + 1;
    }

    /* clear off any extra chunks */
    while (meta->chunks > num_chunks) {
        meta->chunks--;
        unifycr_chunk_free(fid, meta, meta->chunks);
    }

    return UNIFYCR_SUCCESS;
}

/* read data from file stored as fixed-size chunks */
int unifycr_fid_store_fixed_read(int fid, unifycr_filemeta_t *meta, off_t pos,
                                 void *buf, size_t count)
{
    int rc;

    /* get pointer to position within first chunk */
    int chunk_id = pos >> unifycr_chunk_bits;
    off_t chunk_offset = pos & unifycr_chunk_mask;

    /* determine how many bytes remain in the current chunk */
    size_t remaining = unifycr_chunk_size - chunk_offset;
    if (count <= remaining) {
        /* all bytes for this read fit within the current chunk */
        rc = unifycr_chunk_read(meta, chunk_id, chunk_offset, buf, count);
    } else {
        /* read what's left of current chunk */
        char *ptr = (char *) buf;
        rc = unifycr_chunk_read(meta, chunk_id, chunk_offset, (void *)ptr, remaining);
        ptr += remaining;

        /* read from the next chunk */
        size_t processed = remaining;
        while (processed < count && rc == UNIFYCR_SUCCESS) {
            /* get pointer to start of next chunk */
            chunk_id++;

            /* compute size to read from this chunk */
            size_t num = count - processed;
            if (num > unifycr_chunk_size) {
                num = unifycr_chunk_size;
            }

            /* read data */
            rc = unifycr_chunk_read(meta, chunk_id, 0, (void *)ptr, num);
            ptr += num;

            /* update number of bytes written */
            processed += num;
        }
    }

    return rc;
}

/* write data to file stored as fixed-size chunks */
int unifycr_fid_store_fixed_write(int fid, unifycr_filemeta_t *meta, off_t pos,
                                  const void *buf, size_t count)
{
    int rc;

    /* get pointer to position within first chunk */
    int chunk_id;
    off_t chunk_offset;

    if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
        chunk_id = pos >> unifycr_chunk_bits;
        chunk_offset = pos & unifycr_chunk_mask;
    } else if (meta->storage == FILE_STORAGE_LOGIO) {
        chunk_id = meta->size >> unifycr_chunk_bits;
        chunk_offset = meta->size & unifycr_chunk_mask;
    } else {
        return UNIFYCR_ERROR_IO;
    }

    /* determine how many bytes remain in the current chunk */
    size_t remaining = unifycr_chunk_size - chunk_offset;
    if (count <= remaining) {
        /* all bytes for this write fit within the current chunk */
        if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
            rc = unifycr_chunk_write(meta, chunk_id, chunk_offset, buf, count);
        } else if (meta->storage == FILE_STORAGE_LOGIO) {
            rc = unifycr_logio_chunk_write(fid, pos, meta, chunk_id, chunk_offset,
                                           buf, count);
        } else {
            return UNIFYCR_ERROR_IO;
        }
    } else {
        /* otherwise, fill up the remainder of the current chunk */
        char *ptr = (char *) buf;
        if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
            rc = unifycr_chunk_write(meta, chunk_id, chunk_offset, (void *)ptr, remaining);
        } else if (meta->storage == FILE_STORAGE_LOGIO) {
            rc = unifycr_logio_chunk_write(fid, pos, meta, chunk_id, chunk_offset,
                                           (void *)ptr, remaining);
        } else {
            return UNIFYCR_ERROR_IO;
        }

        ptr += remaining;
        pos += remaining;

        /* then write the rest of the bytes starting from beginning
         * of chunks */
        size_t processed = remaining;
        while (processed < count && rc == UNIFYCR_SUCCESS) {
            /* get pointer to start of next chunk */
            chunk_id++;

            /* compute size to write to this chunk */
            size_t num = count - processed;
            if (num > unifycr_chunk_size) {
                num = unifycr_chunk_size;
            }

            /* write data */
            if (meta->storage == FILE_STORAGE_FIXED_CHUNK) {
                rc = unifycr_chunk_write(meta, chunk_id, 0, (void *)ptr, num);
            } else if (meta->storage == FILE_STORAGE_LOGIO)
                rc = unifycr_logio_chunk_write(fid, pos, meta, chunk_id, 0,
                                               (void *)ptr, num);
            else {
                return UNIFYCR_ERROR_IO;
            }
            ptr += num;
            pos += num;

            /* update number of bytes processed */
            processed += num;
        }
    }

    return rc;
}
