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
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>
#include <mpi.h>
#include <unifycr.h>

#include "testlib.h"

static uint64_t blocksize = 1 << 20;        /* 1MB */
static uint64_t nblocks = 128;              /* Each process writes 128MB */
static uint64_t chunksize = 64 * (1 << 10); /* 64KB for each write(2) call */

static int type;            /* 0 for write (default), 1 for read */
static int pattern;         /* N to 1 (N1, default) or N to N (NN) */
static MPI_File fh;         /* MPI file handle */
static MPI_Status status;   /* MPI I/O status */
static int iomode = MPI_MODE_CREATE | MPI_MODE_RDWR;

static int standard;        /* not mounting unifycr when set */

/* time statistics */
static struct timeval iostart, ioend;

#define BUFSIZE 128
static char hostname[BUFSIZE];
static int rank;
static int total_ranks;

static int debug;                   /* pause for attaching debugger */
static int unmount;                 /* unmount unifycr after test */
static char* buf;                   /* I/O buffer */
static char* mountpoint = "/unifycr";   /* unifycr mountpoint */
static char* filename = "testfile"; /* testfile name under mountpoint */
static char targetfile[NAME_MAX];   /* target file name */

/* MPI checker
 * from: https://stackoverflow.com/questions/22859269/
 */
static int mpierror;

static inline void mpi_handle_error(int err, char* str)
{
    char msg[MPI_MAX_ERROR_STRING];
    int len = 0;

    MPI_Error_string(err, msg, &len);
    fprintf(stderr, "[%s:%d] %s: %s\n", hostname, rank, str, msg);

    mpierror = 1;
}

#define MPI_CHECK(fn) do {                                      \
            int err = (fn);                                     \
            if (err != MPI_SUCCESS)                             \
                mpi_handle_error(err, #fn);                     \
        } while (0)

static int read_test_type(const char* str)
{
    if (strcmp("write", str) == 0) {
        return 0;
    } else if (strcmp("read", str) == 0) {
        iomode = MPI_MODE_RDONLY;
        return 1;
    } else {
        return -1;
    }
}

static int do_write(MPI_File* fh)
{
    int ret = 0;
    uint64_t i, j, offset;
    uint64_t nchunks = blocksize / chunksize;

    gettimeofday(&iostart, NULL);

    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < nchunks; j++) {
            if (pattern == IO_PATTERN_N1)
                offset = i * total_ranks * blocksize + rank * blocksize
                         + j * chunksize;
            else {
                offset = i * blocksize + j * chunksize;
            }

            MPI_CHECK(MPI_File_seek(*fh, offset, MPI_SEEK_SET));
            MPI_CHECK(MPI_File_write(*fh, buf, chunksize, MPI_CHAR, &status));

            if (mpierror) {
                goto out;
            }
        }
    }

out:
    MPI_File_close(fh);

    gettimeofday(&ioend, NULL);

    return ret;
}

static int do_read(MPI_File* fh)
{
    int ret = 0;
    uint64_t i, j, offset;
    uint64_t nchunks = blocksize / chunksize;

    gettimeofday(&iostart, NULL);

    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < nchunks; j++) {
            if (pattern == IO_PATTERN_N1)
                offset = i * total_ranks * blocksize + rank * blocksize
                         + j * chunksize;
            else {
                offset = i * blocksize + j * chunksize;
            }

            MPI_CHECK(MPI_File_seek(*fh, offset, MPI_SEEK_SET));
            MPI_CHECK(MPI_File_read(*fh, buf, chunksize, MPI_CHAR, &status));

            if (mpierror) {
                goto out;
            }
        }
    }

out:
    MPI_File_close(fh);

    gettimeofday(&ioend, NULL);

    return ret;
}

