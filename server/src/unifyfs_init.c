/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
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

// common headers
#include "unifyfs_configurator.h"
#include "unifyfs_keyval.h"
#include "unifyfs_runstate.h"

// server components
#include "unifyfs_global.h"
#include "unifyfs_metadata.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"

// margo rpcs
#include "margo_server.h"

int glb_pmi_rank; /* = 0 */
int glb_pmi_size = 1; // for standalone server tests

char glb_host[UNIFYFS_MAX_HOSTNAME];
size_t glb_host_ndx;        // index of localhost in glb_servers

size_t glb_num_servers;     // size of glb_servers array
server_info_t* glb_servers; // array of server_info_t

arraylist_t* app_config_list;

int invert_sock_ids[MAX_NUM_CLIENTS]; /*records app_id for each sock_id*/

unifyfs_cfg_t server_cfg;

static int unifyfs_exit(void);

#if defined(UNIFYFS_MULTIPLE_DELEGATORS)
int* local_rank_lst;
int local_rank_cnt;

/*
 * structure that records the information of
 * each application launched by srun.
 * */
typedef struct {
    char hostname[UNIFYFS_MAX_HOSTNAME];
    int rank;
} name_rank_pair_t;

static int compare_name_rank_pair(const void* a, const void* b);
static int compare_int(const void* a, const void* b);
static int CountTasksPerNode(int rank, int numTasks);
static int find_rank_idx(int my_rank);
#endif

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

#if defined(UNIFYFSD_USE_MPI)
static void init_MPI(void)
{
    int rc, provided;
    rc = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (rc != MPI_SUCCESS) {
        exit(1);
    }

    rc = MPI_Comm_rank(MPI_COMM_WORLD, &glb_pmi_rank);
    if (rc != MPI_SUCCESS) {
        exit(1);
    }

    rc = MPI_Comm_size(MPI_COMM_WORLD, &glb_pmi_size);
    if (rc != MPI_SUCCESS) {
        exit(1);
    }
}

static void fini_MPI(void)
{
    MPI_Finalize();
}
#endif // UNIFYFSD_USE_MPI

static int allocate_servers(size_t n_servers)
{
    glb_num_servers = n_servers;
    glb_servers = (server_info_t*) calloc(n_servers, sizeof(server_info_t));
    if (NULL == glb_servers) {
        LOGERR("failed to allocate server_info array");
        return (int)UNIFYFS_ERROR_NOMEM;
    }
    return (int)UNIFYFS_SUCCESS;
}

static int process_servers_hostfile(const char* hostfile)
{
    int rc;
    size_t i, cnt;
    FILE* fp = NULL;
    char hostbuf[UNIFYFS_MAX_HOSTNAME+1];

    if (NULL == hostfile) {
        return (int)UNIFYFS_ERROR_INVAL;
    }
    fp = fopen(hostfile, "r");
    if (!fp) {
        LOGERR("failed to open hostfile %s", hostfile);
        return (int)UNIFYFS_FAILURE;
    }

    // scan first line: number of hosts
    rc = fscanf(fp, "%zu\n", &cnt);
    if (1 != rc) {
        LOGERR("failed to scan hostfile host count");
        fclose(fp);
        return (int)UNIFYFS_FAILURE;
    }
    rc = allocate_servers(cnt);
    if ((int)UNIFYFS_SUCCESS != rc) {
        fclose(fp);
        return (int)UNIFYFS_FAILURE;
    }

    // scan host lines
    for (i = 0; i < cnt; i++) {
        memset(hostbuf, 0, sizeof(hostbuf));
        rc = fscanf(fp, "%s\n", hostbuf);
        if (1 != rc) {
            LOGERR("failed to scan hostfile host line %zu", i);
            fclose(fp);
            return (int)UNIFYFS_FAILURE;
        }

        // NOTE: following assumes one server per host
        if (0 == strcmp(glb_host, hostbuf)) {
            glb_host_ndx = (int)i;
            LOGDBG("found myself at hostfile index=%zu, pmi_rank=%d",
                   glb_host_ndx, glb_pmi_rank);
        }
    }
    fclose(fp);

    if (glb_pmi_size < cnt) {
        glb_pmi_rank = (int)glb_host_ndx;
        glb_pmi_size = (int)cnt;
        LOGDBG("set pmi rank to host index %d", glb_pmi_rank);
    }

    return (int)UNIFYFS_SUCCESS;
}

