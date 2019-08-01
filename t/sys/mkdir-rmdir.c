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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

/* This function contains the tests for UNIFYFS_WRAP(mkdir) and
 * UNIFYFS_WRAP(rmdir) found in client/src/unifyfs-sysio.c.
 *
 * Notice the tests are ordered in a logical testing order. Changing the order
 * or adding new tests in between two others could negatively affect the
 * desired results. */
int mkdir_rmdir_test(char* unifyfs_root)
{
    /* Diagnostic message for reading and debugging output */
    diag("Starting UNIFYFS_WRAP(mkdir/rmdir) tests");

    char dir_path[64];
    char file_path[64];
    char subdir_path[80];
    char subfile_path[80];
    char file_subdir_path[80];
    int file_mode = 0600;
    int dir_mode = 0700;
    int fd;
    int rc;

    /* Create random dir and file path names at the mountpoint to test on */
    testutil_rand_path(dir_path, sizeof(dir_path), unifyfs_root);
    testutil_rand_path(file_path, sizeof(file_path), unifyfs_root);

    /* Create path of a subdirectory under dir_path */
    strcpy(subdir_path, dir_path);
    strcat(subdir_path, "/subdir");

    /* Create path of a file  under dir_path */
    strcpy(subfile_path, dir_path);
    strcat(subfile_path, "/subfile");

    /* Create path of a directory under file_path */
    strcpy(file_subdir_path, file_path);
    strcat(file_subdir_path, "/file_subdir");

    /* ***NOTE***
     * Our mkdir wrapper currently puts an entry into the filelist for the
     * requested directory (assuming it does not exist).
     * It doesn't check to see if the parent directory exists. */

    /* todo_mkdir_1: Remove when issue is resolved */
    todo("mkdir_1: we currently don't support directory structure");
    /* Verify we cannot create a dir whose parent dir doesn't exist with
     * errno=ENOENT */
    errno = 0;
    rc = mkdir(subdir_path, dir_mode);
    ok(rc < 0 && errno == ENOENT,
       "mkdir dir %s without parent dir should fail (rc=%d, errno=%d): %s",
       subdir_path, rc, errno, strerror(errno));

    end_todo; /* end todo_mkdir_1 */

    /* Verify rmdir a non-existing directory fails with errno=ENOENT */
    errno = 0;
    rc = rmdir(dir_path);
    ok(rc < 0 && errno == ENOENT,
       "rmdir non-existing dir %s should fail (rc=%d, errno=%d): %s",
       dir_path, rc, errno, strerror(errno));

    /* Verify we can create a non-existent directory. */
    errno = 0;
    rc = mkdir(dir_path, dir_mode);
    ok(rc == 0, "mkdir non-existing dir %s (rc=%d): %s",
       dir_path, rc, strerror(errno));

    /* Verify we cannot recreate an already created directory with
     * errno=EEXIST */
    errno = 0;
    rc = mkdir(dir_path, dir_mode);
    ok(rc < 0 && errno == EEXIST,
       "mkdir existing dir %s should fail (rc=%d, errno=%d): %s",
       dir_path, rc, errno, strerror(errno));

    /* todo_mkdir_2: Remove when issue is resolved */
    todo("mkdir_2: should fail with errno=EISDIR=21");
    /* Verify we cannot create a file with same name as a directory with
     * errno=EISDIR */
    errno = 0;
    fd = creat(dir_path, file_mode);
    ok(fd < 0 && errno == EISDIR,
       "creat file with same name as dir %s should fail (fd=%d, errno=%d): %s",
       dir_path, fd, errno, strerror(errno));
    end_todo; /* end todo_mkdir_2 */

    /* todo_mkdir_3: Remove when issue is resolved */
    todo("mkdir_3: this fails because \"TODO mkdir_1\" is failing");
    /* Verify we can create a subdirectory under an existing directory  */
    errno = 0;
    rc = mkdir(subdir_path, dir_mode);
    ok(rc == 0, "mkdir subdirectory %s in existing dir (rc=%d): %s",
       subdir_path, rc, strerror(errno));
    end_todo; /* end todo_mkdir_3 */

    /* Verify we can create a subfile under an existing directory */
    errno = 0;
    fd = creat(subfile_path, file_mode);
    ok(fd > 0, "creat subfile %s in existing dir (fd=%d): %s",
       subfile_path, fd, strerror(errno));

    rc = close(fd);

    /* todo_mkdir_4: Remove when issue is resolved */
    todo("mkdir_4: unifyfs currently creates all paths as separate entities");
    /* Verify creating a directory whose parent is a file fails with
     * errno=ENOTDIR */
    fd = creat(file_path, file_mode);
    rc = close(fd);

    errno = 0;
    rc = mkdir(file_subdir_path, dir_mode);
    ok(rc < 0 && errno == ENOTDIR,
       "mkdir dir %s whose parent is a file should fail (rc=%d, errno=%d): %s",
       file_subdir_path, rc, errno, strerror(errno));
    end_todo; /* end todo_mkdir_4 */

    /* Verify rmdir a non-directory fails with errno=ENOENT */
    errno = 0;
    rc = rmdir(file_path);
    ok(rc < 0 && errno == ENOTDIR,
       "rmdir non-directory %s should fail (rc=%d, errno=%d): %s",
       file_path, rc, errno, strerror(errno));

    /* todo_mkdir_5: Remove when issue is resolved */
    todo("mkdir_5: unifyfs currently creates all paths as separate entities");
    /* Verify rmdir a non-empty directory fails with errno=ENOTEMPTY */
    errno = 0;
    rc = rmdir(dir_path);
    ok(rc < 0 && errno == ENOTEMPTY,
       "rmdir non-empty directory %s should fail (rc=%d, errno=%d): %s",
       dir_path, rc, errno, strerror(errno));
    end_todo; /* end todo_mkdir_5 */

    /* Verify we can rmdir an empty directory */
    errno = 0;
    rc = rmdir(subdir_path);
    ok(rc == 0, "rmdir an empty directory %s (rc=%d): %s",
       subdir_path, rc, strerror(errno));

    /* Verify rmdir an already removed directory fails with errno=ENOENT */
    errno = 0;
    rc = rmdir(subdir_path);
    ok(rc < 0 && errno == ENOENT,
       "rmdir already removed dir %s should fail (rc=%d, errno=%d): %s",
       subdir_path, rc, errno, strerror(errno));

    /* Verify trying to rmdir the mount point fails with errno=EBUSY */
    errno = 0;
    rc = rmdir(unifyfs_root);
    ok(rc < 0 && errno == EBUSY,
       "rmdir mount point %s should fail (rc=%d, errno=%d): %s",
       unifyfs_root, rc, errno, strerror(errno));

    /* CLEANUP
     *
     * Don't rmdir dir_path in the end as test 9020-mountpoint-empty checks if
     * if anything actually ended up in the mountpoint, meaning a function
     * wasn't wrapped properly. */

    diag("Finished UNIFYFS_WRAP(mkdir/rmdir) tests");

    return 0;
}
