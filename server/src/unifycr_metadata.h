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

#include <assert.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "mdhim.h"
#include "indexes.h"
#include "arraylist.h"
#include "unifycr_const.h"
#include "unifycr_meta.h"
#include "unifycr_global.h"


#define MANIFEST_FILE_NAME "mdhim_manifest_"

typedef struct {
    unsigned long fid;
    unsigned long offset;
} unifycr_key_t;

#define UNIFYCR_KEY_SZ (sizeof(unifycr_key_t))

#define UNIFYCR_KEY_FID(keyp) (((unifycr_key_t *)keyp)->fid)
#define UNIFYCR_KEY_OFF(keyp) (((unifycr_key_t *)keyp)->offset)

int unifycr_key_compare(unifycr_key_t *a, unifycr_key_t *b);

typedef struct {
    unsigned long addr;
    unsigned long len;
    int delegator_id;
    int app_id;
    int rank;
} unifycr_val_t;

#define UNIFYCR_VAL_SZ (sizeof(unifycr_val_t))

#define UNIFYCR_VAL_ADDR(valp) (((unifycr_val_t *)valp)->addr)
#define UNIFYCR_VAL_LEN(valp) (((unifycr_val_t *)valp)->len)

typedef struct {
    unifycr_key_t key;
    unifycr_val_t val;
} unifycr_keyval_t;

typedef struct {
    int fid;
    long offset;
    long length;
} cli_req_t;

extern arraylist_t *ulfs_keys;
extern arraylist_t *ulfs_vals;
extern arraylist_t *ulfs_metas;

void debug_log_key_val(const char* ctx,
                       unifycr_key_t *key,
                       unifycr_val_t *val);

int meta_sanitize();
int meta_init_store(unifycr_cfg_t *cfg);
void print_bget_indices(int app_id, int cli_id,
                        send_msg_t *index_set, int tot_num);
int meta_process_fsync(int sock_id);
int meta_batch_get(int app_id, int client_id, int thrd_id, int dbg_rank,
                   shm_meta_t *meta_reqs, size_t req_cnt,
                   msg_meta_t *del_req_set);
int meta_init_indices();
int meta_free_indices();
void print_fsync_indices(unifycr_key_t **unifycr_keys,
                         unifycr_val_t **unifycr_vals, long num_entries);
int meta_process_attr_set(char *ptr_cmd, int sock_id);

//int meta_process_attr_get(char *buf, int sock_id,
//                          unifycr_file_attr_t *ptr_attr_val);
int meta_process_attr_get(fattr_key_t *_fattr_key,
                          unifycr_file_attr_t *ptr_attr_val);

/*
 *
 */
int unifycr_set_file_attribute(unifycr_file_attr_t *ptr_attr_val);

/*
 *
 */
int unifycr_get_file_attribute(int gfid,
                               unifycr_file_attr_t *ptr_attr_val);

/*
 *
 */
int unifycr_get_fvals(int num_keys, unifycr_key_t *keys,
                      int *unifycr_key_lens, int *num_values,
                      unifycr_keyval_t **keyval);

/*
 *
 */
int unifycr_set_fvals(unsigned int num_extents,
                      unifycr_index_t *extents);

/*
 *
 */
int unifycr_bulk_set_file_extents(unsigned int num_files,
                                  const char ** const filename,
                                  unsigned int *num_extents,
                                  unifycr_index_t **extents);

/*
 *
 */
int unifycr_get_file_extents(const char * const filename,
                             unsigned int *num_extents,
                             unifycr_index_t **extents);

#endif
