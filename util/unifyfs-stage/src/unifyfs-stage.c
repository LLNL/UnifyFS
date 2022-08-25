/*
 * Copyright (c) 2022, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2022, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <libgen.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unifyfs-stage.h"


int verbose;

static int debug_pause;
static int checksum;
static int data_distribution;
static int transfer_mode;
static char* manifest_file;
static char* mountpoint = "/unifyfs";
static char* status_file;

static unifyfs_stage stage_ctx;

/**
 * @brief create a status (lock) file to notify the unifyfs executable
 * when the staging is finished
 *
 * @param status 0 indicates success
 *
 * @return 0 on success, errno otherwise
 */
static int create_status_file(int status)
{
    int err;

    errno = 0;
    FILE* fp = fopen(status_file, "w");
    err = errno;
    if (!fp) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: failed to create %s (%s)\n",
                status_file, strerror(err));
        return err;
    }

    const char* msg = (status == 0 ? "success" : "fail");
    fprintf(fp, "%s\n", msg);
    fclose(fp);

    return 0;
}

static char* short_opts = "cDhm:psS:v";

static struct option long_opts[] = {
    { "checksum", 0, 0, 'c' },
    { "debug-pause", 0, 0, 'D' },
    { "help", 0, 0, 'h' },
    { "mountpoint", 1, 0, 'm' },
    { "parallel", 0, 0, 'p' },
    { "skewed", 0, 0, 's' },
    { "status-file", 1, 0, 'S' },
    { "verbose", 0, 0, 'v' },
    { 0, 0, 0, 0 },
};

static const char* usage_str =
    "\n"
    "Usage: %s [OPTION]... <manifest file>\n"
    "\n"
    "Transfer files between UnifyFS volume and external file system.\n"
    "The <manifest file> should contain list of files to be transferred,\n"
    "and each line should be formatted as\n"
    "\n"
    "  /source/file/path /destination/file/path\n"
    "\n"
    "OR in the case of filenames with spaces or special characters:\n"
    "\n"
    "  \"/source/file/path\" \"/destination/file/path\"\n"
    "\n"
    "One file per line; Specifying directories is not supported.\n"
    "\n"
    "Available options:\n"
    "\n"
    "  -c, --checksum           Verify md5 checksum for each transfer\n"
    "                           (default: off)\n"
    "  -h, --help               Print usage information\n"
    "  -m, --mountpoint=<mnt>   Use <mnt> as UnifyFS mountpoint\n"
    "                           (default: /unifyfs)\n"
    "  -p, --parallel           Transfer all files concurrently\n"
    "                           (default: off, use sequential transfers)\n"
    "  -s, --skewed             Use skewed data distribution for stage-in\n"
    "                           (default: off, use balanced distribution)\n"
    "  -S, --status-file=<path> Create stage status file at <path>\n"
    "  -v, --verbose            Print verbose information\n"
    "                           (default: off)\n"
    "\n";

static void print_usage(char* program)
{
    fprintf(stderr, usage_str, program);
}

static int debug_hold;
static void pause_for_debug(int rank, const char* fmt, ...)
{
    if (rank == 0) {
        debug_hold = 1;
        fprintf(stderr,
                "UNIFYFS-STAGE DEBUG - PAUSED: To continue execution, use "
                "debugger to set 'debug_hold' variable = 0 in MPI rank 0\n");
        fflush(stderr);
        while (debug_hold) {
            sleep(1);
        }

        fprintf(stderr, "UNIFYFS-STAGE DEBUG - CONTINUED\n");
        fflush(stderr);
    }

    MPI_Barrier(MPI_COMM_WORLD);
}

