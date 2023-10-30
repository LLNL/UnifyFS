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

static volatile int n_servers_reported; // = 0
static volatile int bootstrap_completed; // = 0
static int* server_pids; // = NULL
static ABT_cond server_bootstrap_cond = ABT_COND_NULL;
static ABT_mutex server_bootstrap_mutex = ABT_MUTEX_NULL;
static struct timespec server_bootstrap_timeout;


static inline int set_bootstrap_timeout(void)
{
    int ret = UNIFYFS_SUCCESS;
    long timeout_sec = 0;

    if (server_cfg.server_init_timeout) {
        ret = configurator_int_val(server_cfg.server_init_timeout,
                                   &timeout_sec);
        if (ret) {
            LOGERR("failed to read configuration");
            return ret;
        }
    }

    clock_gettime(CLOCK_REALTIME, &server_bootstrap_timeout);
    server_bootstrap_timeout.tv_sec += timeout_sec;

    return ret;
}

static void free_bootstrap_state(void)
{
    if (ABT_MUTEX_NULL != server_bootstrap_mutex) {
        ABT_mutex_lock(server_bootstrap_mutex);
        if (ABT_COND_NULL != server_bootstrap_cond) {
            ABT_cond_free(&server_bootstrap_cond);
            server_bootstrap_cond = ABT_COND_NULL;
        }
        ABT_mutex_unlock(server_bootstrap_mutex);
        ABT_mutex_free(&server_bootstrap_mutex);
        server_bootstrap_mutex = ABT_MUTEX_NULL;
    }

    if (NULL != server_pids) {
        free(server_pids);
        server_pids = NULL;
    }
}

static int initialize_bootstrap_state(void)
{
    int rc;
    int ret = UNIFYFS_SUCCESS;

    if (ABT_MUTEX_NULL == server_bootstrap_mutex) {
        rc = ABT_mutex_create(&server_bootstrap_mutex);
        if (ABT_SUCCESS != rc) {
            LOGERR("ABT_mutex_create failed");
            return UNIFYFS_ERROR_MARGO;
        }
    }

    ABT_mutex_lock(server_bootstrap_mutex);
    if (ABT_COND_NULL == server_bootstrap_cond) {
        rc = ABT_cond_create(&server_bootstrap_cond);
        if (ABT_SUCCESS != rc) {
            LOGERR("ABT_cond_create failed");
            ret = UNIFYFS_ERROR_MARGO;
        }
    }

    if (UNIFYFS_SUCCESS == ret) {
        ret = set_bootstrap_timeout();
    }

    if ((UNIFYFS_SUCCESS == ret) && (0 == glb_pmi_rank)) {
        if (NULL == server_pids) {
            server_pids = (int*) calloc(glb_pmi_size, sizeof(int));
            if (NULL == server_pids) {
                LOGERR("failed to allocate memory for pid array (%s)",
                    strerror(errno));
                ret = ENOMEM;
            }
        }
    }

    ABT_mutex_unlock(server_bootstrap_mutex);

    if (ret != UNIFYFS_SUCCESS) {
        LOGERR("failed to initialize bootstrap state!");
        free_bootstrap_state();
    }

    return ret;
}

static int create_server_pid_file(void)
{
    int i = 0;
    int ret = UNIFYFS_SUCCESS;
    char filename[UNIFYFS_MAX_FILENAME] = { 0, };
    FILE* fp = NULL;

    if (!server_pids) {
        LOGERR("cannot access the server pids");
        return EINVAL;
    }

    snprintf(filename, sizeof(filename), "%s/%s",
             server_cfg.sharedfs_dir, UNIFYFS_SERVER_PID_FILENAME);

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
    assert((glb_pmi_rank == 0) && (rank < glb_pmi_size));

    /* NOTE: need this here in case we receive a pid report rpc before we
     *       have initialized state in unifyfs_complete_bootstrap() */
    int rc = initialize_bootstrap_state();
    if (rc) {
        LOGERR("failed to initialize bootstrap state");
        return rc;
    }

    ABT_mutex_lock(server_bootstrap_mutex);
    n_servers_reported++;
    server_pids[rank] = pid;
    ABT_cond_signal(server_bootstrap_cond);
    ABT_mutex_unlock(server_bootstrap_mutex);

    return UNIFYFS_SUCCESS;
}

