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

int sock_init_server(void);
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
