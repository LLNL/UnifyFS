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
#include "unifyfs_transfer.h"


/* service manager pthread routine */
void* service_manager_thread(void* ctx);

/* initialize and launch service manager */
int svcmgr_init(void);

/* join service manager thread and cleanup its state */
int svcmgr_fini(void);

/**
 * @brief submit a server rpc request to the service manager thread.
 *
 * @param req   pointer to server rpc request struct
 *
 * @return UNIFYFS_SUCCESS, or error code
 */
int sm_submit_service_request(server_rpc_req_t* req);

/* submit a transfer request to the service manager thread */
int sm_submit_transfer_request(transfer_thread_args* tta);

/* tell service manager thread transfer has completed */
int sm_complete_transfer_request(transfer_thread_args* tta);

/* decode and issue chunk reads contained in message buffer */
int sm_issue_chunk_reads(int src_rank,
                         int src_app_id,
                         int src_client_id,
                         int src_req_id,
                         int num_chks,
                         size_t total_data_sz,
                         char* msg_buf);

/* File service operations */

int sm_laminate(int gfid);

int sm_get_fileattr(int gfid,
                    unifyfs_file_attr_t* attrs);

int sm_set_fileattr(int gfid,
                    int file_op,
                    unifyfs_file_attr_t* attrs);

int sm_add_extents(int gfid,
                   size_t num_extents,
                   extent_metadata* extents);

int sm_find_extents(int gfid,
                    size_t num_extents,
                    unifyfs_extent_t* extents,
                    unsigned int* out_num_chunks,
                    chunk_read_req_t** out_chunks,
                    int* full_coverage);

int sm_transfer(int client_server,
                int client_app,
                int client_id,
                int transfer_id,
                int gfid,
                int transfer_mode,
                const char* dest_file,
                void* bcast_coll);

int sm_truncate(int gfid,
                size_t filesize);


#endif // UNIFYFS_SERVICE_MANAGER_H
