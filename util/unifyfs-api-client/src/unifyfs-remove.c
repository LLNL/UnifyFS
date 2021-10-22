/*
 * Copyright (c) 2021, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2021, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <stdio.h>
#include "unifyfs_api.h"

static void usage(char* arg0)
{
    fprintf(stderr, "USAGE: %s <mountpoint> <file-path1> [<file-path2> ...]\n",
            arg0);
    fflush(stderr);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "USAGE ERROR: expected two arguments!\n");
        usage(argv[0]);
        return -1;
    }

    char* mountpt = argv[1];

    unifyfs_handle fshdl;
    unifyfs_rc urc = unifyfs_initialize(mountpt, NULL, 0, &fshdl);
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS ERROR: init failed at mountpoint %s - %s\n",
                mountpt, unifyfs_rc_enum_description(urc));
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        char* filepath = argv[i];
        urc = unifyfs_remove(fshdl, filepath);
        if (UNIFYFS_SUCCESS != urc) {
            fprintf(stderr, "UNIFYFS ERROR: failed to remove file %s - %s\n",
                    filepath, unifyfs_rc_enum_description(urc));
            return 2;
        }
    }

    urc = unifyfs_finalize(fshdl);
    if (UNIFYFS_SUCCESS != urc) {
        fprintf(stderr, "UNIFYFS ERROR: failed to finalize - %s\n",
                unifyfs_rc_enum_description(urc));
        return 3;
    }

    return 0;
}
