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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "unifyfs_log.h"
#include "unifyfs_logio.h"
#include "unifyfs_meta.h"
#include "unifyfs_shm.h"
#include "slotmap.h"

#define LOGIO_SHMEM_FMTSTR "logio_mem.%d.%d"
#define LOGIO_SPILL_FMTSTR "%s/logio_spill.%d.%d"


/* log-based I/O header - first page of shmem region or spill file */
typedef struct log_header {
    size_t data_sz;            /* total data bytes in log */
    size_t reserved_sz;        /* reserved data bytes */
    size_t chunk_sz;           /* data chunk size */
    size_t max_reserved_slot;  /* slot index for last reserved chunk */
    off_t data_offset;         /* file/memory offset where data chunks start */
} log_header;
/* chunk slot_map immediately follows header and occupies rest of the page */
// slot_map chunk_map;         /* chunk slot_map that tracks reservations */

static inline
slot_map* log_header_to_chunkmap(log_header* hdr)
{
    char* hdrp = (char*) hdr;
    return (slot_map*)(hdrp + sizeof(log_header));
}

/* method to get page size once, then re-use it */
size_t get_page_size(void)
{
    static size_t page_sz; // = 0
    if (0 == page_sz) {
        page_sz = (size_t) getpagesize();
    }
    return page_sz;
}

/* calculate number of chunks needed for requested bytes */
static inline
size_t bytes_to_chunks(size_t bytes, size_t chunk_sz)
{
    size_t n_chunks = bytes / chunk_sz;
    if (bytes % chunk_sz) {
        n_chunks++;
    }
    return n_chunks;
}

/* determine shmem and spill chunk allocations based on log offset */
static inline
void get_log_sizes(off_t log_offset,
                   size_t nbytes,
                   size_t shmem_data_sz,
                   size_t* sz_in_mem,
                   size_t* sz_in_spill,
                   off_t* spill_offset)
{
    assert((NULL != sz_in_mem) &&
           (NULL != sz_in_spill) &&
           (NULL != spill_offset));

    *sz_in_mem = 0;
    *sz_in_spill = 0;
    *spill_offset = 0;

    if ((log_offset + (off_t)nbytes) <= shmem_data_sz) {
        /* data fully in shared memory */
        *sz_in_mem = nbytes;
    } else if (log_offset < shmem_data_sz) {
        /* requested data spans shared memory and spillover file */
        *sz_in_mem = (size_t)(shmem_data_sz - log_offset);
        *sz_in_spill = nbytes - *sz_in_mem;
    } else {
        /* requested data is totally in spillover file */
        *sz_in_spill = nbytes;
        *spill_offset = log_offset - shmem_data_sz;
    }
}

/* open (or create) spill file at path and set its size */
static int get_spillfile(const char* path,
                         const size_t spill_sz)
{
    /* try to create the spill file */
    mode_t perms = unifyfs_getmode(0640);
    int spill_fd = open(path, O_RDWR | O_CREAT | O_EXCL, perms);
    if (spill_fd < 0) {
        if (errno == EEXIST) {
            /* already exists - try simple open */
            spill_fd = open(path, O_RDWR);
        } else {
            int err = errno;
            LOGERR("open(%s) failed: %s", path, strerror(err));
        }
    } else {
        /* new spillover block created, set its size */
        int rc = ftruncate(spill_fd, (off_t)spill_sz);
        if (rc < 0) {
            int err = errno;
            LOGERR("ftruncate() failed: %s", strerror(err));
        }
    }
    return spill_fd;
}

/* map log header (1st page) of spill file given by file descriptor */
static void* map_spillfile(int spill_fd, int mmap_prot)
{
    size_t pgsz = get_page_size();
    void* addr = mmap(NULL, pgsz, mmap_prot, MAP_SHARED, spill_fd, 0);
    if (NULL == addr) {
        int err = errno;
        LOGERR("mmap(fd=%d, sz=%zu, MAP_SHARED) failed - %s",
               spill_fd, pgsz, strerror(err));
    }
    return addr;
}

