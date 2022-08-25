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

/* Create an arraylist with the given capacity.
 * If capacity == 0, use the default ARRAYLIST_CAPACITY.
 * Returns the new arraylist, or NULL on error. */
arraylist_t* arraylist_create(int capacity)
{
    arraylist_t* arr = (arraylist_t*) malloc(sizeof(arraylist_t));
    if (NULL == arr) {
        return NULL;
    }

    if (capacity) {
        arr->cap = capacity;
    } else {
        arr->cap = ARRAYLIST_CAPACITY;
    }
    arr->size = 0;
    arr->elems = (void**) calloc(arr->cap, sizeof(void*));

    if (NULL == arr->elems) {
        free(arr);
        return NULL;
    }
    return arr;
}

/* Returns the arraylist capacity in elements, or -1 on error */
int arraylist_capacity(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }
    return arr->cap;
}

/* Returns the current arraylist size in elements, or -1 on error */
int arraylist_size(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }
    return arr->size;
}

/* Reset the arraylist size to zero */
int arraylist_reset(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }

    arr->size = 0;

    return 0;
}

/* Free all arraylist elements, the array storage, and the arraylist_t */
int arraylist_free(arraylist_t* arr)
{
    if (NULL == arr) {
        return -1;
    }

    if (NULL != arr->elems) {
        for (int i = 0; i < arr->cap; i++) {
            if (arr->elems[i] != NULL) {
                free(arr->elems[i]);
            }
        }
        free(arr->elems);
    }

    free(arr);

    return 0;
}

/* Get the element at given position */
void* arraylist_get(arraylist_t* arr, int pos)
{
    if ((NULL == arr) || (pos >= arr->size)) {
        return NULL;
    }
    return arr->elems[pos];
}

/* Remove the element at given list index and return it */
void* arraylist_remove(arraylist_t* arr, int pos)
{
    void* item = arraylist_get(arr, pos);
    if (NULL != item) {
        arr->elems[pos] = NULL;
        /* reduce size if pos was last occupied index */
        if ((pos + 1) == arr->size) {
            arr->size -= 1;
            /* keep reducing size for preceding consecutive NULL entries */
            for (int i = pos - 1; i >= 0; i--) {
                if (NULL == arr->elems[i]) {
                    arr->size -= 1;
                } else {
                    break;
                }
            }
        }
    }
    return item;
}

/* Insert the element at the given list index (pos) in the arraylist.
 * Overwrites (and frees) any existing element at that index.
 * Returns 0 on success, or -1 on error */
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

    if ((pos + 1) > arr->size) {
        arr->size = pos + 1;
    }

    return 0;
}

/* Adds element to the end of the current list.
 * Returns list index of newly added element, or -1 on error */
int arraylist_add(arraylist_t* arr, void* elem)
{
    if (NULL == arr) {
        return -1;
    }

    int pos = arr->size;
    int rc = arraylist_insert(arr, pos, elem);
    if (rc == -1) {
        return rc;
    } else {
        return pos;
    }
}

/* Sort the arraylist elements using the given comparison function (cmpfn).
 * Note that the comparison function should properly handle NULL pointer
 * elements of the array.
 * Return 0 on success, -1 on error */
int arraylist_sort(arraylist_t* arr,
                   int (*cmpfn)(const void *, const void *))
{
    if (NULL == arr) {
        return -1;
    }

    /* sort using provided comparison function */
    qsort(arr->elems, arr->cap, sizeof(void*), cmpfn);

    /* adjust size to match last used index */
    int last_used_pos = -1;
    for (int i = 0; i < arr->cap; i++) {
        if (arr->elems[i] != NULL) {
            last_used_pos = i;
        }
    }
    arr->size = last_used_pos + 1;

    return 0;
}
