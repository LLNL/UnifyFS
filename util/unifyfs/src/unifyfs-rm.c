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
 * Written by: Hyogi Sim
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 *
 * Copyright (c) 2014, Los Alamos National Laboratory
 *  All rights reserved.
 *
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <unifyfs_misc.h>

#include "unifyfs.h"

typedef int (*unifyfs_rm_read_resource_t)(unifyfs_resource_t* resource);

typedef int (*unifyfs_rm_launch_t)(unifyfs_resource_t* resource,
                                   unifyfs_args_t* args);

typedef int (*unifyfs_rm_terminate_t)(unifyfs_resource_t* resource,
                                      unifyfs_args_t* args);

typedef int (*unifyfs_rm_stage_t)(unifyfs_resource_t* resource,
                                  unifyfs_args_t* args);

struct _ucr_resource_manager {
    const char* type;

    unifyfs_rm_read_resource_t read_resource;
    unifyfs_rm_launch_t launch;
    unifyfs_rm_terminate_t terminate;
    unifyfs_rm_stage_t stage;
};

typedef struct _ucr_resource_manager _ucr_resource_manager_t;

/*
 * TODO: currently, we cannot launch if no resource manager is detected
 * (UNIFYFS_RM_INVALID).
 */

/**
 * @brief Default resource detection routine
 *
 * @param resource  Not used
 *
 * @return -ENOSYS
 */
static int invalid_read_resource(unifyfs_resource_t* resource)
{
    return -ENOSYS;
}


/**
 * @brief Parse a hostfile containing one host per line
 *
 * @param resource  The job resource record to be filled
 * @param hostfile  The hostfile to be parsed
 * @param n_nodes   The number of nodes allocated by resource manager
 *
 * @return 0 on success, negative errno otherwise
 */
static int parse_hostfile(unifyfs_resource_t* resource,
                          char* hostfile,
                          size_t n_nodes)
{
    int ret = 0;
    int i = 0;
    FILE* fp = NULL;
    char** nodes = NULL;
    char buf[1024];

    if (hostfile == NULL) {
        return -EINVAL;
    }

    /*
     * node names may be duplicated in hostfile, depending on the number of
     * available cores per node. for instance, nodeX will appear 16 times if
     * nodeX has 16 cores. so, get the correct node number first.
     */

    fp = fopen(hostfile, "r");
    if (!fp) {
        return -errno;
    }

    nodes = calloc(sizeof(char*), n_nodes);
    if (!nodes) {
        ret = -errno;
        goto out;
    }

    while (fgets(buf, 1024, fp) != NULL) {
        buf[strlen(buf) - 1] = '\0';    /* discard the newline */
        if (i > 0) {
            if (strcmp(buf, nodes[i - 1]) == 0) {
                continue;    // skip duplicate
            }
            nodes[i] = strdup(buf);
        } else {
            nodes[i] = strdup(buf);
        }
        i++;
    }

    resource->n_nodes = n_nodes;
    resource->nodes = nodes;

out:
    fclose(fp);

    return ret;
}

/**
 * @brief Write a server-shared hostfile
 *
 * @param resource  The job resource record
 * @param args      The command-line options
 *
 * @return 0 on success, negative errno otherwise
 */
static int write_hostfile(unifyfs_resource_t* resource,
                          unifyfs_args_t* args)
{
    int ret = 0;
    size_t i;
    FILE* fp = NULL;
    char hostfile[UNIFYFS_MAX_FILENAME];

    if (NULL == args->share_dir) {
        return -EINVAL;
    }

    snprintf(hostfile, sizeof(hostfile), "%s/unifyfsd.hosts",
             args->share_dir);
    fp = fopen(hostfile, "w");
    if (!fp) {
        return -errno;
    }

    // first line: number of hosts
    fprintf(fp, "%zu\n", resource->n_nodes);
    for (i = 0; i < resource->n_nodes; i++) {
        fprintf(fp, "%s\n", resource->nodes[i]);
    }
    fclose(fp);
    args->share_hostfile = strdup(hostfile);

    return ret;
}

