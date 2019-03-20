/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017-2019, UT-Battelle, LLC.
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

/**
 * Key for a file extent
 */
typedef struct {
    /** global file id */
    int fid;
    /** logical file offset */
    size_t offset;
} unifycr_key_t;

#define UNIFYCR_KEY_SZ (sizeof(unifycr_key_t))
#define UNIFYCR_KEY_FID(keyp) (((unifycr_key_t*)keyp)->fid)
#define UNIFYCR_KEY_OFF(keyp) (((unifycr_key_t*)keyp)->offset)

typedef struct {
    size_t addr;      /* data offset in server */
    size_t len;       /* length of data at addr */
    int delegator_id; /* rank of server where data lives */
    int app_id;       /* application id in server */
    int rank;         /* client id in server */
} unifycr_val_t;

#define UNIFYCR_VAL_SZ (sizeof(unifycr_val_t))
#define UNIFYCR_VAL_ADDR(valp) (((unifycr_val_t*)valp)->addr)
#define UNIFYCR_VAL_LEN(valp) (((unifycr_val_t*)valp)->len)

/**
 * key-value tuple for a file extent
 */
typedef struct {
    /** key */
    unifycr_key_t key;
    /** value */
    unifycr_val_t val;
} unifycr_keyval_t;

int unifycr_key_compare(unifycr_key_t* a, unifycr_key_t* b);

void debug_log_key_val(const char* ctx,
                       unifycr_key_t* key,
                       unifycr_val_t* val);

int meta_sanitize(void);
int meta_init_store(unifycr_cfg_t* cfg);
void print_bget_indices(int app_id, int client_id,
                        send_msg_t* index_set, int tot_num);

int meta_init_indices(void);
void meta_free_indices(void);
void print_fsync_indices(unifycr_key_t** unifycr_keys,
                         unifycr_val_t** unifycr_vals, size_t num_entries);

/**
 * Retrieve a File attribute from the KV-Store.
 *
 * @param [in] gfid
 * @param[out] *ptr_attr_val
 * @return UNIFYCR_SUCCESS on success
 */
int unifycr_get_file_attribute(int gfid,
                               unifycr_file_attr_t* ptr_attr_val);

/**
 * Store a File attribute to the KV-Store.
 *
 * @param[in] *ptr_attr_val
 * @return UNIFYCR_SUCCESS on success
 */
int unifycr_set_file_attribute(unifycr_file_attr_t* ptr_attr_val);

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
                                fattr_key_t** keys, int* key_lens,
                                unifycr_file_attr_t** vals, int* val_lens);

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
                             unifycr_key_t** keys, int* key_lens,
                             int* num_values, unifycr_keyval_t** keyval);

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
int unifycr_set_file_extents(int num_entries, unifycr_key_t** keys,
                             int* key_lens, unifycr_val_t** vals,
                             int* val_lens);

#endif
