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
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

/* ********************** ULFS ERROR CODES ************************ */
#include "err_enumerator.h"
#define ULFS_SUCCESS ((int)UNIFYCR_SUCCESS)

/* ********************** ULFS STRING CONSTANT ************************ */
#define DEFAULT_INTERFACE "ib0"

/* ********************** ULFS INT CONSTANT ************************ */

// Generic
#define GEN_STR_LEN 1024
#define ULFS_MAX_FILENAME 256
#define MAX_PATH_LEN 1024

// Metadata
#define MAX_META_PER_SEND 524288
#define MAX_FILE_CNT_PER_NODE 1024

// Request Manager
#define RECV_BUF_CNT 1
#define RECV_BUF_LEN (1048576 + 131072)
#define REQ_BUF_LEN (128*1048576 + 4096 + 131072)
#define SHM_WAIT_INTERVAL 10

// Service Manager
#define MIN_SLEEP_INTERVAL 10
#define SLEEP_INTERVAL 100 /* unit: us */
#define SLEEP_SLICE_PER_UNIT 10
#define READ_BLOCK_SIZE 1048576
#define SEND_BLOCK_SIZE (1048576 + 131072)
#define READ_BUF_SZ (1048576 * 1024)
#define LARGE_BURSTY_DATA (500 * 1048576)
#define MAX_BURSTY_INTERVAL 10000 /* unit: us */

// Request and Service Managers, Command Handler
#define MAX_NUM_CLIENTS 64 /* app processes per server */
#define CLI_DATA_TAG 5001
#define SER_DATA_TAG 6001

// Client and Command Handler
#define MMAP_OPEN_FLAG (O_RDWR|O_CREAT)
#define MMAP_OPEN_MODE 00777

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
#define META_DEFAULT_RANGE_SZ 1048576

#endif


