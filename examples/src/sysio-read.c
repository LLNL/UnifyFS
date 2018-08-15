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
#include <aio.h>
#include <libgen.h>
#include <getopt.h>
#include <mpi.h>
#include <unifycr.h>

#include "testlib.h"

static uint64_t blocksize = 1<<20;          /* 1MB */
static uint64_t nblocks = 128;              /* Each process reads 128MB */
static uint64_t chunksize = 64*(1<<10);     /* 64KB for each read(2) call */

static int use_listio;      /* use lio_listio(2) */
static int use_pread;       /* use pread(2) */
static int pattern;         /* N to 1 (N1, default) or N to N (NN) */
static int fd;              /* target file descriptor */

static int lipsum;          /* check contents written by the write test. */
static int standard;        /* not mounting unifycr when set */

/* time statistics */
static struct timeval read_start, read_end;

static int rank;
static int total_ranks;

static int debug;           /* pause for attaching debugger */
static int unmount;         /* unmount unifycr after running the test */
static char *mountpoint = "/tmp";   /* unifycr mountpoint */
static char *filename = "testfile"; /* testfile name under mountpoint */
static char targetfile[NAME_MAX];   /* target file name */

static char *buf;                   /* I/O buffer */
static uint64_t n_aiocb_list;       /* number of aio requests */
static struct aiocb **aiocb_list;   /* aio request list */
static struct aiocb *aiocb_items;   /* aio requests */

static int do_read(void)
{
    int ret = 0;
    uint64_t i, j, offset;
    uint64_t nchunks = blocksize / chunksize;

    gettimeofday(&read_start, NULL);

    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < nchunks; j++) {
            if (pattern == IO_PATTERN_N1)
                offset = i*total_ranks*blocksize + rank*blocksize
                         + j*chunksize;
            else
                offset = i*blocksize + j*chunksize;

            if (use_pread)
                ret = pread(fd, buf, chunksize, offset);
            else {
                lseek(fd, offset, SEEK_SET);
                ret = read(fd, buf, chunksize);
            }

            if (ret < 0) {
                test_print(rank, "%s failed",
                                 use_pread ? "pread()" : "read()");
                return -1;
            }

            if (lipsum) {
                uint64_t epos = 0;

                ret = lipsum_check(buf, chunksize, offset, &epos);
                if (ret < 0) {
                    test_print(rank, "lipsum check failed at offset %llu.\n",
                               (unsigned long long) epos);
                    return -1;
                }
            }
        }
    }

    gettimeofday(&read_end, NULL);

    return 0;
}

static int do_listread(void)
{
    int ret = 0;
    uint64_t i, j;
    uint64_t nchunks = blocksize / chunksize;
    uint64_t current_ix = 0;
    struct aiocb *current = NULL;

    gettimeofday(&read_start, NULL);

    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < nchunks; j++) {
            current_ix = i*nchunks + j;

            current = &aiocb_items[current_ix];
            aiocb_list[current_ix] = current;

            current->aio_fildes = fd;
            current->aio_buf = &buf[current_ix*chunksize];
            current->aio_nbytes = chunksize;
            current->aio_lio_opcode = LIO_READ;

            if (pattern == IO_PATTERN_N1)
                current->aio_offset = i*total_ranks*blocksize
                                      + rank*blocksize + j*chunksize;
            else
                current->aio_offset = i*blocksize + j*chunksize;
        }
    }

    ret = lio_listio(LIO_WAIT, aiocb_list, n_aiocb_list, NULL);
    if (ret < 0) {
        test_print(rank, "lio_listio failed");
        return -1;
    }

    if (lipsum) {
        for (i = 0; i < nblocks*(blocksize/chunksize); i++) {
            uint64_t epos = 0;

            current = &aiocb_items[i];

            ret = lipsum_check((const char *) current->aio_buf, chunksize,
                               current->aio_offset, &epos);
            if (ret < 0) {
                test_print(rank, "lipsum check failed at offset %llu.\n",
                           (unsigned long long) epos);
                return -1;
            }
        }
    }

    gettimeofday(&read_end, NULL);

    return ret;
}

