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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
* Please read https://github.com/llnl/burstfs/LICENSE for full license text.
*/

#ifndef BURSTFS_SOCK_H
#define BURSTFS_SOCK_H
#include <poll.h>
#include  "burstfs_const.h"

#define DEF_SOCK_PATH "/tmp/burstfs_server_sock"
#define BURSTFS_SOCK_TIMEOUT 5000

extern int server_sockfd;
extern struct pollfd poll_set[MAX_NUM_CLIENTS];

int sock_init_server(int local_rank_idx);
int sock_add(int fd);
void sock_reset();
int sock_wait_cli_cmd();
char *sock_get_cmd_buf(int sock_id);
int sock_handle_error(int sock_error_no);
int sock_get_id();
int sock_get_error_id();
int sock_ack_cli(int sock_id, int ret_sz);
int sock_sanitize();
char *sock_get_ack_buf(int sock_id);
int sock_remove(int idx);
int sock_notify_cli(int sock_id, int cmd);
char *sock_get_cmd_buf(int sock_id);
#endif
