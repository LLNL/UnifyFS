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

#ifndef UNIFYFS_TEST_UTIL_H
#define UNIFYFS_TEST_UTIL_H

#include <aio.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <mpi.h>

#ifndef DISABLE_UNIFYFS
# include <unifyfs.h>
# include <unifyfs_api.h>
#endif

/* ---------- Common Types and Definitions ---------- */

#define TEST_STR_LEN 1024

#ifndef KIB
# define KIB (1024)
#endif

#ifndef MIB
# define MIB (1048576)
#endif

#ifndef GIB
# define GIB (1073741824)
#endif

static inline
double bytes_to_kib(size_t bytes)
{
    return (double)bytes / KIB;
}

static inline
double bytes_to_mib(size_t bytes)
{
    return (double)bytes / MIB;
}

static inline
double bytes_to_gib(size_t bytes)
{
    return (double)bytes / GIB;
}

static inline
double bandwidth_kib(size_t bytes, double seconds)
{
    return bytes_to_kib(bytes) / seconds;
}

static inline
double bandwidth_mib(size_t bytes, double seconds)
{
    return bytes_to_mib(bytes) / seconds;
}

static inline
double bandwidth_gib(size_t bytes, double seconds)
{
    return bytes_to_gib(bytes) / seconds;
}

#define IO_PATTERN_N1 (0)
#define IO_PATTERN_NN (1)

static inline
const char* io_pattern_str(int pattern)
{
    switch (pattern) {
    case IO_PATTERN_N1:
        return "N-to-1";
    case IO_PATTERN_NN:
        return "N-to-N";
    default:
        break;
    }
    return "Unknown I/O Pattern";
}

#define DEFAULT_IO_CHUNK_SIZE (MIB) // 1 MiB
#define DEFAULT_IO_BLOCK_SIZE (16 * DEFAULT_IO_CHUNK_SIZE) // 16 MiB
#define DEFAULT_IO_NUM_BLOCKS (32) // 32 blocks x 16 MiB = 512 MiB

typedef struct {
    /* program behavior options */
    int debug;        /* during startup, wait for input at rank 0 */
    int verbose;      /* print verbose information to stderr */
    int use_mpi;
    int use_unifyfs;
    int enable_mpi_mount; /* automount during MPI_Init() */
    char* output_file;    /* print test messages to output file */
    FILE* output_fp;
    int reuse_filename; /* remove and then reuse filename from prior run*/

    /* I/O behavior options */
    int io_pattern; /* N1 or NN */
    int io_check;   /* use lipsum to verify data */
    int io_shuffle; /* read and write different extents */
    int pre_wr_trunc;  /* truncate file before writing */
    int post_wr_trunc; /* truncate file after writing */
    int use_aio;    /* use asynchronous IO */
    int use_api;    /* use UnifyFS library API */
    int use_lio;    /* use lio_listio instead of read/write */
    int use_mapio;  /* use mmap instead of read/write */
    int use_mpiio;  /* use MPI-IO instead of POSIX I/O */
    int use_prdwr;  /* use pread/pwrite instead of read/write */
    int use_stdio;  /* use fread/fwrite instead of read/write */
    int use_vecio;  /* use readv/writev instead of read/write */

    /* I/O size options */
    uint64_t n_blocks; /* number of I/O blocks */
    uint64_t block_sz; /* IO block size (multiple of chunk_sz) */
    uint64_t chunk_sz; /* per-IO-op size */
    off_t trunc_size;  /* file size for truncate */

    /* target file info */
    char*  filename;
    char*  mountpt;
    FILE*  fp;
    int    fd;
    int    fd_access;  /* access flags for cfg.fd */
    void*  mapped;     /* address of mapped extent of cfg.fd */
    off_t  mapped_off; /* start offset for mapped extent */
    size_t mapped_sz;  /* size of mapped extent */
    MPI_File mpifh;    /* MPI file handle (when use_mpiio) */

    /* transfer destination file */
    char*    dest_filename;
    int      dest_fd;
    MPI_File dest_mpifh;    /* MPI file handle (when use_mpiio) */

#ifndef DISABLE_UNIFYFS
    /* UnifyFS API info */
    unifyfs_handle fshdl; /* file system client handle */
    unifyfs_gfid   gfid;  /* global file id for target */
#endif

    /* MPI info */
    int rank;
    int n_ranks;
} test_cfg;

static inline
void test_config_init(test_cfg* cfg)
{
    if (NULL == cfg) {
        fprintf(stderr, "INTERNAL ERROR: %s() - cfg is NULL\n", __func__);
        fflush(stderr);
        return;
    }

    // set everything to 0/NULL
    memset(cfg, 0, sizeof(test_cfg));

#ifndef DISABLE_UNIFYFS
    // invalidate UnifyFS API state
    cfg->fshdl = UNIFYFS_INVALID_HANDLE;
    cfg->gfid  = UNIFYFS_INVALID_GFID;
#endif

    // N-to-1 UnifyFS by default
    cfg->use_mpi = 1;
    cfg->use_unifyfs = 1;
    cfg->io_pattern = IO_PATTERN_N1;

#if defined(ENABLE_MPI_MOUNT)
    cfg->enable_mpi_mount = 1;
#endif

    // invalidate file descriptor
    cfg->fd = -1;

    // use default I/O sizes
    cfg->n_blocks = DEFAULT_IO_NUM_BLOCKS;
    cfg->block_sz = DEFAULT_IO_BLOCK_SIZE;
    cfg->chunk_sz = DEFAULT_IO_CHUNK_SIZE;
}