/* Initialize logio context for server */
int unifyfs_logio_init_server(const int app_id,
                              const int client_id,
                              const size_t mem_size,
                              const size_t spill_size,
                              const char* spill_dir,
                              logio_context** pctx)
{
    if (NULL == pctx) {
        return EINVAL;
    }
    *pctx = NULL;

    shm_context* shm_ctx = NULL;
    if (mem_size) {
        /* attach to client shmem region */
        char shm_name[SHMEM_NAME_LEN] = {0};
        snprintf(shm_name, sizeof(shm_name), LOGIO_SHMEM_FMTSTR,
                 app_id, client_id);
        shm_ctx = unifyfs_shm_alloc(shm_name, mem_size);
        if (NULL == shm_ctx) {
            LOGERR("Failed to attach logio shmem buffer!");
            return UNIFYFS_ERROR_SHMEM;
        }
    }

    char spillfile[UNIFYFS_MAX_FILENAME];
    void* spill_mapping = NULL;
    int spill_fd = -1;
    if (spill_size) {
        if (NULL == spill_dir) {
            LOGERR("Spill directory not given!");
            return EINVAL;
        }

        /* open the spill over file */
        snprintf(spillfile, sizeof(spillfile), LOGIO_SPILL_FMTSTR,
                 spill_dir, app_id, client_id);
        spill_fd = get_spillfile(spillfile, spill_size);
        if (spill_fd < 0) {
            LOGERR("Failed to open logio spill file!");
            return UNIFYFS_FAILURE;
        } else {
            /* map first page of the spill over file, which contains log header
             * and chunk slot_map. server only needs read access. */
            spill_mapping = map_spillfile(spill_fd, PROT_READ);
            if (NULL == spill_mapping) {
                LOGERR("Failed to map logio spill file header!");
                return UNIFYFS_FAILURE;
            }
        }
    }

    logio_context* ctx = (logio_context*) calloc(1, sizeof(logio_context));
    if (NULL == ctx) {
        LOGERR("Failed to allocate logio context!");
        return ENOMEM;
    }
    ctx->shmem = shm_ctx;
    ctx->spill_hdr = spill_mapping;
    ctx->spill_fd = spill_fd;
    ctx->spill_sz = spill_size;
    if (spill_size) {
        ctx->spill_file = strdup(spillfile);
    }
    *pctx = ctx;

    return UNIFYFS_SUCCESS;
}


/* initialize the log header page for given log region and size
 * (note: intended for client use only) */
static int init_log_header(char* log_region,
                           size_t region_size,
                           size_t chunk_size)
{
    size_t pgsz = get_page_size();

    /* TODO: need to think about how to support client re-attach */

    /* log header structure resides at start of log region */
    log_header* hdr = (log_header*) log_region;

    /* zero all log header fields */
    memset(log_region, 0, sizeof(log_header));

    /* chunk data starts after header page */
    size_t data_size = region_size - pgsz;
    hdr->data_sz = data_size;
    hdr->chunk_sz = chunk_size;
    hdr->data_offset = (off_t)pgsz;

    /* initialize chunk slot map (immediately follows header in memory) */
    char* slotmap = log_region + sizeof(log_header);
    size_t slotmap_size = pgsz - sizeof(log_header);
    size_t n_chunks = data_size / chunk_size;
    slot_map* chunkmap = slotmap_init(n_chunks, (void*)slotmap, slotmap_size);
    if (NULL == chunkmap) {
        LOGERR("Failed to initialize chunk slotmap @ %p (sz=%zu, #chunks=%zu)",
               slotmap, slotmap_size, n_chunks);
        return UNIFYFS_FAILURE;
    }

    return UNIFYFS_SUCCESS;
}