int main(int argc, char* argv[])
{
    int rc;
    int kv_rank, kv_nranks;
    bool daemon = true;
    struct sigaction sa;
    char rank_str[16] = {0};
    char dbg_fname[UNIFYFS_MAX_FILENAME] = {0};

    rc = unifyfs_config_init(&server_cfg, argc, argv);
    if (rc != 0) {
        exit(1);
    }
    server_cfg.ptype = UNIFYFS_SERVER;

    rc = configurator_bool_val(server_cfg.unifyfs_daemonize, &daemon);
    if (rc != 0) {
        exit(1);
    }
    if (daemon) {
        daemonize();
    }

    /* unifyfs default log level is LOG_ERR */
    if (server_cfg.log_verbosity != NULL) {
        long l;
        rc = configurator_int_val(server_cfg.log_verbosity, &l);
        if (0 == rc) {
            unifyfs_set_log_level((unifyfs_log_level_t)l);
        }
    }

    // setup clean termination by signal
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = exit_request;
    rc = sigemptyset(&sa.sa_mask);
    rc = sigaction(SIGINT, &sa, NULL);
    rc = sigaction(SIGQUIT, &sa, NULL);
    rc = sigaction(SIGTERM, &sa, NULL);

    app_config_list = arraylist_create();
    if (app_config_list == NULL) {
        LOGERR("%s", unifyfs_error_enum_description(UNIFYFS_ERROR_NOMEM));
        exit(1);
    }

    rm_thrd_list = arraylist_create();
    if (rm_thrd_list == NULL) {
        LOGERR("%s", unifyfs_error_enum_description(UNIFYFS_ERROR_NOMEM));
        exit(1);
    }

#if defined(UNIFYFSD_USE_MPI)
    init_MPI();
#endif

    // start logging
    gethostname(glb_host, sizeof(glb_host));
    snprintf(dbg_fname, sizeof(dbg_fname), "%s/%s.%s",
             server_cfg.log_dir, server_cfg.log_file, glb_host);
    rc = unifyfs_log_open(dbg_fname);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("%s", unifyfs_error_enum_description((unifyfs_error_e)rc));
    }

    if (NULL != server_cfg.server_hostfile) {
        rc = process_servers_hostfile(server_cfg.server_hostfile);
        if (rc != (int)UNIFYFS_SUCCESS) {
            LOGERR("failed to gather server information");
            exit(1);
        }
    }

    kv_rank = glb_pmi_rank;
    kv_nranks = glb_pmi_size;
    rc = unifyfs_keyval_init(&server_cfg, &kv_rank, &kv_nranks);
    if (rc != (int)UNIFYFS_SUCCESS) {
        exit(1);
    }
    if (glb_pmi_rank != kv_rank) {
        LOGDBG("mismatch on pmi (%d) vs kvstore (%d) rank",
               glb_pmi_rank, kv_rank);
        glb_pmi_rank = kv_rank;
    }
    if (glb_pmi_size != kv_nranks) {
        LOGDBG("mismatch on pmi (%d) vs kvstore (%d) num ranks",
               glb_pmi_size, kv_nranks);
        glb_pmi_size = kv_nranks;
    }

    snprintf(rank_str, sizeof(rank_str), "%d", glb_pmi_rank);
    rc = unifyfs_keyval_publish_remote(key_unifyfsd_pmi_rank, rank_str);
    if (rc != (int)UNIFYFS_SUCCESS) {
        exit(1);
    }

    if (NULL == server_cfg.server_hostfile) {
        //glb_svr_rank = kv_rank;
        rc = allocate_servers((size_t)kv_nranks);
    }

    rc = unifyfs_write_runstate(&server_cfg);
    if (rc != (int)UNIFYFS_SUCCESS) {
        exit(1);
    }

    LOGDBG("initializing rpc service");
    rc = configurator_bool_val(server_cfg.margo_tcp, &margo_use_tcp);
    rc = margo_server_rpc_init();
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("%s", unifyfs_error_enum_description(UNIFYFS_ERROR_MARGO));
        exit(1);
    }

    LOGDBG("connecting rpc servers");
    rc = margo_connect_servers();
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("%s", unifyfs_error_enum_description(UNIFYFS_ERROR_MARGO));
        exit(1);
    }

#if defined(UNIFYFS_USE_DOMAIN_SOCKET)
    int srvr_rank_idx = 0;
#if defined(UNIFYFS_MULTIPLE_DELEGATORS)
    rc = CountTasksPerNode(glb_pmi_rank, glb_pmi_size);
    if (rc < 0) {
        exit(1);
    }
    srvr_rank_idx = find_rank_idx(glb_pmi_rank);
#endif // UNIFYFS_MULTIPLE_DELEGATORS
    LOGDBG("creating server domain socket");
    rc = sock_init_server(srvr_rank_idx);
    if (rc != 0) {
        LOGERR("%s", unifyfs_error_enum_description(UNIFYFS_ERROR_SOCKET));
        exit(1);
    }
