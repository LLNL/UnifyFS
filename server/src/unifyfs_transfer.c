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


#include "unifyfs_inode.h"
#include "unifyfs_group_rpc.h"
#include "unifyfs_service_manager.h"
#include "unifyfs_transfer.h"
#include <fcntl.h>

/* maximum length in bytes for pwrite() transfers */
#ifndef UNIFYFS_TRANSFER_MAX_WRITE
#define UNIFYFS_TRANSFER_MAX_WRITE (16 * 1048576) // 16 MiB
#endif

/* maximum memory allocation for temporary transfer data copies */
#ifndef UNIFYFS_TRANSFER_MAX_BUFFER
#define UNIFYFS_TRANSFER_MAX_BUFFER (512 * 1048576) // 512 MiB
#endif

typedef struct transfer_chunk {
    char*  chunk_data;
    size_t chunk_sz;
    off_t  file_offset;
} transfer_chunk;

/* write a transfer_chunk to given file descriptor */
static int write_transfer_chunk(int fd,
                                transfer_chunk* chk)
{
    // TODO: use lio_listio to submit all writes at once?
    size_t max_write = UNIFYFS_TRANSFER_MAX_WRITE;
    size_t n_write = 0;
    size_t n_remain = chk->chunk_sz;
    do {
        char* data = chk->chunk_data + n_write;
        off_t off  = chk->file_offset + (off_t)n_write;
        size_t n_bytes = (n_remain > max_write ? max_write : n_remain);
        ssize_t szrc = pwrite(fd, data, n_bytes, off);
        if (-1 == szrc) {
            int err = errno;
            if ((err != EINTR) && (err != EAGAIN)) {
                LOGERR("pwrite(dst_fd=%d, sz=%zu) failed: %s",
                       fd, n_remain, strerror(err));
                return err;
            }

        } else {
            n_write += szrc;
            n_remain -= szrc;
        }
    } while (n_remain);

    return UNIFYFS_SUCCESS;
}

static int read_local_extent(struct extent_tree_node* ext,
                             transfer_chunk* chk)
{
    int ret = UNIFYFS_SUCCESS;

    char* buf = chk->chunk_data;
    chk->chunk_sz = extent_tree_node_length(ext);
    chk->file_offset = extent_tree_node_offset(ext);

    /* read data from client log */
    app_client* app_clnt = NULL;
    int app_id = ext->app_id;
    int cli_id = ext->cli_id;
    off_t log_offset = (off_t) ext->pos;
    app_clnt = get_app_client(app_id, cli_id);
    if (NULL != app_clnt) {
        logio_context* logio_ctx = app_clnt->state.logio_ctx;
        if (NULL != logio_ctx) {
            size_t nread = 0;
            int rc = unifyfs_logio_read(logio_ctx, log_offset, chk->chunk_sz,
                                        buf, &nread);
            if (rc != UNIFYFS_SUCCESS) {
                ret = rc;
            }
        } else {
            LOGERR("app client [%d:%d] has NULL logio context",
                   app_id, cli_id);
            ret = EINVAL;
        }
    } else {
        LOGERR("failed to get application client [%d:%d] state",
               app_id, cli_id);
        ret = EINVAL;
    }

    return ret;
}

/* find local extents for the given gfid and initialize transfer helper
 * thread state */
int create_local_transfers(int gfid,
                           const char* dest_file,
                           transfer_thread_args* tta)
{
    if ((NULL == dest_file) || (NULL == tta)) {
        return EINVAL;
    }

    size_t n_extents = 0;
    struct extent_tree_node* extents = NULL;
    int rc = unifyfs_inode_get_extents(gfid, &n_extents, &extents);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("failed to get extents from inode for gfid=%d", gfid);
        return rc;
    } else if (n_extents == 0) {
        return UNIFYFS_SUCCESS;
    }

    /* determine local extents */
    struct extent_tree_node* ext;
    size_t n_local_extents = 0;
    size_t total_local_data_sz = 0;
    for (size_t i = 0; i < n_extents; i++) {
        ext = extents + i;
        if (glb_pmi_rank == ext->svr_rank) {
            total_local_data_sz += extent_tree_node_length(ext);
            n_local_extents++;
        }
    }

    /* make an array of local extents */
    struct extent_tree_node* local_extents = (struct extent_tree_node*)
        calloc(n_local_extents, sizeof(struct extent_tree_node));
    if (NULL == local_extents) {
        LOGERR("failed to allocate local extents for gfid=%d", gfid);
        free(extents);
        return ENOMEM;
    }

    struct extent_tree_node* dst_ext;
    size_t ext_ndx = 0;
    for (size_t i = 0; i < n_extents; i++) {
        ext = extents + i;
        if (glb_pmi_rank == ext->svr_rank) {
            dst_ext = local_extents + ext_ndx;
            ext_ndx++;
            memcpy(dst_ext, ext, sizeof(*ext));
        }
    }

    free(extents);

    tta->dst_file      = strdup(dest_file);
    tta->gfid          = gfid;
    tta->local_extents = local_extents;
    tta->n_extents     = n_local_extents;
    tta->local_data_sz = total_local_data_sz;

    return UNIFYFS_SUCCESS;
}