/* Initialize logio for client */
int unifyfs_logio_init_client(const int app_id,
                              const int client_id,
                              const unifyfs_cfg_t* client_cfg,
                              logio_context** pctx)
{
    char* cfgval;
    int rc;

    if ((NULL == client_cfg) || (NULL == pctx)) {
        return EINVAL;
    }
    *pctx = NULL;

    /* determine max memory bytes for chunk storage */
    size_t memlog_size = 0;
    cfgval = client_cfg->logio_shmem_size;
    if (cfgval != NULL) {
        long l;
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            memlog_size = (size_t)l;
        }
    }

    /* get chunk size from config */
    size_t chunk_size = UNIFYFS_LOGIO_CHUNK_SIZE;
    cfgval = client_cfg->logio_chunk_size;
    if (cfgval != NULL) {
        long l;
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            chunk_size = (size_t)l;
        }
    }

    shm_context* shm_ctx = NULL;
    if (memlog_size) {
        /* allocate logio shared memory buffer */
        char shm_name[SHMEM_NAME_LEN] = {0};
        snprintf(shm_name, sizeof(shm_name), LOGIO_SHMEM_FMTSTR,
                 app_id, client_id);
        shm_ctx = unifyfs_shm_alloc(shm_name, memlog_size);
        if (NULL == shm_ctx) {
            LOGERR("Failed to create logio shmem buffer!");
            return UNIFYFS_ERROR_SHMEM;
        }

        /* initialize shmem log header */
        char* memlog = (char*) shm_ctx->addr;
        rc = init_log_header(memlog, memlog_size, chunk_size);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("Failed to initialize shmem logio header");
            return rc;
        }
    }

    /* will we use spillover to store the files? */
    size_t spill_size = 0;
    cfgval = client_cfg->logio_spill_size;
    if (cfgval != NULL) {
        long l;
        rc = configurator_int_val(cfgval, &l);
        if (rc == 0) {
            spill_size = (size_t)l;
        }
    }
    int unifyfs_use_spillover = 0;
    if (spill_size > 0) {
        LOGDBG("using spillover - size = %zu B", spill_size);
        unifyfs_use_spillover = 1;
    }

    void* spill_mapping = NULL;
    int spill_fd = -1;
    if (unifyfs_use_spillover) {
        /* get directory in which to create spill over files */
        cfgval = client_cfg->logio_spill_dir;
        if (NULL == cfgval) {
            LOGERR("UNIFYFS_LOGIO_SPILL_DIR configuration not set! "
                   "Set to an existing writable path (e.g., /mnt/ssd)");
            return UNIFYFS_ERROR_BADCONFIG;
        }

        /* define path to the spill over file for data chunks */
        char spillfile[UNIFYFS_MAX_FILENAME];
        snprintf(spillfile, sizeof(spillfile), LOGIO_SPILL_FMTSTR,
                 cfgval, app_id, client_id);

        /* create the spill over file */
        spill_fd = get_spillfile(spillfile, spill_size);
        if (spill_fd < 0) {
            LOGERR("Failed to open logio spill file!");
            return UNIFYFS_FAILURE;
        } else {
            /* map first page of the spill over file, which contains log header
             * and chunk slot_map. client needs read and write access. */
            spill_mapping = map_spillfile(spill_fd, PROT_READ|PROT_WRITE);
            if (NULL == spill_mapping) {
                LOGERR("Failed to map logio spill file header!");
                return UNIFYFS_FAILURE;
            }

            /* initialize spill log header */
            char* spill = (char*) spill_mapping;
            rc = init_log_header(spill, spill_size, chunk_size);
            if (rc != UNIFYFS_SUCCESS) {
                LOGERR("Failed to initialize shmem logio header");
                return rc;
            }
        }
    }

    logio_context* ctx = (logio_context*) calloc(1, sizeof(logio_context));
    if (NULL == ctx) {
        LOGERR("Failed to allocate logio context!");
        return ENOMEM;
    }
    ctx->shmem = shm_ctx;
    ctx->spill_hdr = spill_mapping;
    ctx->spill_fd = spill_fd;
    ctx->spill_sz = spill_size;
    *pctx = ctx;

    return UNIFYFS_SUCCESS;
}

