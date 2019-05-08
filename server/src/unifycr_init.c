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

// system headers
#include <signal.h>
#include <sys/mman.h>
#include <mpi.h>

// common headers
#include "unifycr_configurator.h"
#include "unifycr_runstate.h"

// server components
#include "unifycr_global.h"
#include "unifycr_init.h"
#include "unifycr_metadata.h"
#include "unifycr_request_manager.h"
#include "unifycr_service_manager.h"

// margo rpcs
#include "margo_server.h"

int* local_rank_lst;
int local_rank_cnt;
int glb_rank, glb_size;

arraylist_t* app_config_list;
arraylist_t* thrd_list;

int invert_sock_ids[MAX_NUM_CLIENTS]; /*records app_id for each sock_id*/

unifycr_cfg_t server_cfg;

/*
 * Perform steps to create a daemon process:
 *
 *  1. Fork and exit from parent so child runs in the background
 *  2. Set the daemon umask to 0 so file modes passed to open() and
 *     mkdir() fully control access modes
 *  3. Call setsid() to create a new session and detach from controlling tty
 *  4. Change current working directory to / so daemon doesn't block
 *     filesystem unmounts
 *  5. close STDIN, STDOUT, and STDERR
 *  6. Fork again to abdicate session leader position to guarantee
 *     daemon cannot reacquire a controlling TTY
 *
 */
