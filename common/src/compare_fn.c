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

#include "compare_fn.h"

int int_compare_fn(const void* a, const void* b)
{
    int ai = *(int*)a;
    int bi = *(int*)b;
    if (ai == bi) {
        return 0;
    } else if (ai > bi) {
        return 1;
    } else {
        return -1;
    }
}

int uint_compare_fn(const void* a, const void* b)
{
    unsigned int ai = *(unsigned int*)a;
    unsigned int bi = *(unsigned int*)b;
    if (ai == bi) {
        return 0;
    } else if (ai > bi) {
        return 1;
    } else {
        return -1;
    }
}

int float_compare_fn(const void* a, const void* b)
{
    float af = *(float*)a;
    float bf = *(float*)b;
    if (af == bf) {
        return 0;
    } else if (af > bf) {
        return 1;
    } else {
        return -1;
    }
}

int double_compare_fn(const void* a, const void* b)
{
    double ad = *(double*)a;
    double bd = *(double*)b;
    if (ad == bd) {
        return 0;
    } else if (ad > bd) {
        return 1;
    } else {
        return -1;
    }
}