/* Close logio context */
int unifyfs_logio_close(logio_context* ctx,
                        int clean_spill)
{
    if (NULL == ctx) {
        return EINVAL;
    }

    int rc;
    if (NULL != ctx->shmem) {
        /* release shmem region */
        rc = unifyfs_shm_free(&(ctx->shmem));
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("Failed to release logio shmem region!");
        }
    }

    if (ctx->spill_sz) {
        if (NULL != ctx->spill_hdr) {
            /* unmap log header page */
            rc = munmap(ctx->spill_hdr, get_page_size());
            if (rc != 0) {
                int err = errno;
                LOGERR("Failed to unmap logio spill file header (errno=%s)",
                       strerror(err));
            }
            ctx->spill_hdr = NULL;
        }
        if (-1 != ctx->spill_fd) {
            /* close spill file */
            rc = close(ctx->spill_fd);
            if (rc != 0) {
                int err = errno;
                LOGERR("Failed to close logio spill file (errno=%s)",
                       strerror(err));
            }
            ctx->spill_fd = -1;
        }
        if (clean_spill && (ctx->spill_file != NULL)) {
            rc = unlink(ctx->spill_file);
            if (rc != 0) {
                int err = errno;
                LOGERR("Failed to unlink logio spill file %s (errno=%s)",
                       ctx->spill_file, strerror(err));
            }
            free(ctx->spill_file);
        }
    }

    /* free the context struct */
    free(ctx);

    return UNIFYFS_SUCCESS;
}

