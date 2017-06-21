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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
* Please read https://github.com/llnl/burstfs/LICENSE for full license text.
*/

#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/time.h>

extern FILE* dbg_stream;
extern char dbg_line[1024];
extern int glb_rank;

typedef enum {
    LOG_FATAL = 1,
    LOG_ERR   = 2,
    LOG_WARN  = 3,
    LOG_INFO  = 4,
    LOG_DBG   = 5
} loglevel;

char timestamp[256];
time_t ltime;

// add and change all
struct tm *ttime;
struct timeval logstart, logend;
double mlogtm;

extern int log_print_level;
#define gettid() syscall(__NR_gettid)
#define LOG(level, ...) \
				if(level <= log_print_level) { \
					gettimeofday(&logstart, NULL); \
					ltime = time(NULL); \
					ttime = localtime(&ltime); \
					strftime(timestamp, sizeof(timestamp), \
							"%Y-%m-%dT%H:%M:%S", ttime); \
					fprintf(dbg_stream,"logtime:%lf rank [%d] [%s] [%ld] [%s:%d] [%s] ", \
								mlogtm/1000000, glb_rank, timestamp, gettid(), \
										__FILE__, __LINE__, __FUNCTION__); \
					fprintf(dbg_stream, __VA_ARGS__); \
					fprintf(dbg_stream, "\n"); \
					fflush(dbg_stream); \
					gettimeofday(&logend, NULL); \
					mlogtm += 1000000*(logend.tv_sec-logstart.tv_sec)+logend.tv_usec-logstart.tv_usec; \
						}	
#endif /* LOG_H */
