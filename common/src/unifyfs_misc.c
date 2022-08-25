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

/*
 * This file contains miscellaneous common functions that don't fit into a
 * particular common/src/ file.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "unifyfs_misc.h"

/*
 * Implementation of BSD's strlcpy() function.
 *
 * This is a basically a safer version of strlncpy() since it always
 * NULL-terminates the buffer.  Google 'strlcpy' for full documentation.
 */
size_t strlcpy(char* dest, const char* src, size_t size)
{
    size_t src_len;

    src_len = strnlen(src, size);
    if (src_len == size) {
            /* Our string is too long, have to truncate */
            src_len = size - 1;
    }

    memcpy(dest, src, src_len);
    dest[src_len] = '\0';

    return strlen(dest);
}

/*
 * Implementation of the Linux kernel's scnprintf() function.
 *
 * It's snprintf() but returns the number of chars actually written into buf[]
 * not including the '\0'.  It also avoids the -Wformat-truncation warnings.
 */
int scnprintf(char* buf, size_t size, const char* fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vsnprintf(buf, size, fmt, args);
    va_end(args);

    if (rc >= size) {
        /* We truncated */
        return size - 1;
    }

    return rc;
}


/* Calculate timestamp difference in seconds */
double timediff_sec(struct timeval* before, struct timeval* after)
{
    double diff;
    if (!before || !after) {
        return -1.0F;
    }
    diff = (double)(after->tv_sec - before->tv_sec);
    diff += 0.000001 * ((double)(after->tv_usec) - (double)(before->tv_usec));
    return diff;
}
