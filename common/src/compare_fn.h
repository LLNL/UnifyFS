/*
 * Copyright (c) 2023, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2023, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#ifndef COMPARE_FUNC_H
#define COMPARE_FUNC_H

typedef int (*compare_fn)(const void *, const void *);

int int_compare_fn(const void* a, const void* b);
int uint_compare_fn(const void* a, const void* b);

int float_compare_fn(const void* a, const void* b);
int double_compare_fn(const void* a, const void* b);

#endif /* COMPARE_FN_H */
