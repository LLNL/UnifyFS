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
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <getopt.h>
#include <time.h>
#include <mpi.h>
#include <openssl/md5.h>

#include "unifyfs-stage.h"

/**
 * @brief Run md5 checksum on specified file, send back
 *        digest.
 *
 * @param path      path to the target file
 * @param digest    hash of the file
 *
 * @return 0 on success, errno otherwise
 */
static int md5_checksum(const char* path, unsigned char* digest)
{
    int ret = 0;
    size_t len = 0;
    int fd = -1;
    unsigned char data[UNIFYFS_STAGE_MD5_BLOCKSIZE] = { 0, };
    MD5_CTX md5;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return errno;
    }

    ret = MD5_Init(&md5);
    if (!ret) {
        fprintf(stderr, "failed to create md5 context\n");
        goto out;
    }

    while ((len = read(fd, (void*) data, UNIFYFS_STAGE_MD5_BLOCKSIZE)) != 0) {
        ret = MD5_Update(&md5, data, len);
        if (!ret) {
            fprintf(stderr, "failed to update checksum\n");
            goto out;
        }
    }

    ret = MD5_Final(digest, &md5);
    if (!ret) {
        fprintf(stderr, "failed to finalize md5\n");
    }

out:
    /* MD5_xx returns 1 for success */
    ret = ret == 1 ? 0 : EIO;
    close(fd);

    return ret;
}

/**
 * @brief prints md5 checksum into string
 *
 * @param buf       buffer to print into
 * @param digest    hash of the file
 *
 * @return buffer that has been printed to
 */
static char* checksum_str(char* buf, unsigned char* digest)
{
    int i = 0;
    char* pos = buf;

    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        pos += sprintf(pos, "%02x", digest[i]);
    }

    pos[0] = '\0';

    return buf;
}

/**
 * @brief takes check sums of two files and compares
 *
 * @param src     path to one file
 * @param dst     path to the other file
 *
 * @return 0 if files are identical, non-zero if not, or other error
 */
static int verify_checksum(const char* src, const char* dst)
{
    int ret = 0;
    int i = 0;
    char md5src[2 * MD5_DIGEST_LENGTH + 1] = { 0, };
    char md5dst[2 * MD5_DIGEST_LENGTH + 1] = { 0, };
    unsigned char src_digest[MD5_DIGEST_LENGTH + 1] = { 0, };
    unsigned char dst_digest[MD5_DIGEST_LENGTH + 1] = { 0, };

    src_digest[MD5_DIGEST_LENGTH] = '\0';
    dst_digest[MD5_DIGEST_LENGTH] = '\0';

    ret = md5_checksum(src, src_digest);
    if (ret) {
        fprintf(stderr, "failed to calculate checksum for %s (%s)\n",
                        src, strerror(ret));
        return ret;
    }

    ret = md5_checksum(dst, dst_digest);
    if (ret) {
        fprintf(stderr, "failed to calculate checksum for %s (%s)\n",
                        dst, strerror(ret));
        return ret;
    }

    if (verbose) {
        printf("[%d] src: %s, dst: %s\n", rank,
               checksum_str(md5src, src_digest),
               checksum_str(md5dst, dst_digest));
    }

    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        if (src_digest[i] != dst_digest[i]) {
            fprintf(stderr, "[%d] checksum verification failed: "
                            "(src=%s, dst=%s)\n", rank,
                            checksum_str(md5src, src_digest),
                            checksum_str(md5dst, dst_digest));
            ret = EIO;
        }
    }

    return ret;
}

/*
 * Parse a line from the manifest in the form of:
 *
 * <src path> <whitespace separator> <dest path>
 *
 * If the paths have spaces, they must be quoted.
 *
 * On success, return 0 along with allocated src and dest strings.  These
 * must be freed when you're finished with them.  On failure return non-zero,
 * and set src and dest to NULL.
 *
 * Note, leading and tailing whitespace are ok.  They just get ignored.
 * Lines with only whitespace are ignored.  A line of all whitespace will
 * return 0, with src and dest being NULL, so users should not check for
 * 'if (*src == NULL)' to see if the function failed.  They should be looking
 * at the return code.
 */
/**
 * @brief parses manifest file line, passes back src and dst strings
 *
 * @param line    input manifest file line
 * @param src     return val of src filename
 * @param dst     return val of dst filename
 *
 * @return 0 if all was well, or there was nothing; non-zero on error
 */
