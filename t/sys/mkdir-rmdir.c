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
    int err, fd, rc;

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
    /* Verify creating a dir under non-existent parent dir fails with
     * errno=ENOENT */
    errno = 0;
    rc = mkdir(subdir_path, dir_mode);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d mkdir dir %s without parent dir should fail (errno=%d): %s",
       __FILE__, __LINE__, subdir_path, err, strerror(err));
    end_todo; /* end todo_mkdir_1 */

    /* Verify rmdir a non-existing directory fails with errno=ENOENT */
    errno = 0;
    rc = rmdir(dir_path);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d rmdir non-existing dir %s should fail (errno=%d): %s",
       __FILE__, __LINE__, dir_path, err, strerror(err));

    /* Verify we can create a non-existent directory. */
    errno = 0;
    rc = mkdir(dir_path, dir_mode);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d mkdir non-existing dir %s: %s",
       __FILE__, __LINE__, dir_path, strerror(err));

    /* Verify recreating an already created directory fails with errno=EEXIST */
    errno = 0;
    rc = mkdir(dir_path, dir_mode);
    err = errno;
    ok(rc == -1 && err == EEXIST,
       "%s:%d mkdir existing dir %s should fail (errno=%d): %s",
       __FILE__, __LINE__, dir_path, err, strerror(err));

    /* Verify creating a file with same name as a dir fails with errno=EISDIR */
    errno = 0;
    fd = creat(dir_path, file_mode);
    err = errno;
    ok(fd == -1 && err == EISDIR,
       "%s:%d creat file with same name as dir %s fails (fd=%d, errno=%d): %s",
       __FILE__, __LINE__, dir_path, fd, err, strerror(err));

    /* todo_mkdir_3: Remove when issue is resolved */
    todo("mkdir_3: this fails because \"TODO mkdir_1\" is failing");
    /* Verify we can create a subdirectory under an existing directory  */
    errno = 0;
    rc = mkdir(subdir_path, dir_mode);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d mkdir subdirectory %s in existing dir: %s",
       __FILE__, __LINE__, subdir_path, strerror(err));
    end_todo; /* end todo_mkdir_3 */

    /* Verify we can create a subfile under an existing directory */
    errno = 0;
    fd = creat(subfile_path, file_mode);
    err = errno;
    ok(fd >= 0 && err == 0,
       "%s:%d creat subfile %s in existing dir (fd=%d): %s",
       __FILE__, __LINE__, subfile_path, fd, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* Verify creating a directory whose parent is a file fails with
     * errno=ENOTDIR */
    errno = 0;
    fd = creat(file_path, file_mode);
    err = errno;
    ok(fd >= 0 && err == 0,
       "%s:%d creat parent file %s (fd=%d): %s",
       __FILE__, __LINE__, file_path, fd, strerror(err));

    errno = 0;
    rc = close(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(err));

    /* todo_mkdir_4: Remove when issue is resolved */
    todo("mkdir_4: unifyfs currently creates all paths as separate entities");
    errno = 0;
    rc = mkdir(file_subdir_path, dir_mode);
    err = errno;
    ok(rc == -1 && err == ENOTDIR,
       "%s:%d mkdir dir %s where parent is a file should fail (errno=%d): %s",
       __FILE__, __LINE__, file_subdir_path, err, strerror(err));
    end_todo; /* end todo_mkdir_4 */

    /* Verify rmdir on non-directory fails with errno=ENOTDIR */
    errno = 0;
    rc = rmdir(file_path);
    err = errno;
    ok(rc == -1 && err == ENOTDIR,
       "%s:%d rmdir non-directory %s should fail (errno=%d): %s",
       __FILE__, __LINE__, file_path, err, strerror(err));

    /* todo_mkdir_5: Remove when issue is resolved */
    todo("mkdir_5: unifyfs currently creates all paths as separate entities");
    /* Verify rmdir a non-empty directory fails with errno=ENOTEMPTY */
    errno = 0;
    rc = rmdir(dir_path);
    err = errno;
    ok(rc == -1 && err == ENOTEMPTY,
       "%s:%d rmdir non-empty directory %s should fail (errno=%d): %s",
       __FILE__, __LINE__, dir_path, err, strerror(err));
    end_todo; /* end todo_mkdir_5 */

    /* Verify we can rmdir an empty directory */
    errno = 0;
    rc = rmdir(subdir_path);
    err = errno;
    ok(rc == 0 && err == 0,
       "%s:%d rmdir an empty directory %s: %s",
       __FILE__, __LINE__, subdir_path, strerror(err));

    /* Verify rmdir an already removed directory fails with errno=ENOENT */
    errno = 0;
    rc = rmdir(subdir_path);
    err = errno;
    ok(rc == -1 && err == ENOENT,
       "%s:%d rmdir already removed dir %s should fail (errno=%d): %s",
       __FILE__, __LINE__, subdir_path, err, strerror(err));

    /* Verify trying to rmdir the mount point fails with errno=EBUSY */
    errno = 0;
    rc = rmdir(unifyfs_root);
    err = errno;
    ok(rc == -1 && err == EBUSY,
       "%s:%d rmdir mount point %s should fail (errno=%d): %s",
       __FILE__, __LINE__, unifyfs_root, err, strerror(err));

    /* CLEANUP
     *
     * Don't rmdir dir_path in the end as test 9020-mountpoint-empty checks if
     * if anything actually ended up in the mountpoint, meaning a function
     * wasn't wrapped properly. */

    diag("Finished UNIFYFS_WRAP(mkdir/rmdir) tests");

    return 0;
}
