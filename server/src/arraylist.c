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

#include "arraylist.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

arraylist_t *arraylist_create()
{
    arraylist_t *arr = (arraylist_t *)malloc(sizeof(arraylist_t));
    if (!arr) {
        return NULL;
    }

    arr->cap = DEF_ARR_CAP;
    arr->size = 0;
    arr->elems = (void **)malloc(arr->cap * sizeof(void *));

    if (!arr->elems) {
        return NULL;
    }

    int i;
    for (i = 0; i < arr->cap; i++) {
        arr->elems[i] = NULL;
    }

    return arr;
}

int arraylist_capacity(arraylist_t *arr)
{
    return arr->cap;
}

int arraylist_size(arraylist_t *arr)
{
    return arr->size;
}

void *arraylist_get(arraylist_t *arr, int pos)
{
    if (pos >= arr->size) {
        return NULL;
    }
    return arr->elems[pos];
};

int arraylist_insert(arraylist_t *arr, int pos, void *elem)
{
    if (pos >= arr->cap) {
        arr->elems = (void **)realloc(arr->elems,
                                      2 * pos * sizeof(void *));
        if (!arr->elems) {
            return -1;
        }

        long i;
        for (i = arr->cap; i < 2 * pos; i++) {
            arr->elems[i] = NULL;
        }
        arr->cap = 2 * pos;
    }

    if (arr->elems[pos] != NULL) {
        free(arr->elems[pos]);
    }
    arr->elems[pos] = elem;

    if (pos + 1 > arr->size)
        arr->size = pos + 1;

    return 0;

}


int arraylist_add(arraylist_t *arr, void *elem)
{
    if (arr->size == arr->cap) {
        arr->elems = (void **)realloc(arr->elems, 2 * arr->cap * sizeof(void *));
        if (!arr->elems) {
            return -1;
        }

        long i;
        for (i = arr->cap; i < 2 * arr->cap; i++) {
            arr->elems[i] = NULL;
        }
        arr->cap = arr->cap * 2;
    }

    if (arr->elems[arr->size] != NULL) {
        free(arr->elems[arr->size]);
    }
    arr->elems[arr->size] = elem;
    arr->size = arr->size + 1;
    return 0;

}

int arraylist_reset(arraylist_t *arr)
{
    if (!arr) {
        return -1;
    }

    arr->size = 0;
    return 0;
}

int arraylist_free(arraylist_t *arr)
{
    long i;
    for (i = 0; i < arr->cap; i++) {
        if (arr->elems[i] != NULL) {
            free(arr->elems[i]);
        }
    }

    free(arr);
    return 0;
}
