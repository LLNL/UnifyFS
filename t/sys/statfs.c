/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2018, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* for statfs */
#include <sys/vfs.h>

#include "unifyfs.h"

/* This function contains the tests for UNIFYFS_WRAP(statfs) found in
 * client/src/unifyfs-sysio.c. */
int statfs_test(char* unifyfs_root, int expect_unifyfs_magic)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYFS_WRAP(statfs) tests");

    /* We call statfs on the mountpoint. */
    const char* path = unifyfs_root;

    /* Call statfs and check that we get a 0 return code indicating success. */
    struct statfs fs;
    errno = 0;
    int rc = statfs(path, &fs);
    int err = errno;
    ok(rc == 0,
       "statfs %s (rc=%d, errno=%d): %s",
       path, rc, err, strerror(err));

    /* If statfs succeeded, check that we get the proper magic value back. */
    if (rc == 0) {
        long int fs_magic = fs.f_type;
        if (expect_unifyfs_magic) {
            /* In this case, we expect to get UNIFYFS_SUPER_MAGIC. */
            long int unifyfs_magic = UNIFYFS_SUPER_MAGIC;
            ok(fs_magic == unifyfs_magic,
               "checking statfs.f_type %lx against UNIFYFS expected value %lx",
               fs_magic, unifyfs_magic);
        } else {
            /* In this case, we expect to get TMPFS_MAGIC, see "man statfs". */
            long int tmpfs_magic = 0x01021994;
            ok(fs_magic == tmpfs_magic,
               "checking statfs.f_type %lx against TMPFS expected value %lx",
               fs_magic, tmpfs_magic);
        }
    }

    diag("Finished UNIFYFS_WRAP(statfs) tests");

    return 0;
}
