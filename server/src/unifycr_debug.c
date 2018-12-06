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
#include "unifycr_debug.h"
#include "unifycr_const.h"

/* pointer to debug file stream */
FILE* dbg_stream;

/* open specified file as debug file stream,
 * returns UNIFYCR_SUCCESS on success */
int dbg_open(char* fname)
{
    dbg_stream = fopen(fname, "a");
    if (dbg_stream == NULL) {
        /* failed to open file name, fall back to stderr */
        dbg_stream = stderr;
        return (int)UNIFYCR_ERROR_DBG;
    } else {
        return UNIFYCR_SUCCESS;
    }
}

/* close our debug file stream,
 * returns UNIFYCR_SUCCESS on success */
int dbg_close(void)
{
    if (dbg_stream == NULL) {
        /* nothing to close */
        return (int)UNIFYCR_ERROR_DBG;
    } else {
        /* if stream is open, and its not stderr, close it */
        if (dbg_stream != stderr &&
            fclose(dbg_stream) == 0) {
            return UNIFYCR_SUCCESS;
        }
        return (int)UNIFYCR_ERROR_DBG;
    }
}
