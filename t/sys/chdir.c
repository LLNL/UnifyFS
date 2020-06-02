/*
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
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

 /*
  * Test chdir/fchdir/getcwd/getwd/get_current_dir_name
  */
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include "t/lib/tap.h"
#include "t/lib/testutil.h"

int chdir_test(char* unifyfs_root)
{
    diag("Starting UNIFYFS_WRAP(chdir/fchdir/getcwd/getwd/"
         "get_current_dir_name) tests");

    char path[64];
    int rc;
    char* str;

    errno = 0;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* define a dir1 subdirectory within unifyfs space */
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%s/dir1", unifyfs_root);
    rc = mkdir(buf, 0700);
    ok(rc == 0 || errno == EEXIST, "%s:%d mkdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(errno));
    errno = 0;

    /* define a dir2 subdirectory within unifyfs space */
    char buf2[64] = {0};
    snprintf(buf2, sizeof(buf2), "%s/dir2", unifyfs_root);
    rc = mkdir(buf2, 0700);
    ok(rc == 0 || errno == EEXIST, "%s:%d mkdir(%s): %s",
       __FILE__, __LINE__, buf2, strerror(errno));
    errno = 0;

    /* change to root directory */
    rc = chdir("/");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(errno));

    /* check that we're in root directory */
    str = getcwd(path, sizeof(path));
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    is(str, "/",
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, "/");

    /* change to unifyfs root directory */
    rc = chdir(unifyfs_root);
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, unifyfs_root, strerror(errno));

    /* check that we're in unifyfs root directory */
    str = getcwd(path, sizeof(path));
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, unifyfs_root) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);

    /* get length of current directory */
    size_t len = strlen(str);

    /* try getcwd with a buffer short by one byte,
     * should fail with ERANGE */
    str = getcwd(path, len); // pass
    ok(str == NULL && errno == ERANGE,
       "%s:%d getcwd(buf, %d): expected NULL got %s errno %s",
       __FILE__, __LINE__, len, str, strerror(errno));
    errno = 0;

    /* try getcwd with a NULL buffer */
    str = getcwd(NULL, sizeof(path));
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    if (str != NULL) {
        free(str);
    }

    /* try getcwd with a NULL buffer but short size,
     * should fail with ERANGE */
    str = getcwd(NULL, len);
    ok(str == NULL && errno == ERANGE, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    if (str != NULL) {
        free(str);
    }
    errno = 0;

    /* try getcwd with a buffer and 0 size, should fail with EINVAL */
    str = getcwd(path, 0);
    ok(str == NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    if (str != NULL) {
        free(str);
    }
    errno = 0;

    /* try getcwd with a NULL buffer and 0 size,
     * getcwd should allocate buffer */
    str = getcwd(NULL, 0);
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs/dir1 */
    rc = chdir(buf);
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(errno));

    /* check that we're in unifyfs/dir1 */
    str = getcwd(path, sizeof(path));
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, buf) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf);

    /* change back to root unifyfs directory */
    rc = chdir("..");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "..", strerror(errno));

    /* check that we're in root unifyfs directory */
    str = getcwd(path, sizeof(path));
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, unifyfs_root) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);

    /* change to unifyfs/dir1 directory using relative path */
    rc = chdir("dir1");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "dir1", strerror(errno));

    /* check that we're in unifyfs/dir1 directory */
    str = getcwd(path, sizeof(path));
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, buf) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf);

    /* change to unifyfs/dir2 directory in strange way */
    rc = chdir("././.././dir2");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "dir1", strerror(errno));

    /* check that we're in unifyfs/dir2 directory */
    str = getcwd(path, sizeof(path));
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, buf2) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf2);


/* TODO: Some compilers throw a warning/error if one uses getwd().
 * For those compilers that allow it, it would be nice to execute
 * these tests.  For now, I'll leave this here as a reminder. */
#if 0
    /* change to root directory */
    rc = chdir("/");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(errno));

    /* check that we're in root directory */
    str = getwd(pathmax);
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    is(str, "/",
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, "/");

    /* change to unifyfs root directory */
    rc = chdir(unifyfs_root);
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, unifyfs_root, strerror(errno));

    /* check that we're in unifyfs root directory */
    str = getwd(pathmax);
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, unifyfs_root) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);

    /* change to directory within unifyfs */
    rc = chdir(buf);
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(errno));

    /* check that we're in directory within unifyfs */
    str = getwd(pathmax);
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, buf) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf);

    /* change back to root unifyfs directory */
    rc = chdir("..");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "..", strerror(errno));

    /* check that we're in root unifyfs directory */
    str = getwd(pathmax);
    ok(str != NULL, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, unifyfs_root) == 0,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);
#endif

    /* change to root directory */
    rc = chdir("/");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(errno));

    /* check that we're in root directory */
    str = get_current_dir_name();
    ok(str != NULL, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(errno));
    is(str, "/",
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, "/");
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs root directory */
    rc = chdir(unifyfs_root);
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, unifyfs_root, strerror(errno));

    /* check that we're in unifyfs root directory */
    str = get_current_dir_name();
    ok(str != NULL, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, unifyfs_root) == 0,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs/dir1 */
    rc = chdir(buf);
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(errno));

    /* check that we're in unifyfs/dir1 */
    str = get_current_dir_name();
    ok(str != NULL, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, buf) == 0,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, buf);
    if (str != NULL) {
        free(str);
    }

    /* change back to root unifyfs directory */
    rc = chdir("..");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "..", strerror(errno));

    /* check that we're in root unifyfs directory */
    str = get_current_dir_name();
    ok(str != NULL, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, unifyfs_root) == 0,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs/dir2 directory */
    rc = chdir("dir2");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "dir2", strerror(errno));

    /* check that we're in unifyfs/dir2 */
    str = get_current_dir_name();
    ok(str != NULL, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(errno));
    ok(str != NULL && strcmp(str, buf2) == 0,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, buf2);
    if (str != NULL) {
        free(str);
    }


/* TODO: Our directory wrappers are not fully functioning yet,
 * but when they do, we should check that fchdir works. */
#if 0
    /* change to root directory */
    rc = chdir("/");
    ok(rc == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(errno));

    /* open a directory in unifyfs */
    DIR* dirp = opendir(buf);
    ok(dirp != NULL, "%s:%d opendir(%s): %s",
       __FILE__, __LINE__, buf, strerror(errno));

    int fd = dirfd(dirp);
    ok(fd >= 0, "%s:%d dirfd(%p): %s",
       __FILE__, __LINE__, dirp, strerror(errno));

    /* use fchdir to change into it */
    rc = fchdir(fd);
    ok(rc == 0, "%s:%d fchdir(%d): %s",
       __FILE__, __LINE__, fd, strerror(errno));

    closedir(dirp);

    /* open root directory */
    dirp = opendir("/");
    ok(dirp != NULL, "%s:%d opendir(%s): %s",
       __FILE__, __LINE__, "/", strerror(errno));

    fd = dirfd(dirp);
    ok(fd >= 0, "%s:%d dirfd(%p): %s",
       __FILE__, __LINE__, dirp, strerror(errno));

    /* use fchdir to change into it */
    rc = fchdir(fd);
    ok(rc == 0, "%s:%d fchdir(%d): %s",
       __FILE__, __LINE__, fd, strerror(errno));

    closedir(dirp);
#endif

    return 0;
}
