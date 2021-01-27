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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>
#include <getopt.h>
#include <mpi.h>
#include <unifyfs.h>

#include "testlib.h"

static int fd;              /* target file descriptor */
static int standard;        /* not mounting unifyfs when set */

static int rank;
static int total_ranks;

static int create_rank;
static int open_rank;
static int do_stat;         /* perform stat after closing the file */
static int debug;           /* pause for attaching debugger */
static int exclusive;
static int trunc;
static char* mountpoint = "/unifyfs";   /* unifyfs mountpoint */
static char* filename = "testfile";     /* testfile name under mountpoint */
static char targetfile[NAME_MAX];       /* target file name */

static struct option long_opts[] = {
    { "create", 1, 0, 'c' },
    { "debug", 0, 0, 'd' },
    { "exclusive", 0, 0, 'e' },
    { "filename", 1, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "open", 1, 0, 'o' },
    { "standard", 0, 0, 's' },
    { "stat", 0, 0, 'S' },
    { "truncate", 0, 0, 't' },
    { 0, 0, 0, 0},
};

static char* short_opts = "c:def:hm:o:sSt";

static const char* usage_str =
    "\n"
    "Usage: %s [options...]\n"
    "\n"
    "Available options:\n"
    " -c, --create=<RANK>        create the file from <RANK>\n"
    "                            (default: 0)\n"
    " -d, --debug                pause before running test\n"
    "                            (handy for attaching in debugger)\n"
    " -e, --exclusive            pass O_EXCL to fail when the file exists\n"
    " -f, --filename=<filename>  target file name under mountpoint\n"
    "                            (default: testfile)\n"
    " -h, --help                 help message\n"
    " -m, --mount=<mountpoint>   use <mountpoint> for unifyfs\n"
    "                            (default: /unifyfs)\n"
    " -o, --open=<RANK>          open file from <RANK> after create\n"
    "                            (default: 0)\n"
    " -s, --standard             do not use unifyfs but run standard I/O\n"
    " -S, --stat                 perform stat after closing\n"
    " -t, --truncate             truncate file if exists\n"
    "\n";

static char* program;

static void print_usage(void)
{
    test_print_once(rank, usage_str, program);
    exit(0);
}

int main(int argc, char** argv)
{
    int ret = 0;
    int ch = 0;
    int optidx = 2;

    program = basename(strdup(argv[0]));

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            create_rank = atoi(optarg);
            break;

        case 'd':
            debug = 1;
            break;

        case 'e':
            exclusive = 1;
            break;

        case 'f':
            filename = strdup(optarg);
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 'o':
            open_rank = atoi(optarg);
            break;

        case 's':
            standard = 1;
            break;

        case 'S':
            do_stat = 1;
            break;

        case 't':
            trunc = 1;
            break;

        case 'h':
        default:
            print_usage();
            break;
        }
    }

    if (static_linked(program) && standard) {
        test_print_once(rank, "--standard, -s option only works when "
                        "dynamically linked.");
        exit(-1);
    }

    sprintf(targetfile, "%s/%s", mountpoint, filename);

    if (debug) {
        test_pause(rank, "Attempting to mount");
    }

    if (exclusive && trunc) {
        test_print_once(rank, "-e and -t cannot be used together.");
        exit(-1);
    }

    if (!standard) {
        ret = unifyfs_mount(mountpoint, rank, total_ranks, 0);
        if (ret) {
            test_print(rank, "unifyfs_mount failed (return = %d)", ret);
            exit(-1);
        }
    }

    if ((create_rank < 0 || create_rank > total_ranks - 1) ||
        (open_rank < 0 || open_rank > total_ranks - 1)) {
        test_print(rank, "please specify valid rank\n");
        exit(-1);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    /* create the file from the create_rank */
    if (rank == create_rank) {
        int flags = O_CREAT|O_RDWR;

        if (exclusive) {
            flags |= O_EXCL;
        } else if (trunc) {
            flags |= O_TRUNC;
        }

        fd = open(targetfile, flags, 0600);
        if (fd < 0) {
            test_print(rank, "open failed (%d: %s)\n", errno, strerror(errno));
        } else {
            test_print(rank, "created file %s successfully\n", targetfile);
            close(fd);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    errno = 0;

    /* open from all ranks (open_rank == 0) or a specific open_rank */
    if (!open_rank || rank == open_rank) {
        fd = open(targetfile, O_RDWR);
        if (fd < 0) {
            test_print(rank, "open failed (%d: %s)\n", errno, strerror(errno));
        } else {
            test_print(rank, "opened file %s successfully\n", targetfile);
            close(fd);
        }

        if (do_stat) {
            struct stat sb = { 0, };

            errno = 0;
            ret = stat(targetfile, &sb);
            if (ret < 0) {
                test_print(rank, "stat failed (%d: %s)\n",
                           errno, strerror(errno));
            } else {
                dump_stat(rank, &sb, targetfile);
            }
        }
    }

    if (!standard) {
        unifyfs_unmount();
    }

    MPI_Finalize();

    return ret;
}

