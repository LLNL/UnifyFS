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
* This file is part of BurstFS. For details, see https://github.com/llnl/burstfs
* Please read https://github.com/llnl/burstfs/LICENSE for full license text.
*/

#ifndef __ARRAYLIST_H
#define __ARRAYLIST_H

#define DEF_ARR_CAP 1024

typedef struct {
	long cap;
	long size;
	void **elems;
}arraylist_t;

arraylist_t *arraylist_create();
int arraylist_add(arraylist_t *arr, void *elem);
int arraylist_reset(arraylist_t *arr);
int arraylist_free(arraylist_t *arr);
int arraylist_insert(arraylist_t *arr, int pos, void *elem);
void * arraylist_get(arraylist_t *arr, int pos);
int arraylist_capacity(arraylist_t *arr);
int arraylist_size(arraylist_t *arr);

#endif