#endif // UNIFYFS_USE_DOMAIN_SOCKET

    /* launch the service manager */
    LOGDBG("launching service manager thread");
    rc = svcmgr_init();
    if (rc != (int)UNIFYFS_SUCCESS) {
        LOGERR("launch failed - %s", unifyfs_error_enum_description(rc));
        exit(1);
    }

    LOGDBG("initializing metadata store");
    rc = meta_init_store(&server_cfg);
    if (rc != 0) {
        LOGERR("%s", unifyfs_error_enum_description(UNIFYFS_ERROR_MDINIT));
        exit(1);
    }

    LOGDBG("finished service initialization");

    while (1) {
#if defined(UNIFYFS_USE_DOMAIN_SOCKET)
        int timeout_ms = 2000; /* in milliseconds */
        rc = sock_wait_cmd(timeout_ms);
        if (rc != UNIFYFS_SUCCESS) {
            // we ignore disconnects, they are expected
            if (rc != UNIFYFS_ERROR_SOCK_DISCONNECT) {
                LOGDBG("domain socket error %s",
                       unifyfs_error_enum_description((unifyfs_error_e)rc));
                time_to_exit = 1;
            }
        }
#else
        sleep(1);
#endif // UNIFYFS_USE_DOMAIN_SOCKET
        if (time_to_exit) {
            LOGDBG("starting service shutdown");
            break;
        }
    }

    LOGDBG("stopping service manager thread");
    rc = svcmgr_fini();

    LOGDBG("cleaning run state");
    rc = unifyfs_clean_runstate(&server_cfg);

    return unifyfs_exit();
}

#if defined(UNIFYFSD_USE_MPI)
#if defined(UNIFYFS_MULTIPLE_DELEGATORS)
/* count the number of delegators per node, and
 * the rank of each delegator, the results are stored
 * in local_rank_cnt and local_rank_lst.
 * @param numTasks: number of processes in the communicator
 * @return success/error code */
