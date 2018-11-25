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

#include <mpi.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include "unifycr_metadata.h"
#include "log.h"
#include "unifycr_debug.h"
#include "unifycr_sock.h"
#include "unifycr_init.h"
#include "unifycr_const.h"
#include "arraylist.h"
#include "unifycr_global.h"
#include "unifycr_cmd_handler.h"
#include "unifycr_service_manager.h"
#include "unifycr_request_manager.h"
#include "unifycr_runstate.h"

#include "unifycr_server.h"

int glb_size = -1; /* number of procs in world */
int glb_rank = -1; /* rank of this process within world */

int local_rank_cnt = -1; /* number of procs on the same node */
int local_rank_idx = -1; /* rank of this process within its node */

arraylist_t *app_config_list;
pthread_t data_thrd;
arraylist_t *thrd_list;

int invert_sock_ids[MAX_NUM_CLIENTS]; /*records app_id for each sock_id*/
int log_print_level = 5;

unifycr_cfg_t server_cfg;

static const int IO_POOL_SIZE = 2;

/* not needed
char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1)+strlen(s2)+1);//+1 for the null-terminator
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}
*/

/**
 * calculate the number of ranks sharing the node with
 * the calling process and the rank of the calling process
 * within its node
 *
 * @return prank: rank of calling process within its node
 * @return psize: number of processes on same node as calling process
 * @return success/error code
 */
static int CountTasksPerNode(int *prank, int *psize)
{
    /* split comm world into comm where all ranks can create a shared
     * memory segment, assume this to be all ranks on the same node,
     * using the same key value will order procs on the same node by
     * their rank in comm_world */
    int key = 0;
    MPI_Comm comm_node;
    MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, key,
        MPI_INFO_NULL, &comm_node);

    /* get our local rank */
    MPI_Comm_rank(comm_node, prank);

    /* get number of ranks on our node */
    MPI_Comm_size(comm_node, psize);

    /* free our nod communicator */
    MPI_Comm_free(&comm_node);

    return UNIFYCR_SUCCESS;
}

/* publishes server RPC address to a place where clients can find it */
static void addr_publish_server_rpc(const char* addr)
{
    /* TODO: support other publish modes like PMIX */

    /* write server address to /dev/shm/ for client on node to
     * read from */
    FILE *fp = fopen("/dev/shm/svr_id", "w+");
    if (fp != NULL) {
        fprintf(fp, addr);
        fclose(fp);
    } else {
        fprintf(stderr, "Error writing server rpc addr to file `/dev/shm/svr_id'\n");
    }
}

/* find_address - Resolves a name string to an address structure.
 *
 */
static int find_address(hg_class_t* hg_class,
           hg_context_t* hg_context, const char* arg, char* addr_string)
{
    /* figure out what address this server is listening on */
    hg_addr_t addr_self;
    int ret = HG_Addr_self(hg_class, &addr_self);
    if ( ret != HG_SUCCESS ) {
        fprintf(stderr, "Error: HG_Addr_self()\n");
        HG_Context_destroy(hg_context);
        HG_Finalize(hg_class);
        return(-1);
    }
    char addr_self_string[128];
    hg_size_t addr_self_string_sz = 128;
    ret = HG_Addr_to_string(hg_class, addr_self_string,
                            &addr_self_string_sz, addr_self);
    if ( ret != HG_SUCCESS ) {
        fprintf(stderr, "Error: HG_Addr_self()\n");
        HG_Context_destroy(hg_context);
        HG_Finalize(hg_class);
        return(-1);
    }
    HG_Addr_free(hg_class, addr_self);

    printf("%s\n", addr_self_string);

    //add to address string here
    addr_string = strcpy(addr_string, addr_self_string);

    printf("addr string:%s\n", addr_string);

    /* publish rpc address of server for local clients */
    addr_publish_server_rpc(addr_self_string);

    return 0;
}

/* setup_sm_target - Initializes the shared-memory target.
 *
 *                   Not used for FUSE deployment.
 *
 */
