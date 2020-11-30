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

arraylist_t* arraylist_create(int capacity);
int arraylist_add(arraylist_t* arr, void* elem);
int arraylist_reset(arraylist_t* arr);
int arraylist_free(arraylist_t* arr);
int arraylist_insert(arraylist_t* arr, int pos, void* elem);
void* arraylist_get(arraylist_t* arr, int pos);
void* arraylist_remove(arraylist_t* arr, int pos);
int arraylist_capacity(arraylist_t* arr);
int arraylist_size(arraylist_t* arr);

#endif
