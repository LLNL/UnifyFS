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

#ifndef __UNIFYFS_MISC__
#define __UNIFYFS_MISC__

size_t strlcpy(char* dest, const char* src, size_t size);

int scnprintf(char* buf, size_t size, const char* fmt, ...);

#endif