/**
 * @brief wait until servers become ready for client connections
 *
 * @param resource  The job resource record
 * @param args      The command-line options
 *
 * @return 0 on success, negative errno otherwise
 */
static int wait_server_initialization(unifyfs_resource_t* resource,
                                      unifyfs_args_t* args)
{
    int ret = UNIFYFS_SUCCESS;
    int count = 0;
    unsigned int interval = 3;
    unsigned int wait_time = 0;
    FILE* fp = NULL;
    char linebuf[32];
    char filename[PATH_MAX];
    int return_val_from_scnprintf;

    return_val_from_scnprintf =
        scnprintf(filename, PATH_MAX,
                  "%s/%s", args->share_dir, UNIFYFSD_PID_FILENAME);
    if (return_val_from_scnprintf > (PATH_MAX - 2)) {
        fprintf(stderr, "Unifyfs status filename is too long!\n");
        return -ENOMEM;
    }

    while (1) {
        fp = fopen(filename, "r");
        if (fp) {
            while (fgets(linebuf, 31, fp) != NULL) {
                count++;
            }

            if (count != resource->n_nodes) {
                fprintf(stderr,
                        "incorrect server initialization: "
                        "expected %lu processes but only %u processes found\n",
                        resource->n_nodes, count);
                ret = UNIFYFS_FAILURE;
            }

            fclose(fp);
            break;
        }

        if (errno != ENOENT) {
            fprintf(stderr, "failed to open file %s (%s)\n",
                    filename, strerror(errno));
            ret = -errno;
            break;
        }

        wait_time += interval;
        sleep(interval);

        if (wait_time > args->timeout) {
            ret = UNIFYFS_FAILURE;
            break;
        }
    }

    return ret;
}

enum {
    UNIFYFS_STAGE_IN = 0,
    UNIFYFS_STAGE_OUT = 1,
};

static inline unsigned int estimate_timeout(const char* manifest_file, int op)
{
    /* FIXME: we just wait for 20 mins.
     * In fact, we can roughly estimate time for stage in by looking at the
     * total file size. However, such a method is not possible for stage out
     * because we have no idea on what the total transfer size is.
     */
    return 20 * 60;
}


/**
 * @brief wait until data stage operation finishes
 *
 * @param resource
 * @param args
 *
 * @return
 */
static
int wait_stage(unifyfs_resource_t* resource, unifyfs_args_t* args, int op)
{
    int ret = UNIFYFS_SUCCESS;
    unsigned int interval = 5;
    unsigned int wait_time = 0;
    unsigned int timeout = 0;
    FILE* fp = NULL;
    const char* manifest_file = NULL;
    char filename[PATH_MAX];
    char linebuf[16];
    int return_val_from_scnprintf;

    return_val_from_scnprintf =
        scnprintf(filename, PATH_MAX,
                  "%s/%s", args->share_dir, UNIFYFS_STAGE_STATUS_FILENAME);
    if (return_val_from_scnprintf > (PATH_MAX - 2)) {
        fprintf(stderr, "Unifyfs status filename is too long!\n");
        return -ENOMEM;
    }


    if (op == UNIFYFS_STAGE_IN) {
        manifest_file = args->stage_in;
    } else {
        manifest_file = args->stage_out;
    }

    if (args->stage_timeout > 0) {
        timeout = args->stage_timeout;
    } else {
        timeout = estimate_timeout(manifest_file, op);
    }

    while (1) {
        fp = fopen(filename, "r");
        if (fp) {
            char* line = fgets(linebuf, 15, fp);
            if (0 == strncmp("success", line, strlen("success"))) {
                ret = 0;
                break;      // transfer completed
            } else if (0 == strncmp("fail", line, strlen("fail"))) {
                ret = -EIO;
                break;      // transfer failed
            } else {
                fclose(fp); // try again
            }
        }


        if (errno != ENOENT) {
            fprintf(stderr, "failed to open file %s (%s)\n",
                    UNIFYFS_STAGE_STATUS_FILENAME, strerror(errno));
            ret = -errno;
            break;
        }

        wait_time += interval;
        sleep(interval);

        if (wait_time > timeout) {
            ret = UNIFYFS_FAILURE;
            break;
        }
    }

    return ret;
}

