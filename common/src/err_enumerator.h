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

/*  Copyright (c) 2018 - Michael J. Brim
 *
 *  Enumerator is part of https://github.com/MichaelBrim/tedium
 *
 *  MIT License - See LICENSE.tedium
 */

#ifndef _UNIFYCR_ERROR_ENUMERATOR_H_
#define _UNIFYCR_ERROR_ENUMERATOR_H_

/**
 * @brief enumerator list expanded many times with varied ENUMITEM() definitions
 *
 * @param item name
 * @param item short description
 */
#define UNIFYCR_ERROR_ENUMERATOR                                        \
    ENUMITEM(DBG, "Failed to open/close debug file.")                   \
    ENUMITEM(MDHIM, "Error in operating MDHIM.")                        \
    ENUMITEM(GENERAL, "General system call error.")                     \
    ENUMITEM(NOENV, "Environment variable is not defined.")             \
    ENUMITEM(NOMEM, "Error in memory allocation/free.")                 \
    ENUMITEM(TIMEOUT, "Error - timed out.")                             \
    ENUMITEM(EXIT, "Error - remote peer exited.")                       \
    ENUMITEM(POLL, "Error on poll.")                                    \
    ENUMITEM(SHMEM, "Error on shared memory attach.")                   \
    ENUMITEM(MDINIT, "Init MDHIM error.")                               \
    ENUMITEM(THRDINIT, "Thread initialization failure.")                \
    ENUMITEM(FILE, "File operation error.")                             \
    ENUMITEM(SOCKET, "Error creating/open socket.")                     \
    ENUMITEM(SOCKET_FD_EXCEED, "Number of connections exceeds the max value.") \
    ENUMITEM(SOCK_DISCONNECT, "Remote peer disconnected.")              \
    ENUMITEM(SOCK_CMD, "Unknown exception on the remote peer.")         \
    ENUMITEM(SOCK_LISTEN, "Exception on listening socket.")             \
    ENUMITEM(SOCK_OTHER, "Unknown socket error.")                       \
    ENUMITEM(RM_INIT, "Failed to init request manager.")                \
    ENUMITEM(RM_RECV, "Fail to receive data in request manager.")       \
    ENUMITEM(ADDR, "Failed to parse IP address and port.")              \
    ENUMITEM(ROUTE, "Failed to resolve route.")                         \
    ENUMITEM(EVENT_UNKNOWN, "Unknown event detected.")                  \
    ENUMITEM(CONTEXT, "Wrong connection context.")                      \
    ENUMITEM(QP, "Error creating or destroying QP.")                    \
    ENUMITEM(REGMEM, "Memory [de]registration failure.")                \
    ENUMITEM(POSTRECV, "Failed to post receive operation.")             \
    ENUMITEM(PD, "Error creating PD.")                                  \
    ENUMITEM(CHANNEL, "Error creating completion channel.")             \
    ENUMITEM(CQ, "Error creating or polling completion queue.")         \
    ENUMITEM(CONNECT, "Error in RDMA connect or disconnect.")           \
    ENUMITEM(POSTSEND, "Failed to post send operation.")                \
    ENUMITEM(ACCEPT, "Failed to accept RDMA connection.")               \
    ENUMITEM(WC, "Write completion with error.")                        \
    ENUMITEM(APPCONFIG, "Failed to initialize application configuration.") \
    ENUMITEM(ARRAY_EXCEED, "Array access out of bounds.")               \
    ENUMITEM(READ, "Read error.")                                       \
    ENUMITEM(WRITE, "Write error.")                                     \
    ENUMITEM(SEND, "Send error.")                                       \
    ENUMITEM(RECV, "Receive error.")                                    \
    ENUMITEM(PIPE, "Pipe error.")                                       \


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief supported consistency models
 */
typedef enum {
    UNIFYCR_ERROR_INVALID = -1,
    UNIFYCR_SUCCESS = 0,
#define ENUMITEM(name, desc)                    \
        UNIFYCR_ERROR_ ## name,
    UNIFYCR_ERROR_ENUMERATOR
#undef ENUMITEM
    UNIFYCR_ERROR_MAX
} unifycr_error_e;

/**
 * @brief get C-string for given consistency model enum value
 */
const char *unifycr_error_enum_str(unifycr_error_e e);

/**
 * @brief get description for given consistency model enum value
 */
const char *unifycr_error_enum_description(unifycr_error_e e);

/**
 * @brief check validity of given consistency model enum value
 */
int check_valid_unifycr_error_enum(unifycr_error_e e);

/**
 * @brief get enum value for given consistency model C-string
 */
unifycr_error_e unifycr_error_enum_from_str(const char *s);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* UNIFYCR_ERROR_ENUMERATOR_H */
