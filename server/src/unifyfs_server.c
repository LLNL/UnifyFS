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

// server components
#include "unifyfs_global.h"
#include "unifyfs_metadata_mdhim.h"
#include "unifyfs_request_manager.h"
#include "unifyfs_service_manager.h"
#include "unifyfs_inode_tree.h"

// margo rpcs
#include "margo_server.h"

/* PMI information */
int glb_pmi_rank; /* = 0 */
int glb_pmi_size = 1; // for standalone server tests
int server_pid;

char glb_host[UNIFYFS_MAX_HOSTNAME];
size_t glb_host_ndx;        // index of localhost in glb_servers

size_t glb_num_servers;     // size of glb_servers array
server_info_t* glb_servers; // array of server_info_t

unifyfs_cfg_t server_cfg;

static ABT_mutex app_configs_abt_sync;
static app_config* app_configs[MAX_NUM_APPS]; /* list of apps */
static size_t clients_per_app = MAX_APP_CLIENTS;

/**
 * @brief create a ready status file to notify that all servers are ready for
 * accepting client requests.
 *
 * @return 0 on success, error otherwise
 */
int unifyfs_publish_server_pids(void);

static int unifyfs_exit(void);

#if defined(UNIFYFS_MULTIPLE_DELEGATORS)
int* local_rank_lst;
int local_rank_cnt;

static int CountTasksPerNode(int rank, int numTasks);
static int find_rank_idx(int my_rank);
#endif

struct unifyfs_fops* global_fops_tab;

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
static void init_MPI(int* argc, char*** argv)
{
    int rc, provided;
    rc = MPI_Init_thread(argc, argv, MPI_THREAD_MULTIPLE, &provided);
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
        return ENOMEM;
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
        return EINVAL;
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

    // to daemon or not to daemon, that is the question
    rc = configurator_bool_val(server_cfg.unifyfs_daemonize, &daemon);
    if (rc != 0) {
        exit(1);
    }
    if (daemon) {
        daemonize();
    }

    server_pid = getpid();

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

    // update clients_per_app based on configuration
    if (server_cfg.server_max_app_clients != NULL) {
        long l;
        rc = configurator_int_val(server_cfg.server_max_app_clients, &l);
        if (0 == rc) {
            clients_per_app = l;
        }
    }

    // initialize empty app_configs[]
    memset(app_configs, 0, sizeof(app_configs));

#if defined(UNIFYFSD_USE_MPI)
    init_MPI(&argc, &argv);
#endif

    // start logging
    gethostname(glb_host, sizeof(glb_host));
    snprintf(dbg_fname, sizeof(dbg_fname), "%s/%s.%s",
             server_cfg.log_dir, server_cfg.log_file, glb_host);
    rc = unifyfs_log_open(dbg_fname);
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("%s", unifyfs_rc_enum_description((unifyfs_rc)rc));
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

    LOGDBG("initializing rpc service");
    ABT_init(argc, argv);
    ABT_mutex_create(&app_configs_abt_sync);
    rc = configurator_bool_val(server_cfg.margo_tcp, &margo_use_tcp);
    rc = margo_server_rpc_init();
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("%s", unifyfs_rc_enum_description(rc));
        exit(1);
    }

    LOGDBG("connecting rpc servers");
    rc = margo_connect_servers();
    if (rc != UNIFYFS_SUCCESS) {
        LOGERR("%s", unifyfs_rc_enum_description(rc));
        exit(1);
    }

    /* launch the service manager */
    LOGDBG("launching service manager thread");
    rc = svcmgr_init();
    if (rc != (int)UNIFYFS_SUCCESS) {
        LOGERR("launch failed - %s", unifyfs_rc_enum_description(rc));
        exit(1);
    }

    LOGDBG("initializing file operations");
    rc = unifyfs_fops_init(&server_cfg);
    if (rc != 0) {
        LOGERR("%s", unifyfs_rc_enum_description(rc));
        exit(1);
    }

    /* initialize our tree that maps a gfid to its extent tree */
    unifyfs_inode_tree_init(global_inode_tree);

    LOGDBG("publishing server pid");
    rc = unifyfs_publish_server_pids();
    if (rc != 0) {
        LOGERR("failed to publish server pid file: %s",
               unifyfs_rc_enum_description(rc));
        exit(1);
    }

    LOGINFO("server[%d] - finished initialization", glb_pmi_rank);

    while (1) {
        sleep(1);
        if (time_to_exit) {
            LOGDBG("starting service shutdown");
            break;
        }
    }

    /* tear down gfid-to-extents tree */
    unifyfs_inode_tree_destroy(global_inode_tree);

    LOGDBG("stopping service manager thread");
    rc = svcmgr_fini();

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

