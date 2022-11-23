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

/* read-data: This program aims to test reading data from a file with arbitrary
 * offset and length. This program can run either interactively (only
 * specifying filename) or non-interactively (specifying filename with offset
 * and length).
 */

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <unifyfs.h>

#include "testutil.h"

#define DEFAULT_MOUNTPOINT "/unifyfs"

static char filename[PATH_MAX];
static char mountpoint[PATH_MAX];

static char* buf;
static uint64_t bufsize;
static int check;

static int parse_line(char* line, uint64_t* offset, uint64_t* length)
{
    char* pos = NULL;

    line[strlen(line)-1] = '\0';

    if (strncmp("quit", line, strlen("quit")) == 0) {
        return 1;
    }

    pos = strchr(line, ',');
    if (!pos) {
        return -1;
    }

    *pos = '\0';
    pos++;

    *offset = strtoull(line, NULL, 0);
    *length = strtoull(pos, NULL, 0);

    return 0;
}

static void alloc_buf(uint64_t length)
{
    if (!buf) {
        bufsize = length;
        buf = malloc(bufsize);
    } else {
        if (bufsize < length) {
            buf = realloc(buf, length);
        }
    }

    if (!buf) {
        perror("failed to allocate buffer");
        exit(1);
    }
}

static void aligned_offlen(uint64_t filesize, uint64_t blocksize,
                          uint64_t* off, uint64_t* len)
{
    uint64_t block_count = filesize / blocksize;

    *off = (random() % (block_count - 1)) * blocksize;
    *len = blocksize;
}


static void random_offlen(uint64_t filesize, uint64_t maxoff, uint64_t maxlen,
                          uint64_t* off, uint64_t* len)
{
    uint64_t _off;
    uint64_t _len;

    _off = random() % maxoff;
    _len = random() % maxlen;

    while (_off + _len > filesize) {
        _len = _len / 2 + 1;
    }

    *len = _len;
    *off = _off;
}

static void do_pread(int fd, size_t length, off_t offset)
{
    int err;
    ssize_t ret = 0;
    struct timespec ts1, ts2;
    double ts1nsec, ts2nsec;
    double elapsed_sec, mbps;

    alloc_buf(length);

    clock_gettime(CLOCK_REALTIME, &ts1);

    errno = 0;
    ret = pread(fd, buf, length, offset);
    err = errno;

    clock_gettime(CLOCK_REALTIME, &ts2);

    ts1nsec = 1e9 * 1.0 * ts1.tv_sec + 1.0 * ts1.tv_nsec;
    ts2nsec = 1e9 * 1.0 * ts2.tv_sec + 1.0 * ts2.tv_nsec;
    elapsed_sec = (ts2nsec - ts1nsec) / (1e9);

    mbps = (1.0 * length / (1<<20)) / elapsed_sec;

    printf(" -> pread(off=%lu, len=%lu) = %zd", offset, length, ret);
    if (err) {
        printf("  (err=%d, %s)\n", err, strerror(err));
    } else {
        printf(" (%.3f sec, %.3lf MB/s)\n", elapsed_sec, mbps);

        if (check) {
            uint64_t error_offset;
            ret = lipsum_check(buf, length, offset, &error_offset);
            if (ret < 0) {
                printf("    * data verification failed at offset %" PRIu64 "\n",
                       error_offset);
            } else {
                printf("    * data verification success\n");
            }
        }
    }
}

static void run_interactive(int fd)
{
    int ret = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
    char* line = NULL;
    char linebuf[LINE_MAX];

    while (1) {
        printf("\nread (offset,length)> ");
        fflush(stdout);
        line = fgets(linebuf, LINE_MAX-1, stdin);
        if (!line) {
            continue;
        }

        ret = parse_line(line, &offset, &length);
        if (ret < 0) {
            continue;
        } else if (1 == ret) {
            printf("terminating..\n");
            break;
        }

        do_pread(fd, length, offset);
    }
}

static struct option long_opts[] = {
    { "help", 0, 0, 'h' },
    { "check", 0, 0, 'c' },
    { "mount", 1, 0, 'm' },
    { "offset", 1, 0, 'o' },
    { "length", 1, 0, 'l' },
    { "random", 1, 0, 'r' },
    { "max-offset", 1, 0, 'O' },
    { "max-length", 1, 0, 'L' },
    { "aligned", 1, 0, 'a' },
    { 0, 0, 0, 0},
};