/* Allocate write space from logio context */
int unifyfs_logio_alloc(logio_context* ctx,
                        const size_t nbytes,
                        off_t* log_offset)
{
    if ((NULL == ctx) ||
        ((nbytes > 0) && (NULL == log_offset))) {
        return EINVAL;
    }

    if (0 == nbytes) {
        LOGWARN("zero bytes allocated from log!");
        return UNIFYFS_SUCCESS;
    }

    size_t chunk_sz = 0;
    size_t allocated_bytes = 0;
    size_t needed_bytes = nbytes;
    size_t needed_chunks;
    size_t res_chunks;
    ssize_t res_slot;
    off_t res_off = -1;

    size_t mem_res_slot = 0;
    size_t mem_res_nchk = 0;
    int mem_res_at_end = 0;
    size_t mem_allocation = 0;

    log_header* shmem_hdr = NULL;
    log_header* spill_hdr = NULL;
    slot_map* chunkmap;

    if (NULL != ctx->shmem) {
        /* get shmem log header and chunk slotmap */
        shmem_hdr = (log_header*) ctx->shmem->addr;
        chunkmap = log_header_to_chunkmap(shmem_hdr);

        /* calculate number of chunks needed for requested bytes */
        chunk_sz = shmem_hdr->chunk_sz;
        needed_chunks = bytes_to_chunks(needed_bytes, chunk_sz);

        /* try to reserve all chunks from shmem */
        res_chunks = needed_chunks;
        res_slot = slotmap_reserve(chunkmap, res_chunks);
        if (-1 != res_slot) {
            /* success, all needed chunks allocated in shmem */
            allocated_bytes = res_chunks * chunk_sz;
            shmem_hdr->reserved_sz += allocated_bytes;
            shmem_hdr->max_reserved_slot = (res_slot + res_chunks) - 1;
            res_off = (off_t)(res_slot * chunk_sz);
            *log_offset = res_off;
            return UNIFYFS_SUCCESS;
        }

        /* could not get full allocation in shmem, reserve any available
         * chunks at the end of the shmem log */
        size_t log_end_chunks = chunkmap->total_slots -
                                (shmem_hdr->max_reserved_slot + 1);
        if (log_end_chunks > 0) {
            res_chunks = log_end_chunks;
            res_slot = slotmap_reserve(chunkmap, res_chunks);
            if (-1 != res_slot) {
                /* reserved all chunks at end of shmem log */
                allocated_bytes = res_chunks * chunk_sz;
                needed_bytes -= allocated_bytes;
                res_off = (off_t)(res_slot * chunk_sz);
                mem_allocation = allocated_bytes;
                mem_res_slot = res_slot;
                mem_res_nchk = res_chunks;
                mem_res_at_end = 1;
            }
        }
    }

    if (NULL != ctx->spill_hdr) {
        /* get spill log header and chunk slotmap */
        spill_hdr = (log_header*) ctx->spill_hdr;
        chunkmap = log_header_to_chunkmap(spill_hdr);

        /* calculate number of chunks needed for remaining bytes */
        chunk_sz = spill_hdr->chunk_sz;
        needed_chunks = bytes_to_chunks(needed_bytes, chunk_sz);

        /* reserve the rest of the chunks from spill file */
        res_chunks = needed_chunks;
        res_slot = slotmap_reserve(chunkmap, res_chunks);
        if (-1 != res_slot) {
            allocated_bytes = res_chunks * chunk_sz;
            if (0 == mem_res_at_end) {
                /* success, full reservation in spill */
                spill_hdr->reserved_sz += allocated_bytes;
                spill_hdr->max_reserved_slot = (res_slot + res_chunks) - 1;
                res_off = (off_t)(res_slot * chunk_sz);
                if (NULL != shmem_hdr) {
                    /* update log offset to account for shmem log size */
                    res_off += shmem_hdr->data_sz;
                }
                *log_offset = res_off;
                return UNIFYFS_SUCCESS;
            } else {
                /* if we have an allocation from end of shmem log, make sure
                 * spill allocation starts at first chunk (slot=0) */
                if (res_slot != 0) {
                    /* incompatible shmem and spill reservations, release both
                     * and try to get the full allocation from spill */

                    /* release the spill chunks we just got */
                    int rc = slotmap_release(chunkmap, res_slot, res_chunks);
                    if (rc != UNIFYFS_SUCCESS) {
                        LOGERR("slotmap_release() for logio shmem failed");
                    }

                    /* release the shmem chunks */
                    chunkmap = log_header_to_chunkmap(shmem_hdr);
                    rc = slotmap_release(chunkmap, mem_res_slot, mem_res_nchk);
                    if (rc != UNIFYFS_SUCCESS) {
                        LOGERR("slotmap_release() for logio shmem failed");
                    }
                    mem_res_slot = 0;
                    mem_res_nchk = 0;
                    mem_allocation = 0;

                    /* try again with full reservation in spill */
                    chunkmap = log_header_to_chunkmap(spill_hdr);
                    needed_chunks = bytes_to_chunks(nbytes, chunk_sz);
                    res_chunks = needed_chunks;
                    res_slot = slotmap_reserve(chunkmap, res_chunks);
                    if (-1 != res_slot) {
                        /* success, full reservation in spill */
                        spill_hdr->reserved_sz += allocated_bytes;
                        spill_hdr->max_reserved_slot =
                            (res_slot + res_chunks) - 1;
                        res_off = (off_t)(res_slot * chunk_sz);
                        if (NULL != shmem_hdr) {
                            /* update log offset to include shmem log size */
                            res_off += shmem_hdr->data_sz;
                        }
                        *log_offset = res_off;
                        return UNIFYFS_SUCCESS;
                    }
                } else {
                    /* successful reservation spanning shmem and spill */
                    shmem_hdr->reserved_sz += mem_allocation;
                    shmem_hdr->max_reserved_slot =
                        (mem_res_slot + mem_res_nchk) - 1;
                    spill_hdr->reserved_sz += allocated_bytes;
                    spill_hdr->max_reserved_slot = (res_slot + res_chunks) - 1;
                    *log_offset = res_off;
                    return UNIFYFS_SUCCESS;
                }
            }
        }
    }

    /* can't fulfill request from spill file, roll back any prior
     * shmem reservation and return ENOSPC */
    if (mem_res_nchk) {
        chunkmap = log_header_to_chunkmap(shmem_hdr);
        int rc = slotmap_release(chunkmap, mem_res_slot, mem_res_nchk);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("slotmap_release() for logio shmem failed");
        }
    }
    return ENOSPC;
}

