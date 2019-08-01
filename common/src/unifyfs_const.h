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

#ifndef UNIFYFS_CONST_H
#define UNIFYFS_CONST_H

/* ********************** ERROR CODES ************************ */
#include "err_enumerator.h"
#define ULFS_SUCCESS ((int)UNIFYFS_SUCCESS)

/* ********************** STRING CONSTANTS ************************ */
#define DEFAULT_INTERFACE "ib0"
#define SOCKET_PATH "/tmp/unifyfs_server_sock"

/* ********************** INT CONSTANTS ************************ */

// Byte counts
#define KIB 1024
#define MIB 1048576
#define GIB 1073741824

// Generic
#define GEN_STR_LEN KIB
#define UNIFYFS_MAX_FILENAME KIB
#define UNIFYFS_MAX_HOSTNAME 64

// Metadata
#define MAX_FILE_CNT_PER_NODE KIB

// Request Manager
#define RECV_BUF_CNT 4               /* number of remote read buffers */
#define SENDRECV_BUF_LEN (8 * MIB)   /* remote read buffer size */
#define MAX_META_PER_SEND (4 * KIB)  /* max read request count per server */
#define REQ_BUF_LEN (MAX_META_PER_SEND * 128) /* read requests (send_msg_t) */
#define SHM_WAIT_INTERVAL 1000       /* unit: ns */
#define RM_MAX_ACTIVE_REQUESTS 64    /* number of concurrent read requests */

// Service Manager
#define LARGE_BURSTY_DATA (512 * MIB)
#define MAX_BURSTY_INTERVAL 10000 /* unit: us */
#define MIN_SLEEP_INTERVAL 10     /* unit: us */
#define SLEEP_INTERVAL 500        /* unit: us */
#define SLEEP_SLICE_PER_UNIT 50   /* unit: us */
#define READ_BLOCK_SIZE MIB
#define READ_BUF_SZ GIB

// Request and Service Managers, Command Handler
#define MAX_NUM_CLIENTS 64 /* app processes per server */

// Client and Command Handler
#define CMD_BUF_SIZE (2 * KIB)

// Client
#define UNIFYFS_MAX_FILES 128
#define UNIFYFS_MAX_FILEDESCS UNIFYFS_MAX_FILES
#define UNIFYFS_STREAM_BUFSIZE MIB
#define UNIFYFS_CHUNK_BITS 24
#define UNIFYFS_CHUNK_MEM (256 * MIB)
#define UNIFYFS_SPILLOVER_SIZE (KIB * MIB)
#define UNIFYFS_SUPERBLOCK_KEY 4321
#define UNIFYFS_SHMEM_REQ_SIZE (MIB*128 + 128*KIB)
#define UNIFYFS_SHMEM_RECV_SIZE (MIB + 128*KIB)
#define UNIFYFS_INDEX_BUF_SIZE  (20 * MIB)
#define UNIFYFS_FATTR_BUF_SIZE MIB
#define UNIFYFS_MAX_READ_CNT KIB

/* max read size = UNIFYFS_MAX_SPLIT_CNT * META_DEFAULT_RANGE_SZ */
#define UNIFYFS_MAX_SPLIT_CNT (4 * KIB)

// Metadata/MDHIM Default Values
#define META_DEFAULT_DB_NAME unifyfs_db
#define META_DEFAULT_SERVER_RATIO 1
#define META_DEFAULT_RANGE_SZ MIB

#endif // UNIFYFS_CONST_H