static int parse_option(int argc, char** argv)
{
    int ch = 0;
    int optidx = 0;
    char* filepath = NULL;

    /* set defaults */
    checksum = 0;
    debug_pause = 0;
    status_file = NULL;
    transfer_mode = UNIFYFS_STAGE_MODE_SERIAL;
    data_distribution = UNIFYFS_STAGE_DATA_BALANCED;

    if (argc < 2) {
        return EINVAL;
    }

    while ((ch = getopt_long(argc, argv,
                             short_opts, long_opts, &optidx)) >= 0) {
        switch (ch) {
        case 'c':
            checksum = 1;
            break;

        case 'D':
            debug_pause = 1;
            break;

        case 'm':
            mountpoint = strdup(optarg);
            break;

        case 'p':
            transfer_mode = UNIFYFS_STAGE_MODE_PARALLEL;
            break;

        case 's':
            data_distribution = UNIFYFS_STAGE_DATA_SKEWED;
            break;

        case 'S':
            status_file = strdup(optarg);
            break;

        case 'v':
            verbose = 1;
            break;

        case 'h':
        default:
            break;
        }
    }

    if (argc - optind != 1) {
        return EINVAL;
    }

    filepath = argv[optind];

    manifest_file = realpath(filepath, NULL);
    if (!manifest_file) {
        fprintf(stderr,
                "UNIFYFS-STAGE ERROR: "
                "could not access manifest file %s: %s\n",
                filepath, strerror(errno));
        return errno;
    }

    if (NULL == status_file) {
        /* status_file is required for transfer status file */
        return EINVAL;
    }

    return 0;
}

void print_unifyfs_stage_context(unifyfs_stage* ctx)
{
    const char* mode_str = (ctx->mode == UNIFYFS_STAGE_MODE_SERIAL ?
                            "serial" : "parallel");
    const char* dist_str = (ctx->data_dist == UNIFYFS_STAGE_DATA_BALANCED ?
                            "balanced" : "skewed");
    fprintf(stderr,
            "UNIFYFS-STAGE INFO: ==== stage context ====\n"
            "                  : manifest file = %s\n"
            "                  : mountpoint = %s\n"
            "                  : transfer mode = %s\n"
            "                  : data distribution = %s\n"
            "                  : verify checksums = %d\n"
            "                  : rank = %d of %d\n",
            ctx->manifest_file,
            ctx->mountpoint,
            mode_str,
            dist_str,
            ctx->checksum,
            ctx->rank, ctx->total_ranks);
}

/*
 * Parse a line from the manifest in the form of:
 *
 * <source path> <whitespace separator> <destination path>
 *
 * If the paths have spaces, they must be quoted.
 *
 * On success, returns 0 along with allocated src_file and dst_file strings.
 * These strings should be freed by the caller.
 *
 * On failure, returns non-zero, and set src and dst to NULL.
 *
 * Note, leading and tailing whitespace are ok.  They just get ignored.
 * Lines with only whitespace or starting with the comment character '#'
 * are ignored, and the return value will be 0 with src and dst being NULL.
 */
/**
 * @brief parses manifest file line, passes back src and dst strings
 *
 * @param line_number       manifest file line number
 * @param line              manifest file line
 * @param[out] src_file     source file path
 * @param[out] dst_file     destination file path
 *
 * @return 0 if all was well, or there was nothing; non-zero on error
 */
int unifyfs_parse_manifest_line(int line_number,
                                char* line,
                                char** src_file,
                                char** dst_file)
{
    char* src = NULL;
    char* dst = NULL;
    char* copy;
    char* tmp;
    size_t copy_len;
    size_t tmp_len;
    size_t i;
    int in_quotes = 0;
    int ret = 0;


    if ((NULL == line) || (NULL == src_file) || (NULL == dst_file)) {
        return EINVAL;
    }

    *src_file = NULL;
    *dst_file = NULL;

    if ((line[0] == '\n') || (line[0] == '#')) {
        // skip blank or comment (#) lines in manifest file
        return 0;
    }

    copy = strdup(line);
    if (NULL == copy) {
        return ENOMEM;
    }
    copy_len = strlen(copy) + 1; /* +1 for '\0' */

    /* Replace quotes and separator with NUL character */
    for (i = 0; i < copy_len; i++) {
        if (copy[i] == '"') {
            in_quotes ^= 1;/* toggle */
            copy[i] = '\0';
        } else if (isspace(copy[i]) && !in_quotes) {
            /*
             * Allow any whitespace for our separator
             */
            copy[i] = '\0';
        }
    }

    /* copy now contains a series of strings, separated by NUL characters */
    tmp = copy;
    while (tmp < (copy + copy_len)) {
        tmp_len = strlen(tmp);
        if (tmp_len > 0) {
            /* We have a real string */
            if (!src) {
                src = strdup(tmp);
                if (NULL == src) {
                    return ENOMEM;
                }
            } else {
                if (!dst) {
                    dst = strdup(tmp);
                    if (NULL == dst) {
                        return ENOMEM;
                    }
                } else {
                    /* Error: a third file name */
                    ret = EINVAL;
                    break;
                }
            }
        }
        tmp += tmp_len + 1;
    }
    free(copy);

    /* Some kind of error parsing a line */
    if ((ret != 0) || (NULL == src) || (NULL == dst)) {
        if (NULL != src) {
            free(src);
            src = NULL;
        }
        if (NULL != dst) {
            free(dst);
            dst = NULL;
        }
        if (ret == 0) {
            ret = EINVAL;
        }
    } else {
        *src_file = src;
        *dst_file = dst;
    }

    return ret;
}

