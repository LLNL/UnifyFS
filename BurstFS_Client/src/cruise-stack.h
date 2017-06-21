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

#ifndef CRUISE_STACK_H
#define CRUISE_STACK_H

/* implements a fixed-size stack which stores integer values in range
 * of 0 to size-1, entire structure stored in an int array of size+2
 *   int size
 *   int last
 *   int entries[size] 
 * last records index within entries that points to item one past
 * the item at the top of the stack
 *
 * used to record which entries in a fixed-size array are free */

#include <stddef.h>

typedef struct {
  int size;
  int last;
} cruise_stack;

/* returns number of bytes needed to represent stack data structure */
size_t cruise_stack_bytes(int size);
  
/* intializes stack to record all entries as being free */
void cruise_stack_init(void* start, int size);

/* pops one entry from stack and returns its value */
int cruise_stack_pop(void* start);

/* pushes item onto free stack */
void cruise_stack_push(void* start, int value);

#endif /* CRUISE_STACK_H */
