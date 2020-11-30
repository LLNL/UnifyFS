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
#include <sys/types.h>

#include "unifyfs_misc.h" // scnprintf

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
extern int unifyfs_log_on_error;
extern FILE* unifyfs_log_stream;

pid_t unifyfs_gettid(void);

/* print one message to debug file stream */
void unifyfs_log_print(time_t now,
                       const char* srcfile,
                       int lineno,
                       const char* function,
                       char* msg);

/* open specified file as debug file stream,
 * returns UNIFYFS_SUCCESS on success */
int unifyfs_log_open(const char* file);

/* close our debug file stream,
 * returns UNIFYFS_SUCCESS on success */
int unifyfs_log_close(void);

/* set log level */
void unifyfs_set_log_level(unifyfs_log_level_t lvl);

/* enable verbose logging upon error */
void unifyfs_set_log_on_error(void);

#define LOG(level, ...) \
    do { \
        if (level <= unifyfs_log_level) { \
            if (NULL == unifyfs_log_stream) { \
                unifyfs_log_stream = stderr; \
            } \
            const char* srcfile = __FILE__; \
            time_t log_time = time(NULL); \
            char msg[4096] = {0}; \
            scnprintf(msg, sizeof(msg), __VA_ARGS__); \
            unifyfs_log_print(log_time, srcfile, __LINE__, __func__, msg); \
        } \
    } while (0)

#define LOGFATAL(...) \
    do { \
        LOG(LOG_FATAL, __VA_ARGS__); \
        unifyfs_log_close(); \
        fprintf(stderr, "UnifyFS Fatal Error Encountered!"); \
        exit(-1); \
    } while (0)

#define LOGERR(...) \
    do { \
        if (unifyfs_log_on_error) { \
            unifyfs_set_log_level(LOG_DBG); \
        } \
        LOG(LOG_ERR, __VA_ARGS__); \
    } while (0)

#define LOGWARN(...) LOG(LOG_WARN, __VA_ARGS__)
#define LOGINFO(...) LOG(LOG_INFO, __VA_ARGS__)
#define LOGDBG(...)  LOG(LOG_DBG, __VA_ARGS__)

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYFS_LOG_H */