#endif // UNIFYFS_MULTIPLE_DELEGATORS
#endif // UNIFYFSD_USE_MPI


static int unifyfs_exit(void)
{
    int ret = UNIFYFS_SUCCESS;

    /* iterate over each active application and free resources */
    ABT_mutex_lock(app_configs_abt_sync);
    for (int i = 0; i < MAX_NUM_APPS; i++) {
        /* get pointer to app config for this app_id */
        app_config* app = app_configs[i];
        if (NULL != app) {
            app_configs[i] = NULL;
            unifyfs_rc rc = cleanup_application(app);
            if (rc != UNIFYFS_SUCCESS) {
                ret = rc;
            }
        }
    }
    ABT_mutex_unlock(app_configs_abt_sync);

    /* TODO: notify the service threads to exit */

    /* finalize kvstore service*/
    LOGDBG("finalizing kvstore service");
    unifyfs_keyval_fini();

    /* shutdown rpc service
     * (note: this needs to happen after app-client cleanup above) */
    LOGDBG("stopping rpc service");
    margo_server_rpc_finalize();

#if defined(USE_MDHIM)
    /* shutdown the metadata service*/
    LOGDBG("stopping metadata service");
    meta_sanitize();
#endif

#if defined(UNIFYFSD_USE_MPI)
    LOGDBG("finalizing MPI");
    fini_MPI();
#endif

    LOGDBG("all done!");
    unifyfs_log_close();

    return ret;
}

/* get pointer to app config for this app_id */
app_config* get_application(int app_id)
{
    ABT_mutex_lock(app_configs_abt_sync);
    for (int i = 0; i < MAX_NUM_APPS; i++) {
        app_config* app_cfg = app_configs[i];
        if ((NULL != app_cfg) && (app_cfg->app_id == app_id)) {
            ABT_mutex_unlock(app_configs_abt_sync);
            return app_cfg;
        }
    }
    ABT_mutex_unlock(app_configs_abt_sync);
    return NULL;
}

/* insert a new app config in app_configs[] */
app_config* new_application(int app_id)
{
    ABT_mutex_lock(app_configs_abt_sync);

    /* don't have an app_config for this app_id,
     * so allocate and fill a new one */
    app_config* new_app = (app_config*) calloc(1, sizeof(app_config));
    if (NULL == new_app) {
        LOGERR("failed to allocate application structure")
        ABT_mutex_unlock(app_configs_abt_sync);
        return NULL;
    }

    new_app->app_id = app_id;

    /* insert the given app_config in an empty slot */
    for (int i = 0; i < MAX_NUM_APPS; i++) {
        app_config* existing = app_configs[i];
        if (NULL == existing) {
            new_app->clients = (app_client**) calloc(clients_per_app,
                                                     sizeof(app_client*));
            if (NULL == new_app->clients) {
                LOGERR("failed to allocate application clients arrays")
                ABT_mutex_unlock(app_configs_abt_sync);
                return NULL;
            }
            new_app->clients_sz = clients_per_app;
            app_configs[i] = new_app;
            ABT_mutex_unlock(app_configs_abt_sync);
            return new_app;
        } else if (existing->app_id == app_id) {
            /* someone beat us to it, use existing */
            LOGDBG("found existing application for id=%d", app_id);
            ABT_mutex_unlock(app_configs_abt_sync);
            free(new_app);
            return existing;
        }
    }

    ABT_mutex_unlock(app_configs_abt_sync);

    /* no empty slots found */
    LOGERR("insert into app_configs[] failed");
    free(new_app);
    return NULL;
}

/* free application state
 *
 * NOTE: the application state mutex (app_configs_abt_sync) should be locked
 *       before calling this function
 */
