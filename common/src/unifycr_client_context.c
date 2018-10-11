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

#include "unifycr_const.h"
#include "unifycr_client_context.h"

#include "string.h"
int unifycr_pack_client_context(unifycr_client_context_t ctx, char *buffer,
                                off_t *offset)
{
    int rc = UNIFYCR_SUCCESS;

    memcpy(buffer + sizeof(int), &ctx.app_id, sizeof(int));
    *offset += sizeof(ctx.app_id);

    memcpy(buffer + 2 * sizeof(int),
           &ctx.local_rank_index, sizeof(int));
    *offset += sizeof(ctx.local_rank_index);

    memcpy(buffer + 3 * sizeof(int),
           &ctx.dbg_rank, sizeof(int)); /*add debug info*/
    *offset += sizeof(ctx.dbg_rank);

    memcpy(buffer + 4 * sizeof(int), &ctx.num_procs_per_node, sizeof(int));
    *offset += sizeof(ctx.num_procs_per_node);

    memcpy(buffer + 5 * sizeof(int), &ctx.req_buf_sz, sizeof(int));
    *offset += sizeof(ctx.req_buf_sz);

    memcpy(buffer + 6 * sizeof(int), &ctx.recv_buf_sz, sizeof(int));
    *offset += sizeof(ctx.recv_buf_sz);

    memcpy(buffer + 7 * sizeof(int), &ctx.superblock_sz, sizeof(long));
    *offset += sizeof(ctx.superblock_sz);

    memcpy(buffer + 7 * sizeof(int) + sizeof(long),
           &ctx.meta_offset, sizeof(long));
    *offset += sizeof(ctx.meta_offset);

    memcpy(buffer + 7 * sizeof(int) + 2 * sizeof(long),
           &ctx.meta_size, sizeof(long));
    *offset += sizeof(ctx.meta_size);

    memcpy(buffer + 7 * sizeof(int) + 3 * sizeof(long),
           &ctx.fmeta_offset, sizeof(long));
    *offset += sizeof(ctx.fmeta_offset);

    memcpy(buffer + 7 * sizeof(int) + 4 * sizeof(long),
           &ctx.fmeta_size, sizeof(long));
    *offset += sizeof(ctx.fmeta_size);

    memcpy(buffer + 7 * sizeof(int) + 5 * sizeof(long),
           &ctx.data_offset, sizeof(long));
    *offset += sizeof(ctx.data_offset);

    memcpy(buffer + 7 * sizeof(int) + 6 * sizeof(long),
           &ctx.data_size, sizeof(long));
    *offset += sizeof(ctx.data_size);

    memcpy(buffer + 7 * sizeof(int) + 7 * sizeof(long),
           ctx.external_spill_dir, UNIFYCR_MAX_FILENAME);
    *offset += UNIFYCR_MAX_FILENAME;

    return rc;
}

int unifycr_unpack_client_context(char *buffer, off_t *offset,
                                  unifycr_client_context_t *ctx)
{
    int rc = UNIFYCR_SUCCESS;

    ctx->app_id = *(int *)(buffer + *offset);
    *offset += sizeof(ctx->app_id);

    ctx->local_rank_index = *(int *)(buffer + *offset);
    *offset += sizeof(ctx->local_rank_index);

    ctx->dbg_rank = *(int *)(buffer + *offset);
    *offset += sizeof(ctx->dbg_rank);

    ctx->num_procs_per_node = *(int *)(buffer + *offset);
    *offset += sizeof(ctx->num_procs_per_node);

    ctx->req_buf_sz = *(int *)(buffer + *offset);
    *offset += sizeof(ctx->req_buf_sz);

    ctx->recv_buf_sz = *(int *)(buffer + *offset);
    *offset += sizeof(ctx->recv_buf_sz);

    ctx->superblock_sz = *(long *)(buffer + *offset);
    *offset += sizeof(ctx->superblock_sz);

    ctx->meta_offset = *(long *)(buffer + *offset);
    *offset += sizeof(ctx->meta_offset);

    ctx->meta_size = *(long *)(buffer + *offset);
    *offset += sizeof(ctx->meta_size);

    ctx->fmeta_offset = *(long *)(buffer + *offset);
    *offset += sizeof(ctx->fmeta_offset);

    ctx->fmeta_size = *(long *)(buffer + *offset);
    *offset += sizeof(ctx->fmeta_size);

    ctx->data_offset = *(long *)(buffer + *offset);
    *offset += sizeof(ctx->data_offset);

    ctx->data_size = *(long *)(buffer + *offset);
    *offset += sizeof(ctx->data_size);

    // TODO: determain if a pointer to the string will
    // be fine, since it will be copied
    strcpy(ctx->external_spill_dir, buffer + *offset);
    offset += UNIFYCR_MAX_FILENAME;

    return rc;
}
