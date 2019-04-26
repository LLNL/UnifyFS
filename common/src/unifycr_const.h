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

#ifndef UNIFYCR_CONST_H
#define UNIFYCR_CONST_H

#include <limits.h> // provides HOST_NAME_MAX

/* ********************** ERROR CODES ************************ */
#include "err_enumerator.h"
#define ULFS_SUCCESS ((int)UNIFYCR_SUCCESS)

/* ********************** STRING CONSTANTS ************************ */
#define DEFAULT_INTERFACE "ib0"
#define SOCKET_PATH "/tmp/unifycr_server_sock"

/* ********************** INT CONSTANTS ************************ */

// Byte counts
#define KIB 1024
#define MIB 1048576
#define GIB 1073741824

// Generic
#define GEN_STR_LEN KIB
#define UNIFYCR_MAX_FILENAME KIB

// Metadata
#define MAX_FILE_CNT_PER_NODE KIB

// Request Manager
#define RECV_BUF_CNT 4               /* number of remote read buffers */
#define SENDRECV_BUF_LEN (8 * MIB)   /* remote read buffer size */
#define MAX_META_PER_SEND (64 * KIB) /* max read request count per server */
#define REQ_BUF_LEN (MAX_META_PER_SEND * 128) /* read requests (send_msg_t) */
#define SHM_WAIT_INTERVAL 100 /* unit: ns */

// Service Manager
#define LARGE_BURSTY_DATA (512 * MIB)
#define MAX_BURSTY_INTERVAL 10000 /* unit: us */
#define MIN_SLEEP_INTERVAL 10     /* unit: us */
#define SLEEP_INTERVAL 100        /* unit: us */
#define SLEEP_SLICE_PER_UNIT 10   /* unit: us */
#define READ_BLOCK_SIZE MIB
#define READ_BUF_SZ GIB

// Request and Service Managers, Command Handler
#define MAX_NUM_CLIENTS 64 /* app processes per server */
#define CLI_DATA_TAG 5001
#define SER_DATA_TAG 6001

// Client and Command Handler
#define CMD_BUF_SIZE (2 * KIB)

// Client
#define UNIFYCR_MAX_FILES 128
#define UNIFYCR_MAX_FILEDESCS UNIFYCR_MAX_FILES
#define UNIFYCR_STREAM_BUFSIZE MIB
#define UNIFYCR_CHUNK_BITS 24
#define UNIFYCR_CHUNK_MEM (256 * MIB)
#define UNIFYCR_SPILLOVER_SIZE (KIB * MIB)
#define UNIFYCR_SUPERBLOCK_KEY 4321
#define UNIFYCR_SHMEM_REQ_SIZE (MIB*128 + 128*KIB)
#define UNIFYCR_SHMEM_RECV_SIZE (MIB + 128*KIB)
#define UNIFYCR_INDEX_BUF_SIZE  (20 * MIB)
#define UNIFYCR_FATTR_BUF_SIZE MIB
#define UNIFYCR_MAX_READ_CNT KIB

/* max read size = UNIFYCR_MAX_SPLIT_CNT * META_DEFAULT_RANGE_SZ */
#define UNIFYCR_MAX_SPLIT_CNT (4 * KIB)

// Metadata/MDHIM Default Values
#define META_DEFAULT_DB_NAME unifycr_db
#define META_DEFAULT_SERVER_RATIO 1
#define META_DEFAULT_RANGE_SZ MIB

#endif // UNIFYCR_CONST_H

