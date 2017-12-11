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

#ifndef UNIFYCR_CMD_HANDLER_H
#define UNIFYCR_CMD_HANDLER_H
int delegator_handle_command(char *ptr_cmd, int sock_id);
int sync_with_client(char *buf, int client_id);
int open_log_file(app_config_t *app_config,\
		int app_id, int client_id);
int attach_to_shm(app_config_t *app_config,\
		int app_id, int sock_id);
int pack_ack_msg(char *ptr_cmd, int cmd,\
		int rc, void *val,\
		int val_len);
int unifycr_broadcast_exit(int sock_id);
int sync_with_client(char *cmd_buf, int sock_id);
int open_log_file(app_config_t *app_config,\
		int app_id, int sock_id);
int attach_to_shm(app_config_t *app_config,\
		int app_id, int sock_id);
int pack_ack_msg(char *ptr_cmd, int cmd,\
		int rc, void *val, int val_len);
int unifycr_broadcast_exit(int sock_id);
#endif