static inline int wait_stage_in(unifyfs_resource_t* resource,
                                unifyfs_args_t* args)
{
    return wait_stage(resource, args, UNIFYFS_STAGE_IN);
}

static inline int wait_stage_out(unifyfs_resource_t* resource,
                                 unifyfs_args_t* args)
{
    return wait_stage(resource, args, UNIFYFS_STAGE_OUT);
}

/**
 * @brief remove server pid file if exists (possibly from previous run).
 * returns 0 (success) if the pid file does not exist.
 *
 * @return 0 on success, negative errno otherwise
 */
static int remove_server_pid_file(unifyfs_args_t* args)
{
    int ret = 0;
    char filename[PATH_MAX];
    int return_val_from_scnprintf;

    return_val_from_scnprintf =
        scnprintf(filename, PATH_MAX,
                  "%s/%s", args->share_dir, UNIFYFSD_PID_FILENAME);
    if (return_val_from_scnprintf > (PATH_MAX - 2)) {
        fprintf(stderr, "Unifyfs status filename is too long!\n");
        return -ENOMEM;
    }

    ret = unlink(filename);
    if (ret) {
        if (ENOENT == errno) {
            ret = 0;
        } else {
            fprintf(stderr, "failed to unlink existing pid file %s (%s)\n",
                    filename, strerror(errno));
            ret = -errno;
        }
    }

    return ret;
}

/**
 * @brief remove stagein/out status file if exists (possibly from previous run).
 * returns 0 (success) if the pid file does not exist.
 *
 * @return 0 on success, negative errno otherwise
 */
static int remove_stage_status_file(unifyfs_args_t* args)
{
    int ret = 0;
    char filename[PATH_MAX];
    int return_val_from_scnprintf;

    return_val_from_scnprintf =
        scnprintf(filename, PATH_MAX,
                  "%s/%s", args->share_dir, UNIFYFS_STAGE_STATUS_FILENAME);
    if (return_val_from_scnprintf > (PATH_MAX - 2)) {
        fprintf(stderr, "Unifyfs stage status filename is too long!\n");
        return -ENOMEM;
    }

    ret = unlink(filename);
    if (ret) {
        if (ENOENT == errno) {
            ret = 0;
        } else {
            fprintf(stderr, "failed to unlink existing stage status file "
                    "%s (%s)\n", filename, strerror(errno));
            ret = -errno;
        }
    }

    return ret;
}

static inline char* str_rtrim(char* str)
{
    if (str) {
        char* pos = &str[strlen(str) - 1];

        while (pos >= str && isspace(*pos)) {
            *pos = '\0';
            pos--;
        }
    }

    return str;
}

