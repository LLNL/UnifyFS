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

#ifndef UNIFYCR_METADATA_H
#define UNIFYCR_METADATA_H

#include "unifycr_const.h"
#include "unifycr_global.h"
#include "unifycr_meta.h"

#define MANIFEST_FILE_NAME "mdhim_manifest_"

typedef struct {
    unsigned long fid;
    unsigned long offset;
} unifycr_key_t;

#define UNIFYCR_KEY_SZ (sizeof(unifycr_key_t))

#define UNIFYCR_KEY_FID(keyp) (((unifycr_key_t*)keyp)->fid)
#define UNIFYCR_KEY_OFF(keyp) (((unifycr_key_t*)keyp)->offset)

int unifycr_key_compare(unifycr_key_t* a, unifycr_key_t* b);

typedef struct {
    unsigned long addr;
    unsigned long len;
    int delegator_id;
    int app_id;
    int rank;
} unifycr_val_t;

#define UNIFYCR_VAL_SZ (sizeof(unifycr_val_t))

#define UNIFYCR_VAL_ADDR(valp) (((unifycr_val_t*)valp)->addr)
#define UNIFYCR_VAL_LEN(valp) (((unifycr_val_t*)valp)->len)

void debug_log_key_val(const char* ctx,
                       unifycr_key_t* key,
                       unifycr_val_t* val);

int meta_sanitize();
int meta_init_store(unifycr_cfg_t* cfg);
void print_bget_indices(int app_id, int client_id,
                        send_msg_t* index_set, int tot_num);
int meta_process_fsync(int app_id, int client_id, int gfid);
int meta_read_get(int app_id, int client_id, int thrd_id, int dbg_rank,
                  int gfid, size_t offset, size_t length,
                  msg_meta_t* del_req_set);
int meta_batch_get(int app_id, int client_id, int thrd_id, int dbg_rank,
                   void* reqbuf, size_t req_cnt,
                   msg_meta_t* del_req_set);
int meta_init_indices();
int meta_free_indices();
void print_fsync_indices(unifycr_key_t** unifycr_keys,
                         unifycr_val_t** unifycr_vals, size_t num_entries);

int meta_process_attr_get(unifycr_file_attr_t* ptr_attr_val);
int meta_process_attr_set(unifycr_file_attr_t* ptr_attr_val);

#endif