/* Release previously allocated write space from logio context */
int unifyfs_logio_free(logio_context* ctx,
                       const off_t log_offset,
                       const size_t nbytes)
{
    if (NULL == ctx) {
        return EINVAL;
    }

    if (0 == nbytes) {
        LOGWARN("zero bytes freed from log!");
        return UNIFYFS_SUCCESS;
    }

    log_header* shmem_hdr = NULL;
    log_header* spill_hdr = NULL;
    slot_map* chunkmap;

    off_t mem_size = 0;
    if (NULL != ctx->shmem) {
        shmem_hdr = (log_header*) ctx->shmem->addr;
        mem_size = (off_t) shmem_hdr->data_sz;
    }

    /* determine chunk allocations based on log offset */
    size_t sz_in_mem = 0;
    size_t sz_in_spill = 0;
    off_t spill_offset = 0;
    get_log_sizes(log_offset, nbytes, mem_size,
                  &sz_in_mem, &sz_in_spill, &spill_offset);

    int rc = UNIFYFS_SUCCESS;
    size_t chunk_sz, chunk_slot, num_chunks;
    if (sz_in_mem > 0) {
        /* release shared memory chunks */
        chunk_sz = shmem_hdr->chunk_sz;
        chunk_slot = log_offset / chunk_sz;
        num_chunks = bytes_to_chunks(sz_in_mem, chunk_sz);
        chunkmap = log_header_to_chunkmap(shmem_hdr);
        rc = slotmap_release(chunkmap, chunk_slot, num_chunks);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("slotmap_release() for logio shmem failed");
        }
    }
    if (sz_in_spill > 0) {
        /* release spill chunks */
        spill_hdr = (log_header*) ctx->spill_hdr;
        chunk_sz = spill_hdr->chunk_sz;
        chunk_slot = spill_offset / chunk_sz;
        num_chunks = bytes_to_chunks(sz_in_spill, chunk_sz);
        chunkmap = log_header_to_chunkmap(spill_hdr);
        rc = slotmap_release(chunkmap, chunk_slot, num_chunks);
        if (rc != UNIFYFS_SUCCESS) {
            LOGERR("slotmap_release() for logio spill failed");
        }
    }
    return rc;
}

/* Read data from logio context */
int unifyfs_logio_read(logio_context* ctx,
                       const off_t log_offset,
                       const size_t nbytes,
                       char* obuf,
                       size_t* obytes)
{
    if ((NULL == ctx) ||
        ((nbytes > 0) && (NULL == obuf))) {
        return EINVAL;
    }

    if (NULL != obytes) {
        *obytes = 0;
    }

    if (0 == nbytes) {
        LOGWARN("zero bytes read from log!");
        return UNIFYFS_SUCCESS;
    }

    log_header* shmem_hdr = NULL;
    off_t mem_size = 0;
    if (NULL != ctx->shmem) {
        shmem_hdr = (log_header*) ctx->shmem->addr;
        mem_size = (off_t) shmem_hdr->data_sz;
    }

    /* prepare read operations based on log offset */
    size_t nread = 0;
    size_t sz_in_mem = 0;
    size_t sz_in_spill = 0;
    off_t spill_offset = 0;
    get_log_sizes(log_offset, nbytes, mem_size,
                  &sz_in_mem, &sz_in_spill, &spill_offset);

    /* do reads */
    int err_rc = 0;
    if (sz_in_mem > 0) {
        /* read data from shared memory */
        char* shmem_data = (char*)(ctx->shmem->addr) + shmem_hdr->data_offset;
        char* log_ptr = shmem_data + log_offset;
        memcpy(obuf, log_ptr, sz_in_mem);
        nread += sz_in_mem;
    }
    if (sz_in_spill > 0) {
        log_header* spill_hdr = (log_header*) ctx->spill_hdr;
        spill_offset += spill_hdr->data_offset;

        /* read data from spillover file */
        ssize_t rc = pread(ctx->spill_fd, (obuf + sz_in_mem),
                           sz_in_spill, spill_offset);
        if (-1 == rc) {
            err_rc = errno;
            LOGERR("pread(spillfile) failed: %s", strerror(err_rc));
        } else {
            nread += rc;
        }
    }

    if (nread) {
        if (nread != nbytes) {
            LOGDBG("partial log read: %zu of %zu bytes", nread, nbytes);
        }
        if (NULL != obytes) {
            *obytes = nread;
        }
        return UNIFYFS_SUCCESS;
    } else {
        return err_rc;
    }
}

