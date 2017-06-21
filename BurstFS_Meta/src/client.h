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

#ifndef      __CLIENT_H
#define      __CLIENT_H

#include "messages.h"

struct mdhim_rm_t *client_put(struct mdhim_t *md, struct mdhim_putm_t *pm);
struct mdhim_brm_t *client_bput(struct mdhim_t *md, struct index_t *index, 
				struct mdhim_bputm_t **bpm_list);
struct mdhim_bgetrm_t *client_bget(struct mdhim_t *md, struct index_t *index, 
				   struct mdhim_bgetm_t **bgm_list);
struct mdhim_bgetrm_t *client_bget_op(struct mdhim_t *md, struct mdhim_getm_t *gm);
struct mdhim_rm_t *client_delete(struct mdhim_t *md, struct mdhim_delm_t *dm);
struct mdhim_brm_t *client_bdelete(struct mdhim_t *md, struct index_t *index, 
				   struct mdhim_bdelm_t **bdm_list);

#endif
