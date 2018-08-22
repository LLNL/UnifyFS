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

/* ********************** ULFS ERROR CODES ************************ */
#define ULFS_SUCCESS        0
#define ULFS_ERROR_BASE     -2000
#define ULFS_ERROR_DBG      ( ULFS_ERROR_BASE - 1 )
#define ULFS_ERROR_MDHIM    ( ULFS_ERROR_BASE - 2 )
#define ULFS_ERROR_THRD ( ULFS_ERROR_BASE - 3 )
#define ULFS_ERROR_GENERAL ( ULFS_ERROR_BASE - 4 )
#define ULFS_ERROR_NOENV ( ULFS_ERROR_BASE - 5 )
#define ULFS_ERROR_NOMEM ( ULFS_ERROR_BASE - 6 )
#define ULFS_ERROR_TIMEOUT ( ULFS_ERROR_BASE - 7 )
#define ULFS_ERROR_EXIT ( ULFS_ERROR_BASE - 8 )
#define ULFS_ERROR_POLL ( ULFS_ERROR_BASE - 9)
#define ULFS_ERROR_SOCKET ( ULFS_ERROR_BASE - 10)
#define ULFS_ERROR_SHMEM ( ULFS_ERROR_BASE - 11)
#define ULFS_ERROR_MDINIT ( ULFS_ERROR_BASE - 12)
#define ULFS_EXIT ( ULFS_ERROR_BASE - 13)
#define ULFS_ERROR_THRDINIT ( ULFS_ERROR_BASE - 14)
#define ULFS_ERROR_FILE  ( ULFS_ERROR_BASE - 15)
#define ULFS_ERROR_SOCKET_FD_EXCEED (ULFS_ERROR_BASE - 16)
#define ULFS_ERROR_SOCK_DISCONNECT (ULFS_ERROR_BASE - 17)
#define ULFS_ERROR_SOCK_CMD (ULFS_ERROR_BASE - 18)
#define ULFS_ERROR_SOCK_LISTEN (ULFS_ERROR_BASE - 19)
#define ULFS_ERROR_RM_INIT (ULFS_ERROR_BASE - 20)
#define ULFS_ERROR_RM_RECV (ULFS_ERROR_BASE - 21)



#define ULFS_ERROR_ADDR ( ULFS_ERROR_BASE+10 )
#define ULFS_ERROR_ROUTE ( ULFS_ERROR_BASE+11 )
#define ULFS_ERROR_EVENT_UNKNOWN ( ULFS_ERROR_BASE+12 )
#define ULFS_ERROR_CONTEXT  ( ULFS_ERROR_BASE+13 )
#define ULFS_ERROR_QP   ( ULFS_ERROR_BASE+14 )
#define ULFS_ERROR_REGMEM   ( ULFS_ERROR_BASE+15 )
#define ULFS_ERROR_POSTRECV ( ULFS_ERROR_BASE+17 )
#define ULFS_ERROR_PD ( ULFS_ERROR_BASE+18 )
#define ULFS_ERROR_CHANNEL ( ULFS_ERROR_BASE+19 )
#define ULFS_ERROR_CQ   ( ULFS_ERROR_BASE+20 )
#define ULFS_ERROR_CONNECT ( ULFS_ERROR_BASE+21 )
#define ULFS_ERROR_POSTSEND ( ULFS_ERROR_BASE+22 )
#define ULFS_ERROR_ACCEPT ( ULFS_ERROR_BASE+23 )
#define ULFS_ERROR_WC ( ULFS_ERROR_BASE+24 )
#define ULFS_ERROR_APPCONFIG (ULFS_ERROR_BASE+25)
#define ULFS_ERROR_ARRAY_EXCEED (ULFS_ERROR_BASE+26)
#define ULFS_ERROR_READ (ULFS_ERROR_BASE + 27)
#define ULFS_ERROR_SEND (ULFS_ERROR_BASE + 28)
#define ULFS_ERROR_RECV (ULFS_ERROR_BASE + 29)
#define ULFS_SOCKETFD_EXCEED (ULFS_ERROR_BASE + 30)
#define ULFS_SOCK_DISCONNECT (ULFS_ERROR_BASE + 31)
#define ULFS_SOCK_LISTEN (ULFS_ERROR_BASE + 32)
#define ULFS_SOCK_OTHER (ULFS_ERROR_BASE + 33)
#define ULFS_ERROR_PIPE (ULFS_ERROR_BASE + 34)
#define ULFS_ERROR_WRITE (ULFS_ERROR_BASE + 35)
/* ********************** ULFS ERROR STRING ************************ */
#define ULFS_STR_ERROR_DBG "Fail to initialize debug file."
#define ULFS_STR_ERROR_MDHIM "Error in operating MDHIM."
#define ULFS_STR_ERROR_THRD "Fail to init/finalize thread."
#define ULFS_STR_ERROR_GENERAL "General system call error."
#define ULFS_STR_ERROR_MDINIT "Init MDHIM error."