static char* short_opts = "hcm:o:l:r:O:L:f:";

static const char* usage_str =
"\n"
"Usage: %s [options...] <pathname>\n"
"\n"
"Test reading data from a file <pathname>. <pathname> should be a full\n"
"pathname. If running without --offset and --length options, this will run\n"
"in an interactive mode where the offset and length should be specified\n"
"with a separating comma between them, e.g., '<offset>,<length>'.\n"
"'quit' will terminate the program.\n"
"\n"
"Available options:\n"
" -h, --help               help message\n"
" -c, --check              verify data content. data should be written using\n"
"                          the write example with --check option\n"
" -m, --mount=<mountpoint> use <mountpoint> for unifyfs (default: /unifyfs)\n"
" -o, --offset=<offset>    read from <offset>\n"
" -l, --length=<length>    read <length> bytes\n"
" -r, --random=<repeat>    generate random offset and length <repeat> times,\n"
"                          only workin in the non-interactive mode\n"
" -O, --max-offset=<off>   generate a random offset not exceeding <off>\n"
" -L, --max-length=<len>   generate a random length not exceeding <len>\n"
" -f, --aligned=<size>     generate requests aligned with a blocksize <size>\n"
"\n";

static char* program;

static void print_usage(void)
{
    printf(usage_str, program);
    exit(0);
}

int main(int argc, char** argv)
{
    int ret = 0;
    int ch = 0;
    int optidx = 0;
    int unifyfs = 0;
    int fd = -1;
    int random = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t maxoff = 0;
    uint64_t maxlen = 0;
    uint64_t aligned = 0;
    uint64_t filesize = 0;
    struct stat sb;
    char* tmp_program = NULL;

    tmp_program = strdup(argv[0]);
    if (!tmp_program) {
        perror("failed to allocate memory");
        return -1;
    }

    program = basename(tmp_program);

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            check = 1;
            break;

        case 'm':
            sprintf(mountpoint, "%s", optarg);
            break;

        case 'o':
            offset = strtoull(optarg, NULL, 0);
            break;

        case 'l':
            length = strtoull(optarg, NULL, 0);
            break;

        case 'r':
            random = atoi(optarg);
            break;

        case 'O':
            maxoff = strtoull(optarg, NULL, 0);
            break;

        case 'L':
            maxlen = strtoull(optarg, NULL, 0);
            break;

        case 'a':
            aligned = strtoull(optarg, NULL, 0);
            break;

        case 'h':
        default:
            print_usage();
            break;
        }
    }

    if (argc - optind != 1) {
        print_usage();
        return -1;
    }

    sprintf(filename, "%s", argv[optind]);

    if (mountpoint[0] == '\0') {
        sprintf(mountpoint, "%s", DEFAULT_MOUNTPOINT);
    }

    if (strncmp(filename, mountpoint, strlen(mountpoint)) == 0) {
        printf("mounting unifyfs at %s ..\n", mountpoint);

        ret = unifyfs_mount(mountpoint, 0, 1);
        if (ret) {
            fprintf(stderr, "unifyfs_mount failed (return = %d)\n", ret);
            return -1;
        }

        unifyfs = 1;
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        return -1;
    }

    ret = stat(filename, &sb);
    if (ret < 0) {
        perror("stat failed");
        goto out;
    }

    filesize = sb.st_size;
    printf("%s (size = %lu)\n", filename, filesize);

    if (offset == 0 && length == 0 && random == 0) {
        run_interactive(fd);
    } else {
        if (random) {
            struct timespec ts;

            clock_gettime(CLOCK_REALTIME, &ts);
            srandom(ts.tv_nsec % ts.tv_sec);

            if (0 == maxoff) {
                maxoff = filesize / 2;
            }

            if (0 == maxlen) {
                maxlen = filesize / 2;
            }

            for (int i = 0; i < random; i++) {
                if (aligned) {
                    aligned_offlen(filesize, aligned, &offset, &length);
                } else {
                    random_offlen(filesize, maxoff, maxlen, &offset, &length);
                }
                do_pread(fd, length, offset);
            }
        } else {
            do_pread(fd, length, offset);
        }
    }

    ret = 0;
out:
    close(fd);

    if (buf) {
        free(buf);
    }

    if (unifyfs) {
        unifyfs_unmount();
    }

    if (tmp_program) {
        free(tmp_program);
    }

    return ret;
}