static void report_result(void)
{
    double read_bw = .0F;
    double agg_read_bw = .0F;
    double max_read_time = .0F;
    double min_read_bw = .0F;
    double read_time = .0F;

    read_time = timediff_sec(&read_start, &read_end);
    read_bw = 1.0*blocksize*nblocks/read_time/(1<<20);

    MPI_Reduce(&read_bw, &agg_read_bw,
               1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&read_time, &max_read_time,
               1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    min_read_bw = 1.0*blocksize*nblocks*total_ranks
                    /max_read_time/(1<<20);

    test_print_once(rank,
                    "\n"
                    "Number of processes:      %d\n"
                    "Each process wrote:       %lf MB\n"
                    "Total reads:              %lf MB\n"
                    "I/O pattern:              %s\n"
                    "I/O request size:         %llu B\n"
                    "Aggregate read bandwidth: %lf MB/s\n"
                    "Min. read bandwidth:      %lf MB/s\n"
                    "Total Read time:          %lf sec.\n\n",
                    total_ranks,
                    1.0*blocksize*nblocks/(1<<20),
                    1.0*total_ranks*blocksize*nblocks/(1<<20),
                    io_pattern_string(pattern),
                    chunksize,
                    agg_read_bw,
                    min_read_bw,
                    max_read_time);
}

static struct option const long_opts[] = {
    { "blocksize", 1, 0, 'b' },
    { "nblocks", 1, 0, 'n' },
    { "chunksize", 1, 0, 'c' },
    { "debug", 0, 0, 'd' },
    { "filename", 1, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "lipsum", 0, 0, 'L' },
    { "listio", 0, 0, 'l' },
    { "mount", 1, 0, 'm' },
    { "pattern", 1, 0, 'p' },
    { "pread", 0, 0, 'P' },
    { "standard", 0, 0, 's' },
    { "unmount", 0, 0, 'u' },
    { 0, 0, 0, 0},
};

static char *short_opts = "b:n:c:df:hLlm:Pp:su";

static const char *usage_str =
"\n"
"Usage: %s [options...]\n"
"\n"
"Available options:\n"
" -b, --blocksize=<size in bytes>  logical block size for the target file\n"
"                                  (default 1048576, 1MB)\n"
" -n, --nblocks=<count>            count of blocks each process will read\n"
"                                  (default 128)\n"
" -c, --chunksize=<size in bytes>  I/O chunk size for each read operation\n"
"                                  (default 64436, 64KB)\n"
" -d, --debug                      pause before running test\n"
"                                  (handy for attaching in debugger)\n"
" -f, --filename=<filename>        target file name under mountpoint\n"
"                                  (default: testfile)\n"
" -h, --help                       help message\n"
" -L, --lipsum                     check contents written by write test\n"
" -l, --listio                     use lio_listio(2) instead of read(2)\n"
" -m, --mount=<mountpoint>         use <mountpoint> for unifycr\n"
"                                  (default: /tmp)\n"
" -P, --pread                      use pread(2) instead of read(2)\n"
" -p, --pattern=<pattern>          should be 'n1'(n to 1) or 'nn' (n to n)\n"
" -s, --standard                   do not use unifycr but run standard I/O\n"
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
    uint64_t bufsize = 0;

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

        case 'L':
            lipsum = 1;
            break;

        case 'l':
            use_listio = 1;
            break;

        case 'P':
            use_pread = 1;
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
        test_print_once(rank, "pattern should be 'n1' or 'nn'\n");
        exit(-1);
    }

    if (blocksize < chunksize || blocksize % chunksize > 0) {
        test_print_once(rank, "blocksize should be larger than "
                              "and divisible by chunksize.\n");
        exit(-1);
    }

    if (chunksize % (1<<10) > 0) {
        test_print_once(rank, "chunksize and blocksize should be divisible "
                              "by 1024.\n");
        exit(-1);
    }

    if (static_linked(program) && standard) {
        test_print_once(rank, "--standard, -s option only works when "
                              "dynamically linked.\n");
        exit(-1);
    }

    if (use_listio && use_pread) {
        test_print_once(rank,
                        "--listio and --pread should be set exclusively\n");
        exit(-1);
    }

    if (use_listio) {
        bufsize = blocksize*nblocks;
        n_aiocb_list = blocksize*nblocks/chunksize;
    } else
        bufsize = chunksize;

    sprintf(targetfile, "%s/%s", mountpoint, filename);

    if (debug)
        test_pause(rank, "Attempting to mount");

    if (!standard) {
        ret = unifycr_mount(mountpoint, rank, total_ranks, 0);
        if (ret) {
            test_print(rank, "unifycr_mount failed (return = %d)", ret);
            exit(-1);
        }
    }

    buf = calloc(1, bufsize);
    if (!buf) {
        test_print(rank, "calloc failed");
        exit(-1);
    }

    if (use_listio) {
        aiocb_list = calloc(n_aiocb_list, sizeof(*aiocb_list));
        aiocb_items = calloc(n_aiocb_list, sizeof(*aiocb_items));

        if (!aiocb_list || !aiocb_items) {
            test_print(rank, "calloc failed");
            exit(-1);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (pattern == IO_PATTERN_NN)
        sprintf(&targetfile[strlen(targetfile)], "-%d", rank);

    fd = open(targetfile, O_RDONLY, 0600);
    if (fd < 0) {
        test_print(rank, "open failed");
        exit(-1);
    }

    ret = use_listio ? do_listread() : do_read();

    close(fd);

    fflush(stdout);

    MPI_Barrier(MPI_COMM_WORLD);

    if (!standard && unmount && rank == 0)
        unifycr_unmount();

    if (ret == 0)
        report_result();

    free(buf);

    MPI_Finalize();

    return ret;
}

