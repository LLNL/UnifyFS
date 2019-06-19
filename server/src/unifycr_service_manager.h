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

#ifndef UNIFYCR_SERVICE_MANAGER_H
#define UNIFYCR_SERVICE_MANAGER_H

#include "unifycr_global.h"

/* service manager pthread routine */
void* sm_service_reads(void* ctx);

/* initialize and launch service manager */
int svcmgr_init(void);

/* join service manager thread and cleanup its state */
int svcmgr_fini(void);

/* process service request message */
int sm_decode_msg(char* msg_buf);

/* decode and issue chunk reads contained in message buffer */
int sm_issue_chunk_reads(int src_rank,
                         int src_app_id,
                         int src_client_id,
                         int src_req_id,
                         int num_chks,
                         char* msg_buf);

/* MARGO SERVER-SERVER RPC INVOCATION FUNCTIONS */
int invoke_chunk_read_response_rpc(remote_chunk_reads_t* rcr);

#endif // UNIFYCR_SERVICE_MANAGER_H