int unifyfs_signal_bootstrap_complete(void)
{
    assert(glb_pmi_rank != 0);

    ABT_mutex_lock(server_bootstrap_mutex);
    bootstrap_completed = 1;
    ABT_cond_signal(server_bootstrap_cond);
    ABT_mutex_unlock(server_bootstrap_mutex);

    return UNIFYFS_SUCCESS;
}

static int wait_for_bootstrap_condition(void)
{
    int ret = UNIFYFS_SUCCESS;
    int rc = ABT_cond_timedwait(server_bootstrap_cond, server_bootstrap_mutex,
                                &server_bootstrap_timeout);
    if (ABT_ERR_COND_TIMEDOUT == rc) {
        LOGERR("server initialization timeout");
        ret = UNIFYFS_ERROR_TIMEOUT;
    } else if (rc) {
        LOGERR("failed to wait on condition (err=%d)", rc);
        ret = UNIFYFS_ERROR_MARGO;
    }
    return ret;
}

int unifyfs_complete_bootstrap(void)
{
    int ret = UNIFYFS_SUCCESS;

    int rc = initialize_bootstrap_state();
    if (UNIFYFS_SUCCESS != rc) {
        LOGERR("ABT_mutex_create failed");
        return UNIFYFS_ERROR_MARGO;
    }

    if (glb_pmi_rank > 0) {
        /* publish my pid to server rank 0 */
        LOGDBG("server[%d] - reporting pid", glb_pmi_rank);
        ret = unifyfs_invoke_server_pid_rpc();
        if (ret) {
            LOGERR("failed to invoke pid rpc (%s)", strerror(ret));
        } else {
             /* wait for "bootstrap-complete" broadcast from server rank 0 */
            ABT_mutex_lock(server_bootstrap_mutex);
            while (!bootstrap_completed) {
                ret = wait_for_bootstrap_condition();
                if (UNIFYFS_ERROR_TIMEOUT == ret) {
                    break;
                }
            }
            ABT_mutex_unlock(server_bootstrap_mutex);
            if (bootstrap_completed) {
                LOGDBG("server[%d] - bootstrap completed", glb_pmi_rank);
            }
        }
    } else { /* server rank 0 acts as coordinator */

        /* keep checking count of reported servers until all have reported
         * or we hit the timeout */
        ABT_mutex_lock(server_bootstrap_mutex);
        server_pids[0] = server_pid;
        n_servers_reported++;
        while (n_servers_reported < glb_pmi_size) {
            ret = wait_for_bootstrap_condition();
            if (UNIFYFS_ERROR_TIMEOUT == ret) {
                break;
            }
        }
        ABT_mutex_unlock(server_bootstrap_mutex);

        if (n_servers_reported == glb_pmi_size) {
            bootstrap_completed = 1;
            LOGDBG("server[%d] - bootstrap completed", glb_pmi_rank);
            ret = unifyfs_invoke_broadcast_bootstrap_complete();
            if (UNIFYFS_SUCCESS == ret) {
                LOGDBG("servers ready to accept client connections");
                ret = create_server_pid_file();
                if (UNIFYFS_SUCCESS != ret) {
                    LOGERR("pid file creation failed!");
                }
            } else {
                LOGERR("bootstrap broadcast failed!");
            }
        } else {
            LOGERR("%d of %d servers reported their pids",
                   n_servers_reported, glb_pmi_size);
        }
    }

    free_bootstrap_state();

    return ret;
}
