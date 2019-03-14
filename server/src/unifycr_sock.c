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
#include "unifycr_log.h"
#include "unifycr_const.h"
#include "unifycr_global.h"
#include "unifycr_pmix.h"
#include "unifycr_setup.h"
#include "unifycr_sock.h"

char sock_path[UNIFYCR_MAX_FILENAME];
int server_sockfd = -1;
int num_fds;
struct pollfd poll_set[MAX_NUM_CLIENTS];
struct sockaddr_un server_address;

int detached_sock_idx = -1;
int cur_sock_idx = -1;

char cmd_buf[MAX_NUM_CLIENTS][CMD_BUF_SIZE];
char ack_buf[MAX_NUM_CLIENTS][CMD_BUF_SIZE];
int ack_msg[3] = {0};

/**
* initialize the listening socket on this delegator
* @return success/error code
*/
int sock_init_server(int srvr_id)
{
    int i, rc;

    for (i = 0; i < MAX_NUM_CLIENTS; i++) {
        poll_set[i].fd = -1;
    }

    num_fds = 0;

    snprintf(sock_path, sizeof(sock_path), "%s.%d.%d",
             SOCKET_PATH, getuid(), srvr_id);
    LOGDBG("domain socket path is %s", sock_path);
    unlink(sock_path); // remove domain socket leftover from prior run

    server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sun_family = AF_UNIX;
    strcpy(server_address.sun_path, sock_path);
    rc = bind(server_sockfd, (struct sockaddr*)&server_address,
              (socklen_t)sizeof(server_address));
    if (rc != 0) {
        close(server_sockfd);
        return -1;
    }

    rc = listen(server_sockfd, MAX_NUM_CLIENTS);
    if (rc != 0) {
        close(server_sockfd);
        return -1;
    }

    sock_add(server_sockfd); // puts server fd at index 0 of poll_set
    LOGDBG("completed sock init server");

#ifdef HAVE_PMIX_H
    // publish domain socket path
    unifycr_pmix_publish(pmix_key_unifycrd_socket, sock_path);
#endif

    return 0;
}

void sock_sanitize_client(int client_idx)
{
    /* close socket for this client id
     * and set fd back to -1 */
    if (poll_set[client_idx].fd != -1) {
        close(poll_set[client_idx].fd);
        poll_set[client_idx].fd = -1;
    }
}

int sock_sanitize(void)
{
    int i;
    for (i = 0; i < num_fds; i++) {
        sock_sanitize_client(i);
    }

    if (server_sockfd != -1) {
        server_sockfd = -1;
        unlink(sock_path);
    }

    return 0;
}

int sock_add(int fd)
{
    if (num_fds == MAX_NUM_CLIENTS) {
        LOGERR("exceeded MAX_NUM_CLIENTS");
        return -1;
    }

    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);

    LOGDBG("sock_adding fd: %d", fd);
    poll_set[num_fds].fd = fd;
    poll_set[num_fds].events = POLLIN | POLLHUP;
    poll_set[num_fds].revents = 0;
    num_fds++;
    return 0;
}

void sock_reset(void)
{
    int i;
    cur_sock_idx = -1;
    detached_sock_idx = -1;
    for (i = 0; i < num_fds; i++) {
        poll_set[i].events = POLLIN | POLLHUP;
        poll_set[i].revents = 0;
    }
}

int sock_remove(int client_idx)
{
    /* in this case, we simply disable the disconnected
     * file descriptor. */
    poll_set[client_idx].fd = -1;
    return 0;
}

/*
 * wait for the client-side command
 * */

int sock_wait_cmd(int poll_timeout)
{
    int rc, i, client_fd;

    sock_reset();
    rc = poll(poll_set, num_fds, poll_timeout);
    if (rc < 0) {
        return (int)UNIFYCR_ERROR_POLL;
    } else if (rc == 0) { // timeout
        return (int)UNIFYCR_SUCCESS;
    } else {
        LOGDBG("poll detected socket activity");
        for (i = 0; i < num_fds; i++) {
            if (poll_set[i].fd == -1) {
                continue;
            }
            if (i == 0) { // listening socket
                if (poll_set[i].revents & POLLIN) {
                    int client_len = sizeof(struct sockaddr_un);
                    struct sockaddr_un client_address;
                    client_fd = accept(server_sockfd,
                                       (struct sockaddr*)&client_address,
                                       (socklen_t*)&client_len);
                    LOGDBG("accepted client on socket %d", client_fd);
                    rc = sock_add(client_fd);
                    if (rc < 0) {
                        return (int)UNIFYCR_ERROR_SOCKET_FD_EXCEED;
                    }
                } else if (poll_set[i].revents & POLLERR) {
                    // unknown error on listening socket
                    return (int)UNIFYCR_ERROR_SOCK_LISTEN;
                }
            } else { // (i != 0) client sockets
                rc = 0;
                if (poll_set[i].revents & POLLIN) {
                    ssize_t bytes_read = read(poll_set[i].fd,
                                              cmd_buf[i], CMD_BUF_SIZE);
                    if (bytes_read == 0) {
                        rc = (int)UNIFYCR_ERROR_SOCK_DISCONNECT;
                    } else { // read a client command
                        cur_sock_idx = i;
                        return UNIFYCR_SUCCESS;
                    }
                } else if (poll_set[i].revents & POLLHUP) {
                    rc = (int)UNIFYCR_ERROR_SOCK_DISCONNECT;
                } else if (poll_set[i].revents & POLLERR) {
                    // unknown error on client socket
                    rc = (int)UNIFYCR_ERROR_SOCK_OTHER;
                }
                if (rc) {
                    if (rc == (int)UNIFYCR_ERROR_SOCK_DISCONNECT) {
                        sock_remove(i);
                        detached_sock_idx = i;
                    }
                    return rc;
                }
            }
        }
    }

    return UNIFYCR_SUCCESS;

}

#if 0 // DEPRECATED DUE TO MARGO
/*
 * send command to the client to let the client digest the
 * data in the shared receive buffer
 * @param: sock_id: socket index in poll_set
 * @param: cmd: command type
 *
 * */
int sock_notify_client(int client_idx, int cmd)
{
    LOGDBG("sock notifying fd: %d", client_sockfd);

    memset(ack_buf[client_idx], 0, sizeof(ack_buf[client_idx]));
    memcpy(ack_buf[client_idx], &cmd, sizeof(int));

    ssize_t rc = write(client_sockfd, ack_buf[client_idx],
                       sizeof(ack_buf[client_idx]));
    if (rc < 0) {
        return (int)UNIFYCR_ERROR_SOCK_OTHER;
    }
    return UNIFYCR_SUCCESS;
}

int sock_ack_client(int client_idx, int ret_sz)
{
    ssize_t rc = write(poll_set[client_idx].fd, ack_buf[client_idx], ret_sz);
    if (rc < 0) {
        return (int)UNIFYCR_ERROR_SOCK_OTHER;
    }
    return UNIFYCR_SUCCESS;
}

int sock_handle_error(int sock_error_no)
{
    return UNIFYCR_SUCCESS;
}

char* sock_get_cmd_buf(int client_idx)
{
    return (char*) cmd_buf[client_idx];
}

char* sock_get_ack_buf(int client_idx)
{
    return (char*) ack_buf[client_idx];
}

int sock_get_id(void)
{
    return cur_sock_idx;
}

int sock_get_error_id(void)
{
    return detached_sock_idx;
}
#endif // DEPRECATED DUE TO MARGO
