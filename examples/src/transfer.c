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


#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#include <mpi.h>
#include <unifyfs.h>

#include "testlib.h"


static unsigned long bufsize = 64 * (1 << 10);

/* options */
static int rank;
static int total_ranks;
static int rank_worker;
static int parallel;
static int debug;
static char* mountpoint;  /* unifyfs mountpoint */

static struct option long_opts[] = {
    { "debug", 0, 0, 'd' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "parallel", 0, 0, 'p' },
    { "rank", 1, 0, 'r' },
    { 0, 0, 0, 0},
};

static char* short_opts = "dhm:pr:";

static const char* usage_str =
    "\n"
    "Usage: %s [options...] <source path> <destination path>\n"
    "\n"
    "Available options:\n"
    " -d, --debug                  pause before running test\n"
    "                              (handy for attaching in debugger)\n"
    " -h, --help                   help message\n"
    " -m, --mount=<mountpoint>     use <mountpoint> for unifyfs\n"
    "                              (default: /unifyfs)\n"
    " -p, --parallel               parallel transfer\n"
    " -r, --rank=<rank>            use <rank> for transfer (default: 0)\n"
    "\n";

static char* program;

static void print_usage(void)
{
    test_print_once(rank, usage_str, program);
    exit(0);
}

int main(int argc, char** argv)
{
    int mounted = 0;
    int ret = 0;
    int ch = 0;
    int optidx = 0;

    size_t srclen;
    char* srcpath;
    char* dstpath;

    program = basename(strdup(argv[0]));

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'b':
            bufsize = strtoul(optarg, NULL, 0);
            break;

        case 'd':
            debug = 1;
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 'p':
            parallel = 1;
            break;

        case 'r':
            rank_worker = atoi(optarg);
            if (rank_worker >= total_ranks) {
                test_print(rank, "ERROR - %d is not a valid rank");
                print_usage();
            }
            break;

        case 'h':
        default:
            print_usage();
            break;
        }
    }

    /* get source and destination files */
    if (argc - optind != 2) {
        print_usage();
    }
    srcpath = strdup(argv[optind++]);
    dstpath = strdup(argv[optind++]);

    srclen = strlen(srcpath);
    if (srcpath[srclen - 1] == '/') {
        srcpath[srclen - 1] = '\0';
    }

    if (debug) {
        test_pause(rank, "Before mounting UnifyFS");
    }

    if (NULL == mountpoint) {
        mountpoint = strdup("/unifyfs");
    }

    ret = unifyfs_mount(mountpoint, rank, total_ranks);
    if (ret) {
        test_print(rank, "unifyfs_mount(%s) failed (rc=%d)",
                   mountpoint, ret);
    } else {
        mounted = 1;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (mounted) {
        if (parallel) {
            ret = unifyfs_transfer_file_parallel(srcpath, dstpath);
            if (ret) {
                test_print(rank, "paralled transfer failed (rc=%d: %s)",
                        ret, strerror(ret));
            }
        } else if (rank == rank_worker) {
            ret = unifyfs_transfer_file_serial(srcpath, dstpath);
            if (ret) {
                test_print(rank, "serial transfer failed (rc=%d: %s)",
                        ret, strerror(ret));
            }
        }
        unifyfs_unmount();
    }

    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Finalize();

    free(mountpoint);
    free(srcpath);
    free(dstpath);

    return ret;
}

