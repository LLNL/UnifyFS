/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
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

/*
 * Re-implementation of BSD's strlcpy() function.
 *
 * This is a basically a safer version of strlncpy() since it always
 * NULL-terminates the buffer.  Google 'strlcpy' for full documentation.
 */
size_t strlcpy(char* dest, const char* src, size_t size)
{
    size_t src_len = strnlen(src, size);

    memcpy(dest, src, src_len);
    if (src_len == size) {
        dest[size - 1] = '\0';
    }
    return strlen(dest);
}

/*
 * This is a re-implementation of the Linux kernel's scnprintf() function.
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