/**
 * @brief Get node list from $LSB_HOSTS or $LSB_MCPU_HOSTS.
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int lsf_read_resource(unifyfs_resource_t* resource)
{
    size_t i, n_nodes;
    char* val;
    char* node;
    char* last_node = NULL;
    char* lsb_hosts;
    char* pos;
    char** nodes;
    int mcpu = 0;

    // LSB_HOSTS is space-separated list of host-slots with duplicates,
    // and includes launch node as first entry
    val = getenv("LSB_HOSTS");
    if (val == NULL) {
        // LSB_MCPU_HOSTS is space-separated list of host slot-count pairs,
        // and includes launch node as first entry
        val = getenv("LSB_MCPU_HOSTS");
        if (val == NULL) {
            return -EINVAL;
        } else {
            mcpu = 1;
        }
    }

    // LSB_MCPU_HOSTS string includes a space at the end, which causes extra
    // node count (n_nodes).
    lsb_hosts = str_rtrim(strdup(val));

    // get length of host string
    size_t hosts_len = strlen(lsb_hosts) + 1;

    // pointer to character just past terminating NULL
    char* hosts_end = lsb_hosts + hosts_len;

    // replace spaces with zeroes
    for (pos = lsb_hosts; *pos; pos++) {
        if (isspace(*pos)) {
            *pos = '\0';
        }
    }

    // count nodes, skipping first
    pos = lsb_hosts + (strlen(lsb_hosts) + 1); // skip launch node
    if (!mcpu) {
        last_node = lsb_hosts;
    } else {
        pos += (strlen(pos) + 1);    // skip launch node slot count
    }
    for (n_nodes = 0; pos < hosts_end;) {
        node = pos;
        if (!mcpu) {
            if (strcmp(last_node, node) != 0) {
                n_nodes++;
                last_node = node;
            }
            pos += (strlen(node) + 1); // skip node
        } else {
            n_nodes++;
            pos += (strlen(node) + 1); // skip node
            pos += (strlen(pos) + 1);  // skip count
        }
    }

    nodes = calloc(sizeof(char*), n_nodes);
    if (nodes == NULL) {
        return -ENOMEM;
    }

    // fill nodes array, skipping first
    pos = lsb_hosts + (strlen(lsb_hosts) + 1); // skip launch node
    if (!mcpu) {
        last_node = lsb_hosts;
    } else {
        pos += (strlen(pos) + 1);    // skip launch node slot count
    }
    for (i = 0; pos < hosts_end && i < n_nodes;) {
        node = pos;
        if (!mcpu) {
            if (strcmp(last_node, node) != 0) {
                nodes[i++] = node;
                last_node = node;
            }
            pos += (strlen(node) + 1); // skip node
        } else {
            nodes[i++] = node;
            pos += (strlen(node) + 1); // skip node
            pos += (strlen(pos) + 1);  // skip count
        }
    }

    resource->n_nodes = n_nodes;
    resource->nodes = nodes;

    return 0;
}

/**
 * @brief Get list of nodes from $PBS_NODEFILE
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int pbs_read_resource(unifyfs_resource_t* resource)
{
    size_t n_nodes = 0;
    char* num_nodes_str = NULL;
    char* nodefile = NULL;

    nodefile = getenv("PBS_NODEFILE");
    if (nodefile == NULL) {
        return -EINVAL;
    }

    num_nodes_str = getenv("PBS_NUM_NODES");
    if (num_nodes_str == NULL) {
        return -EINVAL;
    }
    n_nodes = (size_t) strtoul(num_nodes_str, NULL, 10);

    return parse_hostfile(resource, nodefile, n_nodes);
}

/**
 *
 * @brief Get list of nodes using SLURM scontrol
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int slurm_read_resource(unifyfs_resource_t* resource)
{
    int ret;
    size_t len = 0;
    size_t n_nodes = 0;
    char* num_nodes_str = NULL;
    char* cmd = NULL;
    char* hostfile = NULL;
    char* tmpdir = NULL;
    const char* cmd_fmt = "scontrol show hostnames > %s";
    const char* hostfile_fmt = "%s/unifyfs-hosts.%d";
    const char* tmp = "/tmp";

    // get num nodes
    num_nodes_str = getenv("SLURM_NNODES");
    if (num_nodes_str == NULL) {
        return -EINVAL;
    }
    n_nodes = (size_t) strtoul(num_nodes_str, NULL, 10);

    // get temporary hostfile
    tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = (char*)tmp;
    }
    len = strlen(tmpdir) + strlen(hostfile_fmt) + 16;
    hostfile = malloc(len);
    if (hostfile == NULL) {
        return -ENOMEM;
    }
    snprintf(hostfile, len, hostfile_fmt, tmpdir, (int)getpid());

    // write SLURM hostnames to temporary hostfile for parsing
    len = strlen(cmd_fmt) + strlen(hostfile) + 8;
    cmd = malloc(len);
    if (cmd == NULL) {
        return -ENOMEM;
    }
    snprintf(cmd, len, cmd_fmt, hostfile);
    ret = system(cmd);
    free(cmd);

    ret = parse_hostfile(resource, hostfile, n_nodes);
    free(hostfile);

    return ret;
}

// construct_server_argv():
// This function is called in two ways.
// Call it once with server_argv==NULL and it
// will count up the number of arguments you'll have, but
// doesn't construct the list itself.  Call it again with
// the same args but with a buffer in server_argv, and it will
// construct the argument list there.
/**
 * @brief Constructs argument chain to mpi-start (or terminate)
 *        unifyfs server processes.
 *
 * @param args         The command-line options
 * @param server_args  Server argument vector to be filled
 *
 * @return number of server arguments
 */