static margo_instance_id setup_sm_target()
{
    /*
    hg_class_t* hg_class = HG_Init(SMSVR_ADDR_STR, HG_TRUE);
    if ( !hg_class ) {
        fprintf(stderr, "Error: HG_Init() for sm_target\n");
        assert(false);
    }
    hg_context_t* hg_context = HG_Context_create(hg_class);
    if ( !hg_context ) {
        fprintf(stderr, "Error: HG_Context_create() for sm_target\n");
        HG_Finalize(hg_class);
        assert(false);
    }*/

    //char addr_str[128];

    /* figure out what address this server is listening on */
    /*if (find_address(hg_class, hg_context, SMSVR_ADDR_STR, addr_str) == -1)
        assert(false);*/

        /*ABT_xstream handler_xstream;
        ABT_pool handler_pool;
        int ret = ABT_snoozer_xstream_create(1, &handler_pool,
                                             &handler_xstream);
        if ( ret != 0 ) {
                fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
                assert(false);
        }*/
        /* initialize margo */
    hg_return_t hret;
    margo_instance_id mid;
    hg_addr_t addr_self;
    char addr_self_string[128];
    hg_size_t addr_self_string_sz = 128;
    mid = margo_init(SMSVR_ADDR_STR, MARGO_SERVER_MODE,
                                            1, 1);
    assert(mid);
    // printf("mid: %d\n", mid);
    //margo_diag_start(mid);

    /* figure out what address this server is listening on */
    hret = margo_addr_self(mid, &addr_self);
    if(hret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: margo_addr_self()\n");
        margo_finalize(mid);
        return(NULL);
        }
    hret = margo_addr_to_string(mid, addr_self_string,
                               &addr_self_string_sz, addr_self);
    if(hret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: margo_addr_to_string()\n");
        margo_addr_free(mid, addr_self);
        margo_finalize(mid);
        return(NULL);
     }
    margo_addr_free(mid, addr_self);

    printf("# accepting RPCs on address \"%s\"\n", addr_self_string);

    /* publish rpc address of server for local clients */
    addr_publish_server_rpc(addr_self_string);

        //margo_instance_id mid = margo_init_pool(*progress_pool, handler_pool,
        //                                    hg_context);
        //assert(mid);

    MARGO_REGISTER(mid, "unifycr_mount_rpc",
                     unifycr_mount_in_t, unifycr_mount_out_t,
                     unifycr_mount_rpc);

    MARGO_REGISTER(mid, "unifycr_metaget_rpc",
                     unifycr_metaget_in_t, unifycr_metaget_out_t,
                     unifycr_metaget_rpc);

    MARGO_REGISTER(mid, "unifycr_metaset_rpc",
                     unifycr_metaset_in_t, unifycr_metaset_out_t,
                     unifycr_metaset_rpc);

    MARGO_REGISTER(mid, "unifycr_fsync_rpc",
                     unifycr_fsync_in_t, unifycr_fsync_out_t,
                     unifycr_fsync_rpc);

    MARGO_REGISTER(mid, "unifycr_read_rpc",
                     unifycr_read_in_t, unifycr_read_out_t,
                     unifycr_read_rpc);

    MARGO_REGISTER(mid, "unifycr_mread_rpc",
                     unifycr_mread_in_t, unifycr_mread_out_t,
                     unifycr_mread_rpc);

    return mid;
}

/* setup_verbs_target - Initializes the inter-server target.
 *
 */
