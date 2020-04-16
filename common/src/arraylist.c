/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
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

#include "arraylist.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

arraylist_t* arraylist_create(void)
{
    arraylist_t* arr = (arraylist_t*) malloc(sizeof(arraylist_t));
    if (NULL == arr) {
        return NULL;
    }

    arr->cap = DEF_ARR_CAP;
    arr->size = 0;
    arr->elems = (void**) calloc(arr->cap, sizeof(void*));

    if (NULL == arr->elems) {
        free(arr);
        return NULL;
    }
    return arr;
}

int arraylist_capacity(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }
    return arr->cap;
}

int arraylist_size(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }
    return arr->size;
}

void* arraylist_get(arraylist_t* arr, int pos)
{
    if ((NULL == arr) || (pos >= arr->size)) {
        return NULL;
    }
    return arr->elems[pos];
}

int arraylist_insert(arraylist_t* arr, int pos, void* elem)
{
    if (NULL == arr) {
        return -1;
    }

    if (pos >= arr->cap) {
        int newcap = 2 * pos;
        void** newlist = (void**) realloc(arr->elems,
                                          newcap * sizeof(void*));
        if (NULL == newlist) {
            return -1;
        }
        arr->elems = newlist;

        int i;
        for (i = arr->cap; i < newcap; i++) {
            arr->elems[i] = NULL;
        }
        arr->cap = newcap;
    }

    if (arr->elems[pos] != NULL) {
        free(arr->elems[pos]);
    }
    arr->elems[pos] = elem;

    if (pos + 1 > arr->size) {
        arr->size = pos + 1;
    }

    return 0;
}


int arraylist_add(arraylist_t* arr, void* elem)
{
    if (NULL == arr) {
        return -1;
    }

    if (arr->size == arr->cap) {
        int newcap = 2 * arr->cap;
        void** newlist = (void**) realloc(arr->elems,
                                          newcap * sizeof(void*));
        if (NULL == newlist) {
            return -1;
        }
        arr->elems = newlist;
        arr->cap = newcap;

        int i;
        for (i = arr->size; i < newcap; i++) {
            arr->elems[i] = NULL;
        }
    }

    if (arr->elems[arr->size] != NULL) {
        free(arr->elems[arr->size]);
    }
    arr->elems[arr->size] = elem;
    arr->size += 1;

    return 0;
}

int arraylist_reset(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }

    arr->size = 0;

    return 0;
}

int arraylist_free(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }

    int i;
    for (i = 0; i < arr->cap; i++) {
        if (arr->elems[i] != NULL) {
            free(arr->elems[i]);
        }
    }
    free(arr);

    return 0;
}