static void daemonize(void)
{
    pid_t pid;
    pid_t sid;
    int rc;

    pid = fork();

    if (pid < 0) {
        LOGERR("fork failed: %s", strerror(errno));
        exit(1);
    }

    if (pid > 0) {
        exit(0);
    }

    umask(0);

    sid = setsid();
    if (sid < 0) {
        LOGERR("setsid failed: %s", strerror(errno));
        exit(1);
    }

    rc = chdir("/");
    if (rc < 0) {
        LOGERR("chdir failed: %s", strerror(errno));
        exit(1);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    pid = fork();
    if (pid < 0) {
        LOGERR("fork failed: %s", strerror(errno));
        exit(1);
    } else if (pid > 0) {
        exit(0);
    }
}

static int time_to_exit;
void exit_request(int sig)
{
#ifdef HAVE_STRSIGNAL
    const char* sigstr = strsignal(sig);
    LOGDBG("got signal %s", sigstr);
#endif

    switch (sig) {
    case SIGINT:
    case SIGQUIT:
    case SIGTERM:
        time_to_exit = 1;
        LOGDBG("exit requested");
        break;
    default:
        LOGERR("unhandled signal %d", sig);
        break;
    }
}

int main(int argc, char* argv[])
{
    int provided;
    int rc;
    int srvr_rank_idx = 0;
    bool daemon = true;
    pthread_t svcmgr_thrd;
    struct sigaction sa;
    char dbg_fname[UNIFYCR_MAX_FILENAME] = {0};

    rc = unifycr_config_init(&server_cfg, argc, argv);
    if (rc != 0) {
        exit(1);
    }

    rc = configurator_bool_val(server_cfg.unifycr_daemonize, &daemon);
    if (rc != 0) {
        exit(1);
    }
    if (daemon) {
        daemonize();
    }

    // setup clean termination by signal
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = exit_request;
    rc = sigemptyset(&sa.sa_mask);
    rc = sigaction(SIGINT, &sa, NULL);
    rc = sigaction(SIGQUIT, &sa, NULL);
    rc = sigaction(SIGTERM, &sa, NULL);

    rc = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (rc != MPI_SUCCESS) {
        exit(1);
    }

    rc = MPI_Comm_rank(MPI_COMM_WORLD, &glb_rank);
    if (rc != MPI_SUCCESS) {
        exit(1);
    }

    rc = MPI_Comm_size(MPI_COMM_WORLD, &glb_size);
    if (rc != MPI_SUCCESS) {
        exit(1);
    }

    // start logging
    snprintf(dbg_fname, sizeof(dbg_fname), "%s/%s.%d",
             server_cfg.log_dir, server_cfg.log_file, glb_rank);
    rc = unifycr_log_open(dbg_fname);
    if (rc != UNIFYCR_SUCCESS) {
        LOGERR("%s", unifycr_error_enum_description((unifycr_error_e)rc));
    }

    rc = unifycr_write_runstate(&server_cfg);
    if (rc != (int)UNIFYCR_SUCCESS) {
        exit(1);
    }

    rc = CountTasksPerNode(glb_rank, glb_size);
    if (rc < 0) {
        exit(1);
    }

    app_config_list = arraylist_create();
    if (app_config_list == NULL) {
        LOGERR("%s", unifycr_error_enum_description(UNIFYCR_ERROR_NOMEM));
        exit(1);
    }

    thrd_list = arraylist_create();
    if (thrd_list == NULL) {
        LOGERR("%s", unifycr_error_enum_description(UNIFYCR_ERROR_NOMEM));
        exit(1);
    }

    LOGDBG("initializing rpc service");
    rc = configurator_bool_val(server_cfg.margo_tcp, &margo_use_tcp);
    rc = margo_server_rpc_init();
    if (rc != UNIFYCR_SUCCESS) {
        LOGERR("%s", unifycr_error_enum_description(UNIFYCR_ERROR_MARGO));
        exit(1);
    }

    LOGDBG("creating server domain socket");
#if defined(UNIFYCR_MULTIPLE_DELEGATORS)
    srvr_rank_idx = find_rank_idx(glb_rank, local_rank_lst,
                                  local_rank_cnt);
#endif
    rc = sock_init_server(srvr_rank_idx);
    if (rc != 0) {
        LOGERR("%s", unifycr_error_enum_description(UNIFYCR_ERROR_SOCKET));
        exit(1);
    }

    /* launch the service manager */
    LOGDBG("launching service manager thread");
    rc = pthread_create(&svcmgr_thrd, NULL, sm_service_reads, NULL);
    if (rc != 0) {
        LOGERR("%s", unifycr_error_enum_description(UNIFYCR_ERROR_THRDINIT));
        exit(1);
    }

    LOGDBG("initializing metadata store");
    rc = meta_init_store(&server_cfg);
    if (rc != 0) {
        LOGERR("%s", unifycr_error_enum_description(UNIFYCR_ERROR_MDINIT));
        exit(1);
    }

    LOGDBG("finished service initialization");

    MPI_Barrier(MPI_COMM_WORLD);
    int timeout_ms = 2000; /* in milliseconds */
    while (1) {
        rc = sock_wait_cmd(timeout_ms);
        if (rc != UNIFYCR_SUCCESS) {
            // we ignore disconnects, they are expected
            if (rc != UNIFYCR_ERROR_SOCK_DISCONNECT) {
                LOGDBG("domain socket error %s",
                       unifycr_error_enum_description((unifycr_error_e)rc));
                time_to_exit = 1;
            }
        }
        if (time_to_exit) {
            LOGDBG("starting service shutdown");
            break;
        }
    }

    LOGDBG("stopping service manager thread");
    int exit_cmd = XFER_COMM_EXIT;
    MPI_Send(&exit_cmd, sizeof(int), MPI_CHAR, glb_rank,
             CLI_DATA_TAG, MPI_COMM_WORLD);
    rc = pthread_join(svcmgr_thrd, NULL);

    LOGDBG("cleaning run state");
    rc = unifycr_clean_runstate(&server_cfg);

    return unifycr_exit();
}

/**
* count the number of delegators per node, and
* the rank of each delegator, the results are stored
* in local_rank_cnt and local_rank_lst.
* @param numTasks: number of processes in the communicator
* @return success/error code, local_rank_cnt and local_rank_lst.
*/
static int CountTasksPerNode(int rank, int numTasks)
{
    char       localhost[HOST_NAME_MAX];
    char       hostname[HOST_NAME_MAX];
    int        resultsLen = HOST_NAME_MAX;

    MPI_Status status;
    int rc;

    rc = MPI_Get_processor_name(localhost, &resultsLen);
    if (rc != 0) {
        return -1;
    }

    int i;
    if (numTasks > 0) {
        if (rank == 0) {
            /* a container of (rank, host) mappings*/
            name_rank_pair_t* host_set =
                (name_rank_pair_t*)malloc(numTasks
                                          * sizeof(name_rank_pair_t));
            /* MPI_receive all hostnames, and compare to local hostname */
            for (i = 1; i < numTasks; i++) {
                rc = MPI_Recv(hostname, HOST_NAME_MAX,
                              MPI_CHAR, MPI_ANY_SOURCE,
                              MPI_ANY_TAG,
                              MPI_COMM_WORLD, &status);

                if (rc != 0) {
                    return -1;
                }
                strcpy(host_set[i].hostname, hostname);
                host_set[i].rank = status.MPI_SOURCE;
            }
            strcpy(host_set[0].hostname, localhost);
            host_set[0].rank = 0;

            /*sort according to the hostname*/
            qsort(host_set, numTasks, sizeof(name_rank_pair_t),
                  compare_name_rank_pair);

            /* rank_cnt: records the number of processes on each node
             * rank_set: the list of ranks for each node
             * */
            int** rank_set = (int**)malloc(numTasks * sizeof(int*));
            int* rank_cnt = (int*)malloc(numTasks * sizeof(int));

            int cursor = 0, set_counter = 0;
            for (i = 1; i < numTasks; i++) {
                if (strcmp(host_set[i].hostname,
                           host_set[i - 1].hostname) == 0)
                    ; /*do nothing*/
                else {
                    // find a different rank, so switch to a new set
                    int j, k = 0;
                    rank_set[set_counter] =
                        (int*)malloc((i - cursor) * sizeof(int));
                    rank_cnt[set_counter] = i - cursor;
                    for (j = cursor; j <= i - 1; j++) {
                        rank_set[set_counter][k] =  host_set[j].rank;
                        k++;
                    }

                    set_counter++;
                    cursor = i;
                }
            }


            /*fill rank_cnt and rank_set entry for the last node*/
            int j = 0;

            rank_set[set_counter] =
                (int*)malloc((i - cursor) * sizeof(int));
            rank_cnt[set_counter] = numTasks - cursor;
            for (i = cursor; i <= numTasks - 1; i++) {
                rank_set[set_counter][j] = host_set[i].rank;
                j++;
            }
            set_counter++;

            /*broadcast the rank_cnt and rank_set information to each
             * rank*/
            int root_set_no = -1;
            for (i = 0; i < set_counter; i++) {
                for (j = 0; j < rank_cnt[i]; j++) {
                    if (rank_set[i][j] != 0) {
                        rc = MPI_Send(&rank_cnt[i], 1, MPI_INT,
                                      rank_set[i][j], 0, MPI_COMM_WORLD);
                        if (rc != 0) {
                            return -1;
                        }

                        /*send the local rank set to the corresponding rank*/
                        rc = MPI_Send(rank_set[i], rank_cnt[i], MPI_INT,
                                      rank_set[i][j], 0, MPI_COMM_WORLD);
                        if (rc != 0) {
                            return -1;
                        }
                    } else {
                        root_set_no = i;
                    }
                }
            }


            /* root process set its own local rank set and rank_cnt*/
            if (root_set_no >= 0) {
                local_rank_lst = malloc(rank_cnt[root_set_no] * sizeof(int));
                for (i = 0; i < rank_cnt[root_set_no]; i++) {
                    local_rank_lst[i] = rank_set[root_set_no][i];
                }
                local_rank_cnt = rank_cnt[root_set_no];
            }

            for (i = 0; i < set_counter; i++) {
                free(rank_set[i]);
            }
            free(rank_cnt);
            free(host_set);
            free(rank_set);

        } else {
            /* non-root process performs MPI_send to send
             * hostname to root node */
            rc = MPI_Send(localhost, HOST_NAME_MAX, MPI_CHAR,
                          0, 0, MPI_COMM_WORLD);
            if (rc != 0) {
                return -1;
            }
            /*receive the local rank count */
            rc = MPI_Recv(&local_rank_cnt, 1, MPI_INT, 0,
                          0, MPI_COMM_WORLD, &status);
            if (rc != 0) {
                return -1;
            }

            /* receive the the local rank list */
            local_rank_lst = (int*)malloc(local_rank_cnt * sizeof(int));
            rc = MPI_Recv(local_rank_lst, local_rank_cnt, MPI_INT, 0,
                          0, MPI_COMM_WORLD, &status);
            if (rc != 0) {
                free(local_rank_lst);
                return -1;
            }

        }

        qsort(local_rank_lst, local_rank_cnt, sizeof(int),
              compare_int);

        // scatter ranks out
    } else {
        return -1;
    }

    return 0;
}

#if defined(UNIFYCR_MULTIPLE_DELEGATORS)
static int find_rank_idx(int my_rank,
                         int* local_rank_lst, int local_rank_cnt)
{
    int i;
    for (i = 0; i < local_rank_cnt; i++) {
        if (local_rank_lst[i] == my_rank) {
            return i;
        }
    }

    return -1;

}
#endif

static int compare_int(const void* a, const void* b)
{
    const int* ptr_a = a;
    const int* ptr_b = b;

    if (*ptr_a - *ptr_b > 0) {
        return 1;
    }

    if (*ptr_a - *ptr_b < 0) {
        return -1;
    }

    return 0;
}

static int compare_name_rank_pair(const void* a, const void* b)
{
    const name_rank_pair_t* pair_a = a;
    const name_rank_pair_t* pair_b = b;

    if (strcmp(pair_a->hostname, pair_b->hostname) > 0) {
        return 1;
    }

    if (strcmp(pair_a->hostname, pair_b->hostname) < 0) {
        return -1;
    }

    return 0;
}

static int unifycr_exit()
{
    int rc = UNIFYCR_SUCCESS;

    /* shutdown rpc service */
    LOGDBG("stopping rpc service");
    margo_server_rpc_finalize();

    /* close remaining sockets */
    LOGDBG("closing sockets");
    sock_sanitize();

    /* shutdown the metadata service*/
    LOGDBG("stopping metadata service");
    meta_sanitize();

    /* TODO: notify the service threads to exit */

    /* notify the request manager threads to exit*/
    LOGDBG("stopping request manager threads");
    int i, j;
    for (i = 0; i < arraylist_size(thrd_list); i++) {
        /* request and wait for request manager thread exit */
        thrd_ctrl_t* thrd_ctrl = (thrd_ctrl_t*)arraylist_get(thrd_list, i);
        rm_cmd_exit(thrd_ctrl);
    }
    arraylist_free(thrd_list);

    /* sanitize the shared memory and delete the log files
     * */
    int app_sz = arraylist_size(app_config_list);

    /* iterate over each active application and free resources */
    for (i = 0; i < app_sz; i++) {
        /* get pointer to app config for this app_id */
        app_config_t* app =
            (app_config_t*)arraylist_get(app_config_list, i);

        /* skip to next app_id if this is empty */
        if (app == NULL) {
            continue;
        }

        /* free resources allocate for each client */
        for (j = 0; j < MAX_NUM_CLIENTS; j++) {
            /* release request buffer shared memory region */
            if (app->shm_req_bufs[j] != NULL) {
                unifycr_shm_free(app->req_buf_name[j], app->req_buf_sz,
                                 (void**)&(app->shm_req_bufs[j]));
            }

            /* release receive buffer shared memory region */
            if (app->shm_recv_bufs[j] != NULL) {
                unifycr_shm_free(app->recv_buf_name[j], app->recv_buf_sz,
                                 (void**)&(app->shm_recv_bufs[j]));
            }

            /* release super block shared memory region */
            if (app->shm_superblocks[j] != NULL) {
                unifycr_shm_free(app->super_buf_name[j], app->superblock_sz,
                                 (void**)&(app->shm_superblocks[j]));
            }

            /* close spill log file and delete it */
            if (app->spill_log_fds[j] > 0) {
                close(app->spill_log_fds[j]);
                unlink(app->spill_log_name[j]);
            }

            /* close spill log index file and delete it */
            if (app->spill_index_log_fds[j] > 0) {
                close(app->spill_index_log_fds[j]);
                unlink(app->spill_index_log_name[j]);
            }
        }
    }

    LOGDBG("finalizing MPI");
    MPI_Finalize();

    LOGDBG("all done!");
    unifycr_log_close();

    return rc;
}