#if 0
static margo_instance_id setup_verbs_target(ABT_pool& progress_pool)
{
    hg_class_t* hg_class;
    if ( usetcp )
        hg_class = HG_Init(TCPSVR_ADDR_STR, HG_TRUE);
    else
        hg_class = HG_Init(VERBSVR_ADDR_STR, HG_TRUE);

    if ( !hg_class ) {
        fprintf(stderr, "Error: HG_Init() for verbs_target\n");
        assert(false);
    }
    hg_context_t* hg_context = HG_Context_create(hg_class);
    if ( !hg_context ) {
        fprintf(stderr, "Error: HG_Context_create() for verbs_target\n");
        HG_Finalize(hg_class);
        assert(false);
    }

    /* figure out what address this server is listening on */
    char* addr_string = "cci+";
    if (usetcp) {
        if ( find_address(hg_class,hg_context, TCPSVR_ADDR_STR,
                          addr_string) == -1)
            assert(false);
    } else {
        if ( find_address(hg_class,hg_context, VERBSVR_ADDR_STR,
                          addr_string) == -1)
            assert(false);
    }
    printf("finished find_address\n");

    ABT_xstream handler_xstream;
    ABT_pool handler_pool;
    int ret = ABT_snoozer_xstream_create(1, &handler_pool, &handler_xstream);
    if ( ret != 0 ) {
        fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
        assert(false);
    }
    printf("finished handler create\n");

    margo_instance_id mid = margo_init_pool(progress_pool, handler_pool,
                                            hg_context);
    printf("finished pool init\n");

    printf("Starting register is verbs\n");
    //MARGO_REGISTER(mid, "unifycr_mdread_rpc",
    //                 unifycr_mdread_in_t, unifycr_mdread_out_t,
    //                 unifycr_mdread_rpc);

    /*MARGO_REGISTER(mid, "unifycr_mdwrite_rpc",
                     unifycr_mdwrite_in_t, unifycr_mdwrite_out_t,
                     unifycr_mdwrite_rpc);

    MARGO_REGISTER(mid, "unifycr_mdchkdir_rpc",
                     unifycr_mdchkdir_in_t, unifycr_mdchkdir_out_t,
                     unifycr_mdchkdir_rpc);

    MARGO_REGISTER(mid, "unifycr_mdaddfile_rpc",
                     unifycr_mdaddfile_in_t, unifycr_mdaddfile_out_t,
                     unifycr_mdaddfile_rpc);

    MARGO_REGISTER(mid, "unifycr_mdopen_rpc",
                     unifycr_mdopen_in_t, unifycr_mdopen_out_t,
                     unifycr_mdopen_rpc);

    MARGO_REGISTER(mid, "unifycr_mdclose_rpc",
                     unifycr_mdclose_in_t, unifycr_mdclose_out_t,
                     unifycr_mdclose_rpc);

    MARGO_REGISTER(mid, "unifycr_mdgetfilestat_rpc",
                     unifycr_mdgetfilestat_in_t, unifycr_mdgetfilestat_out_t,
                     unifycr_mdgetfilestat_rpc);

    MARGO_REGISTER(mid, "unifycr_mdgetdircontents_rpc",
                     unifycr_mdgetdircontents_in_t, unifycr_mdgetdircontents_out_t,
                     unifycr_mdgetdircontents_rpc);*/

    printf("Completed register in verbs\n");

    server_addresses->at(my_rank).string_address = addr_string;
    char fname[16];
    sprintf(fname, "%d.addr", my_rank);

    //TODO: Add file support for C
    //std::ofstream myfile;
    //myfile.open (fname);
    //myfile << addr_string << std::endl;
    //myfile.close();

    return mid;
}
#endif
/* unifycr_server_rpc_init - Initializes the server-side RPC functionality
 *                       for inter-server communication.
 *
 */
