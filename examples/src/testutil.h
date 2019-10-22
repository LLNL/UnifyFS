/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2019, UT-Battelle, LLC.
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
#include <unistd.h>
#include <mpi.h>

#ifndef DISABLE_UNIFYFS
# include <unifyfs.h>
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

    /* I/O behavior options */
    int io_pattern; /* N1 or NN */
    int io_check;   /* use lipsum to verify data */
    int io_shuffle; /* read and write different extents */
    int use_aio;    /* use asynchronous IO */
    int use_lio;    /* use lio_listio instead of read/write */
    int use_mapio;  /* use mmap instead of read/write */
    int use_prdwr;  /* use pread/pwrite instead of read/write */
    int use_stdio;  /* use fread/fwrite instead of read/write */
    int use_vecio;  /* use readv/writev instead of read/write */

    /* I/O size options */
    uint64_t n_blocks; /* number of I/O blocks */
    uint64_t block_sz; /* IO block size (multiple of chunk_sz) */
    uint64_t chunk_sz; /* per-IO-op size */

    /* target file info */
    char*  filename;
    char*  mountpt;
    FILE*  fp;
    void*  mapped;     /* address of mapped extent of cfg.fd */
    off_t  mapped_off; /* start offset for mapped extent */
    size_t mapped_sz;  /* size of mapped extent */
    int    fd;
    int    fd_access;  /* access flags for cfg.fd */

    /* MPI info */
    int app_id;
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

    // N-to-1 UnifyFS by default
    cfg->use_mpi = 1;
    cfg->use_unifyfs = 1;
    cfg->io_pattern = IO_PATTERN_N1;

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

    fprintf(stderr, "    Test Configuration\n");
    fprintf(stderr, "==========================\n");

    fprintf(stderr, "\n-- Program Behavior --\n");
    fprintf(stderr, "\t debug       = %d\n", cfg->debug);
    fprintf(stderr, "\t verbose     = %d\n", cfg->verbose);
    fprintf(stderr, "\t use_mpi     = %d\n", cfg->use_mpi);
    fprintf(stderr, "\t use_unifyfs = %d\n", cfg->use_unifyfs);

    fprintf(stderr, "\n-- IO Behavior --\n");
    fprintf(stderr, "\t io_pattern  = %s\n", io_pattern_str(cfg->io_pattern));
    fprintf(stderr, "\t io_check    = %d\n", cfg->io_check);
    fprintf(stderr, "\t io_shuffle  = %d\n", cfg->io_shuffle);
    fprintf(stderr, "\t use_aio     = %d\n", cfg->use_aio);
    fprintf(stderr, "\t use_lio     = %d\n", cfg->use_lio);
    fprintf(stderr, "\t use_mapio   = %d\n", cfg->use_mapio);
    fprintf(stderr, "\t use_prdwr   = %d\n", cfg->use_prdwr);
    fprintf(stderr, "\t use_stdio   = %d\n", cfg->use_stdio);
    fprintf(stderr, "\t use_vecio   = %d\n", cfg->use_vecio);

    fprintf(stderr, "\n-- IO Size Config --\n");
    fprintf(stderr, "\t n_blocks    = %" PRIu64 "\n", cfg->n_blocks);
    fprintf(stderr, "\t block_sz    = %" PRIu64 "\n", cfg->block_sz);
    fprintf(stderr, "\t chunk_sz    = %" PRIu64 "\n", cfg->chunk_sz);

    fprintf(stderr, "\n-- Target File --\n");
    fprintf(stderr, "\t filename    = %s\n", cfg->filename);
    fprintf(stderr, "\t mountpt     = %s\n", cfg->mountpt);

    fprintf(stderr, "\n-- MPI Info --\n");
    fprintf(stderr, "\t app_id      = %d\n", cfg->app_id);
    fprintf(stderr, "\t rank        = %d\n", cfg->rank);
    fprintf(stderr, "\t n_ranks     = %d\n", cfg->n_ranks);
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

/* ---------- Print Utilities ---------- */

static inline
void test_print(test_cfg* cfg, const char* fmt, ...)
{
    int err = errno;
    char buf[1024];

    assert(NULL != cfg);

    printf("[%d] ", cfg->rank);

    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    printf("%s", buf);
    va_end(args);

    if (err) {
        printf(" (errno=%d, %s)", err, strerror(err));
    }

    /* Add in a '\n' if the line didn't end with one */
    if (buf[strlen(buf) - 1] != '\n') {
        printf("\n");
    }

    fflush(stdout);
}

