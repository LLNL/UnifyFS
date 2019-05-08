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

#include <stdio.h>
#include "unifycr_log.h"
#include "unifycr_const.h"

/* one of the loglevel values */
unifycr_log_level_t unifycr_log_level = 5;

/* pointer to log file stream */
FILE* unifycr_log_stream; // = NULL

int glb_rank;

/* used within LOG macro to build a timestamp */
time_t unifycr_log_time;
struct tm* unifycr_log_ltime;
char unifycr_log_timestamp[256];

/* open specified file as log file stream,
 * returns UNIFYCR_SUCCESS on success */
int unifycr_log_open(const char* file)
{
    FILE* logf = fopen(file, "a");
    if (logf == NULL) {
        /* failed to open file name, fall back to stderr */
        unifycr_log_stream = stderr;
        return (int)UNIFYCR_ERROR_DBG;
    } else {
        unifycr_log_stream = logf;
        return UNIFYCR_SUCCESS;
    }
}

/* close our log file stream,
 * returns UNIFYCR_SUCCESS on success */
int unifycr_log_close(void)
{
    if (unifycr_log_stream == NULL) {
        /* nothing to close */
        return (int)UNIFYCR_ERROR_DBG;
    } else {
        /* if stream is open, and its not stderr, close it */
        if (unifycr_log_stream != stderr &&
            fclose(unifycr_log_stream) == 0) {
            return UNIFYCR_SUCCESS;
        }
        return (int)UNIFYCR_ERROR_DBG;
    }
}
