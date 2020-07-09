/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
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
/* read-data: This program aims to test reading data from a file with arbitrary
 * offset and length. This program can run either interactively (only
 * specifying filename) or non-interactively (specifying filename with offset
 * and length).
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
#include <unifyfs.h>

#include "testutil.h"

#define DEFAULT_MOUNTPOINT "/unifyfs"

static char filename[PATH_MAX];
static char mountpoint[PATH_MAX];

static char* buf;
static unsigned int bufsize;

static int parse_line(char* line, unsigned long* offset, unsigned long* length)
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

    *offset = strtoul(line, NULL, 0);
    *length = strtoul(pos, NULL, 0);

    return 0;
}

static void alloc_buf(unsigned long length)
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

static void do_pread(int fd, size_t length, off_t offset)
{
    ssize_t ret = 0;

    alloc_buf(length);

    errno = 0;

    ret = pread(fd, buf, length, offset);

    printf(" -> pread(off=%lu, len=%lu) = %zd", offset, length, ret);
    if (errno) {
        printf("  (err=%d, %s)\n", errno, strerror(errno));
    } else {
        printf("\n");
    }
}

static void run_interactive(int fd)
{
    int ret = 0;
    unsigned long offset = 0;
    unsigned long length = 0;
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
    { "mount", 1, 0, 'm' },
    { "offset", 1, 0, 'o' },
    { "length", 1, 0, 'l' },
    { 0, 0, 0, 0},
};

static char* short_opts = "hm:o:l:";

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
" -h, --help                help message\n"
" -m, --mount=<mountpoint>  use <mountpoint> for unifyfs (default: /unifyfs)\n"
" -o, --offset=<offset>     read from <offset>\n"
" -l, --length=<length>     read <length> bytes\n"
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
    unsigned long offset = 0;
    unsigned long length = 0;
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
        case 'm':
            sprintf(mountpoint, "%s", optarg);
            break;

        case 'o':
            offset = strtoul(optarg, NULL, 0);
            break;

        case 'l':
            length = strtoul(optarg, NULL, 0);
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

        ret = unifyfs_mount(mountpoint, 0, 1, 0);
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

    printf("%s (size = %lu)\n", filename, sb.st_size);

    if (offset == 0 && length == 0) {
        run_interactive(fd);
    } else {
        do_pread(fd, length, offset);
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