unifyfs_rc cleanup_application(app_config* app)
{
    unifyfs_rc ret = UNIFYFS_SUCCESS;

    if (NULL == app) {
        return EINVAL;
    }

    int app_id = app->app_id;
    LOGDBG("cleaning application %d", app_id);

    /* free resources allocated for each client */
    for (int j = 0; j < app->clients_sz; j++) {
        app_client* client = app->clients[j];
        if (NULL != client) {
            unifyfs_rc rc = cleanup_app_client(app, client);
            if (rc != UNIFYFS_SUCCESS) {
                ret = rc;
            }
        }
    }
    if (NULL != app->clients) {
        free(app->clients);
    }
    free(app);

    return ret;
}

app_client* get_app_client(int app_id,
                           int client_id)
{
    /* get pointer to app structure for this app id */
    app_config* app_cfg = get_application(app_id);
    if ((NULL == app_cfg) ||
        (client_id <= 0) ||
        (client_id > (int)app_cfg->clients_sz)) {
        return NULL;
    }

    /* clients array index is (id - 1) */
    int client_ndx = client_id - 1;
    return app_cfg->clients[client_ndx];
}

/**
 * Attach to the server-side of client shared memory regions.
 * @param client: client information
 * @return success|error code
 */
static unifyfs_rc attach_to_client_shmem(app_client* client,
                                         size_t shmem_data_sz,
                                         size_t shmem_super_sz)
{
    shm_context* shm_ctx;
    char shm_name[SHMEM_NAME_LEN] = {0};

    if (NULL == client) {
        LOGERR("NULL client");
        return EINVAL;
    }

    int app_id = client->app_id;
    int client_id = client->client_id;

    /* initialize shmem region for client's superblock */
    sprintf(shm_name, SHMEM_SUPER_FMTSTR, app_id, client_id);
    shm_ctx = unifyfs_shm_alloc(shm_name, shmem_super_sz);
    if (NULL == shm_ctx) {
        LOGERR("Failed to attach to shmem superblock region %s", shm_name);
        return UNIFYFS_ERROR_SHMEM;
    }
    client->shmem_super = shm_ctx;

    /* initialize shmem region for read data */
    sprintf(shm_name, SHMEM_DATA_FMTSTR, app_id, client_id);
    shm_ctx = unifyfs_shm_alloc(shm_name, shmem_data_sz);
    if (NULL == shm_ctx) {
        LOGERR("Failed to attach to shmem data region %s", shm_name);
        return UNIFYFS_ERROR_SHMEM;
    }
    client->shmem_data = shm_ctx;

    /* initialize shmem header in data region */
    shm_data_header* shm_hdr = (shm_data_header*) client->shmem_data->addr;
    pthread_mutex_init(&(shm_hdr->sync), NULL);
    shm_hdr->meta_cnt = 0;
    shm_hdr->bytes = 0;
    shm_hdr->state = SHMEM_REGION_EMPTY;

    return UNIFYFS_SUCCESS;
}

/**
 * Initialize client state using passed values.
 *
 * Sets up logio and shmem region contexts, request manager thread,
 * margo rpc address, etc.
 */
app_client* new_app_client(app_config* app,
                           const char* margo_addr_str,
                           const int debug_rank)
{
    if ((NULL == app) || (NULL == margo_addr_str)) {
        return NULL;
    }

    if (app->num_clients == app->clients_sz) {
        LOGERR("reached maximum number of application clients");
        return NULL;
    }

    ABT_mutex_lock(app_configs_abt_sync);

    int app_id = app->app_id;
    int client_id = app->num_clients + 1; /* next client id */
    int client_ndx = client_id - 1;       /* clients array index is (id - 1) */

    app_client* client = (app_client*) calloc(1, sizeof(app_client));
    if (NULL != client) {
        int failure = 0;
        client->app_id = app_id;
        client->client_id = client_id;
        client->dbg_rank = debug_rank;

        /* convert client_addr_str to margo hg_addr_t */
        hg_return_t hret = margo_addr_lookup(unifyfsd_rpc_context->shm_mid,
                                             margo_addr_str,
                                             &(client->margo_addr));
        if (hret != HG_SUCCESS) {
            failure = 1;
        }

        /* create a request manager thread for this client */
        client->reqmgr = unifyfs_rm_thrd_create(app_id, client_id);
        if (NULL == client->reqmgr) {
            failure = 1;
        }

        if (failure) {
            LOGERR("failed to initialize application client");
            cleanup_app_client(app, client);
            ABT_mutex_unlock(app_configs_abt_sync);
            return NULL;
        }

        /* update app state */
        app->num_clients++;
        app->clients[client_ndx] = client;
    } else {
        LOGERR("failed to allocate client structure");
    }

    ABT_mutex_unlock(app_configs_abt_sync);

    return client;
}

