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
#ifndef __UNIFYFS_DIROPS_H
#define __UNIFYFS_DIROPS_H

#include <config.h>

#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>

/*
 * FIXME: is this portable to use the linux dirent structure?
 */

/*
 * standard clib functions to be wrapped:
 *
 * opendir(3)
 * fdopendir(3)
 * closedir(3)
 * readdir(3)
 * rewinddir(3)
 * dirfd(3)
 * telldir(3)
 * scandir(3)
 * seekdir(3)
 */

UNIFYFS_DECL(opendir, DIR*, (const char* name));
UNIFYFS_DECL(fdopendir, DIR*, (int fd));
UNIFYFS_DECL(closedir, int, (DIR* dirp));
UNIFYFS_DECL(readdir, struct dirent*, (DIR* dirp));
UNIFYFS_DECL(rewinddir, void, (DIR* dirp));
UNIFYFS_DECL(dirfd, int, (DIR* dirp));
UNIFYFS_DECL(telldir, long, (DIR* dirp));
UNIFYFS_DECL(scandir, int, (const char* dirp, struct dirent** namelist,
                            int (*filter)(const struct dirent*),
                            int (*compar)(const struct dirent**,
                                    const struct dirent**)));
UNIFYFS_DECL(seekdir, void, (DIR* dirp, long loc));

#endif /* __UNIFYFS_DIROPS_H */