margo_instance_id unifycr_server_rpc_init()
{
    //assert(unifycr_rpc_context);
    /* set up argobots */
    /*int ret = ABT_init(0, NULL);
    if ( ret != 0 ) {
        fprintf(stderr, "Error: ABT_init()\n");
        assert(false);
    }*/

    /* set primary ES to idle without polling */
    /*
    ret = ABT_snoozer_xstream_self_set();
    if ( ret != 0 ) {
        fprintf(stderr, "Error: ABT_snoozer_xstream_self_set()\n");
        assert(false);
    }


    ABT_xstream* io_xstreams = (ABT_xstream*)malloc(sizeof(ABT_xstream) * IO_POOL_SIZE);
    assert(io_xstreams);

    ABT_pool io_pool;
    ret = ABT_snoozer_xstream_create(IO_POOL_SIZE, &io_pool, io_xstreams);
    assert(ret == 0);

    aid = abt_io_init_pool(io_pool);
    assert(aid);


    ABT_xstream progress_xstream;
    ret = ABT_xstream_self(&progress_xstream);
    if ( ret != 0 ) {
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        assert(false);
    }
    ABT_pool progress_pool;

    ret = ABT_xstream_get_main_pools(progress_xstream, 1, &progress_pool);
    if ( ret != 0 ) {
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        assert(false);
    }

    ABT_xstream progress_xstream;
    ABT_pool progress_pool;
    ret = ABT_snoozer_xstream_create(1, &progress_pool, &progress_xstream);
    if ( ret != 0 ) {
        fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
        assert(false);
    }*/
    return setup_sm_target();
#ifdef VERBS_TARGET
    ABT_xstream progress_xstream2;
    ABT_pool progress_pool2;
    ret = ABT_snoozer_xstream_create(1, &progress_pool2, &progress_xstream2);
    if ( ret != 0 ) {
        fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
        assert(false);
    }
    return setup_verbs_target(progress_pool2);
#endif
}

#if defined(UNIFYCR_MULTIPLE_DELEGATORS)
extern int local_rank_idx;
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
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        exit(1);
    }

    if (pid > 0)
        exit(0);

    umask(0);

    sid = setsid();
    if (sid < 0) {
        fprintf(stderr, "setsid failed: %s\n", strerror(errno));
        exit(1);
    }

    rc = chdir("/");
    if (rc < 0) {
        fprintf(stderr, "chdir failed: %s\n", strerror(errno));
        exit(1);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        exit(1);
    } else if (pid > 0)
        exit(0);
}

