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

#include <config.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "arraylist.h"
#include "log.h"
#include "unifycr_const.h"
#include "unifycr_global.h"
#include "unifycr_pmix.h"
#include "unifycr_setup.h"
#include "unifycr_sock.h"

int server_rank_idx;

int server_sockfd;
int client_sockfd;
int num_fds = 0;

//int thrd_pipe_fd[2] = {0};

struct pollfd poll_set[MAX_NUM_CLIENTS];
struct sockaddr_un server_address;
char cmd_buf[MAX_NUM_CLIENTS][CMD_BUF_SIZE];
char ack_buf[MAX_NUM_CLIENTS][CMD_BUF_SIZE];
int ack_msg[3] = {0};

int detached_sock_id = -1;
int cur_sock_id = 1;

/**
* initialize the listening socket on this delegator
* @return success/error code
*/
int sock_init_server(int local_rank_idx)
{
    int rc;
    char sock_path[UNIFYCR_MAX_FILENAME];

    server_rank_idx = local_rank_idx;
    snprintf(sock_path, sizeof(sock_path), "%s%d",
             SOCKET_PATH, server_rank_idx);

    server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sun_family = AF_UNIX;
    strcpy(server_address.sun_path, sock_path);
    int server_len = sizeof(server_address);
    unlink(sock_path);

    rc = bind(server_sockfd, (struct sockaddr *)&server_address,
              (socklen_t)server_len);
    if (rc != 0) {
        close(server_sockfd);
        return -1;
    }

    rc = listen(server_sockfd, MAX_NUM_CLIENTS);
    if (rc != 0) {
        close(server_sockfd);
        return -1;
    }
    LOG(LOG_DBG, "domain socket path is %s", sock_path);

    int flag = fcntl(server_sockfd, F_GETFL);
    fcntl(server_sockfd, F_SETFL, flag | O_NONBLOCK);
    poll_set[0].fd = server_sockfd; //add
    poll_set[0].events = POLLIN | POLLHUP;
    poll_set[0].revents = 0;
    num_fds++;
	printf("completed sock init server\n");

#ifdef HAVE_PMIX_H
    // publish domain socket path
    unifycr_pmix_publish(pmix_key_unifycrd_socket, sock_path);
#endif

    return 0;
}

int sock_add(int fd)
{
	printf("sock_adding fd: %d\n");
    if (num_fds == MAX_NUM_CLIENTS) {
        return -1;
    }
    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
    poll_set[num_fds].fd = fd;
    poll_set[num_fds].events = POLLIN | POLLHUP;
    poll_set[num_fds].revents = 0;
    num_fds++;
    return 0;
}

void sock_reset()
{
    int i;

    for (i = 0; i < num_fds; i++) {
        poll_set[i].events = POLLIN | POLLHUP;
        poll_set[i].revents = 0;
    }
}

int sock_remove(int idx)
{
    /* in this case, we simply disable the disconnected
     * file descriptor. */
    poll_set[idx].fd = -1;
    return 0;
}

/*
 * send command to the client to let the client digest the
 * data in the shared receive buffer
 * @param: sock_id: socket index in poll_set
 * @param: cmd: command type
 *
 * */
int sock_notify_cli(int sock_id, int cmd)
{
    memset(ack_buf[sock_id], 0, sizeof(ack_buf[sock_id]));

	printf("sock notifying fd: %d\n", client_sockfd);

    memcpy(ack_buf[sock_id], &cmd, sizeof(int));
    int rc = write(client_sockfd,
                   ack_buf[sock_id], sizeof(ack_buf[sock_id]));

    if (rc < 0) {
        return (int)UNIFYCR_ERROR_WRITE;
    }
    return ULFS_SUCCESS;
}


/*
 * wait for the client-side command
 * */

int sock_wait_cli_cmd()
{
    int rc, i;

    sock_reset();
    rc = poll(poll_set, num_fds, -1);
    if (rc <= 0) {
        return (int)UNIFYCR_ERROR_POLL;
    } else {
		printf("in wait_cli_cmd\n");
        for (i = 0; i < num_fds; i++) {
            if (poll_set[i].fd != -1 && poll_set[i].revents != 0) {
                if (i == 0 && poll_set[i].revents == POLLIN) {
                    int client_len = sizeof(struct sockaddr_un);

                    struct sockaddr_un client_address;
                    client_sockfd = accept(server_sockfd,
                                               (struct sockaddr *)&client_address,
                                               (socklen_t *)&client_len);
					printf("calling sock_add for sock_id: %d\n", i);
                    rc = sock_add(client_sockfd);
                    if (rc < 0) {
                        return (int)UNIFYCR_ERROR_SOCKET_FD_EXCEED;
                    } else {
                        cur_sock_id = i;
                        return ULFS_SUCCESS;
                    }
                } else if (i != 0 && poll_set[i].revents == POLLIN) {
                    int bytes_read = read(poll_set[i].fd,
                                          cmd_buf[i], CMD_BUF_SIZE);
                    if (bytes_read == 0) {
                        sock_remove(i);
                        detached_sock_id = i;
                        return (int)UNIFYCR_ERROR_SOCK_DISCONNECT;
                    }
                    cur_sock_id = i;
                    return ULFS_SUCCESS;
                } else {
                    if (i == 0) {
                        return (int)UNIFYCR_ERROR_SOCK_LISTEN;
                    } else {
                        detached_sock_id = i;
                        if (i != 0 && poll_set[i].revents == POLLHUP) {
                            sock_remove(i);
                            return (int)UNIFYCR_ERROR_SOCK_DISCONNECT;
                        } else {
                            sock_remove(i);
                            return (int)UNIFYCR_ERROR_SOCK_OTHER;

                        }
                    }
                }
            }
        }
    }

    return ULFS_SUCCESS;

}

int sock_ack_cli(int sock_id, int ret_sz)
{
    int rc = write(poll_set[sock_id].fd,
                   ack_buf[sock_id], ret_sz);
    if (rc < 0) {
        return (int)UNIFYCR_ERROR_SOCK_OTHER;
    }
    return ULFS_SUCCESS;
}

int sock_handle_error(int sock_error_no)
{
    return ULFS_SUCCESS;
}

int sock_get_error_id()
{
    return detached_sock_id;
}

char *sock_get_cmd_buf(int sock_id)
{
    return cmd_buf[sock_id];
}

char *sock_get_ack_buf(int sock_id)
{
    return (char *)ack_buf[sock_id];
}

int sock_get_id()
{
    return 0;
}

int sock_sanitize()
{
    int i;
    char tmp_str[UNIFYCR_MAX_FILENAME] = {0};

    for (i = 0; i < num_fds; i++) {
        if (poll_set[i].fd > 0) {
            close(poll_set[i].fd);
        }
    }

    snprintf(tmp_str, sizeof(tmp_str), "%s%d",
             SOCKET_PATH, server_rank_idx);
    unlink(tmp_str);
    return 0;
}
