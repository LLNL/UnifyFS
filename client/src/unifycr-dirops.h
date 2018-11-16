/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyCR.
 * For details, see https://github.com/LLNL/UnifyCR.
 * Please read https://github.com/LLNL/UnifyCR/LICENSE for full license text.
 */
#ifndef __UNIFYCR_DIROPS_H
#define __UNIFYCR_DIROPS_H

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

UNIFYCR_DECL(opendir, DIR *, (const char *name));
UNIFYCR_DECL(fdopendir, DIR *, (int fd));
UNIFYCR_DECL(closedir, int, (DIR *dirp));
UNIFYCR_DECL(readdir, struct dirent *, (DIR *dirp));
UNIFYCR_DECL(rewinddir, void, (DIR *dirp));
UNIFYCR_DECL(dirfd, int, (DIR *dirp));
UNIFYCR_DECL(telldir, long, (DIR *dirp));
UNIFYCR_DECL(scandir, int, (const char *dirp, struct dirent **namelist,
                            int (*filter)(const struct dirent *),
                            int (*compar)(const struct dirent **,
                                          const struct dirent **)));
UNIFYCR_DECL(seekdir, void, (DIR *dirp, long loc));

#endif /* __UNIFYCR_DIROPS_H */

