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

/**
 * @file unifycr_metadata.h
 * @brief API to store metadata in a KV-Store
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

/** prefix for the manifest file name */
#define MANIFEST_FILE_NAME "mdhim_manifest_"

/**
 * Key for a file extent
 */
typedef struct {
    /** file id */
    unsigned long fid;
    /** offset */
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

/**
 * Value for a file extent
 */
typedef struct {
    /** delegator_id */
    unsigned long delegator_id;
    /** length */
    unsigned long len;
    /** address of the extent*/
    unsigned long addr;
    /** app and rank id  */
    unsigned long app_rank_id; /*include both app and rank id*/
} unifycr_val_t;

/**
 * key-value tuple for a file extent
 */
typedef struct {
    /** key */
    unifycr_key_t key;
    /** value */
    unifycr_val_t val;
} unifycr_keyval_t;

/**
 * Client read request
 */
typedef struct {
    /** file id */
    int fid;
    /** file offset */
    long offset;
    /** length of the chunk */
    long length;
} cli_req_t;

/**
 * Shut down the KV-Store
 */
int meta_finalize(void);

void debug_log_key_val(const char* ctx,
                       unifycr_key_t *key,
                       unifycr_val_t *val);

int meta_sanitize();
/**
 * Initialize the KV-Store
 *
 * @param[in] cfg UnifyCR configuration
 */
int meta_init_store(unifycr_cfg_t *cfg);

#if 0
// Not sure if this is the right place for the print functions.
// They are currently not used and probably broken.
// We might want to remove them.
/**
 *
 */
void print_bget_indices(int app_id, int cli_id,
                        send_msg_t *index_set, int tot_num);
int meta_process_fsync(int sock_id);
int meta_batch_get(int app_id, int client_id, int thrd_id, int dbg_rank,
                   shm_meta_t *meta_reqs, size_t req_cnt,
                   msg_meta_t *del_req_set);
int meta_init_indices();
int meta_free_indices();

/**
 *
 */
void print_fsync_indices(unifycr_key_t **unifycr_keys,
                         unifycr_val_t **unifycr_vals, long num_entries);
#endif

/**
 * Store a File attribute to the KV-Store.
 *
 * @param[in] *ptr_attr_val
 * @return UNIFYCR_SUCCESS on success
 */
int unifycr_set_file_attribute(unifycr_file_attr_t *ptr_attr_val);

/**
 * Store File attributes to the KV-Store.
 *
 * @param[in] num_entries number of key value pairs to store
 * @param[in] keys array storing the keys
 * @param[in] key_lens array with the length of the elements in \p keys
 * @param[in] vals array with the values
 * @param[in] val_lens array with the length of the elements in \p vals
 */
int unifycr_set_file_attributes(int num_entries,
                                fattr_key_t **keys, int *key_lens,
                                unifycr_file_attr_t **vals, int *val_lens);

/**
 * Retrieve a File attribute from the KV-Store.
 *
 * @param [in] gfid
 * @param[out] *ptr_attr_val
 * @return UNIFYCR_SUCCESS on success
 */
int unifycr_get_file_attribute(int gfid,
                               unifycr_file_attr_t *ptr_attr_val);

/**
 * Store File extents in the KV-Store.
 *
 * @param [in] num_entries number of key value pairs to store
 * @param[in] keys array storing the keys
 * @param[in] key_lens array with the length of the elements in \p keys
 * @param[in] vals array with the values
 * @param[in] val_lens array with the length of the elements in \p vals
 * @return UNIFYCR_SUCCESS on success
 */
int unifycr_set_file_extents(int num_entries, unifycr_key_t **keys,
                             int *key_lens, unifycr_val_t **vals,
                             int *val_lens);

/**
 * Retrieve File extents from the KV-Store.
 *
 * @param[in] num_keys number of keys
 * @param[in] keys array of keys to retrieve the values for
 * @param[in] key_lens array with the length of the key in \p keys
 * @param[out] num_values number of values in the keyval array
 * @param[out] keyval array containing the key-value tuples found
 * @return UNIFYCR_SUCCESS on success
 */
int unifycr_get_file_extents(int num_keys,
                             unifycr_key_t *keys, int *key_lens,
                             int *num_values, unifycr_keyval_t **keyval);

#endif
