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
 * Written by: Hyogi Sim
 * LLNL-CODE-728877. All rights reserved.
 *
 * This file is part of burstfs.
 * For details, see https://github.com/llnl/burstfs
 * Please read https://github.com/llnl/burstfs/LICENSE for full license text.
 */

/*
 *
 * Copyright (c) 2014, Los Alamos National Laboratory
 *	All rights reserved.
 *
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unifycr.h"

/**
 * @brief this should match with the enum definition of unifycr_cm_t in
 * unifycr.h
 */
static char *consistency_strs[N_UNIFYCR_CM] = { "none", "laminated", "posix" };

/**
 * @brief return consistency enum entry matching with the given string.
 *
 * @param str input string of the consistency model
 *
 * @return matching enum entry on success, UNIFYCR_CM_INVALID (-1) otherwise
 */
unifycr_cm_t unifycr_read_consistency(const char *str)
{
    int i = 0;

    for (i = 0; i < N_UNIFYCR_CM; i++)
        if (!strcmp(str, consistency_strs[i]))
            return i;

    return UNIFYCR_CM_INVALID;
}

/**
 * @brief return the consistency string based on the given code.
 *
 * @param con the consistency code (integer)
 *
 * @return string that describes the consistency model
 */
const char *unifycr_write_consistency(unifycr_cm_t con)
{
    if (con < 0 || con >= N_UNIFYCR_CM)
        return "invalid";
    else
        return consistency_strs[con];
}