static inline
void test_config_print(test_cfg* cfg)
{
    assert(NULL != cfg);

    FILE* fp = cfg->output_fp;
    if (NULL == fp) {
        fp = stderr;
    }

    fprintf(fp, "    Test Configuration\n");
    fprintf(fp, "==========================\n");

    fprintf(fp, "\n-- Program Behavior --\n");
    fprintf(fp, "\t debug       = %d\n", cfg->debug);
    fprintf(fp, "\t verbose     = %d\n", cfg->verbose);
    fprintf(fp, "\t use_mpi     = %d\n", cfg->use_mpi);
    fprintf(fp, "\t use_unifyfs = %d\n", cfg->use_unifyfs);
    fprintf(fp, "\t mpi_mount   = %d\n", cfg->enable_mpi_mount);
    fprintf(fp, "\t outfile     = %s\n", cfg->output_file);
    fprintf(fp, "\t reuse_fname = %d\n", cfg->reuse_filename);

    fprintf(fp, "\n-- IO Behavior --\n");
    fprintf(fp, "\t io_pattern  = %s\n", io_pattern_str(cfg->io_pattern));
    fprintf(fp, "\t io_check    = %d\n", cfg->io_check);
    fprintf(fp, "\t io_shuffle  = %d\n", cfg->io_shuffle);
    fprintf(fp, "\t pre_trunc   = %d\n", cfg->pre_wr_trunc);
    fprintf(fp, "\t post_trunc  = %d\n", cfg->post_wr_trunc);
    fprintf(fp, "\t use_aio     = %d\n", cfg->use_aio);
    fprintf(fp, "\t use_api     = %d\n", cfg->use_api);
    fprintf(fp, "\t use_lio     = %d\n", cfg->use_lio);
    fprintf(fp, "\t use_mapio   = %d\n", cfg->use_mapio);
    fprintf(fp, "\t use_mpiio   = %d\n", cfg->use_mpiio);
    fprintf(fp, "\t use_prdwr   = %d\n", cfg->use_prdwr);
    fprintf(fp, "\t use_stdio   = %d\n", cfg->use_stdio);
    fprintf(fp, "\t use_vecio   = %d\n", cfg->use_vecio);

    fprintf(fp, "\n-- IO Size Config --\n");
    fprintf(fp, "\t n_blocks    = %" PRIu64 "\n", cfg->n_blocks);
    fprintf(fp, "\t block_sz    = %" PRIu64 "\n", cfg->block_sz);
    fprintf(fp, "\t chunk_sz    = %" PRIu64 "\n", cfg->chunk_sz);
    fprintf(fp, "\t truncate_sz = %lu\n", (unsigned long)cfg->trunc_size);

    fprintf(fp, "\n-- Target File --\n");
    fprintf(fp, "\t filename    = %s\n", cfg->filename);
    fprintf(fp, "\t mountpoint  = %s\n", cfg->mountpt);

    if (NULL != cfg->dest_filename) {
        fprintf(fp, "\n-- Transfer Destination File --\n");
        fprintf(fp, "\t filename    = %s\n", cfg->dest_filename);
    }

    fprintf(fp, "\n-- MPI Info --\n");
    fprintf(fp, "\t rank        = %d\n", cfg->rank);
    fprintf(fp, "\t n_ranks     = %d\n", cfg->n_ranks);
    fprintf(fp, "\n==========================\n\n");
}

static inline
char* test_target_filename(test_cfg* cfg)
{
    char fname[TEST_STR_LEN];

    assert(NULL != cfg);

    if (IO_PATTERN_N1 == cfg->io_pattern) {
        snprintf(fname, sizeof(fname), "%s/%s",
                 cfg->mountpt, cfg->filename);
    } else {
        snprintf(fname, sizeof(fname), "%s/%s-%d",
                 cfg->mountpt, cfg->filename, cfg->rank);
    }
    return strdup(fname);
}

static inline
char* test_destination_filename(test_cfg* cfg)
{
    return strdup(cfg->dest_filename);
}

/* ---------- Print Utilities ---------- */

static inline
void test_print(test_cfg* cfg, const char* fmt, ...)
{
    int err = errno;

    assert(NULL != cfg);

    FILE* fp = cfg->output_fp;
    if (NULL == fp) {
        fp = stdout;
    }

    fprintf(fp, "[%d] ", cfg->rank);

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    if (err) {
        fprintf(fp, " (errno=%d, %s)", err, strerror(err));
    }

    /* End with a newline */
    fprintf(fp, "\n");
    fflush(fp);
}

