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
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>
#include <mpi.h>
#include <unifyfs.h>

#include "testlib.h"

/*
 * I/O test:
 *
 * Each process will write @blocksize*@nblocks. @chunksize denotes I/O request
 * size for each write(2) call. Each block is split into multiple chunks,
 * meaning that @blocksize should be larger than and multiple of @chunksize.
 */

static uint64_t blocksize = 1 << 20;        /* 1MB */
static uint64_t nblocks = 128;              /* Each process writes 128MB */
static uint64_t chunksize = 64 * (1 << 10); /* 64KB for each write(2) call */

static int use_pwrite;      /* use pwrite(2) */
static int pattern;         /* N to 1 (N1, default) or N to N (NN) */
static int fd;              /* target file descriptor */
static int synchronous;     /* sync metadata for each write? (default: no)*/

static int lipsum;          /* generate contents to verify correctness */
static int standard;        /* not mounting unifyfs when set */

/* time statistics */
static struct timeval write_start, meta_start, write_end;

static int rank;
static int total_ranks;

static int debug;           /* pause for attaching debugger */
static int unmount;         /* unmount unifyfs after running the test */
static char* buf;           /* I/O buffer */
static char* mountpoint = "/unifyfs";   /* unifyfs mountpoint */
static char* filename = "testfile"; /* testfile name under mountpoint */
static char targetfile[NAME_MAX];   /* target file name */

static int do_write(void)
{
    int ret = 0;
    uint64_t i, j, offset;
    uint64_t nchunks = blocksize / chunksize;

    gettimeofday(&write_start, NULL);

    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < nchunks; j++) {
            if (pattern == IO_PATTERN_N1)
                offset = i * total_ranks * blocksize + rank * blocksize
                         + j * chunksize;
            else {
                offset = i * blocksize + j * chunksize;
            }

            if (lipsum) {
                lipsum_generate(buf, chunksize, offset);
            }

            if (use_pwrite) {
                ret = pwrite(fd, buf, chunksize, offset);
            } else {
                lseek(fd, offset, SEEK_SET);
                ret = write(fd, buf, chunksize);
            }

            if (ret < 0) {
                test_print(rank, "%s failed",
                           use_pwrite ? "pwrite()" : "write()");
                return -1;
            }

            if (synchronous) {
                fsync(fd);
            }
        }
    }

    gettimeofday(&meta_start, NULL);

    fsync(fd);

    gettimeofday(&write_end, NULL);

    return 0;
}