#define ULFS_STR_ERROR_NOENV "Environment variable is not defined."
#define ULFS_STR_ERROR_NOMEM "Error in memory allocation/free."
#define ULFS_STR_ERROR_TIMEOUT "Error time out."
#define ULFS_STR_ERROR_EXIT  "Error remote exit."
#define ULFS_STR_ERROR_POLL "Error poll."
#define ULFS_STR_ERROR_SOCKT "Error creating/open socket."
#define ULFS_STR_ERROR_SHMEM "Error operation on shared memory."
#define ULFS_STR_ERROR_THRDINIT "Thread init failure."
#define ULFS_STR_ERROR_GETEVENT "Get event error."
#define ULFS_STR_ERROR_ADDR "Fail to parse ip address and port."
#define ULFS_STR_ERROR_ROUTE "Fail to resolve route."
#define ULFS_STR_ERROR_EVENT_UNKNOWN "Unknown event detected."
#define ULFS_STR_ERROR_CONTEXT  "Wrong connection context."
#define ULFS_STR_ERROR_QP "Error creating or destroying QP."
#define ULFS_STR_ERROR_REGMEM "Registering or de-registering memory failure."
#define ULFS_STR_ERROR_POSTRECV "Failed in posting receive operation."
#define ULFS_STR_ERROR_PD   "Error creating PD."
#define ULFS_STR_ERROR_CHANNEL "Error creating completion channel."
#define ULFS_STR_ERROR_CQ   "Error creating or polling completion queue."
#define ULFS_STR_ERROR_CONNECT  "Error in RDMA connect or disconnect."
#define ULFS_STR_ERROR_POSTSEND "Failed in posting send operation."
#define ULFS_STR_ERROR_ACCEPT   "Failed to accept rdma connection."
#define ULFS_STR_ERROR_WC   "Write completion with error."
#define ULFS_STR_ERROR_DEFAULT "Undefined error."
#define ULFS_STR_ERROR_FILE "File operation error."
#define ULFS_STR_ERROR_SOCKET_FD_EXCEED "Number of client-side connections exceeds the max value."
#define ULFS_STR_ERROR_SOCK_DISCONNECT "Remote client disconnected."
#define ULFS_STR_ERROR_SOCK_CMD "Unknown exception on the remote client."
#define ULFS_STR_ERROR_SOCK_LISTEN "Exception on listening socket."
#define ULFS_STR_ERROR_APPCONFIG "Fail to initialize application configuration."
#define ULFS_STR_ERROR_ARRAY_EXCEED "Array out of bound."
#define ULFS_STR_ERROR_RM_INIT "Fail to init request manager."
#define ULFS_STR_ERROR_RM_RECV "Fail to receive data in request manager"
#define ULFS_STR_ERROR_READ "Disk read error"
#define ULFS_STR_ERROR_SEND "Send error"
#define ULFS_STR_ERROR_RECV "Receive error"
#define ULFS_STR_ERROR_WRITE "Write error"
/* ********************** ULFS STRING CONSTANT ************************ */
#define DBG_FNAME "/ccs/home/ogm/ssd/serverdbg"
//#define DBG_FNAME "/p/lscratchf/wang86/stand_unifycr_0217_dbg/UnifyCR_Server/logdir/serverdbg"
//#define DBG_FNAME "/p/lscratchf/wang86/stand_unifycr/UnifyCR_Server/logdir/server"
#define DEFAULT_INTERFACE "ib0"

/* ********************** ULFS INT CONSTANT ************************ */
#define PORT_STR_LEN 10
#define IP_STR_LEN 20
#define MAX_NUM_CONNS 10
#define C_CLI_SEND_BUF_SIZE 1048576
#define C_CLI_RECV_BUF_SIZE 1048576
#define SH_BUF_SIZE 1048576
#define MAX_NUM_CLIENTS 64 /*number of application processes each server node takes charge of*/
#define NUM_MSG 32768 /*number of messages one server sends to each of another server*/
#define MAX_CQ_SIZE 2*NUM_CLI
#define ULFS_MAX_FILENAME 128

#define GEN_STR_LEN 1024
#define MAX_PATH_LEN 100
#define ACK_MSG_SZ 8
//#define DATA_BUF_LEN 1048576+4096+131072
//#define REQ_BUF_LEN 10485760+4096+131072
#define RECV_BUF_LEN 1048576+131072
#define REQ_BUF_LEN 8*16*1048576+4096+131072
#define CLI_DATA_TAG 5001
#define SER_DATA_TAG 6001
#define MMAP_OPEN_FLAG O_RDWR|O_CREAT
#define MMAP_OPEN_MODE 00777
#define MAX_BURSTY_INTERVAL 10000 /*unit: us*/
#define SLEEP_INTERVAL 100 /* how long to wait before
                              the next test*/
#define SHM_WAIT_INTERVAL 10
#define SLEEP_SLICE_PER_UNIT 10

#define LARGE_BURSTY_DATA 500*1048576
#define MIN_SLEEP_INTERVAL 10

// need to set to a large number
#define MAX_META_PER_SEND 524288
#define MAX_FILE_CNT_PER_NODE 1024
#define ULFS_MAX_FILENAME 128

#define NUM_OF_READ_TASKS 65536
#define READ_BLOCK_SIZE 1048576

#define SEND_BLOCK_SIZE 1048576 + 131072

#define READ_BUF_SZ 1048576*1024

#define RECV_BUF_CNT 1

// the buffer size on the service manager for read clustering and pipelining
#define SERVICE_MEM_POOL_SIZE 2147483648
/* ********************** ULFS Constant Functions ************************ */
const char *ULFS_str_errno(int rc);
#endif