static size_t construct_server_argv(unifyfs_args_t* args,
                                    char** server_argv)
{
    size_t argc;
    char number[16];

    if (server_argv != NULL) {
        if (args->server_path != NULL) {
            server_argv[0] = strdup(args->server_path);
        } else {
            server_argv[0] = strdup(BINDIR "/unifyfsd");
        }
    }
    argc = 1;

    if (args->debug) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-v");
            snprintf(number, sizeof(number), "%d", args->debug);
            server_argv[argc + 1] = strdup(number);
        }
        argc += 2;
    }

    if (args->cleanup) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-C");
        }
        argc++;
    }

    if (args->consistency != UNIFYFS_CM_LAMINATED) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-c");
            server_argv[argc + 1] =
                strdup(unifyfs_cm_enum_str(args->consistency));
        }
        argc += 2;
    }

    if (args->mountpoint != NULL) {
        if (server_argv != NULL) {
            server_argv[argc] = strdup("-m");
            server_argv[argc + 1] = strdup(args->mountpoint);
        }
        argc += 2;
    }

    if (server_argv != NULL) {
        server_argv[argc] = strdup("-S");
        server_argv[argc + 1] = strdup(args->share_dir);
        server_argv[argc + 2] = strdup("-H");
        server_argv[argc + 3] = strdup(args->share_hostfile);
    }
    argc += 4;

    return argc;
}

// construct_stage_argv:
// this is currently set up to create one rank per compute node,
// mirroring the configuration of the servers.  However, in the
// future, this may be reconfigured to have more, to support
// more files being staged in or out more quickly.
/**
 * @brief Constructs argument chain to mpi-start (or terminate)
 *        unifyfs-stage stagein/out process.
 *
 * @param args        The command-line options
 * @param stage_args  unifyfs-stage argument vector to be filled
 *
 * @return number of server arguments
 */
static size_t construct_stage_argv(unifyfs_args_t* args,
                                   char** stage_argv)
{
    size_t argc = 0;

    if (stage_argv != NULL) {
        stage_argv[0] = strdup(LIBEXECDIR "/unifyfs-stage");
    }
    argc = 1;

    if (args->mountpoint != NULL) {
        if (stage_argv != NULL) {
            stage_argv[argc] = strdup("-m");
            stage_argv[argc + 1] = strdup(args->mountpoint);
        }
        argc += 2;
    }

    if (stage_argv != NULL) {
        char* manifest_file = args->stage_in ? args->stage_in
                              : args->stage_out;

        stage_argv[argc] = strdup("-s");
        stage_argv[argc + 1] = strdup(args->share_dir);
        stage_argv[argc + 2] = strdup(manifest_file);
    }
    argc += 3;

    return argc;
}

/**
 * @brief Default server launch routine
 *
 * @param resource  Not used
 * @param args      Not used
 *
 * @return -ENOSYS
 */
static int invalid_launch(unifyfs_resource_t* resource,
                          unifyfs_args_t* args)
{
    return -ENOSYS;
}

/**
 * @brief Default server terminate routine
 *
 * @param resource  Not used
 * @param args      Not used
 *
 * @return -ENOSYS
 */
static int invalid_terminate(unifyfs_resource_t* resource,
                             unifyfs_args_t* args)
{
    return -ENOSYS;
}

/**
 * @brief Default data stage routine
 *
 * @param resource  Not used
 * @param args      Not used
 *
 * @return -ENOSYS
 */
static int invalid_stage(unifyfs_resource_t* resource,
                         unifyfs_args_t* args)
{
    return -ENOSYS;
}

