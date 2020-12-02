/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
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

#ifndef UNIFYFS_SERVICE_MANAGER_H
#define UNIFYFS_SERVICE_MANAGER_H

#include "unifyfs_global.h"

/* service manager pthread routine */
void* service_manager_thread(void* ctx);

/* initialize and launch service manager */
int svcmgr_init(void);

/* join service manager thread and cleanup its state */
int svcmgr_fini(void);

/* decode and issue chunk reads contained in message buffer */
int sm_issue_chunk_reads(int src_rank,
                         int src_app_id,
                         int src_client_id,
                         int src_req_id,
                         int num_chks,
                         char* msg_buf);

/* MARGO SERVER-SERVER RPC INVOCATION FUNCTIONS */
int invoke_chunk_read_response_rpc(server_chunk_reads_t* scr);

#endif // UNIFYFS_SERVICE_MANAGER_H
