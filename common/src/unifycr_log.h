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

#ifndef __UNIFYCR_LOG_H__
#define __UNIFYCR_LOG_H__

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_FATAL = 1,
    LOG_ERR   = 2,
    LOG_WARN  = 3,
    LOG_INFO  = 4,
    LOG_DBG   = 5
} unifycr_log_level_t;

extern unifycr_log_level_t unifycr_log_level;
extern FILE* unifycr_log_stream;
extern time_t unifycr_log_time;
extern struct tm* unifycr_log_ltime;
extern char unifycr_log_timestamp[256];

#if defined(__NR_gettid)
#define gettid() syscall(__NR_gettid)
#elif defined(SYS_gettid)
#define gettid() syscall(SYS_gettid)
#else
#error gettid syscall is not defined
#endif

#define LOG(level, ...) \
    if (level <= unifycr_log_level) { \
        unifycr_log_time = time(NULL); \
        unifycr_log_ltime = localtime(&unifycr_log_time); \
        strftime(unifycr_log_timestamp, sizeof(unifycr_log_timestamp), \
            "%Y-%m-%dT%H:%M:%S", unifycr_log_ltime); \
        if (NULL == unifycr_log_stream) { \
            unifycr_log_stream = stderr; \
        } \
        fprintf(unifycr_log_stream, "%s tid=%ld @ %s:%d in %s: ", \
            unifycr_log_timestamp, (long)gettid(), \
            __FILE__, __LINE__, __func__); \
        fprintf(unifycr_log_stream, __VA_ARGS__); \
        fprintf(unifycr_log_stream, "\n"); \
        fflush(unifycr_log_stream); \
    }

#define LOGERR(...)  LOG(LOG_ERR,  __VA_ARGS__)
#define LOGWARN(...) LOG(LOG_WARN, __VA_ARGS__)
#define LOGDBG(...)  LOG(LOG_DBG,  __VA_ARGS__)

/* open specified file as debug file stream,
 * returns UNIFYCR_SUCCESS on success */
int unifycr_log_open(const char* file);

/* close our debug file stream,
 * returns UNIFYCR_SUCCESS on success */
int unifycr_log_close(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UNIFYCR_LOG_H */
