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
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Copyright (c) 2017, Florida State University. Contributions from
 * the Computer Architecture and Systems Research Laboratory (CASTL)
 * at the Department of Computer Science.
 *
 * Written by: Teng Wang, Adam Moody, Weikuan Yu, Kento Sato, Kathryn Mohror
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

#include "unifyfs_const.h"
#include "unifyfs_log.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

/* one of the loglevel values */
unifyfs_log_level_t unifyfs_log_level = LOG_ERR;

/* enable verbose logging on error */
int unifyfs_log_on_error; // = 0

/* pointer to log file stream */
FILE* unifyfs_log_stream; // = NULL

/* used within LOG macro to build a timestamp */
time_t unifyfs_log_time;
struct tm* unifyfs_log_ltime;
char unifyfs_log_timestamp[256];

/* used to reduce source file pathname length */
int unifyfs_log_source_base_len; // = 0
static const char* this_file = __FILE__;

/* open specified file as log file stream, or stderr if no file given.
 * returns UNIFYFS_SUCCESS on success */
int unifyfs_log_open(const char* file)
{
    if (0 == unifyfs_log_source_base_len) {
        // NOTE: if you change the source location of this file, update string
        char* srcdir = strstr(this_file, "../../common/src/unifyfs_log.c");
        if (NULL == srcdir) {
            srcdir = strstr(this_file, "common/src/unifyfs_log.c");
        }
        if (NULL != srcdir) {
            unifyfs_log_source_base_len = srcdir - this_file;
        }
    }

    if (NULL == unifyfs_log_stream) {
        /* stderr is the default log stream */
        unifyfs_log_stream = stderr;
    }

    if (NULL != file) {
        FILE* logf = fopen(file, "a");
        if (logf == NULL) {
            return ENOENT;
        } else {
            unifyfs_log_stream = logf;
        }
    }

    return UNIFYFS_SUCCESS;
}

static const char* null_func = "?func?";

/* print a message to the log with given time and source context */
void unifyfs_log_print(time_t now,
                       const char* srcfile,
                       int lineno,
                       const char* function,
                       char* msg)
{
    char timestamp[64] = {0};
    struct tm* log_ltime = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", log_ltime);

    char* file = (char*)srcfile;
    char* func = (char*)function;
    if (NULL != file) {
        file += unifyfs_log_source_base_len;
    }
    if (NULL == func) {
        func = (char*) null_func;
    }
    fprintf(unifyfs_log_stream, "%s tid=%ld @ %s() [%s:%d] %s\n",
            timestamp, (long)unifyfs_gettid(), func, file, lineno, msg);
    fflush(unifyfs_log_stream);
}

/* close our log file stream.
 * returns UNIFYFS_SUCCESS on success */
int unifyfs_log_close(void)
{
    /* if stream is open, and its not stderr, close it */
    if (NULL != unifyfs_log_stream) {
        if (unifyfs_log_stream != stderr) {
            fclose(unifyfs_log_stream);

            /* revert to stderr for any future log messages */
            unifyfs_log_stream = stderr;
        }
    }

    return UNIFYFS_SUCCESS;
}

/* set log level */
void unifyfs_set_log_level(unifyfs_log_level_t lvl)
{
    if (lvl < LOG_LEVEL_MAX) {
        unifyfs_log_level = lvl;
    }
}

/* enable verbose logging upon error */
void unifyfs_set_log_on_error(void)
{
    unifyfs_log_on_error = 1;
}

pid_t unifyfs_gettid(void)
{
#if defined(gettid)
    return gettid();
#elif defined(SYS_gettid)
    return syscall(SYS_gettid);
#else
#error no gettid()
#endif
    return 0;
}