/**
 * @brief Launch servers using IBM jsrun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int jsrun_launch(unifyfs_resource_t* resource,
                        unifyfs_args_t* args)
{
    size_t argc, jsrun_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: jsrun <jsrun args> <server args>
    jsrun_argc = 13;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + jsrun_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("jsrun");
    argv[1] = strdup("--immediate");
    argv[2] = strdup("-e");
    argv[3] = strdup("individual");
    argv[4] = strdup("--stdio_stderr");
    argv[5] = strdup("unifyfsd.err.%h.%p");
    argv[6] = strdup("--stdio_stdout");
    argv[7] = strdup("unifyfsd.out.%h.%p");
    argv[8] = strdup("--nrs");
    argv[9] = strdup(n_nodes);
    argv[10] = strdup("-r1");
    argv[11] = strdup("-c1");
    argv[12] = strdup("-a1");
    construct_server_argv(args, argv + jsrun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() jsrun to launch unifyfsd");
    return -errno;
}

/**
 * @brief Cleanup servers using IBM jsrun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int jsrun_terminate(unifyfs_resource_t* resource,
                           unifyfs_args_t* args)
{
    size_t argc, jsrun_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: jsrun <jsrun args> pkill -n unifyfsd
    jsrun_argc = 12;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + jsrun_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("jsrun");
    argv[1] = strdup("--immediate");
    argv[2] = strdup("-e");
    argv[3] = strdup("individual");
    argv[4] = strdup("--nrs");
    argv[5] = strdup(n_nodes);
    argv[6] = strdup("-r1");
    argv[7] = strdup("-c1");
    argv[8] = strdup("-a1");
    argv[9] = strdup("pkill");
    argv[10] = strdup("-n");
    argv[11] = strdup("unifyfsd");

    execvp(argv[0], argv);
    perror("failed to execvp() jsrun to pkill unifyfsd");
    return -errno;
}

/**
 * @brief Launch data stage using IBM jsrun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int jsrun_stage(unifyfs_resource_t* resource,
                       unifyfs_args_t* args)
{
    size_t argc, jsrun_argc, stage_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: jsrun <jsrun args> <server args>
    jsrun_argc = 13;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    stage_argc = construct_stage_argv(args, NULL);

    // setup full command argv
    argc = 1 + jsrun_argc + stage_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("jsrun");
    argv[1] = strdup("--immediate");
    argv[2] = strdup("-e");
    argv[3] = strdup("individual");
    argv[4] = strdup("--stdio_stderr");
    argv[5] = strdup("unifyfs-stage.err.%h.%p");
    argv[6] = strdup("--stdio_stdout");
    argv[7] = strdup("unifyfs-stage.out.%h.%p");
    argv[8] = strdup("--nrs");
    argv[9] = strdup(n_nodes);
    argv[10] = strdup("-r1");
    argv[11] = strdup("-c1");
    argv[12] = strdup("-a1");
    construct_stage_argv(args, argv + jsrun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() mpirun to handle data stage");
    return -errno;
}

/**
 * @brief Launch servers using mpirun (OpenMPI)
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int mpirun_launch(unifyfs_resource_t* resource,
                         unifyfs_args_t* args)
{
    size_t argc, mpirun_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: mpirun <mpirun args> <server args>

    mpirun_argc = 5;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + mpirun_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("mpirun");
    argv[1] = strdup("-np");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("--map-by");
    argv[4] = strdup("ppr:1:node");
    construct_server_argv(args, argv + mpirun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() mpirun to launch unifyfsd");
    return -errno;
}

/**
 * @brief Terminate servers using mpirun (OpenMPI)
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int mpirun_terminate(unifyfs_resource_t* resource,
                            unifyfs_args_t* args)
{
    size_t argc, mpirun_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: mpirun <mpirun args> pkill -n unifyfsd
    mpirun_argc = 8;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + mpirun_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("mpirun");
    argv[1] = strdup("-np");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("--map-by");
    argv[4] = strdup("ppr:1:node");
    argv[5] = strdup("pkill");
    argv[6] = strdup("-n");
    argv[7] = strdup("unifyfsd");

    execvp(argv[0], argv);
    perror("failed to execvp() mpirun to pkill unifyfsd");
    return -errno;
}

/**
 * @brief Launch unifyfs-stage using mpirun (OpenMPI)
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int mpirun_stage(unifyfs_resource_t* resource,
                        unifyfs_args_t* args)
{
    size_t argc, mpirun_argc, stage_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: mpirun <mpirun args> <server args>

    mpirun_argc = 5;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    stage_argc = construct_stage_argv(args, NULL);

    // setup full command argv
    argc = 1 + mpirun_argc + stage_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("mpirun");
    argv[1] = strdup("-np");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("--map-by");
    argv[4] = strdup("ppr:1:node");
    construct_stage_argv(args, argv + mpirun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() mpirun to handle data stage");
    return -errno;
}

/**
 * @brief Launch servers using SLURM srun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int srun_launch(unifyfs_resource_t* resource,
                       unifyfs_args_t* args)
{
    size_t argc, srun_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: srun <srun args> <server args>

    srun_argc = 5;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + srun_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("srun");
    argv[1] = strdup("-N");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("--ntasks-per-node");
    argv[4] = strdup("1");
    construct_server_argv(args, argv + srun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() srun to launch unifyfsd");
    return -errno;
}

/**
 * @brief Terminate servers using SLURM srun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int srun_terminate(unifyfs_resource_t* resource,
                          unifyfs_args_t* args)
{
    size_t argc, srun_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: srun <srun args> pkill -n unifyfsd
    srun_argc = 8;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + srun_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("srun");
    argv[1] = strdup("-N");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("-n");
    argv[4] = strdup(n_nodes);
    argv[5] = strdup("pkill");
    argv[6] = strdup("-n");
    argv[7] = strdup("unifyfsd");

    execvp(argv[0], argv);
    perror("failed to execvp() srun to pkill unifyfsd");
    return -errno;
}

/**
 * @brief Launch unifyfs-stage using SLURM srun
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int srun_stage(unifyfs_resource_t* resource,
                      unifyfs_args_t* args)
{
    size_t argc, srun_argc, stage_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: srun <srun args> <server args>

    srun_argc = 5;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    stage_argc = construct_stage_argv(args, NULL);

    // setup full command argv
    argc = 1 + srun_argc + stage_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("srun");
    argv[1] = strdup("-N");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup("--ntasks-per-node");
    argv[4] = strdup("1");
    construct_stage_argv(args, argv + srun_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() srun to launch unifyfsd");
    return -errno;
}

/**
 * @brief Launch servers using custom script
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int script_launch(unifyfs_resource_t* resource,
                         unifyfs_args_t* args)
{
    size_t argc, script_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: <script> <#nodes> <server args>

    script_argc = 2;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + script_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup(args->script);
    argv[1] = strdup(n_nodes);
    construct_server_argv(args, argv + script_argc);

    execvp(argv[0], argv);
    perror("failed to execvp() custom launch script");
    return -errno;
}

/**
 * @brief Terminate servers using custom script
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int script_terminate(unifyfs_resource_t* resource,
                            unifyfs_args_t* args)
{
    size_t argc, script_argc;
    char** argv = NULL;
    char n_nodes[16];

    // full command: <script> <#nodes> pkill -n unifyfsd
    script_argc = 5;
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // setup full command argv
    argc = 1 + script_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup(args->script);
    argv[1] = strdup(n_nodes);
    argv[2] = strdup("pkill");
    argv[3] = strdup("-n");
    argv[3] = strdup("unifyfsd");

    execvp(argv[0], argv);
    perror("failed to execvp() custom terminate script");
    return -errno;
}

/* The following is indexed by unifyfs_rm_e, so the order must
 * match the definition in common/src/rm_enumerator.h
 */
