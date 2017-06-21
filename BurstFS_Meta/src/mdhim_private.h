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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
* Please read https://github.com/llnl/burstfs/LICENSE for full license text.
*/

/*
*
* Copyright (c) 2014, Los Alamos National Laboratory
*	All rights reserved.
*
*/

#include "mdhim.h"

struct mdhim_rm_t *_put_record(struct mdhim_t *md, struct index_t *index, 
			       void *key, int key_len, 
			       void *value, int value_len);
struct mdhim_brm_t *_create_brm(struct mdhim_rm_t *rm);
void _concat_brm(struct mdhim_brm_t *head, struct mdhim_brm_t *addition);
struct mdhim_brm_t *_bput_records(struct mdhim_t *md, struct index_t *index, 
				  void **keys, int *key_lens, 
				  void **values, int *value_lens, int num_records);
struct mdhim_bgetrm_t *_bget_records(struct mdhim_t *md, struct index_t *index,
				     void **keys, int *key_lens, 
				     int num_keys, int num_records, int op);

struct mdhim_bgetrm_t *_bget_range_records(struct mdhim_t *md, struct index_t *index,
				     void *start_key, void *end_key, int key_len);

struct mdhim_brm_t *_bdel_records(struct mdhim_t *md, struct index_t *index,
				  void **keys, int *key_lens,
				  int num_records);
