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

    errno = 0;

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
    ok(mkdir(subdir_path, dir_mode) == -1 && errno == ENOENT,
       "%s:%d mkdir dir %s without parent dir should fail (errno=%d): %s",
       __FILE__, __LINE__, subdir_path, errno, strerror(errno));
    end_todo; /* end todo_mkdir_1 */
    errno = 0; /* Reset errno after test for failure */

    /* Verify rmdir a non-existing directory fails with errno=ENOENT */
    ok(rmdir(dir_path) == -1 && errno == ENOENT,
       "%s:%d rmdir non-existing dir %s should fail (errno=%d): %s",
       __FILE__, __LINE__, dir_path, errno, strerror(errno));
    errno = 0;

    /* Verify we can create a non-existent directory. */
    ok(mkdir(dir_path, dir_mode) == 0, "%s:%d mkdir non-existing dir %s: %s",
       __FILE__, __LINE__, dir_path, strerror(errno));

    /* Verify recreating an already created directory fails with errno=EEXIST */
    ok(mkdir(dir_path, dir_mode) == -1 && errno == EEXIST,
       "%s:%d mkdir existing dir %s should fail (errno=%d): %s",
       __FILE__, __LINE__, dir_path, errno, strerror(errno));
    errno = 0;

    /* Verify creating a file with same name as a dir fails with errno=EISDIR */
    fd = creat(dir_path, file_mode);
    ok(fd == -1 && errno == EISDIR,
       "%s:%d creat file with same name as dir %s fails (fd=%d, errno=%d): %s",
       __FILE__, __LINE__, dir_path, fd, errno, strerror(errno));
    errno = 0;

    /* todo_mkdir_3: Remove when issue is resolved */
    todo("mkdir_3: this fails because \"TODO mkdir_1\" is failing");
    /* Verify we can create a subdirectory under an existing directory  */
    ok(mkdir(subdir_path, dir_mode) == 0,
       "%s:%d mkdir subdirectory %s in existing dir: %s",
       __FILE__, __LINE__, subdir_path, strerror(errno));
    end_todo; /* end todo_mkdir_3 */
    errno = 0; /* can remove when test is passing */

    /* Verify we can create a subfile under an existing directory */
    fd = creat(subfile_path, file_mode);
    ok(fd >= 0, "%s:%d creat subfile %s in existing dir (fd=%d): %s",
       __FILE__, __LINE__, subfile_path, fd, strerror(errno));

    ok(close(fd) == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(errno));

    /* Verify creating a directory whose parent is a file fails with
     * errno=ENOTDIR */
    fd = creat(file_path, file_mode);
    ok(fd >= 0, "%s:%d creat parent file %s (fd=%d): %s",
       __FILE__, __LINE__, file_path, fd, strerror(errno));
    ok(close(fd) == 0, "%s:%d close() worked: %s",
       __FILE__, __LINE__, strerror(errno));

    /* todo_mkdir_4: Remove when issue is resolved */
    todo("mkdir_4: unifyfs currently creates all paths as separate entities");
    ok(mkdir(file_subdir_path, dir_mode) == -1 && errno == ENOTDIR,
       "%s:%d mkdir dir %s where parent is a file should fail (errno=%d): %s",
       __FILE__, __LINE__, file_subdir_path, errno, strerror(errno));
    end_todo; /* end todo_mkdir_4 */
    errno = 0;

    /* Verify rmdir on non-directory fails with errno=ENOTDIR */
    ok(rmdir(file_path) == -1 && errno == ENOTDIR,
       "%s:%d rmdir non-directory %s should fail (errno=%d): %s",
       __FILE__, __LINE__, file_path, errno, strerror(errno));
    errno = 0;

    /* todo_mkdir_5: Remove when issue is resolved */
    todo("mkdir_5: unifyfs currently creates all paths as separate entities");
    /* Verify rmdir a non-empty directory fails with errno=ENOTEMPTY */
    ok(rmdir(dir_path) == -1 && errno == ENOTEMPTY,
       "%s:%d rmdir non-empty directory %s should fail (errno=%d): %s",
       __FILE__, __LINE__, dir_path, errno, strerror(errno));
    end_todo; /* end todo_mkdir_5 */
    errno = 0;

    /* Verify we can rmdir an empty directory */
    ok(rmdir(subdir_path) == 0, "%s:%d rmdir an empty directory %s: %s",
       __FILE__, __LINE__, subdir_path, strerror(errno));

    /* Verify rmdir an already removed directory fails with errno=ENOENT */
    ok(rmdir(subdir_path) == -1 && errno == ENOENT,
       "%s:%d rmdir already removed dir %s should fail (errno=%d): %s",
       __FILE__, __LINE__, subdir_path, errno, strerror(errno));
    errno = 0;

    /* Verify trying to rmdir the mount point fails with errno=EBUSY */
    ok(rmdir(unifyfs_root) == -1 && errno == EBUSY,
       "%s:%d rmdir mount point %s should fail (errno=%d): %s",
       __FILE__, __LINE__, unifyfs_root, errno, strerror(errno));
    errno = 0;

    /* CLEANUP
     *
     * Don't rmdir dir_path in the end as test 9020-mountpoint-empty checks if
     * if anything actually ended up in the mountpoint, meaning a function
     * wasn't wrapped properly. */

    diag("Finished UNIFYFS_WRAP(mkdir/rmdir) tests");

    return 0;
}