int
unifyfs_parse_manifest_line(char* line, char** src, char** dest)
{
    char* new_src = NULL;
    char* new_dest = NULL;
    char* copy;
    char* tmp;
    unsigned long copy_len;
    int i;
    unsigned int tmp_count;
    int in_quotes = 0;
    int rc = 0;

    copy = strdup(line);
    copy_len = strlen(copy) + 1;/* +1 for '\0' */

    /* Replace quotes and separator with '\0' */
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

    /*
     * copy[] now contains a series of strings, one after the other
     * (possibly containing some NULL strings, which we ignore)
     */
    tmp = copy;
    while (tmp < copy + copy_len) {
        tmp_count = strlen(tmp);
        if (tmp_count > 0) {
            /* We have a real string */
            if (!new_src) {
                new_src = strdup(tmp);
            } else {
                if (!new_dest) {
                    new_dest = strdup(tmp);
                } else {
                    /* Error: a third file name */
                    rc = 1;
                    break;
                }
            }
        }
        tmp += tmp_count + 1;
    }

    /* Some kind of error parsing a line */
    if (rc != 0 || (new_src && !new_dest)) {
        fprintf(stderr, "manifest file line >>%s<< is invalid!\n",
                line);
        free(new_src);
        free(new_dest);
        new_src = NULL;
        new_dest = NULL;
        if (rc == 0) {
            rc = 1;
        }
    }

    *src = new_src;
    *dest = new_dest;

    free(copy);
    return rc;
}

/**
 * @brief controls the action of the stage-in or stage-out.  Opens up
 *        the manifest file, sends each line to be parsed, and fires
 *        each source/destination to be staged.
 *
 * @param ctx     stage context and instructions
 *
 * @return 0 indicates success, non-zero is error
 */
int unifyfs_stage_transfer(unifyfs_stage_t* ctx)
{
    int ret = 0;
    int count = 0;
    FILE* fp = NULL;
    char* src = NULL;
    char* dst = NULL;
    char linebuf[LINE_MAX] = { 0, };
    struct stat sb = { 0, };

    if (!ctx) {
        return EINVAL;
    }

    fp = fopen(ctx->manifest_file, "r");
    if (!fp) {
        fprintf(stderr, "failed to open file %s: %s\n",
		ctx->manifest_file, strerror(errno));
        ret = errno;
        goto out;
    }

    while (NULL != fgets(linebuf, LINE_MAX-1, fp)) {
        if (strlen(linebuf) < 5) {
	    // the manifest file perhaps ends with a couple of characters
	    // and/or a newline not meant to be a transfer spec.
            if (linebuf[0] == '\n') {
	        goto out;
            } else{
	        fprintf(stderr, "Short (bad) manifest file line: >%s<\n",
			linebuf);
		ret = -EINVAL;
                goto out;
            }
	}
	ret = unifyfs_parse_manifest_line(linebuf, &src, &dst);
	if (ret < 0) {
	    fprintf(stderr, "failed to parse %s (%s)\n",
		    linebuf, strerror(ret));
            goto out;
	}
        if (ctx->mode == UNIFYFS_STAGE_SERIAL) {
            if (count % total_ranks == rank) {
                if (verbose) {
                    fprintf(stdout, "[%d] serial transfer: src=%s, dst=%s\n",
                                    rank, src, dst);
                }

                ret = unifyfs_transfer_file_serial(src, dst);
                if (ret) {
                    goto out;
                }

                if (ret < 0) {
                    fprintf(stderr, "stat on %s failed (err=%d, %s)\n",
                                    dst, errno, strerror(errno));
                    ret = errno;
                    goto out;
                }

                if (ctx->checksum) {
                    ret = verify_checksum(src, dst);
                    if (ret) {
                        fprintf(stderr, "checksums for >%s< and >%s< differ!\n",
                                src, dst);
                        goto out;
                    }
                }
            }
        } else {
            if (0 == rank) {
                int fd = -1;

                if (verbose) {
                    fprintf(stdout, "[%d] parallel transfer: src=%s, dst=%s\n",
                                    rank, src, dst);
                }

                /* FIXME: Since we cannot use the mpi barrier inside the
                 * unifyfs_transfer_file_parallel(), we need to create the file
                 * here before others start to access. It would be better if we
                 * can skip this step.
                 */
                fd = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0600);
                if (fd < 0) {
                    fprintf(stderr, "[%d] failed to create the file %s\n",
                                    rank, dst);
                    goto out;
                }

                close(fd);
            }

            MPI_Barrier(MPI_COMM_WORLD);

            ret = unifyfs_transfer_file_parallel(src, dst);
            if (ret) {
                goto out;
            }

            MPI_Barrier(MPI_COMM_WORLD);

            ret = stat(dst, &sb);
            if (ret < 0) {
                fprintf(stderr, "stat on %s failed (err=%d, %s)\n",
                                dst, errno, strerror(errno));
                ret = errno;
                goto out;
            }

            if (ctx->checksum && 0 == rank) {
                ret = verify_checksum(src, dst);
                if (ret) {
                    goto out;
                }
            }
        }

        count++;
    }
out:
    if (ret) {
        fprintf(stderr, "failed to transfer file (src=%s, dst=%s): %s\n",
                        src, dst, strerror(ret));
    }

    if (fp) {
        fclose(fp);
        fp = NULL;
    }

    return ret;
}