static int CountTasksPerNode(int rank, int numTasks)
{
    char localhost[UNIFYFS_MAX_HOSTNAME];
    char hostname[UNIFYFS_MAX_HOSTNAME];
    int resultsLen = UNIFYFS_MAX_HOSTNAME;

    MPI_Status status;
    int i, j, rc;

    if (numTasks < 0) {
        return -1;
    }

    rc = MPI_Get_processor_name(localhost, &resultsLen);
    if (rc != 0) {
        return -1;
    }

    if (rank == 0) {
        /* a container of (rank, host) mappings */
        name_rank_pair_t* host_set =
            (name_rank_pair_t*)calloc(numTasks, sizeof(name_rank_pair_t));
        /* MPI_Recv all hostnames, and compare to local hostname */
        for (i = 1; i < numTasks; i++) {
            rc = MPI_Recv(hostname, UNIFYFS_MAX_HOSTNAME,
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

        /* sort by hostname */
        qsort(host_set, numTasks, sizeof(name_rank_pair_t),
              compare_name_rank_pair);

        /* rank_cnt: records the number of processes on each host
         * rank_set: the list of ranks for each host */
        int** rank_set = (int**)calloc(numTasks, sizeof(int*));
        int* rank_cnt = (int*)calloc(numTasks, sizeof(int));

        int cursor = 0;
        int set_counter = 0;
        for (i = 1; i < numTasks; i++) {
            if (strcmp(host_set[i].hostname,
                       host_set[i - 1].hostname) != 0) {
                // found a different host, so switch to a new set
                int hiter, riter = 0;
                rank_set[set_counter] =
                    (int*)calloc((i - cursor), sizeof(int));
                rank_cnt[set_counter] = i - cursor;
                for (hiter = cursor; hiter < i; hiter++, riter++) {
                    rank_set[set_counter][riter] =  host_set[hiter].rank;
                }

                set_counter++;
                cursor = i;
            }
        }

        /* fill rank_cnt and rank_set entry for the last host */

        rank_set[set_counter] =
            (int*)calloc((i - cursor), sizeof(int));
        rank_cnt[set_counter] = numTasks - cursor;
        j = 0;
        for (i = cursor; i < numTasks; i++, j++) {
            rank_set[set_counter][j] = host_set[i].rank;
        }
        set_counter++;

        /* broadcast rank_set information */
        int root_set_no = -1;
        for (i = 0; i < set_counter; i++) {
            /* send rank set to each of its ranks */
            for (j = 0; j < rank_cnt[i]; j++) {
                if (rank_set[i][j] != 0) {
                    rc = MPI_Send(&rank_cnt[i], 1, MPI_INT,
                                  rank_set[i][j], 0, MPI_COMM_WORLD);
                    if (rc != 0) {
                        return -1;
                    }
                    rc = MPI_Send(rank_set[i], rank_cnt[i], MPI_INT,
                                  rank_set[i][j], 0, MPI_COMM_WORLD);
                    if (rc != 0) {
                        return -1;
                    }
                } else {
                    root_set_no = i;
                    local_rank_cnt = rank_cnt[i];
                    local_rank_lst = (int*)calloc(rank_cnt[i], sizeof(int));
                    memcpy(local_rank_lst, rank_set[i],
                           (local_rank_cnt * sizeof(int)))
                }
            }
        }

        for (i = 0; i < set_counter; i++) {
            free(rank_set[i]);
        }
        free(rank_cnt);
        free(host_set);
        free(rank_set);
    } else { /* non-root rank */
        /* MPI_Send hostname to root */
        rc = MPI_Send(localhost, UNIFYFS_MAX_HOSTNAME, MPI_CHAR,
                      0, 0, MPI_COMM_WORLD);
        if (rc != 0) {
            return -1;
        }
        /* receive the local rank set count */
        rc = MPI_Recv(&local_rank_cnt, 1, MPI_INT, 0,
                      0, MPI_COMM_WORLD, &status);
        if (rc != 0) {
            return -1;
        }
        /* receive the the local rank set */
        local_rank_lst = (int*)calloc(local_rank_cnt, sizeof(int));
        rc = MPI_Recv(local_rank_lst, local_rank_cnt, MPI_INT, 0,
                      0, MPI_COMM_WORLD, &status);
        if (rc != 0) {
            free(local_rank_lst);
            return -1;
        }
    }

    /* sort by rank */
    qsort(local_rank_lst, local_rank_cnt, sizeof(int), compare_int);

    return 0;
}

static int find_rank_idx(int my_rank)
{
    int i;
    assert(local_rank_lst != NULL);
    for (i = 0; i < local_rank_cnt; i++) {
        if (local_rank_lst[i] == my_rank) {
            return i;
        }
    }
    return -1;
}

static int compare_name_rank_pair(const void* a, const void* b)
{
    const name_rank_pair_t* pair_a = a;
    const name_rank_pair_t* pair_b = b;
    return strcmp(pair_a->hostname, pair_b->hostname);
}

static int compare_int(const void* a, const void* b)
{
    int aval = *(const int*)a;
    int bval = *(const int*)b;
    return aval - bval;
}

#endif // UNIFYFS_MULTIPLE_DELEGATORS
#endif // UNIFYFSD_USE_MPI


static int unifyfs_exit(void)
{
    int rc = UNIFYFS_SUCCESS;

    /* shutdown rpc service */
    LOGDBG("stopping rpc service");
    margo_server_rpc_finalize();

#if defined(UNIFYFS_USE_DOMAIN_SOCKET)
    /* close remaining sockets */
    LOGDBG("closing sockets");
    sock_sanitize();
#endif

    /* finalize kvstore service*/
    LOGDBG("finalizing kvstore service");
    unifyfs_keyval_fini();

    /* TODO: notify the service threads to exit */

    /* notify the request manager threads to exit*/
    LOGDBG("stopping request manager threads");
    int i, j;
    for (i = 0; i < arraylist_size(rm_thrd_list); i++) {
        /* request and wait for request manager thread exit */
        reqmgr_thrd_t* thrd_ctrl =
            (reqmgr_thrd_t*) arraylist_get(rm_thrd_list, i);
        rm_cmd_exit(thrd_ctrl);
    }
    arraylist_free(rm_thrd_list);

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
            /* Release request buffer shared memory region.  Client
             * should have deleted file already, but will not hurt
             * to do this again. */
            if (app->shm_req_bufs[j] != NULL) {
                unifyfs_shm_free(app->req_buf_name[j], app->req_buf_sz,
                                 (void**)&(app->shm_req_bufs[j]));
                unifyfs_shm_unlink(app->req_buf_name[j]);
            }

            /* Release receive buffer shared memory region.  Client
             * should have deleted file already, but will not hurt
             * to do this again. */
            if (app->shm_recv_bufs[j] != NULL) {
                unifyfs_shm_free(app->recv_buf_name[j], app->recv_buf_sz,
                                 (void**)&(app->shm_recv_bufs[j]));
                unifyfs_shm_unlink(app->recv_buf_name[j]);
            }

            /* Release super block shared memory region.
             * Server is responsible for deleting superblock shared
             * memory file that was created by the client. */
            if (app->shm_superblocks[j] != NULL) {
                unifyfs_shm_free(app->super_buf_name[j], app->superblock_sz,
                                 (void**)&(app->shm_superblocks[j]));
                unifyfs_shm_unlink(app->super_buf_name[j]);
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

    /* shutdown the metadata service*/
    LOGDBG("stopping metadata service");
    meta_sanitize();

#if defined(UNIFYFSD_USE_MPI)
    LOGDBG("finalizing MPI");
    fini_MPI();
#endif

    LOGDBG("all done!");
    unifyfs_log_close();

    return rc;
}