/* Write data to logio context */
int unifyfs_logio_write(logio_context* ctx,
                        const off_t log_offset,
                        const size_t nbytes,
                        const char* ibuf,
                        size_t* obytes)
{
    if ((NULL == ctx) ||
        ((nbytes > 0) && (NULL == ibuf))) {
        return EINVAL;
    }

    if (NULL != obytes) {
        *obytes = 0;
    }

    if (0 == nbytes) {
        LOGWARN("zero bytes written to log!");
        return UNIFYFS_SUCCESS;
    }

    log_header* shmem_hdr = NULL;
    off_t mem_size = 0;
    if (NULL != ctx->shmem) {
        shmem_hdr = (log_header*) ctx->shmem->addr;
        mem_size = (off_t) shmem_hdr->data_sz;
    }

    /* prepare write operations based on log offset */
    size_t nwrite = 0;
    size_t sz_in_mem = 0;
    size_t sz_in_spill = 0;
    off_t spill_offset = 0;
    get_log_sizes(log_offset, nbytes, mem_size,
                  &sz_in_mem, &sz_in_spill, &spill_offset);

    /* do writes */
    int err_rc = 0;
    if (sz_in_mem > 0) {
        /* write data to shared memory */
        char* shmem_data = (char*)(ctx->shmem->addr) + shmem_hdr->data_offset;
        char* log_ptr = shmem_data + log_offset;
        memcpy(log_ptr, ibuf, sz_in_mem);
        nwrite += sz_in_mem;
    }
    if (sz_in_spill > 0) {
        log_header* spill_hdr = (log_header*) ctx->spill_hdr;
        spill_offset += spill_hdr->data_offset;

        /* write data to spillover file */
        ssize_t rc = pwrite(ctx->spill_fd, (ibuf + sz_in_mem),
                            sz_in_spill, spill_offset);
        if (-1 == rc) {
            err_rc = errno;
            LOGERR("pwrite(spillfile) failed: %s", strerror(err_rc));
        } else {
            nwrite += rc;
        }
    }

    /* update output parameter if we wrote anything */
    if (nwrite) {
        if (nwrite != nbytes) {
            LOGDBG("partial log write: %zu of %zu bytes", nwrite, nbytes);
        }

        if (NULL != obytes) {
            /* obytes is set to the number of bytes actually written */
            *obytes = nwrite;
        }
        return UNIFYFS_SUCCESS;
    } else {
        return err_rc;
    }
}

/* Sync any spill data to disk for given logio context */
int unifyfs_logio_sync(logio_context* ctx)
{
    if ((ctx->spill_sz) && (-1 != ctx->spill_fd)) {
        /* fsync spill file */
        int rc = fsync(ctx->spill_fd);
        if (rc != 0) {
            int err = errno;
            LOGERR("Failed to fsync logio spill file (errno=%s)",
                   strerror(err));
            return err;
        }
    }
    return UNIFYFS_SUCCESS;
}

/* Get the shmem and spill data sizes */
int unifyfs_logio_get_sizes(logio_context* ctx,
                            off_t* shmem_sz,
                            off_t* spill_sz)
{
    if (NULL == ctx) {
        return EINVAL;
    }

    if (NULL != shmem_sz) {
        *shmem_sz = 0;
        if (NULL != ctx->shmem) {
            log_header* shmem_hdr = (log_header*) ctx->shmem->addr;
            *shmem_sz = (off_t) shmem_hdr->data_sz;
        }
    }

    if (NULL != spill_sz) {
        *spill_sz = 0;
        if (NULL != ctx->spill_hdr) {
            log_header* spill_hdr = (log_header*) ctx->spill_hdr;
            *spill_sz = (off_t) spill_hdr->data_sz;
        }
    }

    return UNIFYFS_SUCCESS;
}