int main(int argc, char** argv)
{
    int rc, ret;
    int rank, total_ranks;
    unifyfs_stage* ctx = &stage_ctx;

    char* program = basename(strdup(argv[0]));

    ret = parse_option(argc, argv);
    if (ret) {
        if (EINVAL == ret) {
            print_usage(program);
        }
        return ret;
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);


    ctx->checksum = checksum;
    ctx->data_dist = data_distribution;
    ctx->manifest_file = manifest_file;
    ctx->mode = transfer_mode;
    ctx->mountpoint = mountpoint;
    ctx->rank = rank;
    ctx->total_ranks = total_ranks;
    if (verbose) {
        print_unifyfs_stage_context(ctx);
    }

    if (debug_pause) {
        pause_for_debug(rank, "About to initialize UnifyFS.. ");
    }

    // initialize UnifyFS API handle for transfer
    unifyfs_handle fshdl = UNIFYFS_INVALID_HANDLE; /* client handle */
    unifyfs_rc urc = unifyfs_initialize(ctx->mountpoint, NULL, 0, &(fshdl));
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                "UnifyFS initialization for mntpt=%s failed (%s)",
                ctx->mountpoint, unifyfs_rc_enum_description(urc));
        ret = -1;
        MPI_Abort(MPI_COMM_WORLD, ret);
    }
    ctx->fshdl = fshdl;

    MPI_Barrier(MPI_COMM_WORLD);

    /* TODO - Currently, all ranks open and parse the manifest file. It may be
     *        better if rank 0 does that and then broadcasts the file pairs */
    FILE* manifest = NULL;
    manifest = fopen(ctx->manifest_file, "r");
    if (!manifest) {
        if (rank == 0) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "failed to open manifest file %s (%s)\n",
                    ctx->manifest_file, strerror(errno));
        }
        ret = errno;
        MPI_Abort(MPI_COMM_WORLD, ret);
    }

    int line_count = 0;
    int file_count = 0;
    int n_xfer_failures = 0;
    char* src = NULL;
    char* dst = NULL;
    char linebuf[LINE_MAX] = { 0, };
    while (NULL != fgets(linebuf, LINE_MAX - 1, manifest)) {
        line_count++;
        rc = unifyfs_parse_manifest_line(line_count, linebuf, &src, &dst);
        if (EINVAL == rc) {
            if (rank == 0) {
                fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                        "manifest line[%d] is invalid! - '%s'\n",
                        line_count, linebuf);
            }
            ret = rc;
        } else if ((0 == rc) && (NULL != src) && (NULL != dst)) {
            file_count++;
            rc = unifyfs_stage_transfer(ctx, file_count, src, dst);
            if (rc) {
                if (rc != EINVAL) {
                    n_xfer_failures++;
                }
                ret = rc;
            }
        }
    }

    // wait until all processes are done
    MPI_Barrier(MPI_COMM_WORLD);

    // use a global reduction to sum total number of transfer failures
    int total_xfer_failures = 0;
    rc = MPI_Reduce(&n_xfer_failures, &total_xfer_failures,
                    1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    if (rc != MPI_SUCCESS) {
        char errstr[MPI_MAX_ERROR_STRING];
        int len = 0;
        MPI_Error_string(rc, errstr, &len);
        fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                "failed to aggregate transfer failures (error='%s')\n",
                errstr);
        total_xfer_failures = 1;
    }
    if (0 == ret) {
        ret = total_xfer_failures;
    }

    // rank 0 reports overall status
    if (rank == 0) {
        rc = create_status_file(ret);
        if (rc) {
            fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                    "failed to create stage status file (error=%d)\n",
                    rc);
            ret = rc;
        }
    }

    // finalize UnifyFS API handle
    urc = unifyfs_finalize(fshdl);
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS-STAGE ERROR: "
                "UnifyFS finalization failed - %s",
                unifyfs_rc_enum_description(urc));
    }
    fshdl = UNIFYFS_INVALID_HANDLE;

    MPI_Finalize();

    return ret;
}

