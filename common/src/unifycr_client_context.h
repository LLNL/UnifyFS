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

/*
 * Structure that contains the client side context information.
 */
struct unifycr_client_context_s {
    int app_id;
    int local_rank_index;
    int dbg_rank;
    int num_procs_per_node;
    int req_buf_sz; // should probably be a size_t
    int recv_buf_sz; // should probably be a size_t
    long superblock_sz; // should probably be a size_t
    long meta_offset; // off_t?
    long meta_size; // size_t?
    long fmeta_offset; // off_t?
    long fmeta_size; // size_t?
    long data_offset; // off_t
    long data_size; // size_t;
    char external_spill_dir[UNIFYCR_MAX_FILENAME];
} typedef unifycr_client_context_t;


int unifycr_pack_client_context(unifycr_client_context_t ctx, char* buffer,
                                off_t* offset);

int unifycr_unpack_client_context(char* buffer, off_t* offset,
                                  unifycr_client_context_t* ctx);

#endif // UNIFYCR_CLIENT_CONTEXT_H
