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

static inline
int construct_server_pids_filename(unifyfs_args_t* args)
{
    char filename[PATH_MAX];
    int rc = scnprintf(filename, sizeof(filename), "%s/%s",
                       args->share_dir, UNIFYFS_SERVER_PID_FILENAME);
    if (rc > (sizeof(filename) - 2)) {
        fprintf(stderr, "Unifyfs status filename is too long!\n");
        return ENOMEM;
    } else {
        args->share_pidfile = strdup(filename);
        if (NULL == args->share_pidfile) {
            return ENOMEM;
        }
    }
    return 0;
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
    int err;
    int ret = UNIFYFS_SUCCESS;
    size_t count = 0;
    unsigned int interval = 3;
    unsigned int wait_time = 0;
    FILE* fp = NULL;
    char linebuf[32];

    while (1) {
        errno = 0;
        fp = fopen(args->share_pidfile, "r");
        err = errno;
        if (fp) {
            while (fgets(linebuf, 31, fp) != NULL) {
                count++;
            }
            if (count != resource->n_nodes) {
                fprintf(stderr,
                        "only found %zu of %zu server processes\n",
                        count, resource->n_nodes);
                ret = UNIFYFS_FAILURE;
            }
            fclose(fp);
            break;
        } else if (ENOENT != err) {
            fprintf(stderr, "failed to open file %s (%s)\n",
                    args->share_pidfile, strerror(err));
            ret = -err;
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

static inline unsigned int estimate_timeout(const char* manifest_file)
{
    /* crude guess: 20 minutes */
    return 20 * 60;
}

static inline
int construct_stage_status_filename(unifyfs_args_t* args)
{
    char filename[PATH_MAX];
    int rc = scnprintf(filename, sizeof(filename), "%s/%s.%s",
                       args->share_dir,
                       UNIFYFS_STAGE_STATUS_FILENAME,
                       (args->stage_in ? "in" : "out"));
    if (rc > (sizeof(filename) - 2)) {
        fprintf(stderr, "UnifyFS status filename is too long!\n");
        return ENOMEM;
    } else {
        args->stage_status = strdup(filename);
        if (NULL == args->stage_status) {
            return ENOMEM;
        }
    }
    return 0;
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
int wait_stage(unifyfs_resource_t* resource, unifyfs_args_t* args)
{
    int err;
    int ret = UNIFYFS_SUCCESS;
    unsigned int interval = 5;
    unsigned int wait_time = 0;
    unsigned int timeout = 0;
    FILE* fp = NULL;
    const char* manifest_file = NULL;
    char filename[PATH_MAX];
    char linebuf[16];

    if (args->stage_timeout > 0) {
        timeout = args->stage_timeout;
    } else {
        timeout = estimate_timeout(manifest_file);
    }

    while (1) {
        errno = 0;
        fp = fopen(args->stage_status, "r");
        err = errno;
        if (fp) {
            char* line = fgets(linebuf, 15, fp);
            if (0 == strncmp("success", line, strlen("success"))) {
                // transfer completed successfully
                fclose(fp);
                fp = NULL;
                ret = 0;
                break;
            } else if (0 == strncmp("fail", line, strlen("fail"))) {
                // transfer failed
                fclose(fp);
                fp = NULL;
                ret = -EIO;
                break;
            } else { // opened, but no content yet? try again
                fclose(fp);
                fp = NULL;
            }
        } else if (ENOENT != err) {
            fprintf(stderr, "failed to open file %s (%s)\n",
                    filename, strerror(err));
            ret = -err;
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

/**
 * @brief remove server pid file if exists (possibly from previous run).
 * returns 0 (success) if the pid file does not exist.
 *
 * @return 0 on success, negative errno otherwise
 */
static int remove_server_pid_file(unifyfs_args_t* args)
{
    int err, rc;
    int ret = 0;

    errno = 0;
    rc = unlink(args->share_pidfile);
    err = errno;
    if (rc) {
        if (ENOENT == err) {
            ret = 0;
        } else {
            fprintf(stderr, "failed to unlink existing pid file %s (%s)\n",
                    args->share_pidfile, strerror(err));
            ret = -err;
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
    int err, rc;
    int ret = 0;

    errno = 0;
    rc = unlink(args->stage_status);
    err = errno;
    if (rc) {
        if (ENOENT == err) {
            ret = 0;
        } else {
            fprintf(stderr,
                    "failed to unlink existing stage status file %s (%s)\n",
                    args->stage_status, strerror(err));
            ret = -err;
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

/**
 *
 * @brief Get list of nodes using Flux resource
 *
 * @param resource  The job resource record to be filled
 *
 * @return 0 on success, negative errno otherwise
 */
static int flux_read_resource(unifyfs_resource_t* resource)
{
    size_t n_nodes = 0;
    char  num_nodes_str[10] = {0};
    char  nodelist_str[1024] = {0};
    char* ret = NULL;
    FILE* pipe_fp = NULL;
    char** nodes = NULL;
    int node_idx = 0;

    // get num nodes using flux resource command
    pipe_fp = popen("flux resource list --states=free -no {nnodes}", "r");
    ret = fgets(num_nodes_str, sizeof(num_nodes_str), pipe_fp);
    if (ret == NULL) {
        pclose(pipe_fp);
        return -EINVAL;
    }
    n_nodes = (size_t) strtoul(num_nodes_str, NULL, sizeof(num_nodes_str));
    pclose(pipe_fp);

    nodes = calloc(sizeof(char*), n_nodes);
    if (nodes == NULL) {
        return -ENOMEM;
    }

    // get node list using flux resource command
    // the returned list is in a condensed format
    // e.g., tioga[18-19,21,32]
    // TODO: is it safe to assume flux resource only
    // returns a single line?
    pipe_fp = popen("flux resource list --states=free -no {nodelist}", "r");
    ret = fgets(nodelist_str, sizeof(nodelist_str), pipe_fp);
    if (ret == NULL) {
        pclose(pipe_fp);
        return -EINVAL;
    }
    pclose(pipe_fp);

    // get the node ids, i.e., the list in []
    char* node_ids = strstr(nodelist_str, "[");
    if (node_ids) {
        // remove the trailing "]\n"
        nodelist_str[strlen(nodelist_str)-2] = 0;
        char* host = calloc(1, (node_ids-nodelist_str)+2);
        strncpy(host, nodelist_str, (node_ids-nodelist_str));

        // separate by ","
        char* end_str;
        char* token = strtok_r(node_ids+1, ",", &end_str);
        while (token) {
            // case 1: contiguous node range
            //         e.g., 3-5, lo=3, hi=5
            // case 2: a single node, then lo=hi
            int lo, hi;
            if (strstr(token, "-")) {
                char* end_str2;
                char* lo_str = strtok_r(token, "-", &end_str2);
                char* hi_str = strtok_r(NULL, "-", &end_str2);
                lo = atoi(lo_str);
                hi = atoi(hi_str);
            } else {
                lo = atoi(token);
                hi = lo;
            }

            for (int i = lo; i <= hi; i++) {
                char nodename[30] = {0};
                sprintf(nodename, "%s%d", host, i);
                if (node_idx >= n_nodes) {
                    return -EINVAL;
                }
                nodes[node_idx++] = strdup(nodename);
            }
            token = strtok_r(NULL, ",", &end_str);
        }
    } else {
        // no '[' in the string, meaning it has a single node
        if (n_nodes != 1) {
            return -EINVAL;
        }
        nodes[0] = strdup(nodelist_str);
    }

    resource->n_nodes = n_nodes;
    resource->nodes = nodes;
    return 0;
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
 * @brief Constructs argument chain to start (or terminate)
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
        stage_argv[0] = strdup(BINDIR "/unifyfs-stage");
    }
    argc = 1;

    if (args->mountpoint != NULL) {
        if (stage_argv != NULL) {
            stage_argv[argc] = strdup("-m");
            stage_argv[argc + 1] = strdup(args->mountpoint);
        }
        argc += 2;
    }

    if (args->debug) {
        if (stage_argv != NULL) {
            stage_argv[argc] = strdup("--verbose");
        }
        argc += 1;
    }

    if (args->stage_parallel) {
        if (stage_argv != NULL) {
            stage_argv[argc] = strdup("--parallel");
        }
        argc += 1;
    }

    if (stage_argv != NULL) {
        char* manifest_file = args->stage_in ? args->stage_in
                                             : args->stage_out;

        stage_argv[argc]     = strdup("--status-file");
        stage_argv[argc + 1] = strdup(args->stage_status);
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

static int generic_stage(char* cmd, int run_argc, unifyfs_args_t* args)
{
    size_t argc, stage_argc;
    char** argv = NULL;

    stage_argc = construct_stage_argv(args, NULL);

    argc = 1 + run_argc + stage_argc;
    argv = calloc(argc, sizeof(char*));

    char* token = strtok(cmd, " ");
    for (int i = 0; i < run_argc; i++) {
        argv[i] = token;
        token = strtok(NULL, " ");
    }

    construct_stage_argv(args, argv + run_argc);
    if (args->debug) {
        for (int i = 0; i < (argc - 1); i++) {
            fprintf(stdout, "UNIFYFS STAGE DEBUG: stage_argv[%d] = %s\n",
                    i, argv[i]);
            fflush(stdout);
        }
    }
    execvp(argv[0], argv);

    return -errno;
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
    char n_cores[8];

    snprintf(n_cores, sizeof(n_cores), "-c%d", resource->n_cores_per_server);
    snprintf(n_nodes, sizeof(n_nodes), "%zu", resource->n_nodes);

    // full command: jsrun <jsrun args> <server args>
    jsrun_argc = 13;
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
    argv[10] = strdup("-r1"); /* one resource set per node */
    argv[11] = strdup(n_cores);
    argv[12] = strdup("-a1");
    construct_server_argv(args, argv + jsrun_argc);

    if (args->debug) {
        for (int i = 0; i < (argc - 1); i++) {
            fprintf(stdout, "UNIFYFS LAUNCH DEBUG: jsrun argv[%d] = %s\n",
                    i, argv[i]);
            fflush(stdout);
        }
    }

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
    size_t jsrun_argc = 13;
    char cmd[200];

    // full command: jsrun <jsrun args> <server args>
    snprintf(cmd, sizeof(cmd),
             "jsrun --immediate -e individual --stdio_stderr unifyfs-stage.err.%%h.%%p --stdio_stdout unifyfs-stage.out.%%h.%%p --nrs %zu -r1 -c1 -a1",
             resource->n_nodes);

    generic_stage(cmd, jsrun_argc, args);

    perror("failed to execvp() jsrun to handle data stage");
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

    if (args->debug) {
        for (int i = 0; i < (argc - 1); i++) {
            fprintf(stdout, "UNIFYFS LAUNCH DEBUG: mpirun argv[%d] = %s\n",
                    i, argv[i]);
            fflush(stdout);
        }
    }

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
    size_t mpirun_argc = 5;
    char cmd[200];

    // full command: mpirun <mpirun args> <server args>
    snprintf(cmd, sizeof(cmd), "mpirun --np %zu --map-by ppr:1:node",
             resource->n_nodes);

    generic_stage(cmd, mpirun_argc, args);

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
    char n_cores[8];

    snprintf(n_cores, sizeof(n_cores), "-c%d", resource->n_cores_per_server);
    snprintf(n_nodes, sizeof(n_nodes), "-N%zu", resource->n_nodes);

    // full command: srun <srun args> <server args>
    srun_argc = 6;
    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + srun_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("srun");
    argv[1] = strdup("--exact");
    argv[2] = strdup("--overlap");
    argv[3] = strdup(n_nodes);
    argv[4] = strdup("--ntasks-per-node=1");
    argv[5] = strdup(n_cores);
    construct_server_argv(args, argv + srun_argc);

    if (args->debug) {
        for (int i = 0; i < (argc - 1); i++) {
            fprintf(stdout, "UNIFYFS LAUNCH DEBUG: srun argv[%d] = %s\n",
                    i, argv[i]);
            fflush(stdout);
        }
    }

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

    snprintf(n_nodes, sizeof(n_nodes), "-N%zu", resource->n_nodes);

    // full command: srun <srun args> pkill -n unifyfsd
    srun_argc = 8;
    argc = 1 + srun_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("srun");
    argv[1] = strdup("--exact");
    argv[2] = strdup("--overcommit");
    argv[3] = strdup(n_nodes);
    argv[4] = strdup("--ntasks-per-node=1");
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
    size_t srun_argc = 3;
    char cmd[200];

    // full command: srun <srun args> <server args>
    snprintf(cmd, sizeof(cmd), "srun -N%zu --ntasks-per-node=1",
             resource->n_nodes);

    generic_stage(cmd, srun_argc, args);

    perror("failed to execvp() srun to launch unifyfsd");
    return -errno;
}

/**
 * @brief Launch servers using Flux
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int flux_launch(unifyfs_resource_t* resource,
                       unifyfs_args_t* args)
{
    size_t argc, flux_argc, server_argc;
    char** argv = NULL;
    char n_nodes[16];
    char n_tasks[16];
    char n_cores[8];

    snprintf(n_nodes, sizeof(n_nodes), "-N%zu", resource->n_nodes);
    // without -n, --ntasks, Flux will schedule the server job
    // to use all nodes exclusively
    snprintf(n_tasks, sizeof(n_tasks), "-n%zu", resource->n_nodes);
    snprintf(n_cores, sizeof(n_cores), "-c%d", resource->n_cores_per_server);

    // full command: srun <srun args> <server args>
    flux_argc = 5;
    server_argc = construct_server_argv(args, NULL);

    // setup full command argv
    argc = 1 + flux_argc + server_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("flux");
    argv[1] = strdup("run");
    argv[2] = strdup(n_nodes);
    argv[3] = strdup(n_tasks);
    argv[4] = strdup(n_cores);
    construct_server_argv(args, argv + flux_argc);

    if (args->debug) {
        for (int i = 0; i < (argc - 1); i++) {
            fprintf(stdout, "UNIFYFS LAUNCH DEBUG: flux argv[%d] = %s\n",
                    i, argv[i]);
            fflush(stdout);
        }
    }

    execvp(argv[0], argv);
    perror("failed to execvp() flux to launch unifyfsd");
    return -errno;
}

/**
 * @brief Terminate servers using Flux
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int flux_terminate(unifyfs_resource_t* resource,
                          unifyfs_args_t* args)
{
    size_t argc, flux_argc;
    char** argv = NULL;

    // full command: flux <flux args> pkill name:unifyfsd
    flux_argc = 3;
    argc = 1 + flux_argc;
    argv = calloc(argc, sizeof(char*));
    argv[0] = strdup("flux");
    argv[1] = strdup("pkill");
    argv[2] = strdup("name:unifyfsd");

    execvp(argv[0], argv);
    perror("failed to execvp() flux to pkill unifyfsd");
    return -errno;
}

/**
 * @brief Launch unifyfs-stage using flux run
 *
 * @param resource The job resource record
 * @param args     The command-line options
 *
 * @return
 */
static int flux_stage(unifyfs_resource_t* resource,
                      unifyfs_args_t* args)
{
    size_t flux_argc = 5;
    char cmd[200];

    // full command: flux run <flux args> <server args>
    snprintf(cmd, sizeof(cmd), "flux run -N%zu -n%zu -c1",
             resource->n_nodes, resource->n_nodes);

    generic_stage(cmd, flux_argc, args);

    perror("failed to execvp() flux to launch unifyfsd");
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
    {
        .type = "flux",
        .read_resource = &flux_read_resource,
        .launch = &flux_launch,
        .terminate = &flux_terminate,
        .stage = &flux_stage,
    },
};

int unifyfs_detect_resources(unifyfs_resource_t* resource)
{
    if (getenv("PBS_JOBID") != NULL) {
        resource->rm = UNIFYFS_RM_PBS;
    } else if (getenv("SLURM_JOBID") != NULL) {
        resource->rm = UNIFYFS_RM_SLURM;
    } else if (getenv("FLUX_EXEC_PATH") != NULL) {
        // TODO: need to use a better environment
        // variable or maybe a better way to decide
        // whether to use flux scheduler.
        resource->rm = UNIFYFS_RM_FLUX;
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

    /* check environment for number of cores per server */
    resource->n_cores_per_server = 1;
    char* server_cores = getenv("UNIFYFS_SERVER_CORES");
    if (NULL != server_cores) {
        errno = 0;
        long l = strtol(server_cores, NULL, 0);
        if (0 == errno) {
            resource->n_cores_per_server = (int)l;
        } else {
            fprintf(stderr, "Invalid UNIFYFS_SERVER_CORES value '%s'\n",
                    server_cores);
        }
    }

    rc = write_hostfile(resource, args);
    if (rc) {
        fprintf(stderr, "Failed to write server hosts file!\n");
        return rc;
    }

    rc = construct_server_pids_filename(args);
    if (rc) {
        fprintf(stderr, "Failed to construct server pids filename!\n");
        return rc;
    }

    rc = remove_server_pid_file(args);
    if (rc) {
        fprintf(stderr, "Failed to remove server pids file!\n");
        return rc;
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to create server launch process (%s)\n",
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
        fprintf(stderr, "Failed to wait for server initialization\n");
    }

    if (args->stage_in) {
        rc = construct_stage_status_filename(args);
        if (rc) {
            return rc;
        }

        rc = remove_stage_status_file(args);
        if (rc) {
            fprintf(stderr, "Failed to remove stage status file\n");
            return rc;
        }

        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Failed to create stage-in launch process (%s)\n",
                    strerror(errno));
            return -errno;
        } else if (pid == 0) {
            return resource_managers[resource->rm].stage(resource, args);
        }

        rc = wait_stage(resource, args);
        if (rc) {
            fprintf(stderr, "Failed to detect the stage-in status (rc=%d)\n",
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
        rc = construct_stage_status_filename(args);
        if (rc) {
            return rc;
        }

        rc = remove_stage_status_file(args);
        if (rc) {
            fprintf(stderr, "Failed to remove stage status file\n");
            return rc;
        }

        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Failed to create stage-out launch process (%s)\n",
                    strerror(errno));
            return -errno;
        } else if (pid == 0) {
            return resource_managers[resource->rm].stage(resource, args);
        }

        rc = wait_stage(resource, args);
        if (rc) {
            fprintf(stderr, "Failed to detect the stage-out status (rc=%d)\n",
                    rc);
        }
    }

    if (args->script != NULL) {
        return script_terminate(resource, args);
    } else {
        return resource_managers[resource->rm].terminate(resource, args);
    }
}
