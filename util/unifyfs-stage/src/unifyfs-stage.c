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
/* unifyfs-stage: this application is supposed to excuted by the unifyfs
 * command line utility for:
 * - stage in: moving files in pfs to unifyfs volume before user starts
 *   application,
 *   e.g., unifyfs start --stage-in=<manifest file>
 * - stage out: moving files in the unifyfs volume to parallel file system
 *   after user application completes,
 *   e.g., unifyfs terminate --stage-out=<manifest file>
 *
 * Currently, we request users to pass the <manifest file> to specify target
 * files to be transferred. The <manifest file> should list all target files
 * and their destinations, line by line.
 *
 * This supports two transfer modes (although both are technically parallel):
 *
 * - serial: Each process will transfer a file. Data of a single file will
 *   reside in a single compute node.
 * - parallel (-p, --parallel): Each file will be split and transferred by all
 *   processes. Data of a single file will be spread evenly across all
 *   available compute nodes.
 *
 * TODO:
 * Maybe later on, it would be better to have a size threshold. Based on the
 * threshold, we can determine whether a file needs to transferred serially (if
 * smaller than threshold), or parallelly.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <mpi.h>
#include <unifyfs_misc.h>

#include "unifyfs_const.h"
#include "unifyfs-stage.h"

int rank;
int total_ranks;
int verbose;

static int debug;
static int checksum;
static int mode;
static char* manifest_file;
static char* mountpoint = "/unifyfs";
static char* share_dir;

static unifyfs_stage_t _ctx;

/**
 * @brief create a status (lock) file to notify the unifyfs executable
 * when the staging is finished
 *
 * @param status 0 indicates success
 *
 * @return 0 on success, errno otherwise
 */
static int create_status_file(int status)
{
    char filename[PATH_MAX];
    FILE* fp = NULL;
    const char* msg = status ? "fail" : "success";
    int return_val_from_scnprintf;

    return_val_from_scnprintf =
        scnprintf(filename, PATH_MAX,
		  "%s/%s", share_dir, UNIFYFS_STAGE_STATUS_FILENAME);
    if (return_val_from_scnprintf > (PATH_MAX-1)) {
        fprintf(stderr, "Stage status file is too long!\n");
        return -ENOMEM;
    }

    fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "failed to create %s (%s)\n",
                        filename, strerror(errno));
        return errno;
    }

    fprintf(fp, "%s\n", msg);

    fclose(fp);

    return 0;
}

static struct option long_opts[] = {
    { "checksum", 0, 0, 'c' },
    { "debug", 0, 0, 'd' },
    { "help", 0, 0, 'h' },
    { "mountpoint", 1, 0, 'm' },
    { "parallel", 0, 0, 'p' },
    { "share-dir", 1, 0, 's' },
    { "verbose", 0, 0, 'v' },
    { 0, 0, 0, 0 },
};

static char* short_opts = "cdhm:ps:v";

static const char* usage_str =
    "\n"
    "Usage: %s [OPTION]... <manifest file>\n"
    "\n"
    "Transfer files between unifyfs volume and external file system.\n"
    "The <manifest file> should contain list of files to be transferred,\n"
    "and each line should be formatted as\n"
    "\n"
    "  /source/file/path,/destination/file/path\n"
    "\n"
    "Specifying directories is not supported.\n"
    "\n"
    "Available options:\n"
    "\n"
    "  -c, --checksum           verify md5 checksum for each transfer\n"
    "  -h, --help               print this usage\n"
    "  -m, --mountpoint=<mnt>   use <mnt> as unifyfs mountpoint\n"
    "                           (default: /unifyfs)\n"
    "  -p, --parallel           transfer each file in parallel\n"
    "                           (experimental)\n"
    "  -s, --share-dir=<path>   directory path for creating status file\n"
    "  -v, --verbose            print noisy outputs\n"
    "\n"
    "Without the '-p, --parallel' option, a file is transferred by a single\n"
    "process. If the '-p, --parallel' option is specified, each file will be\n"
    "divided by multiple processes and transferred in parallel.\n"
    "\n";

static char* program;

static void print_usage(void)
{
    if (0 == rank) {
        fprintf(stdout, usage_str, program);
    }
}

static inline
void debug_pause(int rank, const char* fmt, ...)
{
    if (rank == 0) {
        va_list args;

        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);

        fprintf(stderr, " ENTER to continue ... ");

        (void) getchar();
    }

    MPI_Barrier(MPI_COMM_WORLD);

    /* internal accept() call from mpi may set errno */
    errno = 0;
}

static int parse_option(int argc, char** argv)
{
    int ch = 0;
    int optidx = 0;
    char* filepath = NULL;

    if (argc < 2) {
        return EINVAL;
    }

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            checksum = 1;
            break;

        case 'd':
            debug = 1;
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 'p':
            mode = UNIFYFS_STAGE_PARALLEL;
            break;

        case 's':
            share_dir = strdup(optarg);
            break;

        case 'v':
            verbose = 1;
            break;

        case 'h':
        default:
            break;
        }
    }

    if (argc - optind != 1) {
        return EINVAL;
    }

    filepath = argv[optind];

    manifest_file = realpath(filepath, NULL);
    if (!manifest_file) {
        fprintf(stderr, "problem with accessing file %s: %s\n",
                        filepath, strerror(errno));
        return errno;
    }

    return 0;
}

int main(int argc, char** argv)
{
    int ret = 0;
    unifyfs_stage_t* ctx = &_ctx;

    program = basename(strdup(argv[0]));

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    ret = parse_option(argc, argv);
    if (ret) {
        if (EINVAL == ret) {
            print_usage();
        }
        goto out;
    }

    ctx->rank = rank;
    ctx->total_ranks = total_ranks;
    ctx->checksum = checksum;
    ctx->mode = mode;
    ctx->mountpoint = mountpoint;
    ctx->manifest_file = manifest_file;

    if (verbose) {
        unifyfs_stage_print(ctx);
    }

    if (debug) {
        debug_pause(rank, "About to mount unifyfs.. ");
    }

    ret = unifyfs_mount(mountpoint, rank, total_ranks, 0);
    if (ret) {
        fprintf(stderr, "failed to mount unifyfs at %s (%s)",
                        ctx->mountpoint, strerror(ret));
        goto out;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    ret = unifyfs_stage_transfer(ctx);
    if (ret) {
        fprintf(stderr, "data transfer failed (%s)\n", strerror(errno));
    }

    if (share_dir) {
        ret = create_status_file(ret);
        if (ret) {
            fprintf(stderr, "failed to create the status file (%s)\n",
                            strerror(errno));
        }
    }

    ret = unifyfs_unmount();
    if (ret) {
        fprintf(stderr, "unmounting unifyfs failed (ret=%d)\n", ret);
    }
out:
    MPI_Finalize();

    return ret;
}

