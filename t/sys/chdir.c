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

#include "config.h" // for USE_SPATH

int chdir_test(char* unifyfs_root)
{
    diag("Starting UNIFYFS_WRAP(chdir/fchdir/getcwd/getwd/"
         "get_current_dir_name) tests");

    char path[64];
    int err, rc;
    char* str;

    testutil_rand_path(path, sizeof(path), unifyfs_root);

    /* define a dir1 subdirectory within unifyfs space */
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%s/dir1", unifyfs_root);
    errno = 0;
    rc = mkdir(buf, 0700);
    err = errno;
    ok(rc == 0 || err == EEXIST, "%s:%d mkdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(err));

    /* define a dir2 subdirectory within unifyfs space */
    char buf2[64] = {0};
    snprintf(buf2, sizeof(buf2), "%s/dir2", unifyfs_root);
    errno = 0;
    rc = mkdir(buf2, 0700);
    err = errno;
    ok(rc == 0 || err == EEXIST, "%s:%d mkdir(%s): %s",
       __FILE__, __LINE__, buf2, strerror(err));

    /* change to root directory */
    errno = 0;
    rc = chdir("/");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(err));

    /* check that we're in root directory */
    errno = 0;
    str = getcwd(path, sizeof(path));
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, "/",
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, "/");

    /* change to unifyfs root directory */
    errno = 0;
    rc = chdir(unifyfs_root);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, unifyfs_root, strerror(err));

    /* check that we're in unifyfs root directory */
    errno = 0;
    str = getcwd(path, sizeof(path));
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, unifyfs_root,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);

    /* get length of current directory */
    size_t len = strlen(str);

    /* try getcwd with a buffer short by one byte,
     * should fail with ERANGE */
    errno = 0;
    str = getcwd(path, len); // pass
    err = errno;
    ok(str == NULL && err == ERANGE,
       "%s:%d getcwd(buf, short_len): (errno=%d) %s",
       __FILE__, __LINE__, err, strerror(err));

    /* try getcwd with a NULL buffer (Linux glibc extension to POSIX) */
    errno = 0;
    str = getcwd(NULL, sizeof(path));
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd(NULL, good_len): (errno=%d) %s",
       __FILE__, __LINE__, err, strerror(err));
    if (str != NULL) {
        free(str);
    }

    /* try getcwd with a NULL buffer but short size,
     * should fail with ERANGE */
    errno = 0;
    str = getcwd(NULL, len);
    err = errno;
    ok(str == NULL && err == ERANGE,
       "%s:%d getcwd(NULL, short_len): (errno=%d) %s",
       __FILE__, __LINE__, err, strerror(err));
    if (str != NULL) {
        free(str);
    }

    /* try getcwd with a buffer and 0 size, should fail with EINVAL */
    errno = 0;
    str = getcwd(path, 0);
    err = errno;
    ok(str == NULL && err == EINVAL, "%s:%d getcwd(buf, 0): (errno=%d) %s",
       __FILE__, __LINE__, err, strerror(err));
    if (str != NULL) {
        free(str);
    }

    /* try getcwd with a NULL buffer and 0 size,
     * getcwd should allocate buffer (Linux glibc extension to POSIX) */
    errno = 0;
    str = getcwd(NULL, 0);
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd(NULL, 0): (errno=%d) %s",
       __FILE__, __LINE__, err, strerror(err));
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs/dir1 */
    errno = 0;
    rc = chdir(buf);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(err));

    /* check that we're in unifyfs/dir1 */
    errno = 0;
    str = getcwd(path, sizeof(path));
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, buf,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf);

#ifdef USE_SPATH
    /* change back to root unifyfs directory */
    errno = 0;
    rc = chdir("..");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "..", strerror(err));

    /* check that we're in root unifyfs directory */
    errno = 0;
    str = getcwd(path, sizeof(path));
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, unifyfs_root,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);

    /* change to unifyfs/dir1 directory using relative path */
    errno = 0;
    rc = chdir("dir1");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "dir1", strerror(err));

    /* check that we're in unifyfs/dir1 directory */
    errno = 0;
    str = getcwd(path, sizeof(path));
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, buf,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf);

    /* change to unifyfs/dir2 directory in strange way */
    errno = 0;
    rc = chdir("././.././dir2");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "dir1", strerror(err));

    /* check that we're in unifyfs/dir2 directory */
    errno = 0;
    str = getcwd(path, sizeof(path));
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, buf2,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf2);
#else
    skip(1, 9, "test requires missing spath dependency");
    end_skip;
#endif /* USE_SPATH */

/* TODO: Some compilers throw a warning/error if one uses getwd().
 * For those compilers that allow it, it would be nice to execute
 * these tests.  For now, I'll leave this here as a reminder. */