static void report_result(void)
{
    double write_bw = .0F;
    double agg_write_bw = .0F;
    double max_write_time = .0F;
    double min_write_bw = .0F;
    double write_time = .0F;
    double meta_time = .0F;
    double max_meta_time = .0F;

    write_time = timediff_sec(&write_start, &write_end);
    write_bw = 1.0 * blocksize * nblocks / write_time / (1 << 20);

    meta_time = timediff_sec(&meta_start, &write_end);

    MPI_Reduce(&write_bw, &agg_write_bw,
               1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&write_time, &max_write_time,
               1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&meta_time, &max_meta_time,
               1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    min_write_bw = 1.0 * blocksize * nblocks * total_ranks
                   / max_write_time / (1 << 20);

    test_print_once(rank,
                    "\n"
                    "Number of processes:       %d\n"
                    "Each process wrote:        %lf MB\n"
                    "Total writes:              %lf MB\n"
                    "I/O pattern:               %s\n"
                    "I/O request size:          %llu B\n"
                    "Aggregate write bandwidth: %lf MB/s\n"
                    "Min. write bandwidth:      %lf MB/s\n"
                    "Total Write time:          %lf sec. (%lf for fsync)\n",
                    total_ranks,
                    1.0 * blocksize * nblocks / (1 << 20),
                    1.0 * total_ranks * blocksize * nblocks / (1 << 20),
                    io_pattern_string(pattern),
                    chunksize,
                    agg_write_bw,
                    min_write_bw,
                    max_write_time,
                    max_meta_time);
}

static struct option const long_opts[] = {
    { "blocksize", 1, 0, 'b' },
    { "nblocks", 1, 0, 'n' },
    { "chunksize", 1, 0, 'c' },
    { "debug", 0, 0, 'd' },
    { "filename", 1, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "lipsum", 0, 0, 'L' },
    { "mount", 1, 0, 'm' },
    { "pattern", 1, 0, 'p' },
    { "pwrite", 0, 0, 'P' },
    { "synchronous", 0, 0, 'S' },
    { "standard", 0, 0, 's' },
    { "unmount", 0, 0, 'u' },
    { 0, 0, 0, 0},
};

static char* short_opts = "b:n:c:df:hlm:p:PSsu";

static const char* usage_str =
    "\n"
    "Usage: %s [options...]\n"
    "\n"
    "Available options:\n"
    " -b, --blocksize=<size in bytes>  logical block size for the target file\n"
    "                                  (default 1048576, 1MB)\n"
    " -n, --nblocks=<count>            count of blocks each process will write\n"
    "                                  (default 128)\n"
    " -c, --chunksize=<size in bytes>  I/O chunk size for each write operation\n"
    "                                  (default 64436, 64KB)\n"
    " -d, --debug                      pause before running test\n"
    "                                  (handy for attaching in debugger)\n"
    " -f, --filename=<filename>        target file name under mountpoint\n"
    "                                  (default: testfile)\n"
    " -h, --help                       help message\n"
    " -L, --lipsum                     generate contents to verify correctness\n"
    " -m, --mount=<mountpoint>         use <mountpoint> for unifyfs\n"
    "                                  (default: /unifyfs)\n"
    " -P, --pwrite                     use pwrite(2) instead of write(2)\n"
    " -p, --pattern=<pattern>          should be 'n1'(n to 1) or 'nn' (n to n)\n"
    "                                  (default: n1)\n"
    " -S, --synchronous                sync metadata on each write\n"
    " -s, --standard                   do not use unifyfs but run standard I/O\n"
    " -u, --unmount                    unmount the filesystem after test\n"
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
        case 'b':
            blocksize = strtoull(optarg, NULL, 0);
            break;

        case 'n':
            nblocks = strtoull(optarg, NULL, 0);
            break;

        case 'c':
            chunksize = strtoull(optarg, NULL, 0);
            break;

        case 'f':
            filename = strdup(optarg);
            break;

        case 'd':
            debug = 1;
            break;

        case 'P':
            use_pwrite = 1;
            break;

        case 'p':
            pattern = read_io_pattern(optarg);
            break;

        case 'L':
            lipsum = 1;
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 'S':
            synchronous = 1;
            break;

        case 's':
            standard = 1;
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

    if (pattern < 0) {
        test_print_once(rank, "pattern should be 'n1' or 'nn'");
        exit(-1);
    }

    if (blocksize < chunksize || blocksize % chunksize > 0) {
        test_print_once(rank, "blocksize should be larger than "
                        "and divisible by chunksize.");
        exit(-1);
    }

    if (chunksize % (1 << 10) > 0) {
        test_print_once(rank, "chunksize and blocksize should be divisible "
                        "by 1024.");
        exit(-1);
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

    if (!standard) {
        ret = unifyfs_mount(mountpoint, rank, total_ranks, 0);
        if (ret) {
            test_print(rank, "unifyfs_mount failed (return = %d)", ret);
            exit(-1);
        }
    }

    buf = calloc(1, chunksize);
    if (!buf) {
        test_print(rank, "calloc failed");
        exit(-1);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (pattern == IO_PATTERN_NN) {
        sprintf(&targetfile[strlen(targetfile)], "-%d", rank);

    if (rank == 0) {
        fd = open(targetfile, O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            test_print(rank, "open failed");
            exit(-1);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank != 0) {
        fd = open(targetfile, O_RDWR, 0600);
        if (fd < 0) {
            test_print(rank, "open failed");
            exit(-1);
        }
    }

    ret = do_write();

    close(fd);

    fflush(stdout);

    MPI_Barrier(MPI_COMM_WORLD);

    /* have rank 0 check the expected file size matches actual file size */
    if (rank == 0) {
        /* compute expected size of file after all procs have written,
         * each process writes nblocks in groups of nchunks each of
         * which is chunksize bytes */
        uint64_t nchunks = blocksize / chunksize;
        off_t expected_size = (off_t)nblocks * (off_t)nchunks *
            (off_t)chunksize * (off_t)total_ranks;

        /* get stat data for the file */
        errno = 0;
        struct stat sbuf;
        int stat_rc = stat(targetfile, &sbuf);
        if (stat_rc == 0) {
            /* check that stat size matches expected size */
            if (sbuf.st_size != expected_size) {
                test_print(rank, "%s size incorrect got %llu, expected %llu",
                    targetfile, (unsigned long long)sbuf.st_size,
                    (unsigned long long)expected_size);
                ret = 1;
            }
        } else {
            /* our call to stat failed */
            test_print(rank, "stat(%s) failed:",
                targetfile);
            ret = 1;
        }
    }

    if (!standard && unmount) {
        unifyfs_unmount();
    }

    if (ret == 0) {
        report_result();
    }

    free(buf);

    MPI_Finalize();

    return ret;
}