static inline
void test_print_once(test_cfg* cfg, const char* fmt, ...)
{
    int err = errno;

    assert(NULL != cfg);

    if (cfg->rank != 0) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    if (err) {
        printf(" (errno=%d, %s)\n", err, strerror(err));
    }

    printf("\n");
    fflush(stdout);
}

static inline
void test_print_verbose(test_cfg* cfg, const char* fmt, ...)
{
    assert(NULL != cfg);

    if (cfg->verbose == 0) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);
}

static inline
void test_print_verbose_once(test_cfg* cfg, const char* fmt, ...)
{
    assert(NULL != cfg);

    if ((cfg->verbose == 0) || (cfg->rank != 0)) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);
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

static const char* test_short_opts = "a:Ab:c:df:hkLm:Mn:p:PSUvVx";

static const struct option test_long_opts[] = {
    { "appid", 1, 0, 'a' },
    { "aio", 0, 0, 'A' },
    { "blocksize", 1, 0, 'b' },
    { "chunksize", 1, 0, 'c' },
    { "debug", 0, 0, 'd' },
    { "file", 1, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "check", 0, 0, 'k' },
    { "listio", 0, 0, 'L' },
    { "mount", 1, 0, 'm' },
    { "mapio", 0, 0, 'M' },
    { "nblocks", 1, 0, 'n' },
    { "pattern", 1, 0, 'p' },
    { "prdwr", 0, 0, 'P' },
    { "stdio", 0, 0, 'S' },
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
    " -a, --appid=<id>                 use given application id\n"
    "                                  (default: 0)\n"
    " -A, --aio                        use asynchronous I/O instead of read|write\n"
    "                                  (default: off)\n"
    " -b, --blocksize=<bytes>          I/O block size\n"
    "                                  (default: 16 MiB)\n"
    " -c, --chunksize=<bytes>          I/O chunk size for each operation\n"
    "                                  (default: 1 MiB)\n"
    " -d, --debug                      for debugging, wait for input (at rank 0) at start\n"
    "                                  (default: off)\n"
    " -f, --file=<filename>            target file name (or path) under mountpoint\n"
    "                                  (default: 'testfile')\n"
    " -k, --check                      check data contents upon read\n"
    "                                  (default: off)\n"
    " -L, --listio                     use lio_listio instead of read|write\n"
    "                                  (default: off)\n"
    " -m, --mount=<mountpoint>         use <mountpoint> for unifyfs\n"
    "                                  (default: /unifyfs)\n"
    " -M, --mapio                      use mmap instead of read|write\n"
    "                                  (default: off)\n"
    " -n, --nblocks=<count>            count of blocks each process will read|write\n"
    "                                  (default: 32)\n"
    " -p, --pattern=<pattern>          'n1' (N-to-1 shared file) or 'nn' (N-to-N file per process)\n"
    "                                  (default: 'n1')\n"
    " -P, --prdwr                      use pread|pwrite instead of read|write\n"
    "                                  (default: off)\n"
    " -S, --stdio                      use fread|fwrite instead of read|write\n"
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
        case 'a':
            cfg->app_id = atoi(optarg);
            break;

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

        case 'f':
            cfg->filename = strdup(optarg);
            break;

        case 'k':
            cfg->io_check = 1;
            break;

        case 'L':
            cfg->use_lio = 1;
            break;

        case 'm':
            cfg->mountpt = strdup(optarg);
            break;

        case 'M':
            cfg->use_mapio = 1;
            break;

        case 'n':
            cfg->n_blocks = (uint64_t) strtoul(optarg, NULL, 0);
            break;

        case 'p':
            cfg->io_pattern = check_io_pattern(optarg);
            break;

        case 'P':
            cfg->use_prdwr = 1;
            break;

        case 'S':
            cfg->use_stdio = 1;
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
    if (cfg->use_aio &&
       (cfg->use_mapio || cfg->use_prdwr || cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg, "USAGE ERROR: --aio incompatible with "
                             "[--mapio, --prdwr, --stdio, --vecio]");
        exit(-1);
    }

    if (cfg->use_lio &&
       (cfg->use_mapio || cfg->use_prdwr || cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg, "USAGE ERROR: --listio incompatible with "
                             "[--mapio, --prdwr, --stdio, --vecio]");
        exit(-1);
    }

    if (cfg->use_mapio &&
       (cfg->use_prdwr || cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg, "USAGE ERROR: --mapio incompatible with "
                             "[--aio, --listio, --prdwr, --stdio, --vecio]");
        exit(-1);
    }

    if (cfg->use_prdwr &&
       (cfg->use_stdio || cfg->use_vecio)) {
        test_print_once(cfg, "USAGE ERROR: --prdwr incompatible with "
                             "[--aio, --listio, --mapio, --stdio, --vecio]");
        exit(-1);
    }

    if (cfg->use_stdio && cfg->use_vecio) {
        test_print_once(cfg, "USAGE ERROR: --stdio incompatible with "
                             "[--aio, --listio, --mapio, --prdwr, --vecio]");
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
    uint64_t start = offset / sizeof(uint64_t);
    uint64_t count = len / sizeof(uint64_t);
    uint64_t* ibuf = (uint64_t*) buf;

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
    uint64_t start = offset / sizeof(uint64_t);
    uint64_t count = len / sizeof(uint64_t);
    const uint64_t* ibuf = (uint64_t*) buf;

    for (i = 0; i < count; i++) {
        val = start + i;
        if (ibuf[i] != val) {
            *error_offset = offset + (i * sizeof(uint64_t));
            fprintf(stderr, "DEBUG: [%" PRIu64 "] @ offset %" PRIu64
                    ", expected=%" PRIu64 " found=%" PRIu64 "\n",
                    i, *error_offset, val, ibuf[i]);
            return -1;
        }
    }

    return 0;
}

/* ---------- MPI Utilities ---------- */

static inline
void test_barrier(test_cfg* cfg)
{
    assert(NULL != cfg);

    if (cfg->use_mpi) {
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

static inline
double test_reduce_double_sum(test_cfg* cfg, double local_val)
{
    double aggr_val = 0.0;

    assert(NULL != cfg);

    if (cfg->use_mpi) {
        MPI_Reduce(&local_val, &aggr_val,
                   1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
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
        MPI_Reduce(&local_val, &aggr_val,
                   1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
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
        MPI_Reduce(&local_val, &aggr_val,
                   1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
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

    if (cfg->use_stdio) {
        fmode = test_access_to_stdio_mode(access);
        fp = fopen(filepath, fmode);
        if (NULL == fp) {
            test_print(cfg, "ERROR: fopen(%s) failed", filepath);
            return -1;
        }
        cfg->fp = fp;
        return 0;
    }

    fd = open(filepath, access);
    if (-1 == fd) {
        test_print(cfg, "ERROR: open(%s) failed", filepath);
        return -1;
    }
    cfg->fd = fd;
    cfg->fd_access = access;
    return 0;
}

/*
 * close the given file
 */
static inline
int test_close_file(test_cfg* cfg)
{
    assert(NULL != cfg);

    if (NULL != cfg->fp) {
        fclose(cfg->fp);
    }

    if (NULL != cfg->mapped) {
        munmap(cfg->mapped, cfg->mapped_sz);
    }

    if (-1 != cfg->fd) {
        close(cfg->fd);
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

    if (cfg->use_stdio) {
        fmode = test_access_to_stdio_mode(access);
    }

    // rank 0 creates or all ranks create if using file-per-process
    if (cfg->rank == 0 || cfg->io_pattern == IO_PATTERN_NN) {
        if (cfg->use_stdio) {
            fp = fopen(filepath, fmode);
            if (NULL == fp) {
                test_print(cfg, "ERROR: fopen(%s) failed", filepath);
                return -1;
            }
            cfg->fp = fp;
        } else {
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
        MPI_Init(&argc, &argv);
        MPI_Comm_size(MPI_COMM_WORLD, &(cfg->n_ranks));
        MPI_Comm_rank(MPI_COMM_WORLD, &(cfg->rank));
    } else {
        cfg->n_ranks = 1;
    }

    if (cfg->verbose) {
        // must come after test_mpi_init() to pick up MPI info
        test_config_print(cfg);
    }

    if (cfg->use_unifyfs) {
#ifndef DISABLE_UNIFYFS
        if (cfg->debug) {
            test_pause(cfg, "Before unifyfs_mount()");
        }
        rc = unifyfs_mount(cfg->mountpt, cfg->rank, cfg->n_ranks, cfg->app_id);
        if (rc) {
            test_print(cfg, "ERROR: unifyfs_mount() failed (rc=%d)", rc);
            test_abort(cfg, rc);
        }
#endif
        test_barrier(cfg);
    } else {
        if (cfg->debug) {
            test_pause(cfg, "Finished test initialization");
        }
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
        int rc = unifyfs_unmount();
        if (rc) {
            test_print(cfg, "ERROR: unifyfs_unmount() failed (rc=%d)", rc);
        }
#endif
    }

    if (cfg->use_mpi) {
        MPI_Finalize();
    }

    if (NULL != cfg->filename) {
        free(cfg->filename);
    }

    if (NULL != cfg->mountpt) {
        free(cfg->mountpt);
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
