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

#include "unifyfs-shell.h"

static char path[PATH_MAX];

static void dump_stat(const struct stat* sb, FILE* out)
{
    fprintf(out, "File: %s\n", path);
    fprintf(out, "File type:                ");

    switch (sb->st_mode & S_IFMT) {
    case S_IFREG:
        fprintf(out, "regular file\n");
        break;
    case S_IFDIR:
        fprintf(out, "directory\n");
        break;
    case S_IFCHR:
        fprintf(out, "character device\n");
        break;
    case S_IFBLK:
        fprintf(out, "block device\n");
        break;
    case S_IFLNK:
        fprintf(out, "symbolic (soft) link\n");
        break;
    case S_IFIFO:
        fprintf(out, "FIFO or pipe\n");
        break;
    case S_IFSOCK:
        fprintf(out, "socket\n");
        break;
    default:
        fprintf(out, "unknown file type?\n");
        break;
    }

    fprintf(out, "Device containing i-node: major=%ld   minor=%ld\n",
           (long) major(sb->st_dev), (long) minor(sb->st_dev));

    fprintf(out, "I-node number:            %ld\n", (long) sb->st_ino);

    fprintf(out, "Mode:                     %lo\n",
           (unsigned long) sb->st_mode);

    if (sb->st_mode & (S_ISUID | S_ISGID | S_ISVTX)) {
        fprintf(out, "    special bits set:     %s%s%s\n",
               (sb->st_mode & S_ISUID) ? "set-UID " : "",
               (sb->st_mode & S_ISGID) ? "set-GID " : "",
               (sb->st_mode & S_ISVTX) ? "sticky " : "");
    }

    fprintf(out, "Number of (hard) links:   %ld\n", (long) sb->st_nlink);

    fprintf(out, "Ownership:                UID=%ld   GID=%ld\n",
           (long) sb->st_uid, (long) sb->st_gid);

    if (S_ISCHR(sb->st_mode) || S_ISBLK(sb->st_mode)) {
        fprintf(out, "Device number (st_rdev):  major=%ld; minor=%ld\n",
               (long) major(sb->st_rdev), (long) minor(sb->st_rdev));
    }

    fprintf(out, "File size:                %lld bytes\n",
                 (long long) sb->st_size);
    fprintf(out, "Optimal I/O block size:   %ld bytes\n",
                 (long) sb->st_blksize);
    fprintf(out, "512B blocks allocated:    %lld\n",
                 (long long) sb->st_blocks);

    fprintf(out, "Last file access:         %s", ctime(&sb->st_atime));
    fprintf(out, "Last file modification:   %s", ctime(&sb->st_mtime));
    fprintf(out, "Last status change:       %s\n", ctime(&sb->st_ctime));
}

static int stat_main(int argc, char** argv, unifyfs_shell_env_t* e)
{
    int ret = 0;
    struct stat sb = { 0, };
    FILE* out = e->output;

    if (argc != 2) {
        fprintf(out, "Usage: stat FILE\n");
        return -1;
    }

    ret = unifyfs_shell_get_absolute_path(e, argv[1], path);
    if (ret < 0) {
        goto fin;
    }

    ret = stat(path, &sb);
    if (ret < 0) {
        fprintf(out, "stat failed on %s: %s\n", path, strerror(errno));
    } else {
        dump_stat(&sb, e->output);
    }

fin:
    return ret;
}

unifyfs_shell_cmd_t unifyfs_shell_cmd_stat = {
    .name = "stat",
    .func = stat_main,
};