static _ucr_resource_manager_t resource_managers[] = {
    {
        .type = "none",
        .read_resource = &invalid_read_resource,
        .launch = &invalid_launch,
        .terminate = &invalid_terminate,
        .stage = &invalid_stage,
    },
    {
        .type = "pbs",
        .read_resource = &pbs_read_resource,
        .launch = &mpirun_launch,
        .terminate = &mpirun_terminate,
        .stage = &mpirun_stage,
    },
    {
        .type = "slurm",
        .read_resource = &slurm_read_resource,
        .launch = &srun_launch,
        .terminate = &srun_terminate,
        .stage = &srun_stage,
    },
    {
        .type = "lsf",
        .read_resource = &lsf_read_resource,
        .launch = &mpirun_launch,
        .terminate = &mpirun_terminate,
        .stage = &mpirun_stage,
    },
    {
        .type = "lsfcsm",
        .read_resource = &lsf_read_resource,
        .launch = &jsrun_launch,
        .terminate = &jsrun_terminate,
        .stage = &jsrun_stage,
    },
};

int unifyfs_detect_resources(unifyfs_resource_t* resource)
{
    if (getenv("PBS_JOBID") != NULL) {
        resource->rm = UNIFYFS_RM_PBS;
    } else if (getenv("SLURM_JOBID") != NULL) {
        resource->rm = UNIFYFS_RM_SLURM;
    } else if (getenv("LSB_JOBID") != NULL) {
        if (getenv("CSM_ALLOCATION_ID") != NULL) {
            resource->rm = UNIFYFS_RM_LSF_CSM;
        } else {
            resource->rm = UNIFYFS_RM_LSF;
        }
    } else {
        resource->rm = UNIFYFS_RM_INVALID;
    }

    return resource_managers[resource->rm].read_resource(resource);
}

