/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
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
 *
 * Copyright (c) 2014, Los Alamos National Laboratory
 *	All rights reserved.
 *
 */

#ifndef __LEVELDB_H
#define __LEVELDB_H

#ifndef LEVELDB_SUPPORT
#include <rocksdb/c.h>
#else
#include <leveldb/c.h>
#endif

#include "mdhim.h"
#include "partitioner.h"
#include "data_store.h"

#include "unifyfs_metadata_mdhim.h"

/* Function pointer for comparator in C */
typedef int (*mdhim_store_cmp_fn_t)(void* arg, const char* a, size_t alen,
                                    const char* b, size_t blen);

struct mdhim_leveldb_t {
	leveldb_t *db;
	leveldb_options_t *options;
	leveldb_comparator_t* cmp;
	leveldb_filterpolicy_t *filter;
	leveldb_cache_t *cache;
	leveldb_env_t *env;
	leveldb_writeoptions_t *write_options;
	leveldb_readoptions_t *read_options;
	mdhim_store_cmp_fn_t compare;
};

int mdhim_leveldb_open(void **dbh, void **dbs, char *path,
                       int flags, int key_type,
                       struct mdhim_options_t	*opts);
int mdhim_leveldb_put(void *dbh, void *key, int key_len,
                      void *data, int32_t data_len);
int mdhim_leveldb_get(void *dbh, void *key, int key_len,
                      void **data, int32_t *data_len);
int mdhim_leveldb_get_next(void *dbh, void **key, int *key_len, 
                           void **data, int32_t *data_len);
int mdhim_leveldb_get_prev(void *dbh, void **key, int *key_len, 
                           void **data, int32_t *data_len);
int mdhim_leveldb_close(void *dbh, void *dbs);
int mdhim_leveldb_del(void *dbh, void *key, int key_len);
int mdhim_leveldb_commit(void *dbh);
int mdhim_leveldb_batch_put(void *dbh, void **key, int32_t *key_lens, 
                            void **data, int32_t *data_lens, int num_records);
int mdhim_leveldb_batch_next(void *dbh, char **key,
                             int *key_len, char **data, int32_t *data_len,
                             int tot_records, int *num_records);
int leveldb_batch_ranges(void *dbh, char **key, int32_t *key_len,
                         char ***out_key, int32_t **out_key_len,
                         char ***out_val, int32_t **out_val_len,
                         int num_ranges, int *out_records_cnt);
int leveldb_process_range(leveldb_iterator_t *iter,
                          char *start_key, char *end_key, int32_t key_len,
                          char ***out_key, int32_t **out_key_len,
                          char ***out_val, int32_t **out_val_len,
                          int *tmp_records_cnt, int *tmp_out_cap);
int handle_next_half(leveldb_iterator_t *iter,
                     char *start_key, char *end_key,
                     char ***out_key, int **out_key_len,
                     char ***out_val, int **out_val_len,
                     int *tmp_records_cnt, int *tmp_out_cap);
int add_kv(char ***out_key, int32_t **out_key_len,
           char ***out_val, int32_t **out_val_len,
           int *tmp_records_cnt, int *tmp_out_cap,
           char *ret_key, char *ret_val,
           size_t key_len, size_t val_len);

#endif
