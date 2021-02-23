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

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include "unifyfs_configurator.h"
#include "unifyfs_global.h"
#include "margo_server.h"
#include "unifyfs_p2p_rpc.h"
#include "unifyfs_server_rpcs.h"

extern unifyfs_cfg_t server_cfg;

static int n_servers_reported; // = 0
static int* server_pids; // = NULL
static pthread_cond_t server_pid_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t server_pid_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec server_pid_timeout;

static int alloc_server_pids(void)
{
    int ret = 0;
    pthread_mutex_lock(&server_pid_mutex);
    if (NULL == server_pids) {
        server_pids = (int*) calloc(glb_pmi_size, sizeof(int));
        if (NULL == server_pids) {
            LOGERR("failed to allocate memory (%s)", strerror(errno));
            ret = ENOMEM;
        }
    }
    pthread_mutex_unlock(&server_pid_mutex);
    return ret;
}

static inline int set_pidfile_timeout(void)
{
    int ret = 0;
    long timeout_sec = 0;

    if (server_cfg.server_init_timeout) {
        ret = configurator_int_val(server_cfg.server_init_timeout,
                                   &timeout_sec);
        if (ret) {
            LOGERR("failed to read configuration");
            return ret;
        }
    }

    clock_gettime(CLOCK_REALTIME, &server_pid_timeout);
    server_pid_timeout.tv_sec += timeout_sec;

    return 0;
}

static int create_server_pid_file(void)
{
    int i = 0;
    int ret = 0;
    char filename[UNIFYFS_MAX_FILENAME] = { 0, };
    FILE* fp = NULL;

    if (!server_pids) {
        LOGERR("cannot access the server pids");
        return EINVAL;
    }

    snprintf(filename, sizeof(filename), "%s/%s",
             server_cfg.sharedfs_dir, UNIFYFSD_PID_FILENAME);

    fp = fopen(filename, "w");
    if (!fp) {
        LOGERR("failed to create file %s (%s)", filename, strerror(errno));
        return errno;
    }

    for (i = 0; i < glb_pmi_size; i++) {
        fprintf(fp, "[%d] %d\n", i, server_pids[i]);
    }

    fclose(fp);

    return ret;
}

int unifyfs_report_server_pid(int rank, int pid)
{
    assert(rank < glb_pmi_size);

    int ret = alloc_server_pids();
    if (ret) {
        LOGERR("failed to allocate pid array");
        return ret;
    }

    pthread_mutex_lock(&server_pid_mutex);
    n_servers_reported++;
    server_pids[rank] = pid;
    pthread_cond_signal(&server_pid_cond);
    pthread_mutex_unlock(&server_pid_mutex);

    return UNIFYFS_SUCCESS;
}

int unifyfs_publish_server_pids(void)
{
    int ret = UNIFYFS_SUCCESS;

    if (glb_pmi_rank > 0) {
        /* publish my pid to server 0 */
        ret = unifyfs_invoke_server_pid_rpc();
        if (ret) {
            LOGERR("failed to invoke pid rpc (%s)", strerror(ret));
        }
    } else { /* rank 0 acts as coordinator */
        ret = alloc_server_pids();
        if (ret) {
            return ret;
        }

        ret = set_pidfile_timeout();
        if (ret) {
            return ret;
        }

        pthread_mutex_lock(&server_pid_mutex);
        server_pids[0] = server_pid;
        n_servers_reported++;

        /* keep checking count of reported servers until all have reported
         * or we hit the timeout */
        while (n_servers_reported < glb_pmi_size) {
            ret = pthread_cond_timedwait(&server_pid_cond,
                                         &server_pid_mutex,
                                         &server_pid_timeout);
            if (ETIMEDOUT == ret) {
                LOGERR("server initialization timeout");
                break;
            } else if (ret) {
                LOGERR("failed to wait on condition (err=%d, %s)",
                       errno, strerror(errno));
                break;
            }
        }

        if (n_servers_reported == glb_pmi_size) {
            ret = create_server_pid_file();
            if (UNIFYFS_SUCCESS == ret) {
                LOGDBG("servers ready to accept client connections");
            }
        } else {
            LOGERR("%d of %d servers reported their pids",
                   n_servers_reported, glb_pmi_size);
        }

        free(server_pids);
        server_pids = NULL;

        pthread_mutex_unlock(&server_pid_mutex);
    }

    return ret;
}