static inline
void test_print_once(test_cfg* cfg, const char* fmt, ...)
{
    int err = errno;

    assert(NULL != cfg);

    if (cfg->rank != 0) {
        return;
    }

    FILE* fp = cfg->output_fp;
    if (NULL == fp) {
        fp = stdout;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    if (err) {
        fprintf(fp, " (errno=%d, %s)\n", err, strerror(err));
    }

    /* End with a newline */
    fprintf(fp, "\n");
    fflush(fp);
}

static inline
void test_print_verbose(test_cfg* cfg, const char* fmt, ...)
{
    assert(NULL != cfg);

    if (cfg->verbose == 0) {
        return;
    }

    FILE* fp = cfg->output_fp;
    if (NULL == fp) {
        fp = stderr;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fprintf(fp, "\n");
    fflush(fp);
}

static inline
void test_print_verbose_once(test_cfg* cfg, const char* fmt, ...)
{
    assert(NULL != cfg);

    if ((cfg->verbose == 0) || (cfg->rank != 0)) {
        return;
    }

    FILE* fp = cfg->output_fp;
    if (NULL == fp) {
        fp = stderr;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fprintf(fp, "\n");
    fflush(fp);
}

/* ---------- Timer Utilities ---------- */

typedef struct {
    struct timeval start;
    struct timeval stop;
    struct timeval stop_all;
    char* name;
    double elapsed_sec;
    double elapsed_sec_all;
} test_timer;

static inline
double timediff_sec(struct timeval* before, struct timeval* after)
{
    double diff;
    if (!before || !after) {
        return -1.0F;
    }
    diff = (double)(after->tv_sec - before->tv_sec);
    diff += 0.000001 * ((double)(after->tv_usec) - (double)(before->tv_usec));
    return diff;
}

static inline
double timediff_usec(struct timeval* before, struct timeval* after)
{
    return 1000000.0 * timediff_sec(before, after);
}

static inline
void timer_init(test_timer* timer,
                     const char* name)
{
    memset(timer, 0, sizeof(test_timer));
    if (NULL != name) {
        timer->name = strdup(name);
    }
}

static inline
void timer_fini(test_timer* timer)
{
    if (NULL != timer->name) {
        free(timer->name);
    }
    memset(timer, 0, sizeof(test_timer));
}

static inline
void timer_start(test_timer* timer)
{
    gettimeofday(&(timer->start), NULL);
}

static inline
void timer_stop(test_timer* timer)
{
    gettimeofday(&(timer->stop), NULL);
    timer->elapsed_sec = timediff_sec(&(timer->start),
                                      &(timer->stop));
}

static inline
void timer_start_barrier(test_cfg* cfg, test_timer* timer)
{
    /* execute a barrier to ensure procs don't start
     * next phase before all have reached this point */
    if (cfg->use_mpi) {
        MPI_Barrier(MPI_COMM_WORLD);
    }

    /* everyone has reached, start the timer,
     * the start field is used in both local and global timers */
    timer_start(timer);
}

static inline
void timer_stop_barrier(test_cfg* cfg, test_timer* timer)
{
    /* stop the local timer and compute elapsed_secs */
    timer_stop(timer);

    /* execute a barrier to ensure procs have reached this point
     * before stopping the global timer */
    if (cfg->use_mpi) {
        MPI_Barrier(MPI_COMM_WORLD);
    }

    /* everyone has reached, stop the global timer and
     * compute elapsed global time */
    gettimeofday(&(timer->stop_all), NULL);
    timer->elapsed_sec_all = timediff_sec(&(timer->start),
                                          &(timer->stop_all));
}

/* ---------- Option Parsing Utilities ---------- */

static const char* unifyfs_mntpt = "/unifyfs";
static const char*     tmp_mntpt = "/tmp";

static inline
int check_io_pattern(const char* pstr)
{
    int pattern = -1;

    if (strcmp(pstr, "n1") == 0) {
        pattern = IO_PATTERN_N1;
    } else if (strcmp(pstr, "nn") == 0) {
        pattern = IO_PATTERN_NN;
    }

    return pattern;
}

static inline
int test_is_static(const char* program)
{
    char* pos = strstr(program, "-static");
    return (pos != NULL);
}

// common options for all tests

static const char* test_short_opts = "Ab:c:dD:f:hklLm:Mn:No:p:PrSt:T:UvVx";

static const struct option test_long_opts[] = {
    { "aio", 0, 0, 'A' },
    { "blocksize", 1, 0, 'b' },
    { "chunksize", 1, 0, 'c' },
    { "debug", 0, 0, 'd' },
    { "destfile", 1, 0, 'D' },
    { "file", 1, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "check", 0, 0, 'k' },
    { "library-api", 0, 0, 'l' },
    { "listio", 0, 0, 'L' },
    { "mount", 1, 0, 'm' },
    { "mpiio", 0, 0, 'M' },
    { "nblocks", 1, 0, 'n' },
    { "mapio", 0, 0, 'N' },
    { "outfile", 1, 0, 'o' },
    { "pattern", 1, 0, 'p' },
    { "prdwr", 0, 0, 'P' },
    { "reuse-filename", 0, 0, 'r' },
    { "stdio", 0, 0, 'S' },
    { "pre-truncate", 1, 0, 't' },
    { "post-truncate", 1, 0, 'T' },
    { "disable-unifyfs", 0, 0, 'U' },
    { "verbose", 0, 0, 'v' },
    { "vecio", 0, 0, 'V' },
    { "shuffle", 0, 0, 'x' },
    { 0, 0, 0, 0},
};

static const char* test_usage_str =
    "\n"
    "Usage: %s [options...]\n"
    "\n"
    "Available options:\n"
    " -A, --aio                        use asynchronous I/O instead of read|write\n"
    "                                  (default: off)\n"
    " -b, --blocksize=<bytes>          I/O block size\n"
    "                                  (default: 16 MiB)\n"
    " -c, --chunksize=<bytes>          I/O chunk size for each operation\n"
    "                                  (default: 1 MiB)\n"
    " -d, --debug                      for debugging, wait for input (at rank 0) at start\n"
    "                                  (default: off)\n"
    " -D, --destfile=<filename>        transfer destination file name (or path) outside mountpoint\n"
    "                                  (default: none)\n"
    " -f, --file=<filename>            target file name (or path) under mountpoint\n"
    "                                  (default: 'testfile')\n"
    " -k, --check                      check data contents upon read\n"
    "                                  (default: off)\n"
    " -l, --library-api                use UnifyFS library API instead of POSIX I/O\n"
    "                                  (default: off)\n"
    " -L, --listio                     use lio_listio instead of read|write\n"
    "                                  (default: off)\n"
    " -m, --mount=<mountpoint>         use <mountpoint> for unifyfs\n"
    "                                  (default: /unifyfs)\n"
    " -M, --mpiio                      use MPI-IO instead of POSIX I/O\n"
    "                                  (default: off)\n"
    " -n, --nblocks=<count>            count of blocks each process will read|write\n"
    "                                  (default: 32)\n"
    " -N, --mapio                      use mmap instead of read|write\n"
    "                                  (default: off)\n"
    " -o, --outfile=<filename>         output file name (or path)\n"
    "                                  (default: 'stdout')\n"
    " -p, --pattern=<pattern>          'n1' (N-to-1 shared file) or 'nn' (N-to-N file per process)\n"
    "                                  (default: 'n1')\n"
    " -P, --prdwr                      use pread|pwrite instead of read|write\n"
    "                                  (default: off)\n"
    " -r, --reuse-filename             remove and reuse the same target file name\n"
    "                                  (default: off)\n"
    " -S, --stdio                      use fread|fwrite instead of read|write\n"
    "                                  (default: off)\n"
    " -t, --pre-truncate=<size>        truncate file to size (B) before writing\n"
    "                                  (default: off)\n"
    " -T, --post-truncate=<size>       truncate file to size (B) after writing\n"
    "                                  (default: off)\n"
    " -U, --disable-unifyfs            do not use UnifyFS\n"
    "                                  (default: enable UnifyFS)\n"
    " -v, --verbose                    print verbose information\n"
    "                                  (default: off)\n"
    " -V, --vecio                      use readv|writev instead of read|write\n"
    "                                  (default: off)\n"
    " -x, --shuffle                    read different data than written\n"
    "                                  (default: off)\n"
    "\n";

static inline
void test_print_usage(test_cfg* cfg, const char* program)
{
    test_print_once(cfg, test_usage_str, program);
    exit(0);
}

static inline
int test_process_argv(test_cfg* cfg,
                      int argc, char** argv)
{
    const char* program;
    int ch;

    assert(NULL != cfg);

    program = basename(strdup(argv[0]));

    while ((ch = getopt_long(argc, argv, test_short_opts,
                             test_long_opts, NULL)) != -1) {
        switch (ch) {
        case 'A':
            cfg->use_aio = 1;
            break;

        case 'b':
            cfg->block_sz = (uint64_t) strtoul(optarg, NULL, 0);
            break;

        case 'c':
            cfg->chunk_sz = (uint64_t) strtoul(optarg, NULL, 0);
            break;

        case 'd':
            cfg->debug = 1;
            break;

        case 'D':
            cfg->dest_filename = strdup(optarg);
            break;

        case 'f':
            cfg->filename = strdup(optarg);
            break;

        case 'k':
            cfg->io_check = 1;
            break;

        case 'l':
            cfg->use_api = 1;
            break;

        case 'L':
            cfg->use_lio = 1;
            break;

        case 'm':
            cfg->mountpt = strdup(optarg);
            break;

        case 'M':
            cfg->use_mpiio = 1;
            break;

        case 'n':
            cfg->n_blocks = (uint64_t) strtoul(optarg, NULL, 0);
            break;

        case 'N':
            cfg->use_mapio = 1;
            break;

        case 'o':
            cfg->output_file = strdup(optarg);
            break;

        case 'p':
            cfg->io_pattern = check_io_pattern(optarg);
            break;

        case 'P':
            cfg->use_prdwr = 1;
            break;

        case 'r':
            cfg->reuse_filename = 1;
            break;

        case 'S':
            cfg->use_stdio = 1;
            break;

        case 't':
            cfg->pre_wr_trunc = 1;
            cfg->trunc_size = (off_t) strtoul(optarg, NULL, 0);
            break;

        case 'T':
            cfg->post_wr_trunc = 1;
            cfg->trunc_size = (off_t) strtoul(optarg, NULL, 0);
            break;

        case 'U':
            cfg->use_unifyfs = 0;
            break;

        case 'v':
            cfg->verbose = 1;
            break;

        case 'V':
            cfg->use_vecio = 1;
            break;

        case 'x':
            cfg->io_shuffle = 1;
            break;

        case 'h':
        default:
            if (ch != 'h') {
                test_print_once(cfg, "USAGE ERROR: unknown flag '-%c'", ch);
            }
            test_print_usage(cfg, program);
            break;
        }
    }

    if (cfg->io_pattern < 0) {
        test_print_once(cfg, "USAGE ERROR: pattern should be 'n1' or 'nn'");
        exit(-1);
    }

    if ((cfg->block_sz < cfg->chunk_sz) ||
        (cfg->block_sz % cfg->chunk_sz)) {
        test_print_once(cfg, "USAGE ERROR: blocksize should be larger than "
                             "and evenly divisible by chunksize.");
        exit(-1);
    }

    if (cfg->chunk_sz % 4096 > 0) {
        test_print_once(cfg, "USAGE ERROR: chunksize and blocksize should be "
                             "divisible by 4096.");
        exit(-1);
    }

#ifdef DISABLE_UNIFYFS
    if (cfg->use_unifyfs) {
        test_print_once(cfg, "USAGE ERROR: UnifyFS enabled, "
                             "but not compiled/linked");
        exit(-1);
    }
#endif

    if (test_is_static(program) && !cfg->use_unifyfs) {
        test_print_once(cfg, "USAGE ERROR: --disable-unifyfs only valid when "
                             "dynamically linked.");
        exit(-1);
    }

    // exhaustive check of incompatible I/O modes
    if (cfg->pre_wr_trunc || cfg->post_wr_trunc) {
        if (cfg->pre_wr_trunc && cfg->post_wr_trunc) {
            test_print_once(cfg,
                "USAGE ERROR: choose --pre-truncate or --post-truncate");
            exit(-1);
        }
        if (cfg->use_mapio || cfg->use_stdio) {
            test_print_once(cfg,
                "USAGE ERROR: pre/post-truncate incompatible with "
                "[--mapio, --stdio]");
            exit(-1);
        }
    }
    if (cfg->use_aio &&
        (cfg->use_api || cfg->use_mapio || cfg->use_mpiio || cfg->use_prdwr
         || cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg,
            "USAGE ERROR: --aio incompatible with "
            "[--library-api, --mapio, --mpiio, --prdwr, --stdio, --vecio]");
        exit(-1);
    }
    if (cfg->use_api) {
        if (cfg->use_lio || cfg->use_mapio || cfg->use_mpiio || cfg->use_prdwr
            || cfg->use_stdio || cfg->use_vecio) {
            test_print_once(cfg,
                "USAGE ERROR: --library-api incompatible with "
                "[--listio, --mapio, --mpiio, --prdwr, --stdio, --vecio]");
            exit(-1);
        }
        if (!cfg->use_unifyfs) {
            test_print_once(cfg,
                "USAGE ERROR: --library-api incompatible with "
                "--disable-unifyfs");
            exit(-1);
        }
    }
    if (cfg->use_lio &&
        (cfg->use_mapio || cfg->use_mpiio || cfg->use_prdwr
         || cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg,
            "USAGE ERROR: --listio incompatible with "
            "[--mapio, --mpiio, --prdwr, --stdio, --vecio]");
        exit(-1);
    }
    if (cfg->use_mapio &&
        (cfg->use_mpiio || cfg->use_prdwr || cfg->use_stdio
         || cfg->use_vecio)) {
        test_print_once(cfg,
            "USAGE ERROR: --mapio incompatible with "
            "[--aio, --listio, --mpiio, --prdwr, --stdio, --vecio]");
        exit(-1);
    }
    if (cfg->use_mpiio &&
        (cfg->use_prdwr || cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg,
            "USAGE ERROR: --mpiio incompatible with "
            "[--aio, --listio, --mpiio, --prdwr, --stdio, --vecio]");
        exit(-1);
    }
    if (cfg->use_prdwr && (cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg,
            "USAGE ERROR: --prdwr incompatible with "
            "[--aio, --listio, --mapio, --mpiio, --stdio, --vecio]");
        exit(-1);
    }
    if (cfg->use_stdio && cfg->use_vecio) {
        test_print_once(cfg,
            "USAGE ERROR: --stdio incompatible with "
            "[--aio, --listio, --mapio, --mpiio, --prdwr, --vecio]");
        exit(-1);
    }

    if (NULL == cfg->filename) {
        // set filename default
        cfg->filename = strdup("testfile");
    }

    if (NULL == cfg->mountpt) {
        // set mountpoint default
        if (cfg->use_unifyfs) {
            cfg->mountpt = strdup(unifyfs_mntpt);
        } else {
            cfg->mountpt = strdup(tmp_mntpt);
        }
    }

    return 0;
}

/* ---------- Data Utilities ---------- */

/*
 * sequentially number every 8 bytes (uint64_t)
 */
static inline
void lipsum_generate(char* buf, uint64_t len, uint64_t offset)
{
    uint64_t i;
    uint64_t skip = 0;
    uint64_t remain = 0;
    uint64_t start = offset / sizeof(uint64_t);
    uint64_t count = len / sizeof(uint64_t);
    uint64_t* ibuf = (uint64_t*) buf;

    /* check if we have any extra bytes at the front and end */
    if (offset % sizeof(uint64_t)) {
        skip = sizeof(uint64_t) - (offset % sizeof(uint64_t));
        remain = (len - skip) % sizeof(uint64_t);

        ibuf = (uint64_t*) &buf[skip];
        start++;

        if (skip + remain >= sizeof(uint64_t)) {
            count--;
        }
    }

    for (i = 0; i < count; i++) {
        ibuf[i] = start + i;
    }
}

/*
 * check buffer contains lipsum generated data
 * returns 0 on success, -1 otherwise with @error_offset set.
 */
static inline
int lipsum_check(const char* buf, uint64_t len, uint64_t offset,
                 uint64_t* error_offset)
{
    uint64_t i, val;
    uint64_t skip = 0;
    uint64_t remain = 0;
    uint64_t start = offset / sizeof(uint64_t);
    uint64_t count = len / sizeof(uint64_t);
    const uint64_t* ibuf = (uint64_t*) buf;

    /* check if we have any extra bytes at the front and end */
    if (offset % sizeof(uint64_t)) {
        skip = sizeof(uint64_t) - (offset % sizeof(uint64_t));
        remain = (len - skip) % sizeof(uint64_t);

        ibuf = (uint64_t*) &buf[skip];
        start++;

        if (skip + remain >= sizeof(uint64_t)) {
            count--;
        }
    }

    for (i = 0; i < count; i++) {
        val = start + i;
        if (ibuf[i] != val) {
            *error_offset = offset + (i * sizeof(uint64_t));
            fprintf(stderr,
                    "LIPSUM CHECK ERROR: [%" PRIu64 "] @ offset %" PRIu64
                    ", expected=%" PRIu64 " found=%" PRIu64 "\n",
                    i, *error_offset, val, ibuf[i]);
            return -1;
        }
    }

    return 0;
}

/* ---------- MPI Utilities ---------- */

/* MPI checker
 * from: https://stackoverflow.com/questions/22859269/
 */
#define MPI_CHECK(cfgp, fncall)              \
    do {                                     \
        mpi_error = 0;                       \
        int _merr = fncall;                  \
        if (_merr != MPI_SUCCESS) {          \
            mpi_error = _merr;               \
            handle_mpi_error(cfgp, #fncall); \
        }                                    \
    } while (0)

static int mpi_error;

static inline
void handle_mpi_error(test_cfg* cfg, char* context)
{
    char errstr[MPI_MAX_ERROR_STRING];
    int len = 0;
    MPI_Error_string(mpi_error, errstr, &len);
    test_print(cfg, "MPI ERROR: %s returned %s", context, errstr);
}

static inline
void test_barrier(test_cfg* cfg)
{
    assert(NULL != cfg);

    if (cfg->use_mpi) {
        MPI_CHECK(cfg, (MPI_Barrier(MPI_COMM_WORLD)));
    }
}

static inline
double test_reduce_double_sum(test_cfg* cfg, double local_val)
{
    double aggr_val = 0.0;

    assert(NULL != cfg);

    if (cfg->use_mpi) {
        MPI_CHECK(cfg, (MPI_Reduce(&local_val, &aggr_val,
                                   1, MPI_DOUBLE, MPI_SUM,
                                   0, MPI_COMM_WORLD)));
    } else {
        aggr_val = local_val;
    }
    return aggr_val;
}

static inline
double test_reduce_double_max(test_cfg* cfg, double local_val)
{
    double aggr_val = 0.0;

    assert(NULL != cfg);

    if (cfg->use_mpi) {
        MPI_CHECK(cfg, (MPI_Reduce(&local_val, &aggr_val,
                                   1, MPI_DOUBLE, MPI_MAX,
                                   0, MPI_COMM_WORLD)));
    } else {
        aggr_val = local_val;
    }
    return aggr_val;
}

static inline
double test_reduce_double_min(test_cfg* cfg, double local_val)
{
    double aggr_val = 0.0;

    assert(NULL != cfg);

    if (cfg->use_mpi) {
        MPI_CHECK(cfg, (MPI_Reduce(&local_val, &aggr_val,
                                   1, MPI_DOUBLE, MPI_MIN,
                                   0, MPI_COMM_WORLD)));
    } else {
        aggr_val = local_val;
    }
    return aggr_val;
}

/* ---------- Program Utilities ---------- */
static
int test_access_to_mmap_prot(int access)
{
    switch (access) {
    case O_RDWR:
        return (PROT_READ | PROT_WRITE);
    case O_RDONLY:
        return PROT_READ;
    case O_WRONLY:
        return PROT_WRITE;
    default:
        break;
    }
    return PROT_NONE;
}

static
const char* test_access_to_stdio_mode(int access)
{
    switch (access) {
    case O_RDWR:
        return "w+";
    case O_RDONLY:
        return "r";
    case O_WRONLY:
        return "w";
    default:
        break;
    }
    return NULL;
}

static int test_access_to_mpiio_mode(int access)
{
    switch (access) {
    case O_RDWR:
        return MPI_MODE_RDWR;
    case O_RDONLY:
        return MPI_MODE_RDONLY;
    case O_WRONLY:
        return MPI_MODE_WRONLY;
    default:
        break;
    }
    return 0;
}

/*
 * open the given file
 */
static inline
int test_open_file(test_cfg* cfg, const char* filepath, int access)
{
    FILE* fp = NULL;
    const char* fmode;
    int fd = -1;

    assert(NULL != cfg);

    if (cfg->use_api) {
#ifndef DISABLE_UNIFYFS
        unifyfs_rc rc = unifyfs_open(cfg->fshdl, access, filepath,
                                     &(cfg->gfid));
        if (UNIFYFS_SUCCESS != rc) {
            test_print(cfg, "ERROR: unifyfs_open(%s) failed - %s",
                       filepath, unifyfs_rc_enum_description(rc));
            return -1;
        }
#endif
    } else if (cfg->use_mpiio) {
        int amode = test_access_to_mpiio_mode(access);
        if (cfg->io_pattern == IO_PATTERN_N1) {
            MPI_CHECK(cfg, (MPI_File_open(MPI_COMM_WORLD, filepath, amode,
                                          MPI_INFO_NULL, &cfg->mpifh)));
        } else {
            MPI_CHECK(cfg, (MPI_File_open(MPI_COMM_SELF, filepath, amode,
                                          MPI_INFO_NULL, &cfg->mpifh)));
        }
        if (mpi_error) {
            return -1;
        }
        cfg->fd = 1; // dummy value to denote target
    } else if (cfg->use_stdio) {
        fmode = test_access_to_stdio_mode(access);
        fp = fopen(filepath, fmode);
        if (NULL == fp) {
            test_print(cfg, "ERROR: fopen(%s) failed", filepath);
            return -1;
        }
        cfg->fp = fp;
    } else {
        fd = open(filepath, access);
        if (-1 == fd) {
            test_print(cfg, "ERROR: open(%s) failed", filepath);
            return -1;
        }
        cfg->fd = fd;
        cfg->fd_access = access;
    }
    return 0;
}

/*
 * open the given file
 */
static inline
int test_open_destination_file(test_cfg* cfg, int access)
{
    FILE* fp = NULL;
    const char* fmode;
    int fd = -1;

    assert(NULL != cfg);

    if (cfg->use_mpiio) {
        int amode = test_access_to_mpiio_mode(access);
        if (cfg->io_pattern == IO_PATTERN_N1) {
            MPI_CHECK(cfg, (MPI_File_open(MPI_COMM_WORLD, cfg->dest_filename,
                                          amode, MPI_INFO_NULL,
                                          &cfg->dest_mpifh)));
        } else {
            MPI_CHECK(cfg, (MPI_File_open(MPI_COMM_SELF, cfg->dest_filename,
                                          amode, MPI_INFO_NULL,
                                          &cfg->dest_mpifh)));
        }
        if (mpi_error) {
            return -1;
        }
        cfg->dest_fd = 2; // dummy value to denote destination
    } else {
        fd = open(cfg->dest_filename, access);
        if (-1 == fd) {
            test_print(cfg, "ERROR: open(%s) failed", cfg->dest_filename);
            return -1;
        }
        cfg->dest_fd = fd;
    }
    return 0;
}

/*
 * close the given file
 */
static inline
int test_close_file(test_cfg* cfg)
{
    assert(NULL != cfg);

    if (cfg->use_api) {
#ifndef DISABLE_UNIFYFS
        cfg->gfid = UNIFYFS_INVALID_GFID;
#endif
        return 0;
    }

    if (cfg->use_mpiio) {
        MPI_CHECK(cfg, (MPI_File_close(&cfg->mpifh)));
        cfg->fd = -1;
    }

    if (NULL != cfg->fp) {
        fclose(cfg->fp);
        cfg->fp = NULL;
    }

    if (NULL != cfg->mapped) {
        munmap(cfg->mapped, cfg->mapped_sz);
    }

    if (-1 != cfg->fd) {
        close(cfg->fd);
        cfg->fd = -1;
    }

    return 0;
}

/*
 * close the given file
 */
static inline
int test_close_destination_file(test_cfg* cfg)
{
    assert(NULL != cfg);

    if (cfg->use_mpiio) {
        MPI_CHECK(cfg, (MPI_File_close(&cfg->dest_mpifh)));
    }

    if (-1 != cfg->dest_fd) {
        close(cfg->dest_fd);
    }

    cfg->dest_fd = -1;

    return 0;
}

/*
 * remove the given file if it exists
 */
static inline
int test_remove_file(test_cfg* cfg, const char* filepath)
{
    struct stat sb;
    int rc;

    assert(NULL != cfg);

    /* stat file and simply return if it already doesn't exist */
    rc = stat(filepath, &sb);
    if (rc) {
        test_print_verbose_once(cfg,
            "DEBUG: stat(%s): file already doesn't exist", filepath);
        return 0;
    }

    if (cfg->use_mpiio) {
        MPI_CHECK(cfg, (MPI_File_delete(filepath, MPI_INFO_NULL)));
        if (mpi_error) {
            return -1;
        }
        return 0;
    }

    /* N-to-1 - rank 0 deletes shared files
     * N-to-N - all ranks delete per-process files */
    if (cfg->rank == 0 || cfg->io_pattern == IO_PATTERN_NN) {
        if (cfg->use_api) {
#ifndef DISABLE_UNIFYFS
            unifyfs_rc urc = unifyfs_remove(cfg->fshdl, filepath);
            if (UNIFYFS_SUCCESS != urc) {
                test_print(cfg, "ERROR: unifyfs_remove(%s) failed - %s",
                           filepath, unifyfs_rc_enum_description(urc));
                return -1;
            }
#endif
        } else if (cfg->use_stdio) {
            rc = remove(filepath);
            if (rc) {
                test_print(cfg, "ERROR: remove(%s) failed", filepath);
                return -1;
            }
        } else {
            rc = unlink(filepath);
            if (rc) {
                test_print(cfg, "ERROR: unlink(%s) failed", filepath);
                return -1;
            }
        }
    }
    return 0;
}

/*
 * create file at rank 0, open elsewhere
 */
static inline
int test_create_file(test_cfg* cfg, const char* filepath, int access)
{
    FILE* fp = NULL;
    const char* fmode;
    int fd = -1;
    int create_flags = O_CREAT | O_TRUNC;
    int create_mode = 0600; // S_IRUSR | S_IWUSR

    assert(NULL != cfg);

    /* MPI-IO */
    if (cfg->use_mpiio) {
        create_mode = test_access_to_mpiio_mode(access);
        create_mode |= MPI_MODE_CREATE;
        if (cfg->io_pattern == IO_PATTERN_N1) {
            MPI_CHECK(cfg, (MPI_File_open(MPI_COMM_WORLD, filepath, create_mode,
                                          MPI_INFO_NULL, &cfg->mpifh)));
        } else {
            create_mode |= MPI_MODE_EXCL;
            MPI_CHECK(cfg, (MPI_File_open(MPI_COMM_SELF, filepath, create_mode,
                                          MPI_INFO_NULL, &cfg->mpifh)));
        }
        if (mpi_error) {
            return -1;
        }
        return 0;
    }

    /* N-to-1 - rank 0 creates shared files
     * N-to-N - all ranks create per-process files */
    if (cfg->rank == 0 || cfg->io_pattern == IO_PATTERN_NN) {
        if (cfg->use_api) {
#ifndef DISABLE_UNIFYFS
            unifyfs_rc urc = unifyfs_create(cfg->fshdl, 0, filepath,
                                            &(cfg->gfid));
            if (UNIFYFS_SUCCESS != urc) {
                test_print(cfg, "ERROR: unifyfs_create(%s) failed - %s",
                           filepath, unifyfs_rc_enum_description(urc));
                return -1;
            }
#endif
        } else if (cfg->use_stdio) {
            fmode = test_access_to_stdio_mode(access);
            fp = fopen(filepath, fmode);
            if (NULL == fp) {
                test_print(cfg, "ERROR: fopen(%s) failed", filepath);
                return -1;
            }
            cfg->fp = fp;
        } else {
            if (cfg->io_pattern == IO_PATTERN_NN) {
                create_flags |= O_EXCL;
            }
            fd = open(filepath, access | create_flags, create_mode);
            if (-1 == fd) {
                test_print(cfg, "ERROR: open(%s, CREAT) failed", filepath);
                return -1;
            }
            cfg->fd = fd;
            cfg->fd_access = access;
        }
    }

    if (cfg->io_pattern == IO_PATTERN_N1) {
        // barrier enforces create before open
        test_barrier(cfg);

        // other ranks just do normal open
        if (cfg->rank != 0) {
            return test_open_file(cfg, filepath, access);
        }
    }
    return 0;
}


/*
 * map a segment of an already open file
 */
static inline
int test_map_file(test_cfg* cfg, off_t off, size_t len)
{
    void* addr;
    int prot;

    assert(NULL != cfg);

    if (!cfg->use_mapio) {
        test_print(cfg, "ERROR: cfg.use_mapio is not enabled");
        return -1;
    } else if (-1 == cfg->fd) {
        test_print(cfg, "ERROR: cfg.fd is invalid, must create/open first");
        return -1;
    }

    prot = test_access_to_mmap_prot(cfg->fd_access);
    addr = mmap(NULL, len, prot, MAP_SHARED, cfg->fd, off);
    if (MAP_FAILED == addr) {
        test_print(cfg, "ERROR: mmap() failed");
        return -1;
    }
    cfg->mapped = addr;
    cfg->mapped_sz = len;
    cfg->mapped_off = off;
    return 0;
}

/*
 * wait for input at rank 0, barrier everywhere
 */
static inline
void test_pause(test_cfg* cfg, const char* fmt, ...)
{
    assert(NULL != cfg);

    if (cfg->rank == 0) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);

        fprintf(stderr, "press ENTER to continue ... ");
        (void) getchar();
    }
    test_barrier(cfg);
}

/*
 * abort the test everywhere
 */
static inline
void test_abort(test_cfg* cfg, int rc)
{
    assert(NULL != cfg);

    if (cfg->use_mpi) {
        MPI_Abort(MPI_COMM_WORLD, rc);
    } else {
        exit(rc);
    }
}

/*
 * initialize test using argv to set config
 */
static inline
int test_init(int argc, char** argv,
              test_cfg* cfg)
{
    int rc;

    if (NULL == cfg) {
        fprintf(stderr, "INTERNAL ERROR: %s() : cfg is NULL\n", __func__);
        return -1;
    }

    test_config_init(cfg);

    rc = test_process_argv(cfg, argc, argv);
    if (rc) {
        fprintf(stderr, "ERROR: failed to process command-line args");
        return rc;
    }

    if (cfg->use_mpi) {
        MPI_CHECK(cfg, (MPI_Init(&argc, &argv)));
        MPI_Comm_size(MPI_COMM_WORLD, &(cfg->n_ranks));
        MPI_Comm_rank(MPI_COMM_WORLD, &(cfg->rank));
    } else {
        cfg->n_ranks = 1;
    }

    if (NULL != cfg->output_file) {
        if (cfg->rank == 0) {
            // only rank 0 writes to output file
            cfg->output_fp = fopen(cfg->output_file, "a");
            if (NULL == cfg->output_fp) {
                test_print_once(cfg,
                    "USAGE ERROR: failed to open output file %s",
                    cfg->output_file);
                exit(-1);
            }
        }
    }

    if (cfg->verbose) {
        // must come after MPI_Init() to have valid MPI info
        test_config_print(cfg);
    }

    if (cfg->use_unifyfs) {
#ifndef DISABLE_UNIFYFS
        if (cfg->debug) {
            test_pause(cfg, "Before mounting UnifyFS");
        }
        if (cfg->use_api) {
            unifyfs_rc urc = unifyfs_initialize(cfg->mountpt, NULL, 0,
                                                &(cfg->fshdl));
            if (UNIFYFS_SUCCESS != urc) {
                test_print(cfg, "ERROR: unifyfs_initialize(%s) failed (%s)",
                           cfg->mountpt, unifyfs_rc_enum_description(urc));
                test_abort(cfg, (int)urc);
                return -1;
            }
        } else if (!cfg->enable_mpi_mount) {
            rc = unifyfs_mount(cfg->mountpt, cfg->rank, cfg->n_ranks);
            if (rc) {
                test_print(cfg, "ERROR: unifyfs_mount(%s) failed (rc=%d)",
                           cfg->mountpt, rc);
                test_abort(cfg, rc);
                return -1;
            }
        }
#endif
        test_barrier(cfg);
    }

    if (cfg->debug) {
        test_pause(cfg, "Finished test initialization");
    }

    return 0;
}

/*
 * finalize test and free allocated config state
 */
static inline
void test_fini(test_cfg* cfg)
{
    if (NULL == cfg) {
        fprintf(stderr, "INTERNAL ERROR: %s() : cfg is NULL\n", __func__);
        return;
    }

    test_close_file(cfg);

    if (cfg->use_unifyfs) {
#ifndef DISABLE_UNIFYFS
        if (cfg->use_api) {
            unifyfs_rc urc = unifyfs_finalize(cfg->fshdl);
            if (UNIFYFS_SUCCESS != urc) {
                test_print(cfg, "ERROR: unifyfs_finalize() failed - %s",
                           unifyfs_rc_enum_description(urc));
            }
            cfg->fshdl = UNIFYFS_INVALID_HANDLE;
        } else if (!cfg->enable_mpi_mount) {
            int rc = unifyfs_unmount();
            if (rc) {
                test_print(cfg, "ERROR: unifyfs_unmount() failed (rc=%d)", rc);
            }
        }
#endif
    }

    if (cfg->use_mpi) {
        MPI_CHECK(cfg, (MPI_Finalize()));
    }

    if (NULL != cfg->filename) {
        free(cfg->filename);
    }

    if (NULL != cfg->mountpt) {
        free(cfg->mountpt);
    }

    if (NULL != cfg->output_file) {
        free(cfg->output_file);
        if (NULL != cfg->output_fp) {
            fflush(cfg->output_fp);
            fclose(cfg->output_fp);
        }
    }

    memset(cfg, 0, sizeof(test_cfg));
}

/*
 * Various C equivalents of bash commands (dd, test, mktemp, du, stat, ...)
 */
int dd_cmd(test_cfg* cfg, char* infile, char* outfile, unsigned long bs,
    unsigned long count, unsigned long seek);
int test_cmd(char* expression, char* path);
char* mktemp_cmd(test_cfg* cfg, char* tmpdir);
long du_cmd(test_cfg* cfg, char* filename, int apparent_size);
int sync_cmd(test_cfg* cfg, char* filename);
int stat_cmd(test_cfg* cfg, char* filename);

#endif /* UNIFYFS_TEST_UTIL_H */