#if 0
    /* change to root directory */
    errno = 0;
    rc = chdir("/");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(err));

    /* check that we're in root directory */
    errno = 0;
    str = getwd(pathmax);
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, "/",
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, "/");

    /* change to unifyfs root directory */
    errno = 0;
    rc = chdir(unifyfs_root);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, unifyfs_root, strerror(err));

    /* check that we're in unifyfs root directory */
    errno = 0;
    str = getwd(pathmax);
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, unifyfs_root,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);

    /* change to directory within unifyfs */
    errno = 0;
    rc = chdir(buf);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(err));

    /* check that we're in directory within unifyfs */
    errno = 0;
    str = getwd(pathmax);
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, buf,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, buf);

    /* change back to root unifyfs directory */
    errno = 0;
    rc = chdir("..");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "..", strerror(err));

    /* check that we're in root unifyfs directory */
    errno = 0;
    str = getwd(pathmax);
    err = errno;
    ok(str != NULL && err == 0, "%s:%d getcwd: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, unifyfs_root,
       "%s:%d getcwd returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);
#endif

    /* change to root directory */
    errno = 0;
    rc = chdir("/");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(err));

    /* check that we're in root directory */
    errno = 0;
    str = get_current_dir_name();
    err = errno;
    ok(str != NULL && err == 0, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, "/",
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, "/");
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs root directory */
    errno = 0;
    rc = chdir(unifyfs_root);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, unifyfs_root, strerror(err));

    /* check that we're in unifyfs root directory */
    errno = 0;
    str = get_current_dir_name();
    err = errno;
    ok(str != NULL && err == 0, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, unifyfs_root,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs/dir1 */
    errno = 0;
    rc = chdir(buf);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, buf, strerror(err));

    /* check that we're in unifyfs/dir1 */
    errno = 0;
    str = get_current_dir_name();
    err = errno;
    ok(str != NULL && err == 0, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, buf,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, buf);
    if (str != NULL) {
        free(str);
    }

#ifdef USE_SPATH
    /* change back to root unifyfs directory */
    errno = 0;
    rc = chdir("..");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "..", strerror(err));

    /* check that we're in root unifyfs directory */
    errno = 0;
    str = get_current_dir_name();
    err = errno;
    ok(str != NULL && err == 0, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, unifyfs_root,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, unifyfs_root);
    if (str != NULL) {
        free(str);
    }

    /* change to unifyfs/dir2 directory */
    errno = 0;
    rc = chdir("dir2");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "dir2", strerror(err));

    /* check that we're in unifyfs/dir2 */
    errno = 0;
    str = get_current_dir_name();
    err = errno;
    ok(str != NULL && err == 0, "%s:%d get_current_dir_name: %s",
       __FILE__, __LINE__, strerror(err));
    is(str, buf2,
       "%s:%d get_current_dir_name returned %s expected %s",
       __FILE__, __LINE__, str, buf2);
    if (str != NULL) {
        free(str);
    }
#else
    skip(1, 6, "test requires missing spath dependency");
    end_skip;
#endif /* USE_SPATH */


/* TODO: Our directory wrappers are not fully functioning yet,
 * but when they do, we should check that fchdir works. */
    skip(1, 7, "fchdir tests until directory wrappers are fully functional")
    /* change to root directory */
    errno = 0;
    rc = chdir("/");
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d chdir(%s): %s",
       __FILE__, __LINE__, "/", strerror(err));

    /* open a directory in unifyfs */
    errno = 0;
    DIR* dirp = opendir(buf);
    err = errno;
    ok(dirp != NULL && err == 0, "%s:%d opendir(%s): %s",
       __FILE__, __LINE__, buf, strerror(err));

    errno = 0;
    int fd = dirfd(dirp);
    err = errno;
    ok(fd >= 0 && err == 0, "%s:%d dirfd(%p): %s",
       __FILE__, __LINE__, dirp, strerror(err));

    /* use fchdir to change into it */
    errno = 0;
    rc = fchdir(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fchdir(%d): %s",
       __FILE__, __LINE__, fd, strerror(err));

    closedir(dirp);

    /* open root directory */
    errno = 0;
    dirp = opendir("/");
    err = errno;
    ok(dirp != NULL && err == 0, "%s:%d opendir(%s): %s",
       __FILE__, __LINE__, "/", strerror(err));

    errno = 0;
    fd = dirfd(dirp);
    err = errno;
    ok(fd >= 0 && err == 0, "%s:%d dirfd(%p): %s",
       __FILE__, __LINE__, dirp, strerror(err));

    /* use fchdir to change into it */
    errno = 0;
    rc = fchdir(fd);
    err = errno;
    ok(rc == 0 && err == 0, "%s:%d fchdir(%d): %s",
       __FILE__, __LINE__, fd, strerror(err));

    closedir(dirp);
    end_skip;

    return 0;
}
