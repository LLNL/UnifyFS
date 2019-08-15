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

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * code Written by
 *   Raghunath Rajachandrasekar <rajachan@cse.ohio-state.edu>
 *   Kathryn Mohror <kathryn@llnl.gov>
 *   Adam Moody <moody20@llnl.gov>
 * All rights reserved.
 * This file is part of CRUISE.
 * For details, see https://github.com/hpc/cruise
 * Please also read this file LICENSE.CRUISE
 */

/* implements a fixed-size stack which stores integer values in range
 * of 0 to size-1, entire structure stored in an int array of size+2
 *   int size
 *   int last
 *   int entries[size]
 * last records index within entries that points to item one past
 * the item at the top of the stack */

#include "unifyfs-stack.h"

/* returns number of bytes needed to represent stack data structure */
size_t unifyfs_stack_bytes(int size)
{
    size_t bytes = sizeof(unifyfs_stack) + size * sizeof(int);
    return bytes;
}

/* intializes stack to record all entries as being free */
void unifyfs_stack_init(void* start, int size)
{
    unifyfs_stack* stack = (unifyfs_stack*) start;
    stack->size = size;
    stack->last = size;

    int i;
    int* entries = (int*)((char*)start + sizeof(unifyfs_stack));
    for (i = 0; i < size; i++) {
        /* flip the order so low numbers are at the top
         * to make debugging easier */
        entries[i] = size - 1 - i;
    }
}

/* pops one entry from stack and returns its value */
int unifyfs_stack_pop(void* start)
{
    unifyfs_stack* stack = (unifyfs_stack*) start;

    /* check that the stack isn't empty */
    if (stack->last > 0) {
        /* decrement the last pointer by one */
        stack->last--;

        /* return the index into entries array,
         * this now points to item at top of stack */
        int idx = stack->last;

        /* return the value of this item */
        int* entries = (int*)((char*)start + sizeof(unifyfs_stack));
        int value = entries[idx];
        return value;
    } else {
        /* out of space */
        return -1;
    }
}

/* pushes item onto free stack */
void unifyfs_stack_push(void* start, int value)
{
    unifyfs_stack* stack = (unifyfs_stack*) start;

    /* check that we have space to push item onto stack */
    if (stack->last < stack->size) {
        /* get index of first free slot in entries array */
        int idx = stack->last;

        /* place item on stack */
        int* entries = (int*)((char*)start + sizeof(unifyfs_stack));
        entries[idx] = value;

        /* increment last pointer to point to next item */
        stack->last++;
    } else {
        /* freed one too many */
    }
}