int unifyfs_start_servers(unifyfs_resource_t* resource,
                          unifyfs_args_t* args)
{
    int rc;
    pid_t pid;

    if ((resource == NULL) || (args == NULL)) {
        return -EINVAL;
    }

    rc = write_hostfile(resource, args);
    if (rc) {
        fprintf(stderr, "ERROR: failed to write shared server hostfile\n");
        return rc;
    }

    rc = remove_server_pid_file(args);
    if (rc) {
        fprintf(stderr, "ERROR: failed to remove server pid file\n");
        return rc;
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "failed to create server launch process (%s)\n",
                strerror(errno));
        return -errno;
    } else if (pid == 0) {
        if (args->script != NULL) {
            return script_launch(resource, args);
        } else {
            return resource_managers[resource->rm].launch(resource, args);
        }
    }

    rc = wait_server_initialization(resource, args);
    if (rc) {
        fprintf(stderr, "ERROR: failed to wait for server initialization\n");
    }

    if (args->stage_in) {
        rc = remove_stage_status_file(args);
        if (rc) {
            fprintf(stderr, "ERROR: failed to remove stage status file\n");
            return rc;
        }

        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "failed to create stage-in launch process (%s)\n",
                    strerror(errno));
            return -errno;
        } else if (pid == 0) {
            return resource_managers[resource->rm].stage(resource, args);
        }

        rc = wait_stage_in(resource, args);
        if (rc) {
            fprintf(stderr, "failed to detect the stage in status (rc=%d)\n",
                    rc);
        }
    }

    return rc;
}

int unifyfs_stop_servers(unifyfs_resource_t* resource,
                         unifyfs_args_t* args)
{
    int rc;
    pid_t pid;

    if ((resource == NULL) || (args == NULL)) {
        return -EINVAL;
    }

    if (args->stage_out) {
        rc = remove_stage_status_file(args);
        if (rc) {
            fprintf(stderr, "ERROR: failed to remove stage status file\n");
            return rc;
        }

        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "failed to create stage-out launch process (%s)\n",
                    strerror(errno));
            return -errno;
        } else if (pid == 0) {
            return resource_managers[resource->rm].stage(resource, args);
        }

        rc = wait_stage_out(resource, args);
        if (rc) {
            fprintf(stderr, "failed to detect the data out status (rc=%d)\n",
                    rc);
        }
    }

    if (args->script != NULL) {
        return script_terminate(resource, args);
    } else {
        return resource_managers[resource->rm].terminate(resource, args);
    }
}
