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

#ifndef UNIFYFS_CONST_H
#define UNIFYFS_CONST_H

/* --------------------- RETURN CODES --------------------- */

#include "unifyfs_rc.h"


/* --------------------- INT CONSTANTS --------------------- */

// Byte counts
#define KIB 1024
#define MIB 1048576
#define GIB 1073741824

// General
#define UNIFYFS_MAX_FILENAME KIB
#define UNIFYFS_MAX_HOSTNAME 64

// Client
#define UNIFYFS_CLIENT_MAX_FILES 128
#define UNIFYFS_CLIENT_STREAM_BUFSIZE MIB
#define UNIFYFS_CLIENT_WRITE_INDEX_SIZE (20 * MIB)
#define UNIFYFS_CLIENT_MAX_READ_COUNT 1000     /* max # active read requests */
#define UNIFYFS_CLIENT_READ_TIMEOUT_SECONDS 60
#define UNIFYFS_CLIENT_MAX_ACTIVE_REQUESTS 256 /* max concurrent client reqs */

// Log-based I/O Default Values
#define UNIFYFS_LOGIO_CHUNK_SIZE (4 * MIB)
#define UNIFYFS_LOGIO_SHMEM_SIZE (256 * MIB)
#define UNIFYFS_LOGIO_SPILL_SIZE (4 * GIB)

// Margo Default Values
#define UNIFYFS_MARGO_POOL_SZ 4
#define UNIFYFS_MARGO_CLIENT_SERVER_TIMEOUT_MSEC  5000  /*  5.0 sec */
#define UNIFYFS_MARGO_SERVER_SERVER_TIMEOUT_MSEC 15000  /* 15.0 sec */

// Metadata Default Values
#define UNIFYFS_META_DEFAULT_SLICE_SZ MIB    /* data slice size for metadata */

// Server
#define UNIFYFS_SERVER_MAX_BULK_TX_SIZE (8 * MIB) /* to-server transmit size */
#define UNIFYFS_SERVER_MAX_DATA_TX_SIZE (4 * MIB) /* to-client transmit size */
#define UNIFYFS_SERVER_MAX_NUM_APPS 64   /* max # apps/mountpoints supported */
#define UNIFYFS_SERVER_MAX_APP_CLIENTS 256  /* max # clients per application */
#define UNIFYFS_SERVER_MAX_READS 2048   /* max # server read reqs per reqmgr */

// Utilities
#define UNIFYFS_DEFAULT_INIT_TIMEOUT 120    /* server init timeout (seconds) */


/* --------------------- STRING CONSTANTS --------------------- */

// Server
#define UNIFYFS_SERVER_PID_FILENAME "unifyfsd.pids"

// Utilities
#define UNIFYFS_STAGE_STATUS_FILENAME "unifyfs-stage.status"


#endif // UNIFYFS_CONST_H