/**
 * Attaches server to shared client state (e.g., logio and shmem regions)
 */
unifyfs_rc attach_app_client(app_client* client,
                             const char* logio_spill_dir,
                             const size_t logio_spill_size,
                             const size_t logio_shmem_size,
                             const size_t shmem_data_size,
                             const size_t shmem_super_size,
                             const size_t super_meta_offset,
                             const size_t super_meta_size)
{
    if (NULL == client) {
        return EINVAL;
    }

    int app_id = client->app_id;
    int client_id = client->client_id;
    int failure = 0;

    /* initialize server-side logio for this client */
    int rc = unifyfs_logio_init_server(app_id, client_id,
                                       logio_shmem_size,
                                       logio_spill_size,
                                       logio_spill_dir,
                                       &(client->logio));
    if (rc != UNIFYFS_SUCCESS) {
        failure = 1;
    }

    /* attach server-side shmem regions for this client */
    rc = attach_to_client_shmem(client, shmem_data_size, shmem_super_size);
    if (rc != UNIFYFS_SUCCESS) {
        failure = 1;
    }

    if (failure) {
        LOGERR("failed to attach application client");
        return UNIFYFS_FAILURE;
    }

    client->super_meta_offset = super_meta_offset;
    client->super_meta_size = super_meta_size;
    client->connected = 1;

    return UNIFYFS_SUCCESS;
}

/**
 * Disconnect ephemeral client state, while maintaining access to any data
 * the client wrote.
 */
unifyfs_rc disconnect_app_client(app_client* client)
{
    if (NULL == client) {
        return EINVAL;
    }

    if (!client->connected) {
        /* already done */
        return UNIFYFS_SUCCESS;
    }

    client->connected = 0;

    /* stop client request manager thread */
    if (NULL != client->reqmgr) {
        rm_request_exit(client->reqmgr);
    }

    /* free margo client address */
    margo_addr_free(unifyfsd_rpc_context->shm_mid,
                    client->margo_addr);

    /* release client shared memory regions */
    if (NULL != client->shmem_data) {
        /* Release read buffer shared memory region.
         * Client should have deleted file already, but will not hurt
         * to do this again. */
        unifyfs_shm_unlink(client->shmem_data);
        unifyfs_shm_free(&(client->shmem_data));
    }
    if (NULL != client->shmem_super) {
        /* Release superblock shared memory region.
         * Server is responsible for deleting superblock shared
         * memory file that was created by the client. */
        unifyfs_shm_unlink(client->shmem_super);
        unifyfs_shm_free(&(client->shmem_super));
    }

    return UNIFYFS_SUCCESS;
}

/**
 * Cleanup any client state that has been setup in preparation for
 * server exit.
 *
 * This function may be called due to a failed initialization, so we can't
 * assume any particular state is valid, other than app_id and client_id.
 *
 * NOTE: the application state mutex (app_configs_abt_sync) should be locked
 *       before calling this function
 */
unifyfs_rc cleanup_app_client(app_config* app, app_client* client)
{
    if ((NULL == app) || (NULL == client)) {
        return EINVAL;
    }

    LOGDBG("cleaning application client %d:%d",
           client->app_id, client->client_id);

    disconnect_app_client(client);

    /* close client logio context */
    if (NULL != client->logio) {
        unifyfs_logio_close(client->logio, 1);
        client->logio = NULL;
    }

    /* reset app->clients array index if set */
    int client_ndx = client->client_id - 1; /* client ids start at 1 */
    if (client == app->clients[client_ndx]) {
        app->clients[client_ndx] = NULL;
    }

    /* free client structure */
    if (NULL != client->reqmgr) {
        free(client->reqmgr);
        client->reqmgr = NULL;
    }
    free(client);

    return UNIFYFS_SUCCESS;
}
