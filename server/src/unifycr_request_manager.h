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

#ifndef UNIFYCR_REQUEST_MANAGER_H
#define UNIFYCR_REQUEST_MANAGER_H

#include "unifycr_global.h"

/* Request Manager pthread main */
void* rm_delegate_request_thread(void* arg);

/* functions called by rpc handlers to assign work
 * to request manager threads */
int rm_cmd_mread(int app_id, int client_id, int gfid,
                 size_t req_num, void* reqbuf);

int rm_cmd_read(int app_id, int client_id, int gfid,
                size_t offset, size_t length);

int rm_cmd_filesize(int app_id, int client_id, int gfid, size_t* outsize);

/* function called by main thread to instruct
 * resource manager thread to exit,
 * returns UNIFYCR_SUCCESS on success */
int rm_cmd_exit(thrd_ctrl_t* thrd_ctrl);

/*
 * synchronize all the indices and file attributes
 * to the key-value store
 * @param sock_id: the connection id in poll_set of the delegator
 * @return success/error code
 */
int rm_cmd_fsync(int app_id, int client_side_id, int gfid);

#endif
