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
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <getopt.h>
#include <mpi.h>
#include <unifycr.h>

#include "testlib.h"

static int standard;        /* not mounting unifycr when set */
static int synchronous;     /* sync metadata for each op? (default: no)*/

static int rank;
static int total_ranks;

static int debug;           /* pause for attaching debugger */
static int unmount;         /* unmount unifycr after running the test */
static uint64_t count = 10; /* number of directories each rank creates */
static char *mountpoint = "/unifycr";  /* unifycr mountpoint */
static char *testdir = "testdir";  /* test directory under mountpoint */
static char targetdir[NAME_MAX];   /* target file name */

static char dirnamebuf[NAME_MAX];

static int do_mkdir(void)
{
    int ret = 0;
    uint64_t i = 0;
    mode_t mode = 0700;
    struct stat sb = { 0, };

    ret = stat(targetdir, &sb);
    if (ret < 0 && errno == ENOENT) {
        ret = mkdir(targetdir, mode);
        if (ret < 0) {
            perror("mkdir");
            return -1;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    sprintf(dirnamebuf, "%s/rank-%d", targetdir, rank);

    ret = mkdir(dirnamebuf, mode);
    if (ret < 0) {
        test_print(rank, "mkdir failed for %s", dirnamebuf);
        ret = -errno;
        goto out;
    }

    for (i = 0; i < count; i++) {
        sprintf(dirnamebuf, "%s/rank-%d/dir-%lu", targetdir, rank, i);

        ret = mkdir(dirnamebuf, mode);
        if (ret < 0) {
            test_print(rank, "mkdir failed for %s", dirnamebuf);
            ret = -errno;
            goto out;
        }
    }

out:
    return ret;
}

static int do_stat(void)
{
    int ret = 0;
    uint64_t i = 0;
    struct stat sb = { 0, };

    sprintf(dirnamebuf, "%s/rank-%d", targetdir, rank);

    ret = stat(dirnamebuf, &sb);
    if (ret < 0) {
        test_print(rank, "stat failed for %s", dirnamebuf);
        ret = -errno;
        goto out;
    }

    for (i = 0; i < count; i++) {
        sprintf(dirnamebuf, "%s/rank-%d/dir-%lu", targetdir, rank, i);

        ret = stat(dirnamebuf, &sb);
        if (ret < 0) {
            test_print(rank, "stat failed for %s", dirnamebuf);
            ret = -errno;
            goto out;
        }

        /* print some fields.. */
        printf("\n## %s\n"
               "ino: %lu\n"
               "mode: %o\n"
               "ctime: %lu\n"
               "atime: %lu\n"
               "mtime: %lu\n",
               dirnamebuf,
               sb.st_ino, sb.st_mode,
               sb.st_ctime, sb.st_atime, sb.st_mtime);
    }

out:
    return ret;
}

static int do_readdir(void)
{
    return 0;
}

static int do_rmdir(void)
{
    return 0;
}

enum {
    DIRTEST_ALL = 0,
    DIRTEST_MKDIR,
    DIRTEST_STAT,
    DIRTEST_READDIR,
    DIRTEST_RMDIR,
    N_DIRTESTS,
};

static int singletest;

static const char *singletest_names[N_DIRTESTS] = {
    "all", "mkdir", "stat", "readdir", "rmdir"
};

static int set_singletest(const char *testname)
{
    int i = 0;

    if (singletest) {
        fprintf(stderr, "Only a single test can be performed with "
                        "--singletest option.\n");
        exit(1);
    }

    for (i = 0; i < N_DIRTESTS; i++)
        if (strcmp(testname, singletest_names[i]) == 0)
            return i;

    fprintf(stderr, "%s is not a valid test name.\n", testname);
    exit(1);
}

typedef int (*dirtest_func_t)(void);

static dirtest_func_t test_funcs[N_DIRTESTS] = {
    0, do_mkdir, do_stat, do_readdir, do_rmdir
};

static struct option const long_opts[] = {
    { "debug", 0, 0, 'd' },
    { "dirname", 1, 0, 'D' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "count", 1, 0, 'n' },
    { "synchronous", 0, 0, 'S' },
    { "standard", 0, 0, 's' },
    { "singletest", 1, 0, 't' },
    { "unmount", 0, 0, 'u' },
    { 0, 0, 0, 0},
};

static char *short_opts = "dD:hm:n:Sst:u";

static const char *usage_str =
"\n"
"Usage: %s [options...]\n"
"\n"
"Available options:\n"
" -d, --debug                      pause before running test\n"
"                                  (handy for attaching in debugger)\n"
" -D, --dirname=<dirname>          test directory name under mountpoint\n"
"                                  (default: testdir)\n"
" -h, --help                       help message\n"
" -m, --mount=<mountpoint>         use <mountpoint> for unifycr\n"
"                                  (default: /unifycr)\n"
" -n, --count=<NUM>                number of directories that each rank will\n"
"                                  create (default: 10)\n"
" -S, --synchronous                sync metadata on each write\n"
" -s, --standard                   do not use unifycr but run standard I/O\n"
" -t, --singletest=<operation>     only test a single operation\n"
"                                  (operations: mkdir, stat, readdir, rmdir)\n"
" -u, --unmount                    unmount the filesystem after test\n"
"\n";

static char *program;

static void print_usage(void)
{
    test_print_once(rank, usage_str, program);
    exit(0);
}

int main(int argc, char **argv)
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
        case 'd':
            debug = 1;
            break;

        case 'D':
            testdir = strdup(optarg);
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 'n':
            count = strtoull(optarg, 0, 0);
            break;

        case 'S':
            synchronous = 1;
            break;

        case 's':
            standard = 1;
            break;

        case 't':
            singletest = set_singletest(optarg);
            break;

        case 'u':
            unmount = 1;
            break;

        case 'h':
        default:
            print_usage();
            break;
        }
    }

    if (static_linked(program) && standard) {
        test_print_once(rank, "--standard, -s option only works when "
                              "dynamically linked.\n");
        exit(-1);
    }

    sprintf(targetdir, "%s/%s", mountpoint, testdir);

    if (debug)
        test_pause(rank, "Attempting to mount");

    if (!standard) {
        ret = unifycr_mount(mountpoint, rank, total_ranks, 0);
        if (ret) {
            test_print(rank, "unifycr_mount failed (return = %d)", ret);
            exit(-1);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (singletest) {
        test_print_once(rank, "only testing %s ..\n",
                        singletest_names[singletest]);
        ret = test_funcs[singletest]();
        if (ret < 0) {
            fprintf(stderr, "%s test failed.\n", singletest_names[singletest]);
            goto out;
        }
        goto out_unmount;
    }

    ret = do_mkdir();
    if (ret < 0) {
        fprintf(stderr, "directory creation failed..\n");
        goto out;
    }

    ret = do_stat();
    if (ret < 0) {
        fprintf(stderr, "directory stat failed..\n");
        goto out;
    }

    ret = do_readdir();
    if (ret < 0) {
        fprintf(stderr, "directory read failed..\n");
        goto out;
    }

    ret = do_rmdir();
    if (ret < 0) {
        fprintf(stderr, "directory  failed..\n");
        goto out;
    }

    MPI_Barrier(MPI_COMM_WORLD);

out_unmount:
    if (!standard && unmount && rank == 0)
        unifycr_unmount();
out:
    MPI_Finalize();

    return ret;
}