int main(int argc, char *argv[])
{
    int provided;
    int rc;
    bool daemon = true;
    char dbg_fname[UNIFYCR_MAX_FILENAME] = {0};

    rc = unifycr_config_init(&server_cfg, argc, argv);
    if (rc != 0)
        exit(1);

    rc = configurator_bool_val(server_cfg.unifycr_daemonize, &daemon);
    if (rc != 0)
        exit(1);
    if (daemon)
        daemonize();

    rc = unifycr_write_runstate(&server_cfg);
    if (rc != (int)UNIFYCR_SUCCESS)
        exit(1);

    rc = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    if (rc != MPI_SUCCESS)
        exit(1);

    rc = MPI_Comm_rank(MPI_COMM_WORLD, &glb_rank);
    if (rc != MPI_SUCCESS)
        exit(1);

    rc = MPI_Comm_size(MPI_COMM_WORLD, &glb_size);
    if (rc != MPI_SUCCESS)
        exit(1);

    rc = CountTasksPerNode(&local_rank_idx, &local_rank_cnt);
    if (rc != UNIFYCR_SUCCESS)
        exit(1);

    snprintf(dbg_fname, sizeof(dbg_fname), "%s/%s.%d",
            server_cfg.log_dir, server_cfg.log_file, glb_rank);

    rc = dbg_open(dbg_fname);
    if (rc != ULFS_SUCCESS)
        LOG(LOG_ERR, "%s",
            unifycr_error_enum_description((unifycr_error_e)rc));

    app_config_list = arraylist_create();
    if (app_config_list == NULL) {
        LOG(LOG_ERR, "%s",
            unifycr_error_enum_description(UNIFYCR_ERROR_NOMEM));
        exit(1);
    }

    thrd_list = arraylist_create();
    if (thrd_list == NULL) {
        LOG(LOG_ERR, "%s",
            unifycr_error_enum_description(UNIFYCR_ERROR_NOMEM));
        exit(1);
    }

    //TODO: replace with unifycr_server_rpc_init??
    margo_instance_id mid = unifycr_server_rpc_init();
    rc = sock_init_server();
    if (rc != 0) {
        LOG(LOG_ERR, "%s",
            unifycr_error_enum_description(UNIFYCR_ERROR_SOCKET));
        exit(1);
    }

    /*launch the service manager*/
    rc = pthread_create(&data_thrd, NULL, sm_service_reads, NULL);
    if (rc != 0) {
        LOG(LOG_ERR, "%s",
            unifycr_error_enum_description(UNIFYCR_ERROR_THRDINIT));
        exit(1);
    }

    rc = meta_init_store(&server_cfg);
    if (rc != 0) {
        LOG(LOG_ERR, "%s",
            unifycr_error_enum_description(UNIFYCR_ERROR_MDINIT));
        exit(1);
    }

    LOG(LOG_DBG, "finished service initialization");

    MPI_Barrier(MPI_COMM_WORLD);
    while (1) {
        rc = sock_wait_cli_cmd();
/*
        if (rc != ULFS_SUCCESS) {
            int ret = sock_handle_error(rc);
            if (ret != 0) {
                LOG(LOG_ERR, "%s",
                    unifycr_error_enum_description((unifycr_error_e)ret));
                exit(1);
            }

        } else {
            int sock_id = sock_get_id();
            if (sock_id != 0) {
                char *cmd = sock_get_cmd_buf(sock_id);
                //TODO: remove this loop and replace with rpc calls?
                int cmd_rc = delegator_handle_command(cmd, sock_id);
                if (cmd_rc != ULFS_SUCCESS) {
                    LOG(LOG_ERR, "%s",
                        unifycr_error_enum_description((unifycr_error_e)ret));
                    return ret;
                }
            }
        }
*/
    }

    margo_wait_for_finalize(mid);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();

    LOG(LOG_DBG, "terminating service");
    rc = unifycr_clean_runstate(&server_cfg);

    return 0;
}

static int unifycr_exit()
{
    int rc = ULFS_SUCCESS;

    /* notify the threads of request manager to exit*/
    int i, j;
    for (i = 0; i < arraylist_size(thrd_list); i++) {
        /* wait for resource manager thread to exit */
        thrd_ctrl_t *thrd_ctrl =
            (thrd_ctrl_t *)arraylist_get(thrd_list, i);
        rm_cmd_exit(thrd_ctrl);
    }

    /* all threads have joined back, so release our control strucutres */
    arraylist_free(thrd_list);

    /* sanitize the shared memory and delete the log files
     * */
    int app_sz = arraylist_size(app_config_list);

    /* iterate over each active application and free resources */
    for (i = 0; i < app_sz; i++) {
        /* get pointer to app config for this app_id */
        app_config_t *app =
            (app_config_t *)arraylist_get(app_config_list, i);

        /* skip to next app_id if this is empty */
        if (app == NULL) {
            continue;
        }

        /* free resources allocate for each client */
        for (j = 0; j < MAX_NUM_CLIENTS; j++) {
            /* release request buffer shared memory region */
            if (app->shm_req_bufs[j] != NULL) {
                unifycr_shm_free(app->req_buf_name[j],
                    app->req_buf_sz, &(app->shm_req_bufs[j]));
            }

            /* release receive buffer shared memory region */
            if (app->shm_recv_bufs[j] != NULL) {
                unifycr_shm_free(app->recv_buf_name[j],
                    app->recv_buf_sz, &(app->shm_recv_bufs[j]));
            }

            /* release super block shared memory region */
            if (app->shm_superblocks[j] != NULL) {
                unifycr_shm_free(app->super_buf_name[j],
                    app->superblock_sz, &(app->shm_superblocks[j]));
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
    meta_sanitize();

    /* TODO: notify the service threads to exit*/

    /* destroy the sockets except for the ones
     * for acks*/
    sock_sanitize();

    return rc;
}
