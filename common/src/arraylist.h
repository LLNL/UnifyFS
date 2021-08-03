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

#ifndef ARRAYLIST_H
#define ARRAYLIST_H

#ifndef ARRAYLIST_CAPACITY
# define ARRAYLIST_CAPACITY 1024
#endif
typedef struct {
    int cap;
    int size;
    void** elems;
} arraylist_t;

/* Create an arraylist with the given capacity.
 * If capacity == 0, use the default ARRAYLIST_CAPACITY.
 * Returns the new arraylist, or NULL on error. */
arraylist_t* arraylist_create(int capacity);

/* Returns the arraylist capacity in elements, or -1 on error */
int arraylist_capacity(arraylist_t* arr);

/* Returns the current arraylist size in elements, or -1 on error */
int arraylist_size(arraylist_t* arr);

/* Reset the arraylist size to zero */
int arraylist_reset(arraylist_t* arr);

/* Get the element at the given list index (pos)) */
void* arraylist_get(arraylist_t* arr, int pos);

/* Remove the element at given list index (pos) and return it */
void* arraylist_remove(arraylist_t* arr, int pos);

/* Free all arraylist elements, the array storage, and the arraylist_t.
 * Returns 0 on success, -1 on error */
int arraylist_free(arraylist_t* arr);

/* Adds element to the end of the current list.
 * Returns list index of newly added element, or -1 on error */
int arraylist_add(arraylist_t* arr, void* elem);

/* Insert the element at the given list index (pos) in the arraylist.
 * Overwrites (and frees) any existing element at that index.
 * Returns 0 on success, or -1 on error */
int arraylist_insert(arraylist_t* arr, int pos, void* elem);

/* Sort the arraylist elements using the given comparison function (cmpfn).
 * Note that the comparison function should properly handle NULL pointer
 * elements of the array.
 * Return 0 on success, -1 on error */
int arraylist_sort(arraylist_t* arr,
                   int (*cmpfn)(const void *, const void *));

#endif
