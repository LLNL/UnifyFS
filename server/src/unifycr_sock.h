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

#ifndef UNIFYCR_SOCK_H
#define UNIFYCR_SOCK_H

#include <poll.h>
#include "unifycr_const.h"

extern int server_sockfd;
extern int client_sockfd;
extern struct pollfd poll_set[MAX_NUM_CLIENTS];

int sock_init_server(int srvr_id);
void sock_sanitize_client(int client_idx);
int sock_sanitize(void);
int sock_add(int fd);
int sock_remove(int client_idx);
void sock_reset(void);
int sock_wait_cmd(int poll_timeout);

#if 0 // DEPRECATED DUE TO MARGO
int sock_handle_error(int sock_error_no);
int sock_get_id(void);
int sock_get_error_id(void);
char* sock_get_cmd_buf(int client_idx);
char* sock_get_ack_buf(int client_idx);
int sock_ack_client(int client_idx, int ret_sz);
int sock_notify_client(int client_idx, int cmd);
#endif // DEPRECATED DUE TO MARGO

#endif