void release_transfer_thread_args(transfer_thread_args* tta)
{
    if (NULL != tta) {
        if (NULL != tta->local_extents) {
            free(tta->local_extents);
        }
        if (NULL != tta->dst_file) {
            free((char*)(tta->dst_file));
        }
        free(tta);
    }
}

void* transfer_helper_thread(void* arg)
{
    transfer_thread_args* tta = (transfer_thread_args*)arg;
    assert(NULL != arg);

    int rc;
    int ret = UNIFYFS_SUCCESS;
    char* data_copy_buf = NULL;
    transfer_chunk* chunks = NULL;
    struct extent_tree_node* ext;
    transfer_chunk* chk;

    LOGDBG("I am transfer thread for gfid=%d file=%s",
           tta->gfid, tta->dst_file);

    /* open destination file (create if it doesn't exist) */
    int flags = O_CREAT | O_WRONLY;
    int mode = 0640;
    int fd = open(tta->dst_file, flags, mode);
    if (fd == -1) {
        int err = errno;
        LOGERR("failed to open(%s) - %s", tta->dst_file, strerror(err));
        tta->status = err;
        return arg;
    }

    /* get number of local extents and their total size */
    size_t total_local_data_sz = tta->local_data_sz;
    size_t n_extents = tta->n_extents;

    /* allocate transfer_chunk array */
    chunks = calloc(n_extents, sizeof(transfer_chunk));
    if (NULL == chunks) {
        LOGERR("failed to allocate transfer chunk state");
        ret = ENOMEM;
        goto transfer_cleanup;
    }

    /* allocate copy buffer for chunk data */
    size_t max_buffer = UNIFYFS_TRANSFER_MAX_BUFFER;
    size_t buf_sz = max_buffer;
    if (total_local_data_sz <= max_buffer) {
        buf_sz = total_local_data_sz;
    } else {
        /* make sure longest extent will fit in copy buffer */
        for (size_t i = 0; i < n_extents; i++) {
            ext = tta->local_extents + i;
            size_t ext_sz = extent_tree_node_length(ext);
            if (ext_sz > buf_sz) {
                buf_sz = ext_sz;
            }
        }
    }
    data_copy_buf = malloc(buf_sz);
    if (NULL == data_copy_buf) {
        LOGERR("failed to allocate transfer copy buffer");
        ret = ENOMEM;
        goto transfer_cleanup;
    }

    /* read local data for all extents and write it to corresponding
     * offsets within destination file. */
    size_t ext_ndx = 0; /* tracks extent array index */
    size_t chk_ndx = 0; /* tracks chunk array index */
    do {
        size_t begin_chk_ndx = chk_ndx;
        size_t copy_sz = 0;
        for (size_t i = ext_ndx; i < n_extents; i++) {
            ext = tta->local_extents + i;
            size_t ext_sz = extent_tree_node_length(ext);
            if ((copy_sz + ext_sz) <= buf_sz) {
                chk = chunks + chk_ndx;
                chk_ndx++;
                ext_ndx++;

                chk->chunk_data = data_copy_buf + copy_sz;
                copy_sz += ext_sz;

                rc = read_local_extent(ext, chk);
                if (rc != UNIFYFS_SUCCESS) {
                    LOGERR("failed to copy extent[%zu] data for gfid=%d",
                           i, tta->gfid);
                    ret = rc;
                    goto transfer_cleanup;
                }
            } else {
                /* no room left in copy buffer */
                break;
            }
        }

        /* write out data chunks for extents processed in this iteration */
        for (size_t i = begin_chk_ndx; i < chk_ndx; i++) {
            chk = chunks + i;
            rc = write_transfer_chunk(fd, chk);
            if (rc != UNIFYFS_SUCCESS) {
                LOGERR("write_transfer_chunk(dst=%s, chk=%zu) failed",
                       tta->dst_file, i);
                ret = rc;
                goto transfer_cleanup;
            }
        }
    } while (ext_ndx < n_extents);

transfer_cleanup:

    close(fd);
    tta->status = ret;

    if (NULL != tta->bcast_req) {
        /* create a ULT to finish broadcast operation */
        collective_set_local_retval(tta->bcast_req->coll, ret);
        invoke_bcast_progress_rpc(tta->bcast_req->coll);
    }

    LOGDBG("signaling transfer completion");

    rc = sm_complete_transfer_request(tta);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("sm_complete_transfer_request() failed");
    }

    /* release allocated memory */
    if (NULL != data_copy_buf) {
        free(data_copy_buf);
    }
    if (NULL != chunks) {
        free(chunks);
    }

    return arg;
}
