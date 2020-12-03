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

#ifndef __UNIFYFS_LOG_H__
#define __UNIFYFS_LOG_H__

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_FATAL = 1,
    LOG_ERR   = 2,
    LOG_WARN  = 3,
    LOG_INFO  = 4,
    LOG_DBG   = 5,
    LOG_LEVEL_MAX
} unifyfs_log_level_t;

extern unifyfs_log_level_t unifyfs_log_level;
extern FILE* unifyfs_log_stream;
extern time_t unifyfs_log_time;
extern struct tm* unifyfs_log_ltime;
extern char unifyfs_log_timestamp[256];
extern size_t unifyfs_log_source_base_len;

pid_t unifyfs_gettid(void);

#define LOG(level, ...) \
    if (level <= unifyfs_log_level) { \
        const char* srcfile = __FILE__ + unifyfs_log_source_base_len; \
        unifyfs_log_time = time(NULL); \
        unifyfs_log_ltime = localtime(&unifyfs_log_time); \
        strftime(unifyfs_log_timestamp, sizeof(unifyfs_log_timestamp), \
            "%Y-%m-%dT%H:%M:%S", unifyfs_log_ltime); \
        if (NULL == unifyfs_log_stream) { \
            unifyfs_log_stream = stderr; \
        } \
        fprintf(unifyfs_log_stream, "%s tid=%ld @ %s() [%s:%d] ", \
            unifyfs_log_timestamp, (long)unifyfs_gettid(), \
            __func__, srcfile, __LINE__); \
        fprintf(unifyfs_log_stream, __VA_ARGS__); \
        fprintf(unifyfs_log_stream, "\n"); \
        fflush(unifyfs_log_stream); \
    }

#define LOGERR(...)  LOG(LOG_ERR,  __VA_ARGS__)
#define LOGWARN(...) LOG(LOG_WARN, __VA_ARGS__)
#define LOGINFO(...) LOG(LOG_INFO, __VA_ARGS__)
#define LOGDBG(...)  LOG(LOG_DBG,  __VA_ARGS__)

/* open specified file as debug file stream,
 * returns UNIFYFS_SUCCESS on success */
int unifyfs_log_open(const char* file);

/* close our debug file stream,
 * returns UNIFYFS_SUCCESS on success */
int unifyfs_log_close(void);

/* set log level */
void unifyfs_set_log_level(unifyfs_log_level_t lvl);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_LOG_H */
