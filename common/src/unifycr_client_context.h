/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */

#ifndef UNIFYCR_CLIENT_CONTEXT_H
#define UNIFYCR_CLIENT_CONTEXT_H

#include <stddef.h>
#include <sys/types.h>

#include "unifycr_const.h"

/*
 * Structure that contains the client side context information.
 */
typedef struct unifycr_client_context_s {
    int    app_id;
    int    local_rank_index;
    int    dbg_rank;
    int    num_procs_per_node;
    size_t req_buf_sz;
    size_t recv_buf_sz;
    size_t superblock_sz;
    size_t meta_offset;
    size_t meta_size;
    size_t fmeta_offset;
    size_t fmeta_size;
    size_t data_offset;
    size_t data_size;
    char   external_spill_dir[UNIFYCR_MAX_FILENAME];
} unifycr_client_context_t;

int unifycr_pack_client_context(unifycr_client_context_t *ctx,
                                char *buffer);

int unifycr_unpack_client_context(char *buffer,
                                  unifycr_client_context_t *ctx);

#endif // UNIFYCR_CLIENT_CONTEXT_H
