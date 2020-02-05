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
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <getopt.h>
#include <time.h>
#include <mpi.h>
#include <unifyfs.h>

#include "testlib.h"

static int rank;
static int total_ranks;
static int debug;

static char* mountpoint = "/unifyfs";  /* unifyfs mountpoint */
static char* filename = "/unifyfs";
static int unmount;                /* unmount unifyfs after running the test */
static int testrank = -1;

#define FP_SPECIAL 1

static void dump_stat(int rank, const struct stat* sb)
{
    printf("## [RANK %d] %s\n", rank, filename);
    printf("File type:                ");

    switch (sb->st_mode & S_IFMT) {
    case S_IFREG:
        printf("regular file\n");
        break;
    case S_IFDIR:
        printf("directory\n");
        break;
    case S_IFCHR:
        printf("character device\n");
        break;
    case S_IFBLK:
        printf("block device\n");
        break;
    case S_IFLNK:
        printf("symbolic (soft) link\n");
        break;
    case S_IFIFO:
        printf("FIFO or pipe\n");
        break;
    case S_IFSOCK:
        printf("socket\n");
        break;
    default:
        printf("unknown file type?\n");
        break;
    }

    printf("Device containing i-node: major=%ld   minor=%ld\n",
           (long) major(sb->st_dev), (long) minor(sb->st_dev));

    printf("I-node number:            %ld\n", (long) sb->st_ino);

    printf("Mode:                     %lo\n",
           (unsigned long) sb->st_mode);

    if (sb->st_mode & (S_ISUID | S_ISGID | S_ISVTX))
        printf("    special bits set:     %s%s%s\n",
               (sb->st_mode & S_ISUID) ? "set-UID " : "",
               (sb->st_mode & S_ISGID) ? "set-GID " : "",
               (sb->st_mode & S_ISVTX) ? "sticky " : "");

    printf("Number of (hard) links:   %ld\n", (long) sb->st_nlink);

    printf("Ownership:                UID=%ld   GID=%ld\n",
           (long) sb->st_uid, (long) sb->st_gid);

    if (S_ISCHR(sb->st_mode) || S_ISBLK(sb->st_mode))
        printf("Device number (st_rdev):  major=%ld; minor=%ld\n",
               (long) major(sb->st_rdev), (long) minor(sb->st_rdev));

    printf("File size:                %lld bytes\n", (long long) sb->st_size);
    printf("Optimal I/O block size:   %ld bytes\n", (long) sb->st_blksize);
    printf("512B blocks allocated:    %lld\n", (long long) sb->st_blocks);

    printf("Last file access:         %s", ctime(&sb->st_atime));
    printf("Last file modification:   %s", ctime(&sb->st_mtime));
    printf("Last status change:       %s\n\n", ctime(&sb->st_ctime));
}

static struct option const long_opts[] = {
    { "debug", 0, 0, 'd' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "unmount", 0, 0, 'u' },
    { "rank", 1, 0, 'r' },
    { 0, 0, 0, 0},
};

static char* short_opts = "dhm:ur:";

static const char* usage_str =
    "\n"
    "Usage: %s [options...] <filename>\n"
    "\n"
    "Available options:\n"
    " -d, --debug                      pause before running test\n"
    "                                  (handy for attaching in debugger)\n"
    " -h, --help                       help message\n"
    " -m, --mount=<mountpoint>         use <mountpoint> for unifyfs\n"
    "                                  (default: /unifyfs)\n"
    " -u, --unmount                    unmount the filesystem after test\n"
    " -r, --rank=<rank>                only test on rank <rank>\n"
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
    int optidx = 0;
    struct stat sb = { 0, };

    program = basename(strdup(argv[0]));

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'd':
            debug = 1;
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 'u':
            unmount = 1;
            break;

        case 'r':
            testrank = atoi(optarg);
            break;

        case 'h':
        default:
            print_usage();
            break;
        }
    }

    if (argc - optind != 1) {
        print_usage();
    }

    if (testrank > total_ranks - 1) {
        test_print(0, "Please specify a vaild rank number.");
        print_usage();
    }

    filename = argv[optind];

    if (debug) {
        test_pause(rank, "Attempting to mount");
    }

    ret = unifyfs_mount(mountpoint, rank, total_ranks, 0);
    if (ret) {
        test_print(rank, "unifyfs_mount failed (return = %d)", ret);
        exit(-1);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (testrank < 0 || (testrank >= 0 && rank == testrank)) {
        ret = stat(filename, &sb);
        if (ret < 0) {
            test_print(rank, "stat failed on \"%s\"", filename);
        } else {
            dump_stat(rank, &sb);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (unmount) {
        unifyfs_unmount();
    }

    MPI_Finalize();

    return ret;
}