static void report_result(void)
{
    double io_bw = .0F;
    double agg_io_bw = .0F;
    double max_io_time = .0F;
    double min_io_bw = .0F;
    double io_time = .0F;

    io_time = timediff_sec(&iostart, &ioend);
    io_bw = 1.0 * blocksize * nblocks / io_time / (1 << 20);

    MPI_Reduce(&io_bw, &agg_io_bw,
               1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&io_time, &max_io_time,
               1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    min_io_bw = 1.0 * blocksize * nblocks * total_ranks /
        max_io_time / (1 << 20);

    errno = 0;

    test_print_once(rank,
                    "\n"
                    "Number of processes:     %d\n"
                    "I/O type:                %s\n"
                    "Each process performed:  %lf MB\n"
                    "Total I/O size:          %lf MB\n"
                    "I/O pattern:             %s\n"
                    "I/O request size:        %llu B\n"
                    "Aggregate I/O bandwidth: %lf MB/s\n"
                    "Min. I/O bandwidth:      %lf MB/s\n"
                    "Total I/O time:          %lf sec.\n\n",
                    total_ranks,
                    type ? "read" : "write",
                    1.0 * blocksize * nblocks / (1 << 20),
                    1.0 * total_ranks * blocksize * nblocks / (1 << 20),
                    io_pattern_string(pattern),
                    chunksize,
                    agg_io_bw,
                    min_io_bw,
                    max_io_time);
}

static struct option const long_opts[] = {
    { "blocksize", 1, 0, 'b' },
    { "nblocks", 1, 0, 'n' },
    { "chunksize", 1, 0, 'c' },
    { "debug", 0, 0, 'd' },
    { "filename", 1, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "mount", 1, 0, 'm' },
    { "pattern", 1, 0, 'p' },
    { "standard", 0, 0, 's' },
    { "type", 1, 0, 't' },
    { "unmount", 0, 0, 'u' },
    { 0, 0, 0, 0},
};

static char* short_opts = "b:n:c:df:hm:p:Pst:u";

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
    " -f, --filename=<filename>        target file name (default: testfile)\n"
    " -h, --help                       help message\n"
    " -m, --mount=<mountpoint>         use <mountpoint> for unifycr\n"
    "                                  (default: /unifycr)\n"
    " -p, --pattern=<pattern>          should be 'n1'(n to 1) or 'nn' (n to n)\n"
    " -s, --standard                   do not use unifycr but run standard I/O\n"
    " -t, --type=<write|read>          I/O type\n"
    "                                  should be 'write' (default) or 'read'\n"
    " -u, --unmount                    unmount the filesystem after test\n"
    "\n";

static char* program;

static void print_usage(void)
{
    test_print_once(rank, usage_str, program);
    exit(0);
}

int main(int argc, char* argv[])
{
    int ret = 0;
    int ch = 0;
    int optidx = 2;

    program = basename(strdup(argv[0]));

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    gethostname(hostname, BUFSIZE);

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

        case 'p':
            pattern = read_io_pattern(optarg);
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 's':
            standard = 1;
            break;

        case 't':
            type = read_test_type(optarg);
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

    if (type == -1) {
        test_print_once(rank, "type should be 'write' or 'read'\n");
        exit(-1);
    }

    if (blocksize < chunksize || blocksize % chunksize > 0) {
        test_print_once(rank, "blocksize should be larger than "
                        "and divisible by chunksize.\n");
        exit(-1);
    }

    if (chunksize % (1 << 10) > 0) {
        test_print_once(rank, "chunksize and blocksize should be divisible "
                        "by 1024.\n");
        exit(-1);
    }

    if (static_linked(program) && standard) {
        test_print_once(rank, "--standard, -s option only works when "
                        "dynamically linked.\n");
        exit(-1);
    }

    sprintf(targetfile, "%s/%s", mountpoint, filename);

    if (debug) {
        test_pause(rank, "Attempting to mount");
    }

    if (!standard) {
        ret = unifycr_mount(mountpoint, rank, total_ranks, 0);
        if (ret) {
            test_print(rank, "unifycr_mount failed (return = %d)", ret);
            exit(-1);
        }
    }

    buf = calloc(1, chunksize);
    if (!buf) {
        test_print(rank, "calloc failed");
        exit(-1);
    }

    if (pattern == IO_PATTERN_NN) {
        sprintf(&targetfile[strlen(targetfile)], "-%d", rank);
    }

    if (!standard) {
        printf("[%d]: unifycr mounted at %s\n", rank, mountpoint);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    printf("[%d]: opening %s\n", rank, targetfile);

    MPI_CHECK(MPI_File_open(MPI_COMM_WORLD,
                            targetfile,
                            iomode,
                            MPI_INFO_NULL, &fh));

    if (debug) {
        test_pause(rank, "Attempting to perform I/O");
    }

    if (type == 0) {
        ret = do_write(&fh);
    } else {
        ret = do_read(&fh);
    }

    fflush(stdout);

    MPI_Barrier(MPI_COMM_WORLD);

    if (!standard && unmount) {
        unifycr_unmount();
    }

    if (ret == 0) {
        report_result();
    } else {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    free(buf);

    MPI_Finalize();

    return ret;
}
