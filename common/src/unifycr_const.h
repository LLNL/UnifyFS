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

#include <fcntl.h>
#include <limits.h>

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
#define MAX_META_PER_SEND (512*KIB)
#define MAX_FILE_CNT_PER_NODE KIB

// Request Manager
#define RECV_BUF_CNT 1
#define RECV_BUF_LEN (MIB + 128*KIB)
#define REQ_BUF_LEN (128*MIB + 4*KIB + 128*KIB)
#define SHM_WAIT_INTERVAL 10

// Service Manager
#define MIN_SLEEP_INTERVAL 10
#define SLEEP_INTERVAL 100 /* unit: us */
#define SLEEP_SLICE_PER_UNIT 10
#define READ_BLOCK_SIZE MIB
#define SEND_BLOCK_SIZE (MIB + 128*KIB)
#define READ_BUF_SZ GIB
#define LARGE_BURSTY_DATA (500 * MIB)
#define MAX_BURSTY_INTERVAL 10000 /* unit: us */

// Request and Service Managers, Command Handler
#define MAX_NUM_CLIENTS 64 /* app processes per server */
#define CLI_DATA_TAG 5001
#define SER_DATA_TAG 6001

// Client and Command Handler
#define CMD_BUF_SIZE (2 * KIB)
#define MMAP_OPEN_FLAG (O_RDWR|O_CREAT)
#define MMAP_OPEN_MODE 00777

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
#define UNIFYCR_MAX_SPLIT_CNT MIB
#define UNIFYCR_MAX_READ_CNT MIB

// Following are not currently used:
// #define ACK_MSG_SZ 8
// #define C_CLI_SEND_BUF_SIZE 1048576
// #define C_CLI_RECV_BUF_SIZE 1048576
// #define DATA_BUF_LEN (1048576 + 4096 + 131072)
// #define IP_STR_LEN 20
// #define MAX_CQ_SIZE 2*NUM_CLI
// #define MAX_NUM_CONNS 10
// #define NUM_MSG 32768 /* number of messages one server sends to each of another server */
// #define NUM_OF_READ_TASKS 65536
// #define PORT_STR_LEN 10
// #define SH_BUF_SIZE 1048576
// #define SERVICE_MEM_POOL_SIZE 2147483648 /* buffer size on the service manager for read clustering and pipelining */

/* ****************** Metadata/MDHIM Default Values ******************** */
#define META_DEFAULT_DB_PATH /tmp
#define META_DEFAULT_DB_NAME unifycr_db
#define META_DEFAULT_SERVER_RATIO 1
#define META_DEFAULT_RANGE_SZ MIB

#endif


