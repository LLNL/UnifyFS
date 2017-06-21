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

#ifndef      __LOCAL_CLIENT_H
#define      __LOCAL_CLIENT_H

#include "messages.h"

struct mdhim_rm_t *local_client_put(struct mdhim_t *md, struct mdhim_putm_t *pm);
struct mdhim_rm_t *local_client_bput(struct mdhim_t *md, struct mdhim_bputm_t *bpm);
struct mdhim_bgetrm_t *local_client_bget(struct mdhim_t *md, struct mdhim_bgetm_t *bgm);
struct mdhim_bgetrm_t *local_client_bget_op(struct mdhim_t *md, struct mdhim_getm_t *gm);
struct mdhim_rm_t *local_client_commit(struct mdhim_t *md, struct mdhim_basem_t *cm);
struct mdhim_rm_t *local_client_delete(struct mdhim_t *md, struct mdhim_delm_t *dm);
struct mdhim_rm_t *local_client_bdelete(struct mdhim_t *md, struct mdhim_bdelm_t *dm);
void local_client_close(struct mdhim_t *md, struct mdhim_basem_t *cm);

#endif
