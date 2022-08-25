/*
 * Copyright (c) 2022, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2022, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef UNIFYFS_MISC_H
#define UNIFYFS_MISC_H

#include <sys/time.h>

/* Implementation of BSD's strlcpy() function. */
size_t strlcpy(char* dest, const char* src, size_t size);

/* Version of snprintf() that returns the actual number of characters
 * written into the buffer, not including the trailing NUL character */
int scnprintf(char* buf, size_t size, const char* fmt, ...);

/* Calculate timestamp difference in seconds */
double timediff_sec(struct timeval* before, struct timeval* after);


#endif
